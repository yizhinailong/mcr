/**
 * @file task.cppm
 * @brief Lazy, single-consumer coroutine tasks with cooperative cancellation.
 */
export module mcr.task;

import std;

namespace mcr::detail {
    export inline thread_local bool coro_runtime_thread{ false }; ///< Reject blocking waits on runtime threads.

    /**
     * @brief Store a coroutine result independently of its frame.
     */
    template <typename T>
    struct TaskValue {
        std::optional<T> value;

        auto Take() -> T { return std::move(*value); }
    };

    /**
     * @brief Preserve lvalue reference results without taking ownership.
     */
    template <typename T>
    struct TaskValue<T&> {
        T* value{};

        auto Take() -> T& { return *value; }
    };

    /**
     * @brief Represent successful completion without a value.
     */
    template <>
    struct TaskValue<void> {
        auto Take() -> void {}
    };

    /**
     * @brief Synchronize final completion with a waiter or a continuation.
     */
    template <typename T>
    struct TaskState : TaskValue<T> {
        std::mutex              mutex;
        std::condition_variable condition;
        bool                    done{ false };
        std::exception_ptr      error;
        std::coroutine_handle<> continuation{};
        std::stop_source        stop;

        /**
         * @brief Publish the result after the coroutine frame has been destroyed.
         */
        auto Complete() noexcept -> std::coroutine_handle<> {
            std::coroutine_handle<> next;
            {
                std::lock_guard lock{ mutex };
                done = true;
                next = continuation;
            }
            condition.notify_all();
            return next ? next : std::noop_coroutine();
        }

        /**
         * @brief Retrieve a completed value or rethrow its exception.
         */
        auto TakeResult() -> T {
            if (error) {
                std::rethrow_exception(error);
            }
            return this->Take();
        }
    };

    /**
     * @brief Supply the promise return operation for value results.
     */
    template <typename T>
    struct TaskReturn {
        std::shared_ptr<TaskState<T>> state{ std::make_shared<TaskState<T>>() };

        template <typename U>
        void return_value(U&& value) {
            if constexpr (std::is_lvalue_reference_v<T>) {
                static_assert(std::is_lvalue_reference_v<U&&>, "A reference task must return an lvalue.");
                state->value = std::addressof(value);
            } else {
                state->value.emplace(std::forward<U>(value));
            }
        }
    };

    /**
     * @brief Supply the distinct promise return operation for void results.
     */
    template <>
    struct TaskReturn<void> {
        std::shared_ptr<TaskState<void>> state{ std::make_shared<TaskState<void>>() };

        void return_void() noexcept {}
    };

    /**
     * @brief Propagate cancellation from a parent task to its awaited child.
     */
    struct ForwardTaskStop {
        std::stop_source source;

        void operator()() noexcept { source.request_stop(); }
    };

    /**
     * @brief Read a task's cancellation token without suspending execution.
     */
    export struct TaskStopToken {
        std::stop_token token;

        bool await_ready() const noexcept { return false; }

        template <typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            token = handle.promise().GetStopToken();
            return false;
        }

        auto await_resume() const noexcept -> std::stop_token { return token; }
    };
} // namespace mcr::detail

export namespace mcr {
    template <typename T>
    class Task;

    /**
     * @brief Start a task and block the calling thread until it completes.
     * @tparam T Value, lvalue reference, or void result type.
     * @param task Task consumed by this wait.
     * @return The task's result.
     * @throws std::logic_error For an empty task or a wait on a coroutine runtime thread.
     * @throws Any exception raised by the task.
     */
    template <typename T>
    auto sync_wait(Task<T> task) -> T;

    /**
     * @brief Own a lazy, move-only coroutine result with one consumer.
     * @tparam T Value, lvalue reference, or void result type; rvalue references are unsupported.
     * @note Start or awaiting transfers frame ownership to the running coroutine. Discarding a
     * started task does not destroy its frame or wait: it runs to completion. Borrowed data must
     * remain alive. An external coroutine awaiting a Task must not be destroyed before completion.
     * Accesses to the same Task object require synchronization; cancellation sources may be copied.
     */
    template <typename T = void>
    class [[nodiscard]] Task {
        static_assert(!std::is_rvalue_reference_v<T>);

    public:
        /**
         * @brief Compiler-facing promise with self-owned execution after initial suspension.
         */
        struct promise_type : detail::TaskReturn<T> {
            auto get_return_object() noexcept -> Task {
                return Task{ std::coroutine_handle<promise_type>::from_promise(*this), this->state };
            }

            auto initial_suspend() const noexcept -> std::suspend_always { return {}; }

            /**
             * @brief Release the completed frame before notifying its consumer.
             */
            struct FinalAwaiter {
                bool await_ready() const noexcept { return false; }

                auto await_suspend(std::coroutine_handle<promise_type> handle) const noexcept -> std::coroutine_handle<> {
                    auto state = handle.promise().state;
                    handle.destroy();
                    return state->Complete();
                }

                void await_resume() const noexcept {}
            };

            auto final_suspend() const noexcept -> FinalAwaiter { return {}; }

            void unhandled_exception() noexcept { this->state->error = std::current_exception(); }

            auto GetStopToken() const noexcept -> std::stop_token { return this->state->stop.get_token(); }
        };

    private:
        using Handle = std::coroutine_handle<promise_type>;
        Handle                                m_coroutine{}; ///< Owned only until the task starts.
        std::shared_ptr<detail::TaskState<T>> m_state;

        Task(Handle coroutine, std::shared_ptr<detail::TaskState<T>> state) noexcept
            : m_coroutine{ coroutine }, m_state{ std::move(state) } {}

        template <typename U>
        friend auto sync_wait(Task<U> task) -> U;

    public:
        Task()                               = default;
        Task(Task const&)                    = delete;
        auto operator=(Task const&) -> Task& = delete;

        Task(Task&& other) noexcept
            : m_coroutine{ std::exchange(other.m_coroutine, {}) }, m_state{ std::move(other.m_state) } {}

        auto operator=(Task&& other) noexcept -> Task& {
            if (this != &other) {
                Task previous{ std::move(*this) };
                m_coroutine = std::exchange(other.m_coroutine, {});
                m_state     = std::move(other.m_state);
            }
            return *this;
        }

        ~Task() {
            if (m_coroutine) {
                m_coroutine.destroy();
            }
        }

        /**
         * @brief Check whether this object still owns an unconsumed result.
         */
        [[nodiscard]] bool Valid() const noexcept { return static_cast<bool>(m_state); }

        /**
         * @brief Start execution until its first suspension; repeated calls have no effect.
         * @throws std::logic_error If this task is empty or has been consumed.
         */
        void Start() {
            if (!m_state) {
                throw std::logic_error{ "mcr::Task::Start: task is empty." };
            }
            if (auto handle = std::exchange(m_coroutine, {})) {
                handle.resume();
            }
        }

        /**
         * @brief Request cooperative cancellation, including before the task starts.
         */
        auto Cancel() noexcept -> bool { return m_state && m_state->stop.request_stop(); }

        /**
         * @brief Obtain a cancellation source usable after moving the task into a consumer.
         */
        [[nodiscard]] auto GetStopSource() const -> std::stop_source {
            if (!m_state) {
                throw std::logic_error{ "mcr::Task::GetStopSource: task is empty." };
            }
            return m_state->stop;
        }

        /**
         * @brief Own the result and propagate parent cancellation while awaiting it.
         */
        class Awaiter {
            Handle                                                       m_coroutine;
            std::shared_ptr<detail::TaskState<T>>                        m_state;
            std::unique_ptr<std::stop_callback<detail::ForwardTaskStop>> m_parent_stop;

        public:
            Awaiter(Handle coroutine, std::shared_ptr<detail::TaskState<T>> state) noexcept
                : m_coroutine{ coroutine }, m_state{ std::move(state) } {}

            Awaiter(Awaiter const&) = delete;
            Awaiter(Awaiter&&)      = delete;

            ~Awaiter() {
                if (m_coroutine) {
                    m_coroutine.destroy();
                }
            }

            bool await_ready() const {
                if (!m_state) {
                    throw std::logic_error{ "mcr::Task: cannot await an empty task." };
                }
                std::lock_guard lock{ m_state->mutex };
                return m_state->done;
            }

            template <typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> continuation) -> std::coroutine_handle<> {
                if constexpr (requires(Promise& promise) { promise.GetStopToken(); }) {
                    m_parent_stop = std::make_unique<std::stop_callback<detail::ForwardTaskStop>>(
                        continuation.promise().GetStopToken(),
                        detail::ForwardTaskStop{ m_state->stop }
                    );
                }
                auto state  = m_state;
                auto handle = std::exchange(m_coroutine, {});
                {
                    std::lock_guard lock{ state->mutex };
                    if (state->done) {
                        return continuation;
                    }
                    state->continuation = continuation;
                }
                // The continuation may run immediately after publication; do not access this.
                return handle ? std::coroutine_handle<>{ handle } : std::noop_coroutine();
            }

            auto await_resume() -> T { return m_state->TakeResult(); }
        };

        /**
         * @brief Consume this task in a co_await expression.
         */
        auto operator co_await() && noexcept -> Awaiter {
            return Awaiter{ std::exchange(m_coroutine, {}), std::move(m_state) };
        }
    };

    template <typename T>
    auto sync_wait(Task<T> task) -> T {
        if (detail::coro_runtime_thread) {
            throw std::logic_error{ "mcr::sync_wait: cannot block a coroutine runtime thread." };
        }
        task.Start();
        auto state = task.m_state;
        {
            std::unique_lock lock{ state->mutex };
            state->condition.wait(lock, [&] { return state->done; });
        }
        return state->TakeResult();
    }
} // namespace mcr
