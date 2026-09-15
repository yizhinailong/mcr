/**
 * @file test_async.cpp
 * @brief Verify global pool startup, task forwarding, cancellation semantics, and permanent cleanup.
 */
import std;
import mcr;
import mcr.singleton;
import mcr.threadpool;

static_assert(std::is_base_of_v<mcr::utils::ThreadPool, mcr::GlobalThreadPool>);
static_assert(std::is_base_of_v<mcr::utils::Singleton<mcr::GlobalThreadPool>, mcr::GlobalThreadPool>);
static_assert(!std::is_default_constructible_v<mcr::GlobalThreadPool>);
static_assert(!std::is_copy_constructible_v<mcr::GlobalThreadPool>);
static_assert(!std::is_move_constructible_v<mcr::GlobalThreadPool>);
static_assert(std::has_virtual_destructor_v<mcr::GlobalThreadPool>);
static_assert(std::is_same_v<std::remove_cvref_t<decltype(mcr::async([] { return 42; }).value())>, mcr::utils::AsyncWrapper<int, false>>);
static_assert(std::is_same_v<std::remove_cvref_t<decltype(mcr::async<true>([] {}).value())>, mcr::utils::AsyncWrapper<void, true>>);
static_assert(std::is_same_v<std::remove_cvref_t<decltype(mcr::async([](int& value) -> int& { return value; }, std::declval<std::reference_wrapper<int>>()).value())>, mcr::utils::AsyncWrapper<int&, false>>);

namespace {

    using namespace std::chrono_literals;

    auto require(bool condition, std::string_view message) -> void {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    template <typename Exception, typename Fn>
    auto rejects(Fn&& function) -> bool {
        try {
            std::forward<Fn>(function)();
        } catch (Exception const&) {
            return true;
        }
        return false;
    }

    auto check_startup() -> void {
        require(rejects<std::logic_error>([] { mcr::Async::Cleanup(); }), "cleanup before initialization must report the singleton lifecycle error");
        auto worker{ mcr::async([] { return std::this_thread::get_id(); }).value() };
        require(worker.Get() != std::this_thread::get_id(), "async must lazily start the global pool and run on a worker");
        auto* pool{ mcr::GlobalThreadPool::GetInstance() };
        require(pool && pool == mcr::GlobalThreadPool::GetInstance() && pool->IsStarted(), "submissions and direct access must share one started pool");
        require(pool->GetMinThreadNum() == mcr::utils::DEFAULT_THREAD_POOL_MIN_THREAD_NUM && pool->GetMaxThreadNum() == mcr::utils::DEFAULT_THREAD_POOL_MAX_THREAD_NUM && pool->GetMaxIdleTime() == mcr::utils::DEFAULT_THREAD_POOL_MAX_IDLE_TIME, "lazy startup must use the existing thread-pool defaults");
        mcr::Async::Startup(99, 0, 0ms).value();
        require(pool->GetMinThreadNum() == mcr::utils::DEFAULT_THREAD_POOL_MIN_THREAD_NUM, "startup must ignore arguments when the pool is already running");
        pool->Stop();
        pool->SetMinThreadNum(0);
        pool->SetMaxThreadNum(1);
        auto const old_idle{ pool->GetMaxIdleTime() };
        require(!mcr::Async::Startup(0, 0), "a zero maximum must be rejected before configuration changes");
        require(!mcr::Async::Startup(3, 2), "a minimum greater than the maximum must be rejected");
        require(!mcr::Async::Startup(2, 3, 0ms), "zero idle time must be rejected before changing otherwise valid thread limits");
        require(!mcr::Async::Startup(2, 3, -1ms), "negative idle time must be rejected");
        require(pool->IsStopped() && pool->GetMinThreadNum() == 0 && pool->GetMaxThreadNum() == 1 && pool->GetMaxIdleTime() == old_idle, "invalid startup settings must preserve all prior configuration and the stopped state");

        mcr::Async::Startup(2, 3, 100ms).value();
        require(pool->GetMinThreadNum() == 2 && pool->GetMaxThreadNum() == 3 && pool->GetCurrentThreadNum() == 2 && pool->GetMaxIdleTime() == 100ms, "startup must support increasing the minimum above the previous maximum");
        pool->Pause();
        mcr::Async::Startup(0, 1, 50ms).value();
        require(pool->GetMinThreadNum() == 2 && pool->GetMaxThreadNum() == 3, "startup must also leave a paused pool unchanged");
        pool->Stop();
        mcr::Async::Startup(0, 1, 50ms).value();
        require(pool->IsStarted() && pool->GetCurrentThreadNum() == 0 && pool->GetMaxThreadNum() == 1, "startup must support lowering the maximum below the previous minimum and a zero-worker minimum");
        require(mcr::async([] { return 7; }).value().Get() == 7, "submission must create a worker when the started pool has no workers");
        pool->Stop();
        mcr::Async::Startup().value();
        require(pool->GetMinThreadNum() == mcr::utils::DEFAULT_THREAD_POOL_MIN_THREAD_NUM && pool->GetMaxThreadNum() == mcr::utils::DEFAULT_THREAD_POOL_MAX_THREAD_NUM && pool->GetMaxIdleTime() == mcr::utils::DEFAULT_THREAD_POOL_MAX_IDLE_TIME, "default startup must restore all default settings on a stopped pool");
        pool->Stop();
        mcr::Async::Startup(1, 3, 100ms).value();
    }

    struct Receiver {
        int value{ 10 };

        auto Add(int amount) -> int { return value += amount; }
    };

    struct MoveOnlyCallable {
        std::unique_ptr<int> value{ std::make_unique<int>(20) };

        auto operator()(std::unique_ptr<int> argument) && -> std::unique_ptr<int> {
            *value += *argument;
            return std::move(value);
        }
    };

    auto check_results() -> void {
        require(mcr::async([](int first, int second) { return first + second; }, 20, 22).value().Get() == 42, "arguments and value results must pass through the global helper");
        auto unique{ mcr::async(MoveOnlyCallable{}, std::make_unique<int>(22)).value().Get() };
        require(unique && *unique == 42, "move-only callables, arguments, and return values must be supported");
        Receiver receiver;
        require(mcr::async(&Receiver::Add, &receiver, 5).value().Get() == 15, "member-function pointers must be forwarded through std::invoke");
        auto reference{ mcr::async(&Receiver::value, std::ref(receiver)).value() };
        reference.Get() = 40;
        auto changed{ mcr::async([](int& value) { value += 2; }, std::ref(receiver.value)).value() };
        changed.Get();
        require(receiver.value == 42, "reference arguments, reference results, and void results must retain their semantics");
        auto failure{ mcr::async([]() -> int { throw std::runtime_error{ "task failure" }; }).value() };
        require(rejects<std::runtime_error>([&] { (void)failure.Get(); }) && !failure.Valid(), "callable exceptions must reach Get and consume its future");
        require(mcr::async([] { return 9; }).value().Get() == 9, "a failed task must not prevent later submissions");
        auto cancellable{ mcr::async<true>([] { return 11; }).value() };
        require(!cancellable.IsCancelled() && cancellable.Get() == 11, "uncancelled wrappers must deliver results normally");
    }

    auto check_concurrent_submission() -> void {
        std::array<bool, 4> completed{};
        {
            std::vector<std::jthread> producers;
            for (std::size_t index{ 0 }; index < completed.size(); ++index) {
                producers.emplace_back([index, &completed] {
                    try {
                        int sum{ 0 };
                        for (int i{ 0 }; i < 8; ++i) {
                            sum += mcr::async([](int value) { return value * 2; }, i).value().Get();
                        }
                        completed[index] = sum == 56;
                    } catch (...) {
                        completed[index] = false;
                    }
                });
            }
        }
        require(std::ranges::all_of(completed, [](bool value) { return value; }), "concurrent producers must retrieve their own submitted results");
    }

    auto check_cancellation() -> void {
        auto* pool{ mcr::GlobalThreadPool::GetInstance() };
        pool->Wait();
        pool->Pause();
        auto calls{ std::make_shared<std::atomic_int>(0) };
        auto cancelled{ mcr::async<true>([calls] { return ++*calls; }).value() };
        auto unaffected{ mcr::async<true>([] { return 42; }).value() };
        require(cancelled.Cancel() == mcr::utils::CancellationResult::success && cancelled.IsCancelled() && !cancelled.Valid(), "cancelling must update wrapper state before queued execution");
        require(cancelled.Cancel() == mcr::utils::CancellationResult::invalid_operation && unaffected.Valid() && !unaffected.IsCancelled(), "cancellation flags must be independent and repeated requests must report invalid_operation");
        require(rejects<std::logic_error>([&] { (void)cancelled.Get(); }) && rejects<std::logic_error>([&] { cancelled.Wait(); }), "cancelled wrappers must reject result access and waits");
        auto shared{ cancelled.Share() };
        {
            auto discarded{ mcr::async<true>([calls] { ++*calls; }).value() };
        }
        pool->Resume();
        require(shared.wait_for(3s) == std::future_status::ready, "cancelled queued work must still run under cpr's helper semantics");
        (void)shared.get();
        require(unaffected.Get() == 42, "cancelling one wrapper must not change another task's result");
        pool->Wait();
        require(calls->load() == 2, "both cancellation and wrapper destruction must leave task execution intact");

        auto release{ std::make_shared<std::promise<void>>() };
        auto gate{ release->get_future().share() };
        auto started{ std::make_shared<std::promise<void>>() };
        auto began{ started->get_future() };
        auto running{ mcr::async<true>([started, gate] {
                          started->set_value();
                          gate.wait();
                          return 23;
                      }).value() };
        bool const did_start{ began.wait_for(3s) == std::future_status::ready };
        auto const cancellation{ running.Cancel() };
        auto       running_result{ running.Share() };
        release->set_value();
        require(did_start && cancellation == mcr::utils::CancellationResult::success && running_result.get() == 23, "cancelling an active wrapper must not interrupt its callable");
    }

    auto check_cleanup() -> void {
        auto* pool{ mcr::GlobalThreadPool::GetInstance() };
        pool->Stop();
        mcr::Async::Startup(1, 1).value();
        auto release{ std::make_shared<std::promise<void>>() };
        auto gate{ release->get_future().share() };
        auto started{ std::make_shared<std::promise<void>>() };
        auto began{ started->get_future() };
        auto running{ mcr::async([started, gate] {
                          started->set_value();
                          gate.wait();
                          return 7;
                      }).value() };
        if (began.wait_for(3s) != std::future_status::ready) {
            release->set_value();
            throw std::runtime_error{ "cleanup fixture must start the active task" };
        }
        auto queued_ran{ std::make_shared<std::atomic_bool>(false) };
        auto queued{ mcr::async([queued_ran] {
                         queued_ran->store(true);
                         return 8;
                     }).value() };
        auto         queued_result{ queued.Share() };
        bool         cancelled_before_release{ false };
        // No singleton access overlaps cleanup. The observer only uses retained future states.
        std::jthread observer{ [&] {
            cancelled_before_release = queued_result.wait_for(3s) == std::future_status::ready && running.WaitFor(0ms) == std::future_status::timeout;
            release->set_value();
        } };
        mcr::Async::Cleanup();
        observer.join();
        require(cancelled_before_release && running.Get() == 7 && !queued_ran->load(), "cleanup must cancel queued work while waiting for the active task to finish");
        bool broken_promise{ false };
        try {
            (void)queued_result.get();
        } catch (std::future_error const& error) {
            broken_promise = error.code() == std::make_error_code(std::future_errc::broken_promise);
        }
        require(broken_promise, "cleanup must expose the thread pool's broken_promise for cancelled queued tasks");
        require(mcr::GlobalThreadPool::GetInstance() == nullptr, "cleanup must permanently release the singleton");
        mcr::Async::Cleanup();
        require(!mcr::async([] {}), "submission after cleanup must return an error");
        require(!mcr::async<true>([] {}), "cancellable submission after cleanup must also return an error");
        require(!mcr::Async::Startup(), "startup after cleanup must not recreate the singleton");
    }

} // namespace

int main() {
    try {
        check_startup();
        check_results();
        check_concurrent_submission();
        check_cancellation();
        check_cleanup();
    } catch (std::exception const& error) {
        std::println("test_async: {}", error.what());
        if (mcr::GlobalThreadPool::GetInstance()) {
            mcr::Async::Cleanup();
        }
        return 1;
    }
    std::println("test_async: ok");
    return 0;
}
