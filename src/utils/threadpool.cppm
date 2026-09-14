/**
 * @file threadpool.cppm
 * @brief A synchronized, elastic thread pool with future-based task results.
 */
export module mcr.threadpool;

import std;

export namespace mcr::utils {

    inline constexpr std::size_t               DEFAULT_THREAD_POOL_MIN_THREAD_NUM{ 1 };                                                   ///< Default minimum worker count.
    inline const std::size_t                   DEFAULT_THREAD_POOL_MAX_THREAD_NUM{ (std::max)(1u, std::thread::hardware_concurrency()) }; ///< Default maximum, including a fallback when hardware concurrency is unknown.
    inline constexpr std::chrono::milliseconds DEFAULT_THREAD_POOL_MAX_IDLE_TIME{ 250 };                                                  ///< Idle lifetime of workers above the minimum.

    /**
     * @brief Execute queued tasks with automatic startup, bounded growth, and idle worker retirement.
     * @note Public operations synchronize shared state. Stop cancels queued tasks and joins active workers.
     * Destruction must occur outside this pool's tasks, after other callers stop accessing the object.
     */
    class ThreadPool {
    public:
        using Task = std::function<void()>; ///< A queued operation without a direct return value.

    private:
        using QueuedTask = std::move_only_function<void(bool)>; ///< Own a task; invoke with true to cancel without calling user code.

        enum class Status : std::uint8_t { STOPPED,
                                           RUNNING,
                                           PAUSED,
                                           STOPPING };

        /**
         * @brief A stable worker record; the thread is joined before its completion flag is destroyed.
         */
        struct Worker {
            bool         finished{ false }; ///< Protected by the pool mutex.
            std::jthread thread;            ///< Joins when this worker record is destroyed.
        };

        mutable std::mutex                     m_mutex;
        std::condition_variable                m_task_cond;
        std::condition_variable                m_done_cond;
        Status                                 m_status{ Status::STOPPED };
        std::size_t                            m_min_thread_num;
        std::size_t                            m_max_thread_num;
        std::chrono::milliseconds              m_max_idle_time;
        std::size_t                            m_current_thread_num{ 0 };
        std::size_t                            m_active_thread_num{ 0 };
        std::list<Worker>                      m_workers;
        std::queue<QueuedTask>                 m_tasks;
        inline static thread_local ThreadPool* s_current_pool{ nullptr };

    public:
        /**
         * @brief Configure a stopped pool; workers are created by Start or the first Submit.
         * @param min_threads Minimum live workers while started; zero permits full idle retirement.
         * @param max_threads Maximum live workers, which must be positive and at least min_threads.
         * @param max_idle_ms Positive idle lifetime for workers above the minimum.
         * @throws std::invalid_argument If thread limits or the idle duration are invalid.
         */
        explicit ThreadPool(
            std::size_t               min_threads = DEFAULT_THREAD_POOL_MIN_THREAD_NUM,
            std::size_t               max_threads = DEFAULT_THREAD_POOL_MAX_THREAD_NUM,
            std::chrono::milliseconds max_idle_ms = DEFAULT_THREAD_POOL_MAX_IDLE_TIME
        ) : m_min_thread_num{ min_threads }, m_max_thread_num{ max_threads }, m_max_idle_time{ max_idle_ms } {
            validateLimits(min_threads, max_threads);
            validateIdleTime(max_idle_ms);
        }

        ThreadPool(ThreadPool const&)            = delete;
        ThreadPool(ThreadPool&&)                 = delete;
        ThreadPool& operator=(ThreadPool const&) = delete;
        ThreadPool& operator=(ThreadPool&&)      = delete;

        /**
         * @brief Cancel pending tasks and join workers; call Wait first to finish all queued work.
         */
        virtual ~ThreadPool() { Stop(); }

        /**
         * @brief Set the minimum worker count, starting additional workers if needed.
         * @param min_threads New minimum, no greater than the current maximum.
         * @throws std::invalid_argument If the new minimum exceeds the maximum.
         * @throws std::system_error If a required worker cannot be started.
         */
        void SetMinThreadNum(std::size_t min_threads) {
            std::list<Worker> retired;
            std::scoped_lock  lock{ m_mutex };
            validateLimits(min_threads, m_max_thread_num);
            collectFinished(retired);
            if (isStarted()) {
                while (m_current_thread_num < min_threads) {
                    createThread();
                }
            }
            m_min_thread_num = min_threads;
            m_task_cond.notify_all();
        }

        /**
         * @brief Set the maximum; excess workers retire after their current task.
         * @param max_threads Positive limit, at least the current minimum.
         * @throws std::invalid_argument If the new maximum is invalid.
         * @throws std::system_error If a worker needed for queued tasks cannot be started.
         */
        void SetMaxThreadNum(std::size_t max_threads) {
            std::list<Worker> retired;
            std::scoped_lock  lock{ m_mutex };
            validateLimits(m_min_thread_num, max_threads);
            collectFinished(retired);
            m_max_thread_num = max_threads;
            m_task_cond.notify_all();
            if (isStarted()) {
                growForTasks(m_tasks.size());
            }
        }

        /**
         * @brief Change the idle timeout and wake idle workers to apply it.
         * @param ms Positive idle duration.
         * @throws std::invalid_argument If ms is zero or negative.
         */
        void SetMaxIdleTime(std::chrono::milliseconds ms) {
            validateIdleTime(ms);
            std::scoped_lock lock{ m_mutex };
            m_max_idle_time = ms;
            m_task_cond.notify_all();
        }

        /**
         * @brief Read the configured minimum.
         * @return The minimum live worker count.
         */
        [[nodiscard]] auto GetMinThreadNum() const -> std::size_t {
            std::scoped_lock lock{ m_mutex };
            return m_min_thread_num;
        }

        /**
         * @brief Read the configured maximum.
         * @return The maximum live worker count.
         */
        [[nodiscard]] auto GetMaxThreadNum() const -> std::size_t {
            std::scoped_lock lock{ m_mutex };
            return m_max_thread_num;
        }

        /**
         * @brief Read the idle timeout.
         * @return The configured idle duration.
         */
        [[nodiscard]] auto GetMaxIdleTime() const -> std::chrono::milliseconds {
            std::scoped_lock lock{ m_mutex };
            return m_max_idle_time;
        }

        /**
         * @brief Count live workers.
         * @return Workers that have not finished retiring.
         */
        [[nodiscard]] auto GetCurrentThreadNum() const -> std::size_t {
            std::scoped_lock lock{ m_mutex };
            return m_current_thread_num;
        }

        /**
         * @brief Count available workers.
         * @return Live workers without a claimed task.
         */
        [[nodiscard]] auto GetIdleThreadNum() const -> std::size_t {
            std::scoped_lock lock{ m_mutex };
            return m_current_thread_num - m_active_thread_num;
        }

        /**
         * @brief Inspect the lifecycle state.
         * @return True while running or paused.
         */
        [[nodiscard]] auto IsStarted() const -> bool {
            std::scoped_lock lock{ m_mutex };
            return isStarted();
        }

        /**
         * @brief Inspect the lifecycle state.
         * @return True once all workers have been joined.
         */
        [[nodiscard]] auto IsStopped() const -> bool {
            std::scoped_lock lock{ m_mutex };
            return m_status == Status::STOPPED;
        }

        /**
         * @brief Start a stopped pool, clamping the initial count to its limits.
         * @param start_threads Initial worker count; zero selects the minimum.
         * @return Zero on startup, or -1 if already started or stopping.
         * @throws std::system_error If a worker cannot be started; already created workers remain usable.
         */
        auto Start(std::size_t start_threads = 0) -> int {
            std::list<Worker> retired;
            std::scoped_lock  lock{ m_mutex };
            collectFinished(retired);
            if (m_status != Status::STOPPED) {
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
            std::list<Worker>      workers;
            std::queue<QueuedTask> canceled;
            {
                std::scoped_lock lock{ m_mutex };
                if (m_status == Status::STOPPED || m_status == Status::STOPPING) {
                    return -1;
                }
                m_status = Status::STOPPING;
                workers.splice(workers.end(), m_workers);
                canceled.swap(m_tasks);
            }
            m_task_cond.notify_all();
            m_done_cond.notify_all();
            // Complete futures explicitly before releasing user captures, outside the pool mutex.
            while (!canceled.empty()) {
                canceled.front()(true);
                canceled.pop();
            }
            workers.clear();
            {
                std::scoped_lock lock{ m_mutex };
                m_status = Status::STOPPED;
            }
            return 0;
        }

        /**
         * @brief Prevent workers from claiming more tasks; claimed tasks may still finish.
         * @return Zero, including when already paused or stopped.
         */
        auto Pause() -> int {
            std::scoped_lock lock{ m_mutex };
            if (m_status == Status::RUNNING) {
                m_status = Status::PAUSED;
                m_task_cond.notify_all();
            }
            return 0;
        }

        /**
         * @brief Resume a paused pool.
         * @return Zero, including when no transition is needed.
         */
        auto Resume() -> int {
            std::scoped_lock lock{ m_mutex };
            if (m_status == Status::PAUSED) {
                m_status = Status::RUNNING;
                m_task_cond.notify_all();
            }
            return 0;
        }

        /**
         * @brief Wait until the queue is empty and every claimed task has finished.
         * @throws std::logic_error If called from a task running in this pool.
         * @note Paused pending work requires another caller to Resume or Stop the pool.
         */
        void Wait() {
            checkExternalWait();
            std::unique_lock lock{ m_mutex };
            m_done_cond.wait(lock, [this] { return m_tasks.empty() && m_active_thread_num == 0; });
        }

        /**
         * @brief Own and enqueue a callable and its arguments, starting a stopped pool automatically.
         * @tparam Fn Callable type, including member pointers and move-only callables.
         * @tparam Args Argument types, decay-copied or moved into the task.
         * @param fn Callable invoked once with the stored arguments as rvalues.
         * @param args Arguments to store; use std::ref or std::cref to preserve references.
         * @return A future containing the return value or the callable's exception.
         * @throws std::runtime_error If shutdown is in progress.
         * @throws std::system_error If a required worker cannot be created.
         */
        template <typename Fn, typename... Args>
        auto Submit(Fn&& fn, Args&&... args) -> std::future<std::invoke_result_t<std::decay_t<Fn>, std::decay_t<Args>...>> {
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
            auto               future = task.get_future();
            QueuedTask         queued;
            std::list<Worker>  retired;
            std::exception_ptr submission_error;
            try {
                queued = std::move(task);
                std::scoped_lock lock{ m_mutex };
                if (m_status == Status::STOPPING) {
                    throw std::runtime_error{ "mcr::utils::ThreadPool: cannot submit while stopping" };
                }
                collectFinished(retired);
                if (m_status == Status::STOPPED) {
                    start(0);
                }
                growForTasks(m_tasks.size() + 1);
                // Allocate the queue slot before transferring ownership so failure leaves queued cancellable.
                m_tasks.emplace();
                m_tasks.back().swap(queued);
            } catch (...) {
                submission_error = std::current_exception();
            }
            if (submission_error) {
                // Avoid packaged_task's abandonment path, which double-frees future_error
                // with the current Clang/MSVC import-std toolchain. Cancel outside the catch
                // handler to avoid nested exception handling in instrumented module builds.
                if (queued) {
                    queued(true);
                } else {
                    task(true);
                }
                std::rethrow_exception(submission_error);
            }
            m_task_cond.notify_one();
            return future;
        }

    private:
        /**
         * @brief Validate the relationship between worker limits.
         */
        static void validateLimits(std::size_t min_threads, std::size_t max_threads) {
            if (max_threads == 0 || min_threads > max_threads) {
                throw std::invalid_argument{ "mcr::utils::ThreadPool: require 0 <= min_threads <= max_threads and max_threads > 0" };
            }
        }

        /**
         * @brief Reject idle durations that would cause immediate repeated wakeups.
         */
        static void validateIdleTime(std::chrono::milliseconds ms) {
            if (ms <= std::chrono::milliseconds::zero()) {
                throw std::invalid_argument{ "mcr::utils::ThreadPool: max idle time must be positive" };
            }
        }

        /**
         * @brief Prevent a worker from waiting for its own completion.
         */
        void checkExternalWait() const {
            if (s_current_pool == this) {
                throw std::logic_error{ "mcr::utils::ThreadPool: a worker cannot Wait or Stop its own pool" };
            }
        }

        /**
         * @brief Read the started state while holding m_mutex.
         */
        auto isStarted() const -> bool {
            return m_status == Status::RUNNING || m_status == Status::PAUSED;
        }

        /**
         * @brief Start workers while holding m_mutex, retaining a valid state on creation failure.
         */
        void start(std::size_t start_threads) {
            m_status = Status::RUNNING;
            try {
                auto const count = std::clamp(start_threads, m_min_thread_num, m_max_thread_num);
                while (m_current_thread_num < count) {
                    createThread();
                }
            } catch (...) {
                if (m_current_thread_num == 0) {
                    m_status = Status::STOPPED;
                }
                throw;
            }
        }

        /**
         * @brief Add workers for pending work while holding m_mutex and respecting the maximum.
         */
        void growForTasks(std::size_t pending_tasks) {
            while (m_current_thread_num < m_max_thread_num && pending_tasks > m_current_thread_num - m_active_thread_num) {
                createThread();
            }
        }

        /**
         * @brief Create a stable worker record before launching its thread; caller holds m_mutex.
         */
        void createThread() {
            auto& worker = m_workers.emplace_back();
            try {
                worker.thread = std::jthread{ [this, &worker] { runWorker(worker); } };
            } catch (...) {
                m_workers.pop_back();
                throw;
            }
            ++m_current_thread_num;
        }

        /**
         * @brief Move finished records out for joining after the caller releases m_mutex.
         */
        void collectFinished(std::list<Worker>& retired) {
            for (auto worker = m_workers.begin(); worker != m_workers.end();) {
                auto current = worker++;
                if (current->finished) {
                    retired.splice(retired.end(), m_workers, current);
                }
            }
        }

        /**
         * @brief Compute a saturated idle deadline without overflowing chrono representations.
         */
        auto idleDeadline(std::chrono::steady_clock::time_point idle_since) const -> std::chrono::steady_clock::time_point {
            using Clock                 = std::chrono::steady_clock;
            constexpr auto MAX_INTERVAL = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::duration::max());
            if (m_max_idle_time >= MAX_INTERVAL) {
                return Clock::time_point::max();
            }
            auto const interval = std::chrono::duration_cast<Clock::duration>(m_max_idle_time);
            if (idle_since > Clock::time_point::max() - interval) {
                return Clock::time_point::max();
            }
            return idle_since + interval;
        }

        /**
         * @brief Execute tasks until shutdown or retirement, synchronizing all queue and count updates.
         */
        void runWorker(Worker& worker) {
            s_current_pool = this;
            std::unique_lock lock{ m_mutex };
            auto             idle_since = std::chrono::steady_clock::now();
            for (;;) {
                if (m_status == Status::STOPPING || m_current_thread_num > m_max_thread_num) {
                    break;
                }
                if (m_status == Status::PAUSED) {
                    m_task_cond.wait(lock, [this] { return m_status != Status::PAUSED || m_current_thread_num > m_max_thread_num; });
                    idle_since = std::chrono::steady_clock::now();
                    continue;
                }
                if (!m_tasks.empty()) {
                    auto task = std::move(m_tasks.front());
                    m_tasks.pop();
                    ++m_active_thread_num;
                    lock.unlock();
                    task(false);
                    task = {};
                    lock.lock();
                    --m_active_thread_num;
                    if (m_tasks.empty() && m_active_thread_num == 0) {
                        m_done_cond.notify_all();
                    }
                    idle_since = std::chrono::steady_clock::now();
                    continue;
                }
                if (m_current_thread_num > m_min_thread_num) {
                    auto const deadline = idleDeadline(idle_since);
                    if (std::chrono::steady_clock::now() >= deadline) {
                        break;
                    }
                    m_task_cond.wait_until(lock, deadline);
                } else {
                    m_task_cond.wait(lock);
                }
            }
            --m_current_thread_num;
            worker.finished = true;
            s_current_pool  = nullptr;
        }
    };

} // namespace mcr::utils
