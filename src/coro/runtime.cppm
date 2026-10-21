/**
 * @file runtime.cppm
 * @brief Curl multi transfers, steady-clock timers, and queued task execution on a separate continuation thread.
 */
module;
#include <curl/curl.h>

export module mcr:coro_runtime;

import :task;
import :session;
import :curlholder;
import :curlmultiholder;
import std;

export namespace mcr::detail {
    class CoroRuntime;
    struct CoroTimer;

    /**
     * @brief Order armed timers by deadline; equal deadlines keep their arming order.
     */
    using CoroTimerQueue = std::multimap<std::chrono::steady_clock::time_point, std::shared_ptr<CoroTimer>>;

    /**
     * @brief Wake the runtime when a task's stop token is signalled.
     */
    struct WakeCoroRuntime {
        CoroRuntime* runtime;
        auto operator()() const noexcept -> void;
    };

    /**
     * @brief Complete an armed timer when one of its stop tokens is signalled.
     */
    struct CancelCoroTimer {
        CoroRuntime* runtime; ///< Runtime owning the deadline queue.
        CoroTimer* timer;     ///< Timer that owns this callback.
        auto operator()() const noexcept -> void;
    };

    /**
     * @brief Link accepted work in the runtime's serialized continuation queue.
     */
    struct CoroWork {
        std::shared_ptr<CoroWork> next; ///< Intrusive queue link, allocated before publication.

        /**
         * @brief Release the concrete work item through its queue owner.
         */
        virtual ~CoroWork() = default;

        /**
         * @brief Resume outside runtime locks on the continuation thread.
         */
        virtual auto Run() noexcept -> void = 0;
    };

    /**
     * @brief Schedule a continuation and optionally own its unstarted frame.
     */
    struct CoroResume : CoroWork {
        std::coroutine_handle<> continuation{}; ///< Frame to resume exactly once.
        bool owns_frame{ false };               ///< Destroy an unstarted frame if posting fails.

        /**
         * @brief Construct empty work before installing a continuation.
         */
        CoroResume() = default;
        CoroResume(CoroResume const&) = delete;
        auto operator=(CoroResume const&) -> CoroResume& = delete;

        /**
         * @brief Release a cold task if the runtime did not accept ownership.
         */
        ~CoroResume() override {
            if (owns_frame && continuation) {
                continuation.destroy();
            }
        }

        /**
         * @brief Transfer execution to the task before it can destroy its frame.
         */
        auto Run() noexcept -> void override {
            auto handle = std::exchange(continuation, {});
            handle.resume();
        }
    };

    /**
     * @brief Own one transfer until its awaiting coroutine has resumed.
     */
    struct CoroTransfer : CoroWork {
        std::shared_ptr<Session> session;
        std::function<Result<void>(Session&)> prepare;
        std::stop_token stop;
        std::optional<std::stop_callback<WakeCoroRuntime>> wake;
        std::coroutine_handle<> continuation;
        CURLcode result{ CURLE_OK };
        std::exception_ptr error;
        Result<void> setup_result;                  ///< Configuration failure returned before publication.
        CURLMcode multi_error{ CURLM_OK };          ///< Multi failure recorded without allocating diagnostics on the I/O thread.
        bool prepared{ false };
        std::shared_ptr<CoroTransfer> pending_next; ///< Intrusive link used before submission to curl.

        /**
         * @brief Own a session, preparation operation, and cancellation token before publication.
         */
        CoroTransfer(
            std::shared_ptr<Session> owned_session,
            std::function<Result<void>(Session&)> prepare_request,
            std::stop_token token
        ) : session{ std::move(owned_session) }, prepare{ std::move(prepare_request) }, stop{ token } {}

        /**
         * @brief Disconnect cancellation before resuming outside curl's callback stack.
         */
        auto Run() noexcept -> void override {
            wake.reset();
            auto handle = std::exchange(continuation, {});
            handle.resume();
        }
    };

    /**
     * @brief Describe how a timed wait finished.
     */
    enum class CoroTimerOutcome : std::uint8_t {
        PENDING,   ///< Not yet completed.
        EXPIRED,   ///< The deadline was reached.
        CANCELLED, ///< A stop token was signalled before the deadline.
        CLOSED,    ///< The runtime stopped before the deadline.
    };

    /**
     * @brief Own one timed wait until its awaiting coroutine has resumed.
     * @note The runtime lock guards arming state and the outcome until the timer is queued for resumption.
     */
    struct CoroTimer : CoroWork {
        std::chrono::steady_clock::time_point deadline;                 ///< Earliest resumption time.
        std::stop_token stop;                                           ///< Explicit cancellation token.
        std::stop_token task_stop;                                      ///< Awaiting task's token, empty for other coroutines.
        std::coroutine_handle<> continuation{};                         ///< Frame to resume exactly once.
        CoroTimerQueue::iterator position{};                            ///< Deadline queue entry, valid while armed.
        bool armed{ false };                                            ///< Whether the deadline queue owns this timer.
        CoroTimerOutcome outcome{ CoroTimerOutcome::PENDING };          ///< Written once before queuing the resumption.
        std::optional<std::stop_callback<CancelCoroTimer>> cancel;      ///< Explicit token callback, destroyed before the fields it reads.
        std::optional<std::stop_callback<CancelCoroTimer>> task_cancel; ///< Task token callback, destroyed before the fields it reads.

        /**
         * @brief Capture the deadline and explicit token before publication.
         * @param expiry Earliest steady-clock time at which to resume.
         * @param token Explicit cancellation token, possibly empty.
         */
        CoroTimer(
            std::chrono::steady_clock::time_point expiry,
            std::stop_token token
        ) : deadline{ expiry }, stop{ std::move(token) } {}

        /**
         * @brief Disconnect cancellation before resuming on the continuation thread.
         */
        auto Run() noexcept -> void override {
            task_cancel.reset();
            cancel.reset();
            auto handle = std::exchange(continuation, {});
            handle.resume();
        }
    };

    /**
     * @brief Convert a relative wait to a deadline that is never earlier than requested.
     * @tparam Rep Duration representation.
     * @tparam Period Duration tick period.
     * @param duration Requested wait; values that are not positive yield the current time.
     * @return Current time plus the duration rounded up to steady_clock ticks, saturated at the maximum time point.
     */
    template <typename Rep, typename Period>
    [[nodiscard]] auto coro_timer_deadline(std::chrono::duration<Rep, Period> duration) -> std::chrono::steady_clock::time_point {
        using Clock = std::chrono::steady_clock;
        using Wide = std::chrono::duration<long double, Clock::period>;
        auto const now = Clock::now();
        if (!(duration > duration.zero())) {
            return now;
        }
        // Compare in floating point so coarse and fine periods saturate before ceil can overflow;
        // the margin absorbs rounding near the representable limit.
        if (Wide{ duration } >= Wide{ (Clock::time_point::max)() - now } - std::chrono::seconds{ 1 }) {
            return (Clock::time_point::max)();
        }
        return now + std::chrono::ceil<Clock::duration>(duration);
    }

    /**
     * @brief Serialize curl operations and timers on one I/O thread and resume tasks on a second thread.
     * @note Submission, timer arming, cancellation, and shutdown synchronize the multi handle's lifetime.
     * Internal transfer entry points accept only exclusively owned, freshly configured sessions.
     */
    class CoroRuntime {
        std::mutex m_mutex;
        std::mutex m_cleanup_mutex;
        std::condition_variable m_completion_ready;
        std::condition_variable m_io_ready; ///< Sleep without polling while no transfers are active.
        std::unique_ptr<curl::CurlMultiHolder> m_multi;
        std::shared_ptr<CoroTransfer> m_pending_head;
        std::shared_ptr<CoroTransfer> m_pending_tail;
        std::shared_ptr<CoroWork> m_completed_head;
        std::shared_ptr<CoroWork> m_completed_tail;
        CoroTimerQueue m_timers;       ///< Armed timers, released to the continuation queue in deadline order.
        Result<void> m_initialization; ///< Curl initialization result, retained for later submissions.
        bool m_stopping{ false };
        bool m_io_done{ false };
        inline static std::atomic_bool s_created{ false }; ///< Whether the lazily created runtime has been constructed.
        std::jthread m_io_thread;
        std::jthread m_completion_thread;

    public:
        /**
         * @brief Create the runtime once, on first use.
         */
        static auto Instance() -> CoroRuntime& {
            /**
             * @brief Join at normal exit, retaining live state if exit begins on a runtime thread.
             */
            struct Owner {
                CoroRuntime* runtime{ new CoroRuntime }; ///< Remains a static root during runtime-thread process exit.

                ~Owner() {
                    if (is_coro_runtime_thread()) {
                        runtime->requestStop();
                    } else {
                        delete runtime;
                    }
                }
            };

            static Owner owner;
            return *owner.runtime;
        }

        /**
         * @brief Check whether Instance() has constructed the runtime without creating it.
         * @return True once construction has started, including after cleanup.
         */
        [[nodiscard]] static auto Created() noexcept -> bool { return s_created.load(std::memory_order_acquire); }

        CoroRuntime(CoroRuntime const&) = delete;
        auto operator=(CoroRuntime const&) -> CoroRuntime& = delete;

        /**
         * @brief Destroy the runtime at exit, normally after Cleanup() has already run.
         * @note The singleton owner deletes only outside runtime threads. Process exit on a runtime
         * thread retains the live runtime until termination instead of joining or freeing its own state.
         */
        ~CoroRuntime() {
            Teardown();
        }

        /**
         * @brief Reject startup after permanent cleanup.
         * @return Success or the first operation error.
         */
        auto CheckRunning() -> Result<void> {
            std::lock_guard lock{ m_mutex };
            if (m_stopping) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::Coro: runtime has been cleaned up." }
                };
            }
            return m_initialization;
        }

        /**
         * @brief Publish a transfer; no operation after publication may throw.
         * @param transfer Transfer whose continuation and cancellation callback are already installed.
         * @return Success or the first operation error.
         */
        auto Submit(std::shared_ptr<CoroTransfer> transfer) -> Result<void> {
            std::lock_guard lock{ m_mutex };
            if (m_stopping) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::Coro: runtime has been cleaned up." }
                };
            }
            if (!m_initialization) {
                return m_initialization;
            }
            if (m_pending_tail) {
                m_pending_tail->pending_next = transfer;
            } else {
                m_pending_head = transfer;
            }
            m_pending_tail = std::move(transfer);
            m_io_ready.notify_one();
            (void)curl_multi_wakeup(m_multi->handle);
            return {};
        }

        /**
         * @brief Queue work without invoking it inline, synchronizing acceptance with shutdown.
         * @param work Work whose frame and ownership are installed before publication.
         * @return Success or an initialization/shutdown error; failed work is never queued.
         */
        auto Post(std::shared_ptr<CoroWork> work) -> Result<void> {
            {
                std::lock_guard lock{ m_mutex };
                if (m_stopping) {
                    return std::unexpected{
                        Error{ ErrorCode::FAILED_INIT, "mcr::Coro: runtime has been cleaned up." }
                    };
                }
                if (!m_initialization) {
                    return m_initialization;
                }
                append(m_completed_head, m_completed_tail, std::move(work));
            }
            m_completion_ready.notify_one();
            return {};
        }

        /**
         * @brief Arm a timer whose continuation and cancellation callbacks are already installed.
         * @param entry Preallocated deadline entry owning the timer, so arming never allocates.
         * @return Success or an initialization/shutdown error; a rejected timer is never queued.
         * @note A stop request or reached deadline completes immediately through the continuation queue;
         * a stop request takes precedence. Callbacks signalled before arming are observed here.
         */
        auto Arm(CoroTimerQueue::node_type entry) -> Result<void> {
            auto& timer = *entry.mapped();
            {
                std::lock_guard lock{ m_mutex };
                if (m_stopping) {
                    return std::unexpected{
                        Error{ ErrorCode::FAILED_INIT, "mcr::Coro: runtime has been cleaned up." }
                    };
                }
                if (!m_initialization) {
                    return m_initialization;
                }
                if (timer.stop.stop_requested() || timer.task_stop.stop_requested()) {
                    timer.outcome = CoroTimerOutcome::CANCELLED;
                } else if (entry.key() <= std::chrono::steady_clock::now()) {
                    timer.outcome = CoroTimerOutcome::EXPIRED;
                } else {
                    auto const position = m_timers.insert(std::move(entry));
                    timer.position = position;
                    timer.armed = true;
                    if (position == m_timers.begin()) {
                        // A new earliest deadline shortens both idle waiting and active polling.
                        m_io_ready.notify_one();
                        (void)curl_multi_wakeup(m_multi->handle);
                    }
                    return {};
                }
                append(m_completed_head, m_completed_tail, std::move(entry.mapped()));
            }
            m_completion_ready.notify_one();
            return {};
        }

        /**
         * @brief Complete an armed timer after a stop request, leaving finished timers unchanged.
         * @param timer Timer whose cancellation callback is still registered.
         * @note The I/O thread recomputes its wait when it next wakes, at the latest by the removed deadline.
         */
        auto Cancel(CoroTimer& timer) noexcept -> void {
            {
                std::lock_guard lock{ m_mutex };
                if (!timer.armed) {
                    return;
                }
                auto entry = m_timers.extract(timer.position);
                timer.armed = false;
                timer.outcome = CoroTimerOutcome::CANCELLED;
                append(m_completed_head, m_completed_tail, std::move(entry.mapped()));
            }
            m_completion_ready.notify_one();
        }

        /**
         * @brief Wake polling without racing destruction of the multi handle.
         */
        auto Wake() noexcept -> void {
            std::lock_guard lock{ m_mutex };
            if (m_multi) {
                (void)curl_multi_wakeup(m_multi->handle);
            }
        }

        /**
         * @brief Cancel unfinished transfers and timers, drain continuations, join threads, and release curl.
         * @note Repeated calls are harmless; later submissions fail. Call before curl_global_cleanup.
         * @return Success or the first operation error.
         */
        auto Cleanup() -> Result<void> {
            if (is_coro_runtime_thread()) {
                return std::unexpected{
                    Error{ ErrorCode::RECURSIVE_API_CALL, "mcr::Coro::Cleanup: must be called outside runtime threads." }
                };
            }
            Teardown();
            return {};
        }

    private:
        /**
         * @brief Stop accepting work and wake both idle waiting and active curl polling.
         */
        auto requestStop() noexcept -> void {
            {
                std::lock_guard lock{ m_mutex };
                m_stopping = true;
                if (m_multi) {
                    (void)curl_multi_wakeup(m_multi->handle);
                }
            }
            m_io_ready.notify_all();
        }

        /**
         * @brief Initialize curl and start the I/O and continuation workers.
         */
        CoroRuntime() {
            s_created.store(true, std::memory_order_release);
            auto holder = curl::CurlMultiHolder::Create();
            if (!holder) {
                m_initialization = std::unexpected{ std::move(holder.error()) };
                return;
            }
            m_multi = std::make_unique<curl::CurlMultiHolder>(std::move(*holder));
            try {
                m_completion_thread = std::jthread{ [this] { runCompletions(); } };
                m_io_thread = std::jthread{ [this] { runIo(); } };
            } catch (...) {
                {
                    std::lock_guard lock{ m_mutex };
                    m_io_done = true;
                }
                m_completion_ready.notify_all();
                if (m_completion_thread.joinable()) {
                    m_completion_thread.join();
                }
                throw;
            }
        }

        /**
         * @brief Cancel unfinished transfers, join threads, and release curl without reporting an outcome.
         * @note Shared by Cleanup and the destructor; callers must already be outside runtime threads.
         */
        auto Teardown() -> void {
            std::lock_guard cleanup_lock{ m_cleanup_mutex };
            requestStop();
            if (m_io_thread.joinable()) {
                m_io_thread.join();
            }
            if (m_completion_thread.joinable()) {
                m_completion_thread.join();
            }
            std::lock_guard lock{ m_mutex };
            m_multi.reset();
        }

        /**
         * @brief Append without allocating; caller owns the relevant queue lock.
         */
        static auto append(
            std::shared_ptr<CoroWork>& head,
            std::shared_ptr<CoroWork>& tail,
            std::shared_ptr<CoroWork> value
        ) noexcept -> void {
            if (tail) {
                tail->next = value;
            } else {
                head = value;
            }
            tail = std::move(value);
        }

        /**
         * @brief Publish an outcome only after removing the easy handle from the multi handle.
         */
        auto complete(
            std::shared_ptr<CoroTransfer> transfer,
            CURLcode result,
            std::exception_ptr error = {},
            CURLMcode multi_error = CURLM_OK
        ) noexcept -> void {
            transfer->result = result;
            transfer->error = std::move(error);
            transfer->multi_error = multi_error;
            {
                std::lock_guard lock{ m_mutex };
                append(m_completed_head, m_completed_tail, std::move(transfer));
            }
            m_completion_ready.notify_one();
        }

        /**
         * @brief Queue timers in deadline order without allocating; caller owns the runtime lock.
         * @param now Deadlines at or before this time complete as expired.
         * @param closing Whether later deadlines also complete, reporting runtime shutdown.
         */
        auto releaseTimers(
            std::chrono::steady_clock::time_point now,
            bool closing
        ) noexcept -> void {
            bool released{ false };
            while (!m_timers.empty() && (closing || m_timers.begin()->first <= now)) {
                auto entry = m_timers.extract(m_timers.begin());
                auto& timer = *entry.mapped();
                timer.armed = false;
                timer.outcome = entry.key() <= now ? CoroTimerOutcome::EXPIRED : CoroTimerOutcome::CLOSED;
                append(m_completed_head, m_completed_tail, std::move(entry.mapped()));
                released = true;
            }
            if (released) {
                m_completion_ready.notify_one();
            }
        }

        /**
         * @brief Resume outside all runtime locks and outside curl's callback stack.
         */
        auto runCompletions() noexcept -> void {
            coro_runtime_thread = true;
            for (;;) {
                std::shared_ptr<CoroWork> work;
                {
                    std::unique_lock lock{ m_mutex };
                    m_completion_ready.wait(lock, [this] { return m_completed_head || m_io_done; });
                    if (!m_completed_head) {
                        break;
                    }
                    work = std::move(m_completed_head);
                    m_completed_head = std::move(work->next);
                    if (!m_completed_head) {
                        m_completed_tail.reset();
                    }
                }
                work->Run();
            }
            coro_runtime_thread = false;
        }

        /**
         * @brief Drive all active HTTP transfers and timers, collecting each completion independently.
         */
        auto runIo() noexcept -> void {
            coro_runtime_thread = true;
            std::unordered_map<CURL*, std::shared_ptr<CoroTransfer>> active;
            std::exception_ptr failure;
            CURLMcode multi_error{ CURLM_OK };
            try {
                for (;;) {
                    std::shared_ptr<CoroTransfer> pending;
                    std::optional<std::chrono::steady_clock::time_point> deadline;
                    {
                        std::unique_lock lock{ m_mutex };
                        for (;;) {
                            releaseTimers(std::chrono::steady_clock::now(), false);
                            if (m_stopping || m_pending_head || !active.empty()) {
                                break;
                            }
                            // Without transfers, sleep until new work or the earliest deadline.
                            if (m_timers.empty()) {
                                m_io_ready.wait(lock);
                            } else {
                                // Copy the key: cancellation may free its node while the lock is released.
                                auto const next = m_timers.begin()->first;
                                m_io_ready.wait_until(lock, next);
                            }
                        }
                        if (m_stopping) {
                            break;
                        }
                        pending = std::move(m_pending_head);
                        m_pending_tail.reset();
                        if (!m_timers.empty()) {
                            deadline = m_timers.begin()->first;
                        }
                    }
                    while (pending) {
                        auto transfer = std::move(pending);
                        pending = std::move(transfer->pending_next);
                        if (transfer->stop.stop_requested()) {
                            complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK);
                            continue;
                        }
                        auto* handle = transfer->session->GetCurlHolder()->handle;
                        bool inserted{};
                        try {
                            // Claim the handle before preparing it: a duplicate must not change an active transfer.
                            auto const entry = active.try_emplace(handle, transfer);
                            inserted = entry.second;
                            if (!inserted) {
                                complete(std::move(transfer), CURLE_FAILED_INIT, {}, CURLM_ADDED_ALREADY);
                                continue;
                            }
                            transfer->setup_result = transfer->prepare(*transfer->session);
                            if (!transfer->setup_result) {
                                active.erase(handle);
                                complete(std::move(transfer), CURLE_FAILED_INIT);
                                continue;
                            }
                            transfer->prepared = true;
                            auto const added = curl_multi_add_handle(m_multi->handle, handle);
                            if (added != CURLM_OK) {
                                active.erase(handle);
                                complete(std::move(transfer), CURLE_FAILED_INIT, {}, added);
                            }
                        } catch (...) {
                            if (inserted) {
                                active.erase(handle);
                            }
                            complete(std::move(transfer), CURLE_FAILED_INIT, std::current_exception());
                        }
                    }
                    for (auto it = active.begin(); it != active.end();) {
                        if (it->second->stop.stop_requested()) {
                            auto* handle = it->first;
                            auto transfer = std::move(it->second);
                            it = active.erase(it);
                            (void)curl_multi_remove_handle(m_multi->handle, handle);
                            complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK);
                        } else {
                            ++it;
                        }
                    }
                    int running{};
                    multi_error = curl_multi_perform(m_multi->handle, &running);
                    if (multi_error != CURLM_OK) {
                        break;
                    }
                    int queued{};
                    while (auto* message = curl_multi_info_read(m_multi->handle, &queued)) {
                        if (message->msg == CURLMSG_DONE) {
                            auto* handle = message->easy_handle;
                            auto result = message->data.result;
                            auto node = active.extract(handle);
                            if (!node.empty()) {
                                (void)curl_multi_remove_handle(m_multi->handle, handle);
                                complete(std::move(node.mapped()), result);
                            }
                        }
                    }
                    if (active.empty()) {
                        continue;
                    }
                    // Arm interrupts polling whenever it adds an earlier deadline than this snapshot.
                    std::chrono::milliseconds timeout{ 100 };
                    if (deadline) {
                        auto const remaining = *deadline - std::chrono::steady_clock::now();
                        timeout = (std::min)(timeout, remaining > remaining.zero() ? std::chrono::ceil<std::chrono::milliseconds>(remaining) : std::chrono::milliseconds{});
                    }
                    multi_error = curl_multi_poll(m_multi->handle, nullptr, 0, static_cast<int>(timeout.count()), nullptr);
                    if (multi_error != CURLM_OK) {
                        break;
                    }
                }
            } catch (...) {
                failure = std::current_exception();
            }
            std::shared_ptr<CoroTransfer> pending;
            {
                std::lock_guard lock{ m_mutex };
                m_stopping = true;
                pending = std::move(m_pending_head);
                m_pending_tail.reset();
                // Deadlines already reached still succeed; later ones report shutdown.
                releaseTimers(std::chrono::steady_clock::now(), true);
            }
            for (auto& [handle, transfer] : active) {
                (void)curl_multi_remove_handle(m_multi->handle, handle);
                complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK, failure, multi_error);
            }
            while (pending) {
                auto transfer = std::move(pending);
                pending = std::move(transfer->pending_next);
                complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK, failure, multi_error);
            }
            {
                std::lock_guard lock{ m_mutex };
                m_io_done = true;
            }
            m_completion_ready.notify_all();
            coro_runtime_thread = false;
        }
    };

    auto WakeCoroRuntime::operator()() const noexcept -> void {
        runtime->Wake();
    }

    auto CancelCoroTimer::operator()() const noexcept -> void {
        runtime->Cancel(*timer);
    }

    /**
     * @brief Resume an existing coroutine through the runtime's serialized queue.
     */
    class CoroScheduleAwaiter {
        std::shared_ptr<CoroResume> m_work{ std::make_shared<CoroResume>() }; ///< Published continuation storage.
        Result<void> m_status;                                                ///< Rejection returned without suspending or transferring execution.

    public:
        /**
         * @brief Prepare a single-use queued resumption before its coroutine suspends.
         */
        CoroScheduleAwaiter() = default;

        CoroScheduleAwaiter(CoroScheduleAwaiter const&) = delete;
        CoroScheduleAwaiter(CoroScheduleAwaiter&&) = default;

        /**
         * @brief Validate ownership and enqueue even when already on the continuation thread.
         * @throws std::logic_error If the awaiter was moved or already consumed.
         */
        auto await_ready() const -> bool {
            if (!m_work) {
                throw std::logic_error{ "mcr::Coro::Schedule: awaiter has been consumed." };
            }
            return false;
        }

        /**
         * @brief Publish only fully initialized state and avoid accessing this after acceptance.
         * @param continuation Already suspended coroutine to resume.
         * @return Whether the runtime accepted responsibility for resumption.
         * @note [expr.await] suspends before this call. Acceptance may destroy the awaiter before return.
         */
        auto await_suspend(std::coroutine_handle<> continuation) -> bool {
            auto work = std::move(m_work);
            work->continuation = continuation;
            auto status = CoroRuntime::Instance().Post(std::move(work));
            if (!status) {
                m_status = std::move(status);
                return false;
            }
            // Only locals may be accessed after acceptance, including during their destruction.
            return true;
        }

        /**
         * @brief Report success on the continuation thread or rejection on the calling thread.
         */
        auto await_resume() -> Result<void> { return std::move(m_status); }
    };

    /**
     * @brief Resume an existing coroutine through the runtime's serialized queue at a steady-clock deadline.
     */
    class CoroTimerAwaiter {
        std::shared_ptr<CoroTimer> m_timer; ///< Outcome storage shared with the runtime.
        CoroTimerQueue::node_type m_entry;  ///< Deadline entry allocated before publication.
        Result<void> m_status;              ///< Rejection returned without suspending or transferring execution.

    public:
        /**
         * @brief Allocate all timer state before its coroutine suspends.
         * @param deadline Earliest steady-clock time at which to resume.
         * @param token Explicit cancellation token, observed together with the awaiting task's token.
         */
        CoroTimerAwaiter(
            std::chrono::steady_clock::time_point deadline,
            std::stop_token token
        ) : m_timer{ std::make_shared<CoroTimer>(deadline, std::move(token)) } {
            CoroTimerQueue staging;
            staging.emplace(deadline, m_timer);
            m_entry = staging.extract(staging.begin());
        }

        CoroTimerAwaiter(CoroTimerAwaiter const&) = delete;
        CoroTimerAwaiter(CoroTimerAwaiter&&) = default;

        /**
         * @brief Validate ownership and suspend even for past deadlines.
         * @throws std::logic_error If the awaiter was moved or already consumed.
         */
        auto await_ready() const -> bool {
            if (!m_timer || m_entry.empty()) {
                throw std::logic_error{ "mcr::Coro::ScheduleAt: awaiter has been consumed." };
            }
            return false;
        }

        /**
         * @brief Observe the task's stop token, then publish without accessing this after acceptance.
         * @tparam Promise Awaiting promise, optionally exposing GetStopToken.
         * @param continuation Already suspended coroutine to resume.
         * @return Whether the runtime accepted responsibility for resumption.
         * @note Cancellation may resume the coroutine on the continuation thread before this returns.
         */
        template <typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> continuation) -> bool {
            auto timer = m_timer;
            auto entry = std::move(m_entry);
            auto& runtime = CoroRuntime::Instance();
            timer->continuation = continuation;
            if constexpr (requires(Promise& promise) {
                              { promise.GetStopToken() } -> std::convertible_to<std::stop_token>;
                          }) {
                timer->task_stop = continuation.promise().GetStopToken();
            }
            // Register before arming: Arm observes, under the runtime lock, any signal that precedes it.
            timer->cancel.emplace(timer->stop, CancelCoroTimer{ &runtime, timer.get() });
            timer->task_cancel.emplace(timer->task_stop, CancelCoroTimer{ &runtime, timer.get() });
            auto status = runtime.Arm(std::move(entry));
            if (!status) {
                timer->task_cancel.reset();
                timer->cancel.reset();
                timer->continuation = {};
                m_status = std::move(status);
                return false;
            }
            // Only locals may be accessed after acceptance, including during their destruction.
            return true;
        }

        /**
         * @brief Report the wait outcome on the continuation thread or rejection on the calling thread.
         * @return Success at the deadline, ABORTED_BY_CALLBACK after a stop request, or FAILED_INIT on shutdown.
         */
        auto await_resume() -> Result<void> {
            auto timer = std::move(m_timer);
            if (!m_status) {
                return std::move(m_status);
            }
            if (timer->outcome == CoroTimerOutcome::EXPIRED) {
                return {};
            }
            if (timer->outcome == CoroTimerOutcome::CANCELLED) {
                return std::unexpected{
                    Error{ ErrorCode::ABORTED_BY_CALLBACK, "mcr::Coro: timed wait was cancelled." }
                };
            }
            return std::unexpected{
                Error{ ErrorCode::FAILED_INIT, "mcr::Coro: runtime stopped before the timed wait expired." }
            };
        }
    };

    /**
     * @brief Await one exclusively owned session through the shared curl multi runtime.
     */
    class CoroTransferAwaiter {
        std::shared_ptr<CoroTransfer> m_transfer;

    public:
        /**
         * @brief Own all transfer state before publishing a continuation to the runtime.
         * @param session Session exclusively used by this transfer.
         * @param prepare Request preparation performed on the I/O thread.
         * @param token Cooperative cancellation token.
         */
        CoroTransferAwaiter(
            std::shared_ptr<Session> session,
            std::function<Result<void>(Session&)> prepare,
            std::stop_token token
        )
            : m_transfer{ std::make_shared<CoroTransfer>(std::move(session), std::move(prepare), token) } {}

        /**
         * @brief Always suspend to serialize curl access through the runtime.
         */
        auto await_ready() const noexcept -> bool { return false; }

        /**
         * @brief Publish fully initialized state without accessing the awaiter after acceptance.
         * @param continuation Already suspended coroutine to resume.
         * @return Whether the runtime accepted responsibility for resumption.
         * @note The local shared owner survives even if completion destroys this awaiter before return.
         */
        auto await_suspend(std::coroutine_handle<> continuation) -> bool {
            auto transfer = m_transfer;
            auto& runtime = CoroRuntime::Instance();
            transfer->continuation = continuation;
            transfer->wake.emplace(transfer->stop, WakeCoroRuntime{ &runtime });
            auto status = runtime.Submit(transfer);
            if (!status) {
                transfer->setup_result = std::move(status);
                return false;
            }
            return true;
        }

        /**
         * @brief Consume the transfer outcome on the continuation thread.
         * @return Response or setup error.
         * @throws Any exception captured while preparing the transfer or running user callbacks.
         */
        auto await_resume() -> Result<Response> {
            if (m_transfer->error) {
                std::rethrow_exception(m_transfer->error);
            }
            if (!m_transfer->setup_result) {
                return std::unexpected{ std::move(m_transfer->setup_result.error()) };
            }
            if (m_transfer->multi_error != CURLM_OK) {
                return std::unexpected{
                    Error{ error_code_from_curl_multi(m_transfer->multi_error), std::string{ "mcr::Coro: " } + curl_multi_strerror(m_transfer->multi_error) }
                };
            }
            if (!m_transfer->prepared) {
                Response response;
                response.error = Error{ static_cast<std::int32_t>(m_transfer->result), "Coroutine request cancelled before transfer." };
                return response;
            }
            return m_transfer->session->Complete(m_transfer->result);
        }
    };
} // namespace mcr::detail

export namespace mcr {
    /**
     * @brief Manage the coroutine runtime independently of the future-based Async runtime.
     */
    class Coro {
    public:
        /**
         * @brief Check whether blocking waits and cleanup are forbidden on this thread.
         * @return True on the runtime's I/O and continuation threads.
         */
        [[nodiscard]] static auto IsRuntimeThread() noexcept -> bool { return detail::is_coro_runtime_thread(); }

        /**
         * @brief Start a cold task on the continuation thread and retain its result and cancellation.
         * @tparam T Value, lvalue reference, or void result type.
         * @param task Task consumed by this call; started tasks keep their existing execution.
         * @return Started task or an initialization/shutdown error before starting a cold task.
         * @throws std::logic_error If the task is empty or has already been consumed.
         * @note Accepted work runs outside runtime locks. External awaiters may change subsequent threads.
         */
        template <typename T>
        [[nodiscard]] static auto Spawn(Task<T> task) -> Result<Task<T>> {
            if (!task.m_state) {
                throw std::logic_error{ "mcr::Coro::Spawn: task is empty." };
            }
            if (task.m_coroutine) {
                auto work = std::make_shared<detail::CoroResume>();
                work->continuation = std::exchange(task.m_coroutine, {});
                work->owns_frame = true;
                if (auto status = detail::CoroRuntime::Instance().Post(std::move(work)); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
            return std::move(task);
        }

        /**
         * @brief Yield to the continuation thread using a queued resumption.
         * @return Awaiter yielding Result<void>; rejection continues on the calling thread.
         * @note The waiting coroutine must remain alive until the queued resumption runs.
         */
        [[nodiscard]] static auto Schedule() -> detail::CoroScheduleAwaiter { return {}; }

        /**
         * @brief Wait without blocking a thread, resuming through the continuation queue after a duration.
         * @tparam Rep Duration representation.
         * @tparam Period Duration tick period.
         * @param duration Minimum wait from this call, rounded up to steady_clock ticks; values beyond its range saturate.
         * @param token Optional cancellation token, observed together with the awaiting Task's token.
         * @return Awaiter yielding Result<void> with the same outcomes as ScheduleAt().
         * @note The deadline is fixed when this function is called; a non-positive duration behaves like Schedule().
         */
        template <typename Rep, typename Period>
        [[nodiscard]] static auto ScheduleAfter(
            std::chrono::duration<Rep, Period> duration,
            std::stop_token token = {}
        ) -> detail::CoroTimerAwaiter {
            return ScheduleAt(detail::coro_timer_deadline(duration), std::move(token));
        }

        /**
         * @brief Wait without blocking a thread, resuming through the continuation queue at a steady-clock deadline.
         * @param deadline Earliest resumption time; a reached deadline behaves like Schedule().
         * @param token Optional cancellation token, observed together with the awaiting Task's token.
         * @return Single-use awaiter yielding success at the deadline, ABORTED_BY_CALLBACK once either token
         * is stopped, or FAILED_INIT when the runtime stops first. Rejection continues on the calling thread.
         * @note A token stopped before the wait takes precedence over a reached deadline. Only the first
         * outcome is reported, and the waiting coroutine must remain alive until it resumes.
         */
        [[nodiscard]] static auto ScheduleAt(
            std::chrono::steady_clock::time_point deadline,
            std::stop_token token = {}
        ) -> detail::CoroTimerAwaiter {
            return detail::CoroTimerAwaiter{ deadline, std::move(token) };
        }

        /**
         * @brief Start lazily and return FAILED_INIT after permanent cleanup.
         * @return Success or the first operation error.
         */
        [[nodiscard]] static auto Startup() -> Result<void> { return detail::CoroRuntime::Instance().CheckRunning(); }

        /**
         * @brief Cancel outstanding HTTP transfers and timed waits, then wait for their continuations to finish.
         * @note Call outside tasks before curl_global_cleanup; this permanently closes the runtime.
         * @return Success or the first operation error.
         */
        [[nodiscard]] static auto Cleanup() -> Result<void> { return detail::CoroRuntime::Instance().Cleanup(); }
    };
} // namespace mcr
