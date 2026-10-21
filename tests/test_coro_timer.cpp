/**
 * @file test_coro_timer.cpp
 * @brief Exercise coroutine timed waits: deadlines, ordering, cancellation, polling, shutdown, and stress.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"
import std;
import mcr;

namespace {
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;
    using mcr::test::HttpServer;

    /**
     * @brief Fail the test when an expectation does not hold.
     * @param value Checked condition.
     * @param message Failure description.
     * @throws std::runtime_error If value is false.
     */
    auto require(bool value, std::string_view message) -> void {
        if (!value) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    /**
     * @brief Bypass environment proxies for loopback requests.
     * @return Proxy option disabling all proxies.
     */
    auto proxies() -> mcr::options::Proxies {
        return {
            { "http", "" },
            { "no_proxy", "*" }
        };
    }

    /**
     * @brief Record the outcome, duration, and resumption thread of one timed wait.
     */
    struct Waited {
        mcr::Result<void> result;  ///< Awaited outcome.
        Clock::duration elapsed{}; ///< Time from before creating the awaiter until resumption.
        Clock::time_point resumed; ///< Steady-clock time observed after resumption.
        std::thread::id thread;    ///< Thread that resumed the coroutine.
    };

    /**
     * @brief Check whether a wait failed with a specific error code.
     * @param waited Recorded outcome.
     * @param code Expected error code.
     * @return True if the wait failed with code.
     */
    auto failed_with(
        Waited const& waited,
        mcr::ErrorCode code
    ) -> bool {
        return !waited.result && waited.result.error().code == code;
    }

    /**
     * @brief Await a relative timed wait and record its outcome.
     * @tparam Rep Duration representation.
     * @tparam Period Duration tick period.
     * @param duration Requested wait.
     * @param token Explicit cancellation token.
     * @return Recorded outcome.
     */
    template <typename Rep, typename Period>
    auto wait_after(
        std::chrono::duration<Rep, Period> duration,
        std::stop_token token = {}
    ) -> mcr::Task<Waited> {
        auto const start = Clock::now();
        auto result = co_await mcr::Coro::ScheduleAfter(duration, std::move(token));
        auto const resumed = Clock::now();
        co_return Waited{ std::move(result), resumed - start, resumed, std::this_thread::get_id() };
    }

    /**
     * @brief Await an absolute timed wait and record its outcome.
     * @param deadline Requested steady-clock deadline.
     * @param token Explicit cancellation token.
     * @return Recorded outcome.
     */
    auto wait_at(
        Clock::time_point deadline,
        std::stop_token token = {}
    ) -> mcr::Task<Waited> {
        auto const start = Clock::now();
        auto result = co_await mcr::Coro::ScheduleAt(deadline, std::move(token));
        auto const resumed = Clock::now();
        co_return Waited{ std::move(result), resumed - start, resumed, std::this_thread::get_id() };
    }

    /**
     * @brief Identify the continuation thread through a queued resumption.
     * @return Thread that resumed the queued continuation.
     */
    auto continuation_thread() -> mcr::Task<std::thread::id> {
        (co_await mcr::Coro::Schedule()).value();
        co_return std::this_thread::get_id();
    }

    /**
     * @brief Append an identifier after a relative wait succeeds.
     * @param order Resumption order, written only on the continuation thread.
     * @param delay Requested wait.
     * @param id Identifier to append.
     */
    auto record_after(
        std::vector<int>& order,
        Clock::duration delay,
        int id
    ) -> mcr::Task<void> {
        (co_await mcr::Coro::ScheduleAfter(delay)).value();
        order.push_back(id);
    }

    /**
     * @brief Append an identifier after an absolute wait succeeds.
     * @param order Resumption order, written only on the continuation thread.
     * @param deadline Requested deadline.
     * @param id Identifier to append.
     */
    auto record_at(
        std::vector<int>& order,
        Clock::time_point deadline,
        int id
    ) -> mcr::Task<void> {
        (co_await mcr::Coro::ScheduleAt(deadline)).value();
        order.push_back(id);
    }

    /**
     * @brief Append a step only after the continuation queue starts this task.
     * @param order Step order, written only on the continuation thread.
     * @param step Step to append.
     */
    auto record_step(
        std::vector<int>& order,
        int step
    ) -> mcr::Task<void> {
        order.push_back(step);
        co_return;
    }

    /**
     * @brief Yield behind an already queued peer with a zero-length wait.
     * @param order Step order, written only on the continuation thread.
     */
    auto zero_wait_chain(std::vector<int>& order) -> mcr::Task<void> {
        (co_await mcr::Coro::Schedule()).value();
        auto peer = mcr::Coro::Spawn(record_step(order, 2)).value();
        order.push_back(1);
        (co_await mcr::Coro::ScheduleAfter(0ms)).value();
        order.push_back(3);
        co_await std::move(peer);
    }

    /**
     * @brief Verify minimum durations, rounding, resumption threads, and deadline ordering.
     * @param server Local HTTP fixture used to identify the I/O thread.
     * @return Continuation thread identifier.
     */
    auto deadlines(HttpServer const& server) -> std::thread::id {
        auto const caller = std::this_thread::get_id();
        auto const continuation = mcr::sync_wait(continuation_thread());
        std::thread::id io;
        auto response = mcr::sync_wait(mcr::GetCoro(server.Url(), proxies(), mcr::options::Timeout{ 3s }, mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
                                                        io = std::this_thread::get_id();
                                                        return true;
                                                    } }))
                            .value();
        require(response.status_code == 200 && io != std::thread::id{}, "the I/O thread must be observable");

        auto waited = mcr::sync_wait(wait_after(60ms));
        require(waited.result.has_value(), "a timed wait must succeed at its deadline");
        require(waited.elapsed >= 60ms, "a timed wait must not resume before its duration");
        require(waited.thread == continuation && waited.thread != caller && waited.thread != io, "timed waits must resume on the continuation thread");
        auto fine = mcr::sync_wait(wait_after(std::chrono::microseconds{ 20'500 }));
        require(fine.result && fine.elapsed >= std::chrono::microseconds{ 20'500 }, "sub-millisecond durations must not be truncated");
        auto fractional = mcr::sync_wait(wait_after(std::chrono::duration<double, std::milli>{ 15.25 }));
        require(fractional.result && fractional.elapsed >= std::chrono::duration<double, std::milli>{ 15.25 }, "fractional durations must round up");
        auto const deadline = Clock::now() + 50ms;
        auto at = mcr::sync_wait(wait_at(deadline));
        require(at.result && at.resumed >= deadline && at.thread == continuation, "ScheduleAt must not resume before its deadline");

        // Deadlines, not arming order, decide resumption; equal deadlines keep arming order.
        std::vector<int> order;
        std::vector<mcr::Task<void>> tasks;
        auto const shared = Clock::now() + 120ms;
        tasks.push_back(record_after(order, 240ms, 5));
        tasks.push_back(record_after(order, 60ms, 1));
        tasks.push_back(record_at(order, shared, 2));
        tasks.push_back(record_at(order, shared, 3));
        tasks.push_back(record_after(order, 180ms, 4));
        tasks.push_back(record_after(order, 0ms, 0));
        for (auto& task : tasks) {
            task.Start();
        }
        for (auto& task : tasks) {
            mcr::sync_wait(std::move(task));
        }
        require(order == std::vector<int>{ 0, 1, 2, 3, 4, 5 }, "timers must resume in deadline order, keeping arming order for equal deadlines");
        return continuation;
    }

    /**
     * @brief Verify that non-positive durations and reached deadlines behave like Schedule().
     * @param continuation Continuation thread identifier.
     */
    auto immediate(std::thread::id continuation) -> void {
        std::vector<int> order;
        mcr::sync_wait(zero_wait_chain(order));
        require(order == std::vector<int>{ 1, 2, 3 }, "a zero-length wait must yield behind queued work like Schedule");
        for (auto const& waited : { mcr::sync_wait(wait_after(0ms)), mcr::sync_wait(wait_after(-5s)), mcr::sync_wait(wait_at(Clock::now() - 1h)), mcr::sync_wait(wait_at((Clock::time_point::min)())) }) {
            require(waited.result && waited.thread == continuation && waited.elapsed < 3s, "non-positive waits must succeed promptly through the continuation queue");
        }
    }

    /**
     * @brief Await a child timer so parent cancellation must propagate into it.
     * @return Child wait outcome.
     */
    auto parent_wait() -> mcr::Task<Waited> {
        co_return co_await wait_after(30s);
    }

    /**
     * @brief Cancel another wait from the continuation thread after a short timed wait.
     * @param source Stop source of the wait to cancel.
     */
    auto cancel_later(std::stop_source source) -> mcr::Task<void> {
        (co_await mcr::Coro::ScheduleAfter(20ms)).value();
        source.request_stop();
    }

    /**
     * @brief Check that a wait was cancelled well before its distant deadline.
     * @param waited Recorded outcome.
     * @return True for a prompt ABORTED_BY_CALLBACK result.
     */
    auto aborted(Waited const& waited) -> bool {
        return failed_with(waited, mcr::ErrorCode::ABORTED_BY_CALLBACK) && waited.elapsed < 3s;
    }

    /**
     * @brief Verify task, cross-thread, explicit-token, parent, and pre-stopped cancellation.
     * @param continuation Continuation thread identifier.
     */
    auto cancellation(std::thread::id continuation) -> void {
        auto cancelled = wait_after(30s);
        cancelled.Start();
        require(cancelled.Cancel(), "a waiting task must accept cancellation");
        auto waited = mcr::sync_wait(std::move(cancelled));
        require(aborted(waited) && waited.thread == continuation, "Task::Cancel must end a pending wait promptly through the continuation queue");

        auto remote = wait_after(30s);
        auto source = remote.GetStopSource();
        remote.Start();
        std::jthread canceller{ [source]() mutable {
            std::this_thread::sleep_for(50ms);
            source.request_stop();
        } };
        require(aborted(mcr::sync_wait(std::move(remote))), "a stop source used from another thread must end the wait");
        canceller.join();

        std::stop_source external;
        auto explicit_wait = wait_after(30s, external.get_token());
        explicit_wait.Start();
        std::jthread external_canceller{ [&external] {
            std::this_thread::sleep_for(50ms);
            external.request_stop();
        } };
        require(aborted(mcr::sync_wait(std::move(explicit_wait))), "an explicit stop token must end the wait");
        external_canceller.join();

        // Saturated deadlines leave the I/O thread waiting on a distant time point until cancellation.
        std::stop_source forever;
        auto hours = wait_after((std::chrono::hours::max)(), forever.get_token());
        auto seconds = wait_after((std::chrono::duration<double>::max)(), forever.get_token());
        auto at_max = wait_at((Clock::time_point::max)(), forever.get_token());
        auto at_explicit = wait_at(Clock::now() + 30s, forever.get_token());
        hours.Start();
        seconds.Start();
        at_max.Start();
        at_explicit.Start();
        std::this_thread::sleep_for(20ms);
        forever.request_stop();
        require(aborted(mcr::sync_wait(std::move(hours))) && aborted(mcr::sync_wait(std::move(seconds))), "saturated durations must remain cancellable");
        require(aborted(mcr::sync_wait(std::move(at_max))) && aborted(mcr::sync_wait(std::move(at_explicit))), "ScheduleAt must observe its explicit stop token");

        auto parent = parent_wait();
        auto parent_source = parent.GetStopSource();
        parent.Start();
        parent_source.request_stop();
        require(aborted(mcr::sync_wait(std::move(parent))), "parent cancellation must reach a child waiting on a timer");

        auto victim = wait_after(30s);
        auto victim_source = victim.GetStopSource();
        victim.Start();
        auto cancelling = mcr::Coro::Spawn(cancel_later(victim_source)).value();
        require(aborted(mcr::sync_wait(std::move(victim))), "cancellation from the continuation thread must end the wait");
        mcr::sync_wait(std::move(cancelling));

        // A stop request before the wait takes precedence, even over a reached deadline.
        auto before = wait_after(30s);
        require(before.Cancel(), "a cold task must accept cancellation");
        auto stopped_before = mcr::sync_wait(std::move(before));
        require(aborted(stopped_before) && stopped_before.thread == continuation, "a pre-stopped task must resume with ABORTED_BY_CALLBACK through the queue");
        auto zero = wait_after(0ms);
        require(zero.Cancel(), "a cold task must accept cancellation");
        require(aborted(mcr::sync_wait(std::move(zero))), "a pre-stopped zero-length wait must report cancellation");
        std::stop_source stopped;
        stopped.request_stop();
        require(aborted(mcr::sync_wait(wait_after(30s, stopped.get_token()))), "a pre-stopped explicit token must end the wait");
        require(aborted(mcr::sync_wait(wait_at(Clock::now() - 1s, stopped.get_token()))), "a pre-stopped token must take precedence over a past deadline");
    }

    /**
     * @brief Hold the continuation thread so completed waits remain queued.
     * @param entered Signalled once the continuation thread is held.
     * @param release Ready when the continuation thread may continue.
     */
    auto hold_scheduler(
        std::promise<void>& entered,
        std::shared_future<void> release
    ) -> mcr::Task<void> {
        entered.set_value();
        require(release.wait_for(3s) == std::future_status::ready, "the held continuation must be released");
        co_return;
    }

    /**
     * @brief Verify that the first queued outcome is not replaced by a later signal.
     */
    auto first_outcome() -> void {
        std::promise<void> entered, release;
        auto began = entered.get_future();
        auto held = mcr::Coro::Spawn(hold_scheduler(entered, release.get_future().share())).value();
        require(began.wait_for(3s) == std::future_status::ready, "the continuation thread must be held");
        // A reached deadline queues success before the stop request arrives.
        std::stop_source immediate_source;
        auto reached = wait_at(Clock::now(), immediate_source.get_token());
        reached.Start();
        immediate_source.request_stop();
        // The I/O thread queues success long before the late stop request.
        std::stop_source expired_source;
        auto expired = wait_after(20ms, expired_source.get_token());
        expired.Start();
        // Cancellation removes the timer, so its later deadline cannot replace the error.
        auto cancelled = wait_after(200ms);
        cancelled.Start();
        require(cancelled.Cancel(), "a waiting task must accept cancellation");
        std::this_thread::sleep_for(500ms);
        expired_source.request_stop();
        release.set_value();
        mcr::sync_wait(std::move(held));
        require(mcr::sync_wait(std::move(reached)).result.has_value(), "a stop request after a reached deadline must not replace success");
        require(mcr::sync_wait(std::move(expired)).result.has_value(), "a stop request after expiry must not replace success");
        require(failed_with(mcr::sync_wait(std::move(cancelled)), mcr::ErrorCode::ABORTED_BY_CALLBACK), "a deadline after cancellation must not replace the error");
    }

    /**
     * @brief Eager coroutine whose promise has no stop token.
     */
    struct Detached {
        /**
         * @brief Minimal promise exercising the awaiter without task cancellation.
         */
        struct promise_type {
            auto get_return_object() noexcept -> Detached { return {}; }

            auto initial_suspend() const noexcept -> std::suspend_never { return {}; }

            auto final_suspend() const noexcept -> std::suspend_never { return {}; }

            auto return_void() noexcept -> void {}

            auto unhandled_exception() noexcept -> void { std::terminate(); }
        };
    };

    /**
     * @brief Publish a timed wait outcome from a coroutine outside mcr::Task.
     * @param duration Requested wait.
     * @param token Explicit cancellation token, the only one this coroutine observes.
     * @param done Receives the outcome; shared so the frame never outlives it.
     */
    auto detached_wait(
        Clock::duration duration,
        std::stop_token token,
        std::shared_ptr<std::promise<mcr::Result<void>>> done
    ) -> Detached {
        done->set_value(co_await mcr::Coro::ScheduleAfter(duration, std::move(token)));
    }

    /**
     * @brief Verify waits from coroutines whose promise lacks GetStopToken.
     */
    auto foreign_coroutine() -> void {
        auto expired = std::make_shared<std::promise<mcr::Result<void>>>();
        auto expired_result = expired->get_future();
        detached_wait(20ms, {}, expired);
        require(expired_result.wait_for(3s) == std::future_status::ready && expired_result.get().has_value(), "coroutines without a stop token must be able to wait");

        std::stop_source source;
        auto cancelled = std::make_shared<std::promise<mcr::Result<void>>>();
        auto cancelled_result = cancelled->get_future();
        detached_wait(30s, source.get_token(), cancelled);
        source.request_stop();
        require(cancelled_result.wait_for(3s) == std::future_status::ready, "an explicit token must end a foreign coroutine's wait");
        auto const outcome = cancelled_result.get();
        require(!outcome && outcome.error().code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "a foreign coroutine must observe cancellation");
    }

    /**
     * @brief Await consecutive short waits and report their total duration.
     * @param count Number of waits.
     * @param delay Duration of each wait.
     * @return Total elapsed time.
     */
    auto consecutive_waits(
        int count,
        Clock::duration delay
    ) -> mcr::Task<Clock::duration> {
        auto const start = Clock::now();
        for (int index{}; index < count; ++index) {
            (co_await mcr::Coro::ScheduleAfter(delay)).value();
        }
        co_return Clock::now() - start;
    }

    /**
     * @brief Verify that timers keep their deadlines while an idle transfer occupies curl polling.
     * @param server Local HTTP fixture with a delayed endpoint.
     */
    auto active_transfer(HttpServer const& server) -> void {
        std::promise<void> progressed;
        auto started = progressed.get_future();
        std::atomic_bool signalled{};
        auto slow = mcr::request_coro<mcr::method::Get>(server.Url("/delay"), proxies(), mcr::options::Timeout{ 30s }, mcr::ProgressCallback{ [&](auto, auto, auto, auto, auto) {
                                                            if (!signalled.exchange(true)) {
                                                                progressed.set_value();
                                                            }
                                                            return true;
                                                        } });
        auto slow_source = slow.GetStopSource();
        slow.Start();
        require(started.wait_for(3s) == std::future_status::ready, "the delayed transfer must become active");

        // Without the deadline in the poll timeout, each wait would last about 100 ms.
        constexpr int COUNT{ 30 };
        auto const total = mcr::sync_wait(consecutive_waits(COUNT, 5ms));
        require(total >= COUNT * 5ms, "consecutive waits must not resume early");
        require(total < 1500ms, std::format("timed waits during a transfer must follow their deadlines, took {}", std::chrono::duration_cast<std::chrono::milliseconds>(total)));

        // A curl callback on the I/O thread may cancel a timed wait.
        auto victim = wait_after(30s);
        auto victim_source = victim.GetStopSource();
        victim.Start();
        auto trigger = mcr::sync_wait(mcr::GetCoro(server.Url(), proxies(), mcr::options::Timeout{ 3s }, mcr::HeaderCallback{ [victim_source](std::string_view, std::intptr_t) mutable {
                                                       victim_source.request_stop();
                                                       return true;
                                                   } }))
                           .value();
        require(trigger.status_code == 200, "the cancelling request must complete");
        require(aborted(mcr::sync_wait(std::move(victim))), "cancellation from a curl callback must end the wait");

        slow_source.request_stop();
        auto const response = mcr::sync_wait(std::move(slow)).value();
        require(response.error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "the delayed transfer must stay active throughout the timed waits");
    }

    /**
     * @brief Track one stress-test wait and how it is cancelled.
     */
    struct StressWait {
        mcr::Task<Waited> task;       ///< Started wait.
        std::stop_source source;      ///< Source observed by the wait.
        Clock::duration delay{};      ///< Requested relative wait.
        Clock::time_point deadline{}; ///< Requested absolute deadline, if absolute.
        bool absolute{};              ///< Whether the wait used ScheduleAt.
        int mode{};                   ///< 0 before, 1 after arming, 2 racing expiry, 3 never cancelled.
    };

    /**
     * @brief Mix thousands of concurrent timers with random cancellation from four threads.
     */
    auto stress() -> void {
        constexpr int CALLERS{ 4 };
        constexpr int TIMERS{ 1000 };
        std::atomic_int resumed{}, expired{}, aborted_count{};
        std::mutex failure_mutex;
        std::exception_ptr failure;
        std::vector<std::jthread> callers;
        for (int caller{}; caller < CALLERS; ++caller) {
            callers.emplace_back([&, caller] {
                try {
                    std::mt19937 random{ static_cast<std::mt19937::result_type>(caller + 1) };
                    std::uniform_int_distribution<int> delays{ 0, 39 }, modes{ 0, 3 }, kinds{ 0, 2 }, pauses{ 0, 200 };
                    std::vector<StressWait> waits;
                    waits.reserve(TIMERS);
                    for (int index{}; index < TIMERS; ++index) {
                        StressWait wait;
                        wait.delay = std::chrono::milliseconds{ delays(random) };
                        wait.mode = modes(random);
                        auto const kind = kinds(random);
                        std::stop_source external;
                        if (kind == 0) {
                            wait.task = wait_after(wait.delay);
                            wait.source = wait.task.GetStopSource();
                        } else if (kind == 1) {
                            wait.task = wait_after(wait.delay, external.get_token());
                            wait.source = external;
                        } else {
                            wait.absolute = true;
                            wait.deadline = Clock::now() + wait.delay;
                            wait.task = wait_at(wait.deadline, external.get_token());
                            wait.source = external;
                        }
                        if (wait.mode == 0) {
                            wait.source.request_stop();
                        }
                        wait.task.Start();
                        if (wait.mode == 1) {
                            wait.source.request_stop();
                        }
                        waits.push_back(std::move(wait));
                    }
                    for (auto& wait : waits) {
                        if (wait.mode == 2) {
                            std::this_thread::sleep_for(std::chrono::microseconds{ pauses(random) });
                            wait.source.request_stop();
                        }
                    }
                    for (auto& wait : waits) {
                        auto const waited = mcr::sync_wait(std::move(wait.task));
                        ++resumed;
                        if (waited.result) {
                            ++expired;
                            require(wait.mode != 0, "a pre-stopped wait must not succeed");
                            require(wait.absolute ? waited.resumed >= wait.deadline : waited.elapsed >= wait.delay, "a successful wait must not resume early");
                        } else {
                            ++aborted_count;
                            require(waited.result.error().code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "a cancelled wait must report ABORTED_BY_CALLBACK");
                            require(wait.mode != 3, "an uncancelled wait must succeed");
                        }
                    }
                } catch (...) {
                    std::lock_guard lock{ failure_mutex };
                    if (!failure) {
                        failure = std::current_exception();
                    }
                }
            });
        }
        callers.clear();
        if (failure) {
            std::rethrow_exception(failure);
        }
        require(resumed == CALLERS * TIMERS && expired + aborted_count == resumed, "every wait must resume exactly once");
        require(expired > 0 && aborted_count > 0, "the stress mix must include both outcomes");
    }

    /**
     * @brief Retry once after shutdown resumes a pending wait.
     * @return Outcomes of the interrupted wait and the retry.
     */
    auto rewait() -> mcr::Task<std::pair<Waited, Waited>> {
        auto first = co_await wait_after(1h);
        auto second = co_await wait_after(0ms);
        co_return std::pair{ std::move(first), std::move(second) };
    }

    /**
     * @brief Keep an owned object in a waiting frame that no consumer awaits.
     * @param owned Object released only with the frame.
     */
    auto hold_owned(std::shared_ptr<int> owned) -> mcr::Task<void> {
        (void)co_await mcr::Coro::ScheduleAfter(1h);
        (void)owned;
    }

    /**
     * @brief Verify that cleanup promptly ends pending waits and rejects later waits.
     */
    auto shutdown() -> void {
        std::vector<mcr::Task<Waited>> pending;
        for (int index{}; index < 100; ++index) {
            pending.push_back(wait_after(1h + index * 1ms));
            pending.back().Start();
        }
        pending.push_back(wait_after((std::chrono::hours::max)()));
        pending.back().Start();
        pending.push_back(wait_at((Clock::time_point::max)()));
        pending.back().Start();
        auto retry = rewait();
        retry.Start();
        auto owned = std::make_shared<int>(7);
        std::weak_ptr<int> released = owned;
        {
            auto abandoned = hold_owned(std::move(owned));
            abandoned.Start();
        }
        auto foreign = std::make_shared<std::promise<mcr::Result<void>>>();
        auto foreign_result = foreign->get_future();
        detached_wait(1h, {}, foreign);

        auto const begin = Clock::now();
        mcr::Coro::Cleanup().value();
        require(Clock::now() - begin < 3s, "cleanup must not wait for pending deadlines");
        require(released.expired(), "cleanup must resume and release abandoned waiting frames");
        require(foreign_result.wait_for(0s) == std::future_status::ready, "cleanup must resume foreign coroutines before returning");
        auto const foreign_outcome = foreign_result.get();
        require(!foreign_outcome && foreign_outcome.error().code == mcr::ErrorCode::FAILED_INIT, "a foreign coroutine must observe shutdown");
        for (auto& task : pending) {
            require(failed_with(mcr::sync_wait(std::move(task)), mcr::ErrorCode::FAILED_INIT), "cleanup must end pending waits with FAILED_INIT");
        }
        auto const [first, second] = mcr::sync_wait(std::move(retry));
        require(failed_with(first, mcr::ErrorCode::FAILED_INIT) && failed_with(second, mcr::ErrorCode::FAILED_INIT) && second.thread == first.thread, "a wait started during shutdown must be rejected without suspending");

        auto const caller = std::this_thread::get_id();
        std::stop_source stopped;
        stopped.request_stop();
        for (auto const& waited : { mcr::sync_wait(wait_after(10ms)), mcr::sync_wait(wait_after(0ms)), mcr::sync_wait(wait_at(Clock::now() + 1s)), mcr::sync_wait(wait_after(30s, stopped.get_token())) }) {
            require(failed_with(waited, mcr::ErrorCode::FAILED_INIT) && waited.thread == caller, "waits after cleanup must return FAILED_INIT without suspending");
        }
        mcr::Coro::Cleanup().value();
    }
} // namespace

/**
 * @brief Run timed-wait checks, ending with permanent runtime cleanup.
 */
auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    int result{};
    try {
        mcr::Coro::Startup().value();
        HttpServer server;
        auto const continuation = deadlines(server);
        immediate(continuation);
        cancellation(continuation);
        first_outcome();
        foreign_coroutine();
        active_transfer(server);
        stress();
        shutdown();
        server.Check();
    } catch (std::exception const& error) {
        std::println("test_coro_timer: {}", error.what());
        result = 1;
    }
    mcr::Coro::Cleanup().value();
    curl_global_cleanup();
    return result;
}
