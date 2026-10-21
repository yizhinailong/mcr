/**
 * @file connection_pool.cppm
 * @brief Shared ownership of curl connection and TLS session caches.
 */
module;

#include <curl/curl.h>

export module mcr:connection_pool;

export import :error;
import :curlholder;
import std;

export namespace mcr {

    /**
     * @brief Share connection and TLS session caches between easy handles.
     * @note Sessions and SetupHandler(CurlHolder&) retain the pool until easy-handle cleanup.
     * Raw SetupHandler(CURL*) borrows the pool; retain a copy until detachment or cleanup.
     * Callers manage curl's global initialization.
     * Libcurl does not support using a shared connection cache in concurrent threads;
     * the lock callbacks do not remove this restriction. Serialize use of the same pool.
     */
    class ConnectionPool {
    private:
        using Mutexes = std::array<std::mutex, CURL_LOCK_DATA_LAST>;

        /**
         * @brief Keep callback storage alive until libcurl actually releases its share handle.
         */
        struct State {
            Mutexes mutexes;                    ///< Locks indexed by curl data type.
            CURLSH* share{ curl_share_init() }; ///< Owned shared caches.
            State* next{};                      ///< Intrusive link used only after failed cleanup.
        };

        /**
         * @brief Retain prematurely released borrowed shares without allocating during destruction.
         */
        struct RetiredStates {
            std::mutex mutex; ///< Serializes collection and retirement.
            State* head{};    ///< States still rejected by curl_share_cleanup.
        };

        std::shared_ptr<State> m_state; ///< Shared caches and callback storage.

    public:
        /**
         * @brief Create connection and TLS session caches and install lock callbacks.
         * @throws std::bad_alloc If allocating shared ownership or mutex storage fails.
         * @return Success or the first operation error.
         */
        [[nodiscard]] static auto Create() -> Result<ConnectionPool> {
            (void)CollectUnused();
            ConnectionPool result;
            if (!result.m_state->share) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::ConnectionPool: curl_share_init failed." }
                };
            }
            auto const share = result.m_state->share;
            if (auto status = checkShareResult(curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT), "curl_share_setopt"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = checkShareResult(curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION), "curl_share_setopt"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = checkShareResult(curl_share_setopt(share, CURLSHOPT_USERDATA, static_cast<void*>(&result.m_state->mutexes)), "curl_share_setopt"); !status) {
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
        ConnectionPool(ConnectionPool const& other) noexcept = default;
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
            auto const result{ curl_easy_setopt(easy_handler, CURLOPT_SHARE, m_state->share) };
            if (result != CURLE_OK) {
                return std::unexpected{
                    Error{ static_cast<std::int32_t>(result), std::format("mcr::ConnectionPool: CURLOPT_SHARE failed: {}", curl_easy_strerror(result)) }
                };
            }
            return {};
        }

        /**
         * @brief Attach an idle owned handle and retain its caches through easy-handle cleanup.
         * @param holder Holder whose previous pool ownership is replaced only after successful setup.
         * @return Success or a curl option error.
         */
        auto SetupHandler(curl::CurlHolder& holder) const -> Result<void> {
            if (auto status = SetupHandler(holder.handle); !status) {
                return status;
            }
            holder.connection_pool = m_state;
            return {};
        }

        /**
         * @brief Retry shares whose last borrowed pool copy was destroyed before handle detachment.
         * @return Number of states still retained because libcurl rejected their cleanup.
         * @note Call after detaching or cleaning raw borrowed handles and before curl_global_cleanup.
         * Create() also retries collection. Normal owned-handle use requires no explicit collection.
         */
        [[nodiscard]] static auto CollectUnused() noexcept -> std::size_t {
            auto& retired = retiredStates();
            std::lock_guard guard{ retired.mutex };
            std::size_t pending{};
            auto** link = &retired.head;
            while (*link) {
                auto* state = *link;
                if (releaseShare(*state)) {
                    *link = state->next;
                    delete state;
                } else {
                    ++pending;
                    link = &state->next;
                }
            }
            return pending;
        }

    private:
        /**
         * @brief Acquire resources before checked share configuration.
         */
        ConnectionPool() : m_state{ std::make_unique<State>().release(), cleanupShare } {}

        /**
         * @brief Lock the mutex for the data type requested by libcurl.
         */
        static auto lock(
            CURL*,
            curl_lock_data data,
            curl_lock_access,
            void* userptr
        ) noexcept -> void {
            (*static_cast<Mutexes*>(userptr))[static_cast<std::size_t>(data)].lock();
        }

        /**
         * @brief Unlock the mutex for the data type requested by libcurl.
         */
        static auto unlock(CURL*, curl_lock_data data, void* userptr) noexcept -> void {
            (*static_cast<Mutexes*>(userptr))[static_cast<std::size_t>(data)].unlock();
        }

        /**
         * @brief Store rejected cleanup states until all raw borrowers have detached.
         */
        static auto cleanupShare(State* state) noexcept -> void {
            if (releaseShare(*state)) {
                delete state;
            } else {
                auto& retired = retiredStates();
                std::lock_guard guard{ retired.mutex };
                state->next = retired.head;
                retired.head = state;
            }
        }

        /**
         * @brief Check libcurl cleanup before releasing any lock callback storage.
         */
        static auto releaseShare(State& state) noexcept -> bool {
            return !state.share || curl_share_cleanup(state.share) == CURLSHE_OK;
        }

        /**
         * @brief Access allocation-free retirement storage without curl work during static destruction.
         */
        static auto retiredStates() noexcept -> RetiredStates& {
            static RetiredStates s_retired;
            return s_retired;
        }

        /**
         * @brief Report a failed share option instead of leaving a partially configured pool.
         * @param result Result returned by curl_share_setopt.
         * @param operation Name of the option being configured.
         * @return Success or the first operation error.
         */
        static auto checkShareResult(
            CURLSHcode result,
            std::string_view operation
        ) -> Result<void> {
            if (result != CURLSHE_OK) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, std::format("mcr::ConnectionPool: {} failed: {}", operation, curl_share_strerror(result)) }
                };
            }
            return {};
        }
    };

} // namespace mcr
