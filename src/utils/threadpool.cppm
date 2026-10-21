/**
 * @file threadpool.cppm
 * @brief A synchronized, elastic thread pool with future-based task results.
 */
export module mcr:threadpool;

export import :error;
import std;

export namespace mcr::utils {

    inline constexpr std::size_t DEFAULT_THREAD_POOL_MIN_THREAD_NUM{ 1 };                                               ///< Default minimum worker count.
    inline const std::size_t DEFAULT_THREAD_POOL_MAX_THREAD_NUM{ (std::max)(1u, std::thread::hardware_concurrency()) }; ///< Default maximum, including a fallback when hardware concurrency is unknown.
    inline constexpr std::chrono::milliseconds DEFAULT_THREAD_POOL_MAX_IDLE_TIME{ 250 };                                ///< Idle lifetime of workers above the minimum.

    /**
     * @brief Execute queued tasks with automatic startup, bounded growth, and idle worker retirement.
     * @note Public operations synchronize shared state. Stop cancels queued tasks and joins active workers.
     * Destruction cancels queued work. Inside a task it defers worker reclamation until claimed tasks finish.
     * Other callers must stop accessing the pool object before destruction.
     */
    class ThreadPool {
    public:
        using Task = std::function<void()>; ///< A queued operation without a direct return value.

    private:
        /**
         * @brief Own a type-erased move-only task; invoke with true to cancel without calling user code.
         * @note Replaces std::move_only_function, which libc++ 23 does not implement.
         */
        class QueuedTask {
        public:
            QueuedTask() = default;
            QueuedTask(QueuedTask const&) = delete;
            QueuedTask(QueuedTask&&) noexcept = default;
            auto operator=(QueuedTask const&) -> QueuedTask& = delete;
            auto operator=(QueuedTask&&) noexcept -> QueuedTask& = default;

            /**
             * @brief Store a callable that accepts the cancellation flag.
             * @tparam Fn Callable type.
             * @param fn Callable moved or copied into the task.
             * @throws std::bad_alloc If storage cannot be allocated.
             */
            template <typename Fn>
            QueuedTask(
                Fn&& fn
            ) : m_target{ std::make_unique<Target<std::decay_t<Fn>>>(std::forward<Fn>(fn)) } {}

            /**
             * @brief Check whether a callable is stored.
             * @return True when the task is not empty.
             */
            [[nodiscard]] explicit operator bool() const noexcept { return m_target != nullptr; }

            /**
             * @brief Invoke the stored callable.
             * @param cancel Forwarded to the callable; true must throw so the future carries the error.
             */
            auto operator()(bool cancel) -> void { m_target->invoke(cancel); }

            /**
             * @brief Exchange the stored callable with another task.
             * @param other Task to swap with.
             */
            auto swap(QueuedTask& other) noexcept -> void { m_target.swap(other.m_target); }

        private:
            /**
             * @brief Type-erased callable interface.
             */
            struct TargetBase {
                virtual ~TargetBase() = default;

                /**
                 * @brief Invoke the erased callable.
                 * @param cancel Cancellation flag.
                 */
                virtual auto invoke(bool cancel) -> void = 0;
            };

            /**
             * @brief Concrete storage for a callable.
             * @tparam Fn Stored callable type.
             */
            template <typename Fn>
            struct Target final : TargetBase {
                template <typename F>
                explicit Target(F&& fn) : m_fn(std::forward<F>(fn)) {}

                auto invoke(bool cancel) -> void override { m_fn(cancel); }

                Fn m_fn; ///< The owned callable.
            };

            std::unique_ptr<TargetBase> m_target; ///< Null when the task is empty.
        };

        enum class Status : std::uint8_t { STOPPED,
                                           RUNNING,
                                           PAUSED,
                                           STOPPING };

        /**
         * @brief Own a worker handle separately from the completion flag retained by its thread.
         */
        struct Worker {
            std::shared_ptr<bool> finished{ std::make_shared<bool>(false) }; ///< Completion flag retained by detached workers; protected by the state mutex.
            std::jthread thread;                                             ///< Joins unless detached during self-destruction.
        };

        /**
         * @brief Retain synchronization, tasks, and worker state independently of the public pool object.
         */
        struct State {
            mutable std::mutex m_mutex;                ///< Protects lifecycle, queues, counts, and completion flags.
            std::condition_variable m_task_cond;       ///< Wakes workers for tasks, configuration changes, or shutdown.
            std::condition_variable m_done_cond;       ///< Wakes callers waiting for all claimed and queued work.
            Status m_status{ Status::STOPPED };        ///< Current pool lifecycle.
            std::size_t m_min_thread_num{};            ///< Minimum live workers while started.
            std::size_t m_max_thread_num{};            ///< Maximum live workers.
            std::chrono::milliseconds m_max_idle_time; ///< Retirement timeout above the minimum.
            std::size_t m_current_thread_num{ 0 };     ///< Workers that have not retired.
            std::size_t m_active_thread_num{ 0 };      ///< Workers executing claimed tasks.
            std::list<Worker> m_workers;               ///< Joinable worker handles until retirement or shutdown.
            std::list<QueuedTask> m_tasks;             ///< Tasks awaiting a worker, in submission order.
        };

        std::shared_ptr<State> m_state;                      ///< Workers retain the state until their final task has finished.
        inline static thread_local State* s_current_state{}; ///< State currently executing on this thread.

    public:
        /**
         * @brief Configure a stopped pool; workers are created by Start or the first Submit.
         * @param min_threads Minimum live workers while started; zero permits full idle retirement.
         * @param max_threads Maximum live workers, which must be positive and at least min_threads.
         * @param max_idle_ms Positive idle lifetime for workers above the minimum.
         * @throws std::invalid_argument If thread limits or the idle duration are invalid.
         */
        explicit ThreadPool(
            std::size_t min_threads = DEFAULT_THREAD_POOL_MIN_THREAD_NUM,
            std::size_t max_threads = DEFAULT_THREAD_POOL_MAX_THREAD_NUM,
            std::chrono::milliseconds max_idle_ms = DEFAULT_THREAD_POOL_MAX_IDLE_TIME
        ) : m_state{ makeState(min_threads, max_threads, max_idle_ms) } {}

        ThreadPool(ThreadPool const&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool const&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        /**
         * @brief Cancel queued tasks; join workers externally or let claimed tasks finish after self-destruction.
         */
        virtual ~ThreadPool() { shutdown(s_current_state == m_state.get()); }

        /**
         * @brief Set the minimum worker count, starting additional workers if needed.
         * @param min_threads New minimum, no greater than the current maximum.
         * @throws std::invalid_argument If the new minimum exceeds the maximum.
         * @throws std::system_error If a required worker cannot be started.
         */
        auto SetMinThreadNum(std::size_t min_threads) -> void {
            std::list<Worker> retired;
            std::scoped_lock lock{ m_state->m_mutex };
            validateLimits(min_threads, m_state->m_max_thread_num);
            collectFinished(retired);
            if (isStarted()) {
                while (m_state->m_current_thread_num < min_threads) {
                    createThread();
                }
            }
            m_state->m_min_thread_num = min_threads;
            m_state->m_task_cond.notify_all();
        }

        /**
         * @brief Set the maximum; excess workers retire after their current task.
         * @param max_threads Positive limit, at least the current minimum.
         * @throws std::invalid_argument If the new maximum is invalid.
         * @throws std::system_error If a worker needed for queued tasks cannot be started.
         */
        auto SetMaxThreadNum(std::size_t max_threads) -> void {
            std::list<Worker> retired;
            std::scoped_lock lock{ m_state->m_mutex };
            validateLimits(m_state->m_min_thread_num, max_threads);
            collectFinished(retired);
            m_state->m_max_thread_num = max_threads;
            m_state->m_task_cond.notify_all();
            if (isStarted()) {
                growForTasks(m_state->m_tasks.size());
            }
        }

        /**
         * @brief Change the idle timeout and wake idle workers to apply it.
         * @param ms Positive idle duration.
         * @throws std::invalid_argument If ms is zero or negative.
         */
        auto SetMaxIdleTime(std::chrono::milliseconds ms) -> void {
            validateIdleTime(ms);
            std::scoped_lock lock{ m_state->m_mutex };
            m_state->m_max_idle_time = ms;
            m_state->m_task_cond.notify_all();
        }

        /**
         * @brief Read the configured minimum.
         * @return The minimum live worker count.
         */
        [[nodiscard]] auto GetMinThreadNum() const -> std::size_t {
            std::scoped_lock lock{ m_state->m_mutex };
            return m_state->m_min_thread_num;
        }

        /**
         * @brief Read the configured maximum.
         * @return The maximum live worker count.
         */
        [[nodiscard]] auto GetMaxThreadNum() const -> std::size_t {
            std::scoped_lock lock{ m_state->m_mutex };
            return m_state->m_max_thread_num;
        }

        /**
         * @brief Read the idle timeout.
         * @return The configured idle duration.
         */
        [[nodiscard]] auto GetMaxIdleTime() const -> std::chrono::milliseconds {
            std::scoped_lock lock{ m_state->m_mutex };
            return m_state->m_max_idle_time;
        }

        /**
         * @brief Count live workers.
         * @return Workers that have not finished retiring.
         */
        [[nodiscard]] auto GetCurrentThreadNum() const -> std::size_t {
            std::scoped_lock lock{ m_state->m_mutex };
            return m_state->m_current_thread_num;
        }

        /**
         * @brief Count available workers.
         * @return Live workers without a claimed task.
         */
        [[nodiscard]] auto GetIdleThreadNum() const -> std::size_t {
            std::scoped_lock lock{ m_state->m_mutex };
            return m_state->m_current_thread_num - m_state->m_active_thread_num;
        }

        /**
         * @brief Inspect the lifecycle state.
         * @return True while running or paused.
         */
        [[nodiscard]] auto IsStarted() const -> bool {
            std::scoped_lock lock{ m_state->m_mutex };
            return isStarted();
        }

        /**
         * @brief Inspect the lifecycle state.
         * @return True once all workers have been joined.
         */
        [[nodiscard]] auto IsStopped() const -> bool {
            std::scoped_lock lock{ m_state->m_mutex };
            return m_state->m_status == Status::STOPPED;
        }

        /**
         * @brief Start a stopped pool, clamping the initial count to its limits.
         * @param start_threads Initial worker count; zero selects the minimum.
         * @return Zero on startup, or -1 if already started or stopping.
         * @throws std::system_error If a worker cannot be started; already created workers remain usable.
         */
        auto Start(std::size_t start_threads = 0) -> int {
            std::list<Worker> retired;
            std::scoped_lock lock{ m_state->m_mutex };
            collectFinished(retired);
            if (m_state->m_status != Status::STOPPED) {
                return -1;
            }
            start(start_threads);
            return 0;
        }

        /**
         * @brief Cancel queued tasks and wait for claimed tasks and workers to finish.
         * @return Zero on shutdown, or -1 if already stopped or another Stop is in progress.
         * @throws std::logic_error If called from a task running in this pool.
         * @note Canceled futures report std::future_errc::broken_promise. Submit rejects work while stopping.
         */
        auto Stop() -> int {
            checkExternalWait();
            return shutdown(false);
        }

        /**
         * @brief Prevent workers from claiming more tasks; claimed tasks may still finish.
         * @return Zero, including when already paused or stopped.
         */
        auto Pause() -> int {
            std::scoped_lock lock{ m_state->m_mutex };
            if (m_state->m_status == Status::RUNNING) {
                m_state->m_status = Status::PAUSED;
                m_state->m_task_cond.notify_all();
            }
            return 0;
        }

        /**
         * @brief Resume a paused pool.
         * @return Zero, including when no transition is needed.
         */
        auto Resume() -> int {
            std::scoped_lock lock{ m_state->m_mutex };
            if (m_state->m_status == Status::PAUSED) {
                m_state->m_status = Status::RUNNING;
                m_state->m_task_cond.notify_all();
            }
            return 0;
        }

        /**
         * @brief Wait until the queue is empty and every claimed task has finished.
         * @throws std::logic_error If called from a task running in this pool.
         * @note Paused pending work requires another caller to Resume or Stop the pool.
         */
        auto Wait() -> void {
            checkExternalWait();
            std::unique_lock lock{ m_state->m_mutex };
            m_state->m_done_cond.wait(lock, [this] { return m_state->m_tasks.empty() && m_state->m_active_thread_num == 0; });
        }

        /**
         * @brief Own and enqueue a callable and its arguments, starting a stopped pool automatically.
         * @tparam Fn Callable type, including member pointers and move-only callables.
         * @tparam Args Argument types, decay-copied or moved into the task.
         * @param fn Callable invoked once with the stored arguments as rvalues.
         * @param args Arguments to store; use std::ref or std::cref to preserve references.
         * @return A future containing the value or callable's exception, or FAILED_INIT if shutdown is in progress.
         * @throws std::system_error If a required worker cannot be created.
         */
        template <typename Fn, typename... Args>
        auto Submit(
            Fn&& fn,
            Args&&... args
        ) -> Result<std::future<std::invoke_result_t<std::decay_t<Fn>, std::decay_t<Args>...>>> {
            auto state = m_state;
            using ReturnType = std::invoke_result_t<std::decay_t<Fn>, std::decay_t<Args>...>;
            std::packaged_task<ReturnType(bool)> task{
                [function = std::forward<Fn>(fn), arguments = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)](bool cancel) mutable -> ReturnType {
                    if (cancel) {
                        throw std::future_error{ std::future_errc::broken_promise };
                    }
                    return std::apply([&function](auto&... values) -> ReturnType {
                        return std::invoke(std::move(function), std::move(values)...);
                    },
                                      arguments);
                }
            };
            auto future = task.get_future();
            QueuedTask queued;
            std::list<Worker> retired;
            std::exception_ptr submission_error;
            bool rejected{};
            try {
                queued = std::move(task);
                std::scoped_lock lock{ m_state->m_mutex };
                if (m_state->m_status == Status::STOPPING) {
                    rejected = true;
                } else {
                    collectFinished(retired);
                    if (m_state->m_status == Status::STOPPED) {
                        start(0);
                    }
                    growForTasks(m_state->m_tasks.size() + 1);
                    // Allocate the queue slot before transferring ownership so failure leaves queued cancellable.
                    m_state->m_tasks.emplace_back();
                    m_state->m_tasks.back().swap(queued);
                }
            } catch (...) {
                submission_error = std::current_exception();
            }
            if (submission_error || rejected) {
                // Avoid packaged_task's abandonment path, which double-frees future_error
                // with the current Clang/MSVC import-std toolchain. Cancel outside the catch
                // handler to avoid nested exception handling in instrumented module builds.
                if (queued) {
                    queued(true);
                } else {
                    task(true);
                }
                if (submission_error) {
                    std::rethrow_exception(submission_error);
                }
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::utils::ThreadPool: cannot submit while stopping" }
                };
            }
            state->m_task_cond.notify_one();
            return future;
        }

    private:
        /**
         * @brief Stop without waiting for the calling worker or retaining the public pool object.
         * @param detach Let all claimed tasks finish independently when destruction occurs inside a task.
         * @return Zero on shutdown, or -1 if already stopped or stopping.
         */
        auto shutdown(bool detach) noexcept -> int {
            auto state = m_state;
            {
                std::scoped_lock lock{ state->m_mutex };
                if (state->m_status == Status::STOPPED || state->m_status == Status::STOPPING) {
                    return -1;
                }
                state->m_status = Status::STOPPING;
            }
            state->m_task_cond.notify_all();
            // Move one item at a time: even an empty std::list can allocate on some standard libraries.
            for (;;) {
                QueuedTask canceled;
                {
                    std::scoped_lock lock{ state->m_mutex };
                    if (state->m_tasks.empty()) {
                        break;
                    }
                    canceled = std::move(state->m_tasks.front());
                    state->m_tasks.pop_front();
                }
                canceled(true);
            }
            state->m_done_cond.notify_all();
            for (;;) {
                std::jthread thread;
                {
                    std::scoped_lock lock{ state->m_mutex };
                    if (state->m_workers.empty()) {
                        break;
                    }
                    thread = std::move(state->m_workers.front().thread);
                    state->m_workers.pop_front();
                }
                if (detach) {
                    thread.detach();
                }
            }
            if (!detach) {
                std::scoped_lock lock{ state->m_mutex };
                state->m_status = Status::STOPPED;
            }
            return 0;
        }

        /**
         * @brief Validate configuration before allocating independent worker state.
         */
        static auto makeState(
            std::size_t min_threads,
            std::size_t max_threads,
            std::chrono::milliseconds max_idle_ms
        ) -> std::shared_ptr<State> {
            validateLimits(min_threads, max_threads);
            validateIdleTime(max_idle_ms);
            auto state = std::make_shared<State>();
            state->m_min_thread_num = min_threads;
            state->m_max_thread_num = max_threads;
            state->m_max_idle_time = max_idle_ms;
            return state;
        }

        /**
         * @brief Validate the relationship between worker limits.
         */
        static auto validateLimits(std::size_t min_threads, std::size_t max_threads) -> void {
            if (max_threads == 0 || min_threads > max_threads) {
                throw std::invalid_argument{ "mcr::utils::ThreadPool: require 0 <= min_threads <= max_threads and max_threads > 0" };
            }
        }

        /**
         * @brief Reject idle durations that would cause immediate repeated wakeups.
         */
        static auto validateIdleTime(std::chrono::milliseconds ms) -> void {
            if (ms <= std::chrono::milliseconds::zero()) {
                throw std::invalid_argument{ "mcr::utils::ThreadPool: max idle time must be positive" };
            }
        }

        /**
         * @brief Prevent a worker from waiting for its own completion.
         */
        auto checkExternalWait() const -> void {
            if (s_current_state == m_state.get()) {
                throw std::logic_error{ "mcr::utils::ThreadPool: a worker cannot Wait or Stop its own pool" };
            }
        }

        /**
         * @brief Read the started state while holding the state mutex.
         */
        auto isStarted() const -> bool {
            return m_state->m_status == Status::RUNNING || m_state->m_status == Status::PAUSED;
        }

        /**
         * @brief Start workers while holding m_state->m_mutex, retaining a valid state on creation failure.
         */
        auto start(std::size_t start_threads) -> void {
            m_state->m_status = Status::RUNNING;
            try {
                auto const count = std::clamp(start_threads, m_state->m_min_thread_num, m_state->m_max_thread_num);
                while (m_state->m_current_thread_num < count) {
                    createThread();
                }
            } catch (...) {
                if (m_state->m_current_thread_num == 0) {
                    m_state->m_status = Status::STOPPED;
                }
                throw;
            }
        }

        /**
         * @brief Add workers for pending work while holding m_state->m_mutex and respecting the maximum.
         */
        auto growForTasks(std::size_t pending_tasks) -> void {
            while (m_state->m_current_thread_num < m_state->m_max_thread_num && pending_tasks > m_state->m_current_thread_num - m_state->m_active_thread_num) {
                createThread();
            }
        }

        /**
         * @brief Create a stable worker record before launching its thread; caller holds the state mutex.
         */
        auto createThread() -> void {
            auto& worker = m_state->m_workers.emplace_back();
            try {
                worker.thread = std::jthread{ [state = m_state, finished = worker.finished] { runWorker(*state, *finished); } };
            } catch (...) {
                m_state->m_workers.pop_back();
                throw;
            }
            ++m_state->m_current_thread_num;
        }

        /**
         * @brief Move finished records out for joining after the caller releases the state mutex.
         */
        auto collectFinished(std::list<Worker>& retired) -> void {
            for (auto worker = m_state->m_workers.begin(); worker != m_state->m_workers.end();) {
                auto current = worker++;
                if (*current->finished) {
                    retired.splice(retired.end(), m_state->m_workers, current);
                }
            }
        }

        /**
         * @brief Compute a saturated idle deadline without overflowing chrono representations.
         */
        static auto idleDeadline(
            State const& state,
            std::chrono::steady_clock::time_point idle_since
        ) -> std::chrono::steady_clock::time_point {
            using Clock = std::chrono::steady_clock;
            constexpr auto MAX_INTERVAL = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::duration::max());
            if (state.m_max_idle_time >= MAX_INTERVAL) {
                return Clock::time_point::max();
            }
            auto const interval = std::chrono::duration_cast<Clock::duration>(state.m_max_idle_time);
            if (idle_since > Clock::time_point::max() - interval) {
                return Clock::time_point::max();
            }
            return idle_since + interval;
        }

        /**
         * @brief Execute tasks until shutdown or retirement, synchronizing all queue and count updates.
         */
        static auto runWorker(State& state, bool& finished) -> void {
            s_current_state = &state;
            std::unique_lock lock{ state.m_mutex };
            auto idle_since = std::chrono::steady_clock::now();
            for (;;) {
                if (state.m_status == Status::STOPPING || state.m_current_thread_num > state.m_max_thread_num) {
                    break;
                }
                if (state.m_status == Status::PAUSED) {
                    state.m_task_cond.wait(lock, [&state] { return state.m_status != Status::PAUSED || state.m_current_thread_num > state.m_max_thread_num; });
                    idle_since = std::chrono::steady_clock::now();
                    continue;
                }
                if (!state.m_tasks.empty()) {
                    auto task = std::move(state.m_tasks.front());
                    state.m_tasks.pop_front();
                    ++state.m_active_thread_num;
                    lock.unlock();
                    task(false);
                    task = {};
                    lock.lock();
                    --state.m_active_thread_num;
                    if (state.m_tasks.empty() && state.m_active_thread_num == 0) {
                        state.m_done_cond.notify_all();
                    }
                    idle_since = std::chrono::steady_clock::now();
                    continue;
                }
                if (state.m_current_thread_num > state.m_min_thread_num) {
                    auto const deadline = idleDeadline(state, idle_since);
                    if (std::chrono::steady_clock::now() >= deadline) {
                        break;
                    }
                    state.m_task_cond.wait_until(lock, deadline);
                } else {
                    state.m_task_cond.wait(lock);
                }
            }
            --state.m_current_thread_num;
            finished = true;
            s_current_state = nullptr;
        }
    };

} // namespace mcr::utils
