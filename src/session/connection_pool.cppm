/**
 * @file connection_pool.cppm
 * @brief Shared ownership of curl connection and TLS session caches.
 */
module;

#include <curl/curl.h>

export module mcr.connection_pool;

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
         * @throws std::runtime_error If curl initialization or share configuration fails.
         */
        ConnectionPool()
            : m_mutexes{ std::make_shared<Mutexes>() },
              m_curl_share{ curl_share_init(), cleanupShare } {
            if (!m_curl_share) {
                throw std::runtime_error{ "mcr::ConnectionPool: curl_share_init failed." };
            }
            checkShareResult(curl_share_setopt(m_curl_share.get(), CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT), "CURLSHOPT_SHARE(CURL_LOCK_DATA_CONNECT)");
            checkShareResult(curl_share_setopt(m_curl_share.get(), CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION), "CURLSHOPT_SHARE(CURL_LOCK_DATA_SSL_SESSION)");
            checkShareResult(curl_share_setopt(m_curl_share.get(), CURLSHOPT_USERDATA, static_cast<void*>(m_mutexes.get())), "CURLSHOPT_USERDATA");
            checkShareResult(curl_share_setopt(m_curl_share.get(), CURLSHOPT_LOCKFUNC, static_cast<curl_lock_function>(lock)), "CURLSHOPT_LOCKFUNC");
            checkShareResult(curl_share_setopt(m_curl_share.get(), CURLSHOPT_UNLOCKFUNC, static_cast<curl_unlock_function>(unlock)), "CURLSHOPT_UNLOCKFUNC");
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
         * @throws std::invalid_argument If easy_handler is null.
         * @throws std::runtime_error If setting CURLOPT_SHARE fails.
         * @note The easy handle does not retain C++ ownership of the pool.
         */
        auto SetupHandler(CURL* easy_handler) const -> void {
            if (!easy_handler) {
                throw std::invalid_argument{ "mcr::ConnectionPool: SetupHandler requires a nonnull easy handle." };
            }
            auto const result{ curl_easy_setopt(easy_handler, CURLOPT_SHARE, m_curl_share.get()) };
            if (result != CURLE_OK) {
                throw std::runtime_error{ std::format("mcr::ConnectionPool: CURLOPT_SHARE failed: {}", curl_easy_strerror(result)) };
            }
        }

    private:
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
         * @throws std::runtime_error If result is not CURLSHE_OK.
         */
        static auto checkShareResult(CURLSHcode result, std::string_view operation) -> void {
            if (result != CURLSHE_OK) {
                throw std::runtime_error{ std::format("mcr::ConnectionPool: {} failed: {}", operation, curl_share_strerror(result)) };
            }
        }
    };

} // namespace mcr
