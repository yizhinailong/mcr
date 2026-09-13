/**
 * @file test_threadpool.cpp
 * @brief Verify task ownership, pool lifecycle, resizing, and concurrent submission.
 */
import std;
import mcr;

static_assert(!std::is_copy_constructible_v<mcr::utils::ThreadPool>);
static_assert(!std::is_move_constructible_v<mcr::utils::ThreadPool>);
static_assert(std::has_virtual_destructor_v<mcr::utils::ThreadPool>);

namespace {
    using namespace std::chrono_literals;

    void require(bool condition, std::string_view message) {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    template <typename Predicate>
    auto eventually(Predicate predicate) -> bool {
        auto const deadline = std::chrono::steady_clock::now() + 3s;
        while (!predicate()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    template <typename Fn>
    void rejects_configuration(Fn fn) {
        try {
            fn();
        } catch (std::invalid_argument const&) {
            return;
        }
        throw std::runtime_error{ "invalid configuration must throw invalid_argument" };
    }

    template <typename T>
    auto is_canceled(std::future<T>& future) -> bool {
        if (future.wait_for(3s) != std::future_status::ready) {
            return false;
        }
        try {
            (void)future.get();
        } catch (std::future_error const& error) {
            return error.code() == std::make_error_code(std::future_errc::broken_promise);
        }
        return false;
    }

    void check_configuration() {
        rejects_configuration([] { mcr::utils::ThreadPool pool{ 0, 0 }; });
        rejects_configuration([] { mcr::utils::ThreadPool pool{ 3, 2 }; });
        rejects_configuration([] { mcr::utils::ThreadPool pool{ 1, 2, 0ms }; });
        rejects_configuration([] { mcr::utils::ThreadPool pool{ 1, 2, -1ms }; });

        mcr::utils::ThreadPool pool{ 1, 3, 40ms };
        require(pool.IsStopped() && !pool.IsStarted(), "construction must not start workers");
        require(pool.GetCurrentThreadNum() == 0 && pool.GetIdleThreadNum() == 0, "a stopped pool must have no workers");
        rejects_configuration([&] { pool.SetMinThreadNum(4); });
        rejects_configuration([&] { pool.SetMaxThreadNum(0); });
        rejects_configuration([&] { pool.SetMaxIdleTime(0ms); });
        require(pool.GetMinThreadNum() == 1 && pool.GetMaxThreadNum() == 3 && pool.GetMaxIdleTime() == 40ms, "rejected settings must preserve the previous configuration");
        require(pool.Pause() == 0 && pool.Resume() == 0 && pool.Stop() == -1, "stopped lifecycle calls must retain cpr return values");
        pool.Wait();
        require(pool.Start(99) == 0 && pool.GetCurrentThreadNum() == 3, "Start must clamp its count to the maximum");
        require(pool.Start() == -1, "a second Start must not create more workers");
        require(pool.Stop() == 0 && pool.IsStopped(), "Stop must join workers");
        require(pool.Start() == 0 && pool.GetCurrentThreadNum() == 1, "default Start must use the minimum");
        pool.SetMinThreadNum(2);
        require(pool.GetCurrentThreadNum() == 2, "raising the minimum must create workers");
        rejects_configuration([&] { pool.SetMaxThreadNum(1); });
        pool.SetMaxIdleTime(20ms);
        require(pool.GetMaxIdleTime() == 20ms, "idle timeout changes must be observable");
    }

    void check_tasks() {
        mcr::utils::ThreadPool pool{ 1, 4 };
        require(pool.Submit([](int a, int b) { return a + b; }, 20, 22).get() == 42, "Submit must automatically start a stopped pool and return a result");

        auto moved_argument = pool.Submit([](std::unique_ptr<int> value) { return *value; }, std::make_unique<int>(7));
        auto moved_callable = pool.Submit([value = std::make_unique<int>(9)] { return *value; });
        require(moved_argument.get() == 7 && moved_callable.get() == 9, "move-only arguments and callables must retain ownership");

        struct Counter {
            int value{ 0 };

            auto Add(int amount) -> int { return value += amount; }
        };

        Counter counter;
        require(pool.Submit(&Counter::Add, std::ref(counter), 3).get() == 3, "member pointers must work with reference wrappers");
        pool.Submit([](int& value) { ++value; }, std::ref(counter.value)).get();
        auto reference = pool.Submit([&counter]() -> int& { return counter.value; });
        require(&reference.get() == &counter.value && counter.value == 4, "void tasks and reference results must work");

        std::string source{ "owned argument" };
        pool.Pause();
        auto copied_argument = pool.Submit([](std::string value) { return value; }, source);
        source.clear();
        pool.Resume();
        require(copied_argument.get() == "owned argument", "submitted lvalues must be copied before execution");

        auto failure = pool.Submit([]() -> int { throw std::runtime_error{ "task failure" }; });
        bool propagated{ false };
        try {
            (void)failure.get();
        } catch (std::runtime_error const& error) {
            propagated = std::string_view{ error.what() } == "task failure";
        }
        require(propagated && pool.Submit([] { return true; }).get(), "task exceptions must reach the future without killing a worker");
        for (bool const stop : { false, true }) {
            auto self_wait = pool.Submit([&pool, stop] {
                try {
                    if (stop) {
                        pool.Stop();
                    } else {
                        pool.Wait();
                    }
                } catch (std::logic_error const&) {
                    return true;
                }
                return false;
            });
            require(self_wait.get(), "waiting for the same pool inside a task must fail without deadlocking");
        }
        pool.Wait();
        require(pool.GetIdleThreadNum() == pool.GetCurrentThreadNum(), "Wait must observe all workers as idle");
    }

    void check_pause_and_wait() {
        mcr::utils::ThreadPool pool{ 1, 3 };
        pool.Start();
        for (int round{ 0 }; round < 20; ++round) {
            pool.Pause();
            std::atomic<int>               completed{ 0 };
            std::vector<std::future<void>> futures;
            for (int task{ 0 }; task < 20; ++task) {
                futures.push_back(pool.Submit([&completed] { ++completed; }));
            }
            auto       waiter        = std::async(std::launch::async, [&pool] { pool.Wait(); });
            bool const stayed_paused = futures.front().wait_for(5ms) == std::future_status::timeout && completed == 0;
            bool const wait_blocked  = waiter.wait_for(5ms) == std::future_status::timeout;
            pool.Resume();
            for (auto& future : futures) {
                future.get();
            }
            waiter.get();
            require(stayed_paused && wait_blocked, "paused queued work must neither execute nor satisfy Wait");
            require(completed == 20, "Resume must execute every queued task exactly once");
        }
    }

    void check_growth_and_retirement() {
        mcr::utils::ThreadPool         pool{ 1, 4, 30ms };
        std::promise<void>             release;
        auto                           gate = release.get_future().share();
        std::atomic<int>               entered{ 0 };
        std::vector<std::future<void>> futures;
        for (int task{ 0 }; task < 4; ++task) {
            futures.push_back(pool.Submit([&entered, gate] { ++entered; gate.wait(); }));
        }
        bool const all_entered = eventually([&] { return entered == 4; });
        bool const bounded     = pool.GetCurrentThreadNum() == 4 && pool.GetIdleThreadNum() == 0;
        pool.SetMaxThreadNum(2);
        release.set_value();
        for (auto& future : futures) {
            future.get();
        }
        pool.Wait();
        require(all_entered && bounded, "backlogged tasks must expand to the maximum with accurate idle counts");
        require(eventually([&] { return pool.GetCurrentThreadNum() <= 2; }), "lowering the maximum must retire excess workers");
        require(eventually([&] { return pool.GetCurrentThreadNum() == 1; }), "idle workers must retire down to the minimum");
        pool.SetMinThreadNum(0);
        require(eventually([&] { return pool.GetCurrentThreadNum() == 0; }), "a zero minimum must allow all idle workers to retire");
        require(pool.IsStarted() && pool.Submit([] { return 17; }).get() == 17, "a pool with no remaining workers must accept new tasks");

        mcr::utils::ThreadPool dormant{ 0, 2, 10ms };
        dormant.Start();
        require(dormant.GetCurrentThreadNum() == 0 && dormant.Submit([] { return true; }).get(), "zero-minimum startup must still schedule the first task");

        mcr::utils::ThreadPool long_idle{ 0, 1, std::chrono::milliseconds::max() };
        long_idle.Start(1);
        std::this_thread::sleep_for(10ms);
        require(long_idle.GetCurrentThreadNum() == 1, "large idle durations must not overflow into immediate retirement");
        long_idle.SetMaxIdleTime(1ms);
        require(eventually([&] { return long_idle.GetCurrentThreadNum() == 0; }), "changing idle time must wake workers with a previous long timeout");
    }

    void check_concurrent_submit() {
        mcr::utils::ThreadPool                       pool{ 1, 4, 20ms };
        std::array<std::atomic<int>, 400>            counts{};
        std::array<std::vector<std::future<int>>, 4> results;
        std::vector<std::jthread>                    producers;
        for (int producer{ 0 }; producer < 4; ++producer) {
            producers.emplace_back([&, producer] {
                for (int index{ 0 }; index < 100; ++index) {
                    int const id = producer * 100 + index;
                    results[producer].push_back(pool.Submit([&, id] { ++counts[id]; return id; }));
                }
            });
        }
        producers.clear();
        pool.Wait();
        for (int producer{ 0 }; producer < 4; ++producer) {
            for (int index{ 0 }; index < 100; ++index) {
                int const id = producer * 100 + index;
                require(results[producer][index].get() == id && counts[id] == 1, "concurrent submission must deliver every task exactly once");
            }
        }
        require(pool.GetCurrentThreadNum() <= 4, "concurrent submission must respect the maximum");
    }

    void check_stop_and_restart() {
        mcr::utils::ThreadPool pool{ 1, 1 };
        std::promise<void> release;
        auto               gate = release.get_future().share();
        std::atomic<bool>  entered{ false };
        auto               active            = pool.Submit([&entered, gate] { entered = true; gate.wait(); return 23; });
        bool const         active_started    = eventually([&] { return entered.load(); });
        auto               waiter            = std::async(std::launch::async, [&pool] { pool.Wait(); });
        bool const         waited_for_active = waiter.wait_for(10ms) == std::future_status::timeout;
        auto               pending           = pool.Submit([] { return 99; });
        auto               stopped           = std::async(std::launch::async, [&pool] { return pool.Stop(); });
        bool const         stopping          = eventually([&] { return !pool.IsStarted(); });
        bool               rejected{ false };
        try {
            (void)pool.Submit([] {});
        } catch (std::runtime_error const&) {
            rejected = true;
        }
        bool const canceled         = is_canceled(pending);
        bool const joined_active    = stopped.wait_for(10ms) == std::future_status::timeout;
        bool const restart_rejected = pool.Start() == -1 && pool.Stop() == -1 && !pool.IsStopped();
        release.set_value();
        int const stop_result = stopped.get();
        waiter.get();
        require(active_started && waited_for_active && stopping && rejected && canceled && joined_active && restart_rejected, "Stop must cancel pending work, reject submissions, and wait for the active task");
        require(stop_result == 0 && active.get() == 23 && pool.IsStopped() && pool.GetCurrentThreadNum() == 0, "Stop must preserve the active result and join every worker");
        require(pool.Submit([] { return 42; }).get() == 42, "Submit must restart a fully stopped pool");
        pool.Wait();

        std::future<int> abandoned;
        std::atomic<int> canceled_runs{ 0 };
        {
            mcr::utils::ThreadPool paused{ 1, 1 };
            paused.Start();
            paused.Pause();
            abandoned = paused.Submit([] { return 5; });
            (void)paused.Submit([&canceled_runs] { ++canceled_runs; });
        }
        require(is_canceled(abandoned) && canceled_runs == 0, "destruction must cancel pending tasks even when their futures were discarded");
    }
} // namespace

int main() {
    try {
        check_configuration();
        check_tasks();
        check_pause_and_wait();
        check_growth_and_retirement();
        check_concurrent_submit();
        check_stop_and_restart();
    } catch (std::exception const& error) {
        std::println("test_threadpool: {}", error.what());
        return 1;
    }
    std::println("test_threadpool: ok");
    return 0;
}
