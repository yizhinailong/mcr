/**
 * @file async.cppm
 * @brief Global thread-pool submission and explicit asynchronous runtime lifecycle.
 */
export module mcr.async;

export import mcr.async_wrapper;

import mcr.singleton;
import mcr.threadpool;
export import mcr.error;
import std;

export namespace mcr {

    /**
     * @brief Provide one lazily initialized thread pool for async submissions.
     * @note Singleton shutdown is explicit and permanent. Returned pointers are borrowed.
     * Shutdown must occur outside pool workers after other pool users stop accessing the instance.
     */
    class GlobalThreadPool : public utils::ThreadPool, public utils::Singleton<GlobalThreadPool> {
    private:
        friend utils::Singleton<GlobalThreadPool>;

    protected:
        /**
         * @brief Construct the global pool with ThreadPool's default configuration.
         */
        GlobalThreadPool() = default;

    public:
        /**
         * @brief Cancel queued work and join active workers through the base pool destructor.
         */
        ~GlobalThreadPool() override = default;
    };

    /**
     * @brief Submit a callable to the global pool and wrap its future result.
     * @tparam is_cancellable Whether to provide cancellation state in the returned wrapper.
     * @tparam Fn Callable type, decay-copied or moved into the task.
     * @tparam Args Argument types, decay-copied or moved; use std::ref to preserve references.
     * @param fn Callable invoked once with stored arguments as rvalues.
     * @param args Arguments forwarded to ThreadPool::Submit.
     * @return A wrapper preserving value, reference, void, and move-only result types.
     * @note Submission automatically starts a stopped pool. Callable exceptions are stored in the result.
     * As in cpr, the cancellation flag is not passed to the callable: cancelling the wrapper does
     * not prevent or interrupt task execution. Thread creation and allocation exceptions propagate.
     */
    template <bool is_cancellable = false, typename Fn, typename... Args>
    [[nodiscard]] auto async(Fn&& fn, Args&&... args) -> Result<utils::AsyncWrapper<std::invoke_result_t<std::decay_t<Fn>, std::decay_t<Args>...>, is_cancellable>> {
        auto* pool{ GlobalThreadPool::GetInstance() };
        if (!pool) {
            return std::unexpected{
                Error{ ErrorCode::FAILED_INIT, "mcr::async: global thread pool has been cleaned up." }
            };
        }
        using ReturnType = std::invoke_result_t<std::decay_t<Fn>, std::decay_t<Args>...>;
        if constexpr (is_cancellable) {
            // Allocate before submission so allocation failure cannot leave an unreturned task running.
            auto state{ std::make_shared<std::atomic_bool>(false) };
            auto future{ pool->Submit(std::forward<Fn>(fn), std::forward<Args>(args)...) };
            if (!future) {
                return std::unexpected{ std::move(future.error()) };
            }
            return utils::AsyncWrapper<ReturnType, true>{ std::move(*future), std::move(state) };
        } else {
            auto future = pool->Submit(std::forward<Fn>(fn), std::forward<Args>(args)...);
            if (!future) {
                return std::unexpected{ std::move(future.error()) };
            }
            return utils::AsyncWrapper<ReturnType, false>{ std::move(*future) };
        }
    }

    /**
     * @brief Configure startup and permanently clean up the global asynchronous runtime.
     * @note Lifecycle operations require external coordination with submissions and direct pool access.
     * Call Cleanup outside pool workers. Call the pool's Wait first if all queued tasks must finish.
     */
    class Async {
    public:
        /**
         * @brief Configure and start a stopped global pool; leave an already started pool unchanged.
         * @param min_threads Minimum worker count; zero allows all workers to retire when idle.
         * @param max_threads Positive maximum worker count, at least min_threads.
         * @param max_idle_ms Positive idle lifetime for workers above the minimum.
         * @note Parameters are ignored when the pool is already running or paused, following cpr.
         * Initialization and worker creation exceptions propagate.
         * @return Success or the first operation error.
         */
        static auto Startup(
            std::size_t               min_threads = utils::DEFAULT_THREAD_POOL_MIN_THREAD_NUM,
            std::size_t               max_threads = utils::DEFAULT_THREAD_POOL_MAX_THREAD_NUM,
            std::chrono::milliseconds max_idle_ms = utils::DEFAULT_THREAD_POOL_MAX_IDLE_TIME
        ) -> Result<void> {
            auto* pool{ GlobalThreadPool::GetInstance() };
            if (!pool) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::Async::Startup: global thread pool has been cleaned up." }
                };
            }
            if (pool->IsStarted()) {
                return {};
            }
            if (max_threads == 0 || min_threads > max_threads || max_idle_ms <= std::chrono::milliseconds::zero()) {
                return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::Async::Startup: require 0 <= min <= max, max > 0, and a positive idle time." }
                };
            }
            // The pool validates each setter against the current limits, so choose a valid order.
            if (min_threads > pool->GetMaxThreadNum()) {
                pool->SetMaxThreadNum(max_threads);
                pool->SetMinThreadNum(min_threads);
            } else {
                pool->SetMinThreadNum(min_threads);
                pool->SetMaxThreadNum(max_threads);
            }
            pool->SetMaxIdleTime(max_idle_ms);
            (void)pool->Start();
            return {};
        }

        /**
         * @brief Permanently destroy the singleton, cancelling queued tasks and joining active workers.
         * @throws std::logic_error If the singleton has never been successfully initialized.
         * @note Repeated cleanup has no effect. Later async or Startup calls return errors; cleanup is not a pause.
         * Do not call from a global pool task or overlap other access to the singleton.
         */
        static auto Cleanup() -> void {
            GlobalThreadPool::ExitInstance();
        }
    };

} // namespace mcr
