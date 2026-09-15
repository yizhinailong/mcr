/**
 * @file connection_pool.cppm
 * @brief Shared ownership of curl connection and TLS session caches.
 */
module;

#include <curl/curl.h>

export module mcr.connection_pool;

export import mcr.error;
import std;

export namespace mcr {

    /**
     * @brief Share connection and TLS session caches between easy handles.
     * @note Keep at least one copy alive until all attached easy handles are cleaned up or
     * detached with CURLOPT_SHARE set to null. Callers manage curl's global initialization.
     * Libcurl does not support using a shared connection cache in concurrent threads;
     * the lock callbacks do not remove this restriction. Serialize use of the same pool.
     */
    class ConnectionPool {
    private:
        using Mutexes = std::array<std::mutex, CURL_LOCK_DATA_LAST>;

        std::shared_ptr<Mutexes> m_mutexes;    ///< Locks by curl data type; outlive the share handle.
        std::shared_ptr<CURLSH>  m_curl_share; ///< Shared caches, cleaned up when the final copy is destroyed.

    public:
        /**
         * @brief Create connection and TLS session caches and install lock callbacks.
         * @throws std::bad_alloc If allocating shared ownership or mutex storage fails.
         * @return Success or the first operation error.
         */
        [[nodiscard]] static auto Create() -> Result<ConnectionPool> {
            ConnectionPool result;
            if (!result.m_curl_share) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::ConnectionPool: curl_share_init failed." }
                };
            }
            auto const share = result.m_curl_share.get();
            if (auto status = checkShareResult(curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT), "curl_share_setopt"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = checkShareResult(curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION), "curl_share_setopt"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = checkShareResult(curl_share_setopt(share, CURLSHOPT_USERDATA, static_cast<void*>(result.m_mutexes.get())), "curl_share_setopt"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = checkShareResult(curl_share_setopt(share, CURLSHOPT_LOCKFUNC, static_cast<curl_lock_function>(lock)), "curl_share_setopt"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = checkShareResult(curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, static_cast<curl_unlock_function>(unlock)), "curl_share_setopt"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return result;
        }

        /**
         * @brief Share the same caches and locks with another pool, following cpr.
         * @param other Pool whose shared state is retained; rvalues are also copied.
         */
        ConnectionPool(ConnectionPool const& other) noexcept     = default;
        auto operator=(ConnectionPool const&) -> ConnectionPool& = delete;

        /**
         * @brief Attach an idle easy handle to this pool's shared caches.
         * @param easy_handler Valid easy handle owned by the caller.
         * @note The easy handle does not retain C++ ownership of the pool.
         * @return Success or the first operation error.
         */
        auto SetupHandler(CURL* easy_handler) const -> Result<void> {
            if (!easy_handler) {
                return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::ConnectionPool: SetupHandler requires a nonnull easy handle." }
                };
            }
            auto const result{ curl_easy_setopt(easy_handler, CURLOPT_SHARE, m_curl_share.get()) };
            if (result != CURLE_OK) {
                return std::unexpected{
                    Error{ static_cast<std::int32_t>(result), std::format("mcr::ConnectionPool: CURLOPT_SHARE failed: {}", curl_easy_strerror(result)) }
                };
            }
            return {};
        }

    private:
        /**
         * @brief Acquire resources before checked share configuration.
         */
        ConnectionPool() : m_mutexes{ std::make_shared<Mutexes>() }, m_curl_share{ curl_share_init(), cleanupShare } {}

        /**
         * @brief Lock the mutex for the data type requested by libcurl.
         */
        static auto lock(CURL*, curl_lock_data data, curl_lock_access, void* userptr) noexcept -> void {
            (*static_cast<Mutexes*>(userptr))[static_cast<std::size_t>(data)].lock();
        }

        /**
         * @brief Unlock the mutex for the data type requested by libcurl.
         */
        static auto unlock(CURL*, curl_lock_data data, void* userptr) noexcept -> void {
            (*static_cast<Mutexes*>(userptr))[static_cast<std::size_t>(data)].unlock();
        }

        /**
         * @brief Disable callbacks and release a share handle while its mutex storage is alive.
         */
        static auto cleanupShare(CURLSH* share) noexcept -> void {
            if (share) {
                (void)curl_share_setopt(share, CURLSHOPT_LOCKFUNC, static_cast<curl_lock_function>(nullptr));
                (void)curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, static_cast<curl_unlock_function>(nullptr));
                (void)curl_share_cleanup(share);
            }
        }

        /**
         * @brief Report a failed share option instead of leaving a partially configured pool.
         * @param result Result returned by curl_share_setopt.
         * @param operation Name of the option being configured.
         * @return Success or the first operation error.
         */
        static auto checkShareResult(CURLSHcode result, std::string_view operation) -> Result<void> {
            if (result != CURLSHE_OK) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, std::format("mcr::ConnectionPool: {} failed: {}", operation, curl_share_strerror(result)) }
                };
            }
            return {};
        }
    };

} // namespace mcr
