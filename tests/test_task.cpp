/**
 * @file test_task.cpp
 * @brief Exercise lazy tasks, ownership, continuations, exceptions, and cancellation.
 */
import std;
import mcr.task;

namespace {
    void require(bool value, std::string_view message) {
        if (!value) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    /**
     * @brief Allow deterministic completion from either side of an await registration.
     */
    struct Gate {
        std::mutex              mutex;
        bool                    ready{ false };
        std::coroutine_handle<> continuation;

        bool await_ready() {
            std::lock_guard lock{ mutex };
            return ready;
        }

        bool await_suspend(std::coroutine_handle<> handle) {
            std::lock_guard lock{ mutex };
            if (ready) {
                return false;
            }
            continuation = handle;
            return true;
        }

        void await_resume() const noexcept {}

        void Release() {
            std::coroutine_handle<> next;
            {
                std::lock_guard lock{ mutex };
                ready = true;
                next  = std::exchange(continuation, {});
            }
            if (next) {
                next.resume();
            }
        }
    };

    auto value(int& runs) -> mcr::Task<int> {
        ++runs;
        co_return 42;
    }

    auto move_value(std::unique_ptr<int> value) -> mcr::Task<std::unique_ptr<int>> {
        co_return std::move(value);
    }

    auto reference(int& value) -> mcr::Task<int&> {
        co_return value;
    }

    auto failing() -> mcr::Task<int> {
        throw std::runtime_error{ "task failure" };
        co_return 0;
    }

    auto nested(int depth) -> mcr::Task<int> {
        if (!depth) {
            co_return 1;
        }
        co_return 1 + co_await nested(depth - 1);
    }

    auto deferred(Gate& gate, std::shared_ptr<int> owned) -> mcr::Task<int> {
        co_await gate;
        co_return *owned;
    }

    auto consume(mcr::Task<int> child) -> mcr::Task<int> {
        co_return co_await std::move(child);
    }

    auto discard_result(mcr::Task<int> child) -> mcr::Task<void> {
        (void)co_await std::move(child);
    }

    auto cancellation(Gate& gate) -> mcr::Task<bool> {
        auto stop = co_await mcr::detail::TaskStopToken{};
        co_await gate;
        co_return stop.stop_requested();
    }

    auto cancel_parent(Gate& gate) -> mcr::Task<bool> {
        co_return co_await cancellation(gate);
    }
} // namespace

int main() {
    try {
        static_assert(!std::is_copy_constructible_v<mcr::Task<int>>);
        static_assert(std::is_nothrow_move_constructible_v<mcr::Task<int>>);
        int runs{};
        { auto unused = value(runs); }
        require(runs == 0, "destroying an unstarted task must not run its body");
        auto task = value(runs);
        require(runs == 0, "task must be lazy");
        task.Start();
        task.Start();
        require(runs == 1 && mcr::sync_wait(std::move(task)) == 42 && !task.Valid(), "Start is idempotent and waiting consumes the task");
        require(*mcr::sync_wait(move_value(std::make_unique<int>(7))) == 7, "move-only results must survive frame destruction");
        mcr::sync_wait(reference(runs)) = 5;
        require(runs == 5, "reference results must preserve identity");
        require(mcr::sync_wait(nested(1000)) == 1001, "nested tasks must transfer to their continuations");
        mcr::sync_wait(discard_result(value(runs)));
        try {
            (void)mcr::sync_wait(consume(failing()));
            require(false, "task exceptions must propagate through parents");
        } catch (std::runtime_error const& error) {
            require(std::string_view{ error.what() } == "task failure", "original exception must survive");
        }
        try {
            (void)mcr::sync_wait(mcr::Task<int>{});
            require(false, "empty tasks must be rejected");
        } catch (std::logic_error const&) {}

        Gate               lifetime_gate;
        auto               owned = std::make_shared<int>(11);
        std::weak_ptr<int> weak  = owned;
        {
            auto abandoned = deferred(lifetime_gate, std::move(owned));
            abandoned.Start();
        }
        require(!weak.expired(), "discarding a running result must preserve the suspended frame");
        lifetime_gate.Release();
        require(weak.expired(), "completion must release the coroutine's owned arguments");

        Gate cancel_gate;
        auto parent = cancel_parent(cancel_gate);
        auto source = parent.GetStopSource();
        parent.Start();
        require(source.request_stop(), "cancellation source remains usable after start");
        cancel_gate.Release();
        require(mcr::sync_wait(std::move(parent)), "parent cancellation must reach the awaited child");

        // Complete an already started child concurrently with registering its parent continuation.
        for (int iteration{}; iteration < 300; ++iteration) {
            Gate gate;
            auto child = deferred(gate, std::make_shared<int>(iteration));
            child.Start();
            std::jthread release{ [&] { gate.Release(); } };
            require(mcr::sync_wait(consume(std::move(child))) == iteration, "completion/registration race must neither lose nor duplicate a resume");
        }
        std::println("test_task: ok");
        return 0;
    } catch (std::exception const& error) {
        std::println("test_task: {}", error.what());
        return 1;
    }
}
