/**
 * @file global_init.cppm
 * @brief Scoped curl global initialization and ordered shutdown of process-wide runtimes.
 */
module;

#include <curl/curl.h>

export module mcr:global_init;

export import :error;
import :async;
import :connection_pool;
import :coro_runtime;
import std;

export namespace mcr {

    /**
     * @brief Initialize curl for the process and release library runtimes before curl cleanup.
     * @note Optional: without a guard, curl initializes lazily on the first handle, as in cpr.
     * Create the first guard before other threads use curl. Guards are reference counted;
     * destroying the last one permanently cleans up Coro and Async runtimes, collects unused
     * connection pool shares, and then calls curl_global_cleanup. Later Create calls fail.
     * Destroy the last guard outside runtime and global pool threads after all sessions,
     * responses, tasks, and raw curl handles have been released.
     */
    class GlobalInit {
    public:
        /**
         * @brief Acquire a reference to curl's global initialization.
         * @return Guard, or the curl_global_init error, or FAILED_INIT after final cleanup.
         */
        [[nodiscard]] static auto Create() -> Result<GlobalInit> {
            auto& shared = state();
            std::lock_guard lock{ shared.mutex };
            if (shared.finished) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::GlobalInit: library resources have been cleaned up." }
                };
            }
            if (shared.count == 0) {
                if (auto const code = curl_global_init(CURL_GLOBAL_DEFAULT); code != CURLE_OK) {
                    return std::unexpected{
                        Error{ static_cast<std::int32_t>(code), std::format("mcr::GlobalInit: curl_global_init failed: {}", curl_easy_strerror(code)) }
                    };
                }
            }
            ++shared.count;
            return GlobalInit{};
        }

        GlobalInit(GlobalInit const&) = delete;
        auto operator=(GlobalInit const&) -> GlobalInit& = delete;

        /**
         * @brief Transfer the reference without changing the count.
         * @param other Guard left without a reference.
         */
        GlobalInit(GlobalInit&& other) noexcept : m_owner{ std::exchange(other.m_owner, false) } {}

        /**
         * @brief Release the current reference, then take over another guard's reference.
         * @param other Guard left without a reference.
         * @return This guard.
         */
        auto operator=(GlobalInit&& other) noexcept -> GlobalInit& {
            if (this != &other) {
                release();
                m_owner = std::exchange(other.m_owner, false);
            }
            return *this;
        }

        /**
         * @brief Release the reference; the last one shuts down runtimes and curl.
         */
        ~GlobalInit() { release(); }

    private:
        /**
         * @brief Process-wide reference count and permanent shutdown flag.
         */
        struct State {
            std::mutex mutex;    ///< Serializes initialization and reference changes.
            std::size_t count{}; ///< Live guards.
            bool finished{};     ///< Whether the last guard has been released.
        };

        bool m_owner{ true }; ///< Whether this guard holds a reference.

        /**
         * @brief Construct a guard after its reference has been counted.
         */
        GlobalInit() = default;

        /**
         * @brief Access the shared state without static initialization order dependencies.
         * @return Function-local state.
         */
        static auto state() -> State& {
            static State s_state;
            return s_state;
        }

        /**
         * @brief Drop this guard's reference and shut down after the last one.
         */
        auto release() noexcept -> void {
            if (!std::exchange(m_owner, false)) {
                return;
            }
            {
                auto& shared = state();
                std::lock_guard lock{ shared.mutex };
                if (--shared.count != 0) {
                    return;
                }
                shared.finished = true;
            }
            shutdown();
        }

        /**
         * @brief Stop runtimes in dependency order, keeping curl initialized if any step cannot finish.
         * @note Skipping curl_global_cleanup leaves resources for process exit instead of freeing state in use.
         */
        static auto shutdown() noexcept -> void {
            try {
                if (Coro::IsRuntimeThread()) {
                    return;
                }
                auto* pool = GlobalThreadPool::HasInstance() ? GlobalThreadPool::GetInstance() : nullptr;
                if (pool && pool->IsWorkerThread()) {
                    return;
                }
                if (detail::CoroRuntime::Created() && !Coro::Cleanup()) {
                    return;
                }
                if (pool) {
                    Async::Cleanup();
                }
                if (ConnectionPool::CollectUnused() != 0) {
                    return;
                }
            } catch (...) {
                return;
            }
            curl_global_cleanup();
        }
    };

} // namespace mcr
