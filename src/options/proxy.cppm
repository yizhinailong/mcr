/**
 * @file proxy.cppm
 * @brief Protocol-specific proxy addresses and encoded authentication credentials.
 */
export module mcr.proxy;

import mcr.secure_string;
import mcr.util;
export import mcr.error;
import std;

export namespace mcr::options {

    /**
     * @brief Store protocol-to-proxy mappings, following cpr's Proxies option.
     * @note Protocol names are case-sensitive and all text is stored without validation.
     * Has() only queries; subscript inserts an empty value when a protocol is absent.
     */
    class Proxies {
    private:
        std::map<std::string, std::string, std::less<>> m_hosts; ///< Owned mappings with transparent, case-sensitive lookup.

    public:
        /**
         * @brief Construct an option with no proxy mappings.
         */
        Proxies() = default;

        /**
         * @brief Copy protocol-to-proxy pairs from an initializer list.
         * @param hosts Mappings to store using std::map's duplicate-key initialization rules.
         */
        Proxies(std::initializer_list<std::pair<std::string const, std::string>> hosts)
            : m_hosts{ hosts } {}

        /**
         * @brief Explicitly copy protocol-to-proxy mappings from a standard map.
         * @param hosts Source map, which remains independent of this option.
         */
        explicit Proxies(std::map<std::string, std::string> const& hosts)
            : m_hosts{ hosts.begin(), hosts.end() } {}

        /**
         * @brief Check for an exactly matching protocol without inserting or copying the key.
         * @param protocol Borrowed protocol name; bounded views and embedded nulls are supported.
         * @return True when the key exists, even if its proxy address is empty.
         */
        [[nodiscard]] auto Has(std::string_view protocol) const -> bool {
            return m_hosts.contains(protocol);
        }

        /**
         * @brief Access a proxy address, inserting an empty string for an absent protocol.
         * @param protocol Protocol name to look up, copied into owned storage if inserted.
         * @return A read-only reference to the stored address, stable across other insertions.
         * @note This operation is non-const because lookup may insert, matching cpr.
         */
        auto operator[](std::string_view protocol) -> std::string const& {
            auto const found{ m_hosts.find(protocol) };
            if (found != m_hosts.end()) {
                return found->second;
            }
            return m_hosts.try_emplace(std::string{ protocol }).first->second;
        }
    };

    /**
     * @brief Own percent-encoded proxy credentials, preserving cpr's credential accessors.
     */
    class EncodedAuthentication {
    private:
        utils::SecureString m_username; ///< Encoded username.
        utils::SecureString m_password; ///< Encoded password.

    public:
        EncodedAuthentication() = default;

        /**
         * @brief Encode and own credentials.
         * @param username Raw username.
         * @param password Raw password.
         * @return Success or the first operation error.
         */
        [[nodiscard]] static auto Create(std::string_view username, std::string_view password) -> Result<EncodedAuthentication> {
            auto encoded_username = utils::url_encode(username);
            if (!encoded_username) {
                return std::unexpected{ std::move(encoded_username.error()) };
            }
            auto encoded_password = utils::url_encode(password);
            if (!encoded_password) {
                return std::unexpected{ std::move(encoded_password.error()) };
            }
            EncodedAuthentication result;
            result.m_username = std::move(*encoded_username);
            result.m_password = std::move(*encoded_password);
            return result;
        }

        virtual ~EncodedAuthentication()                                           = default;
        EncodedAuthentication(EncodedAuthentication const&)                        = default;
        EncodedAuthentication(EncodedAuthentication&&) noexcept                    = default;
        auto operator=(EncodedAuthentication const&) -> EncodedAuthentication&     = default;
        auto operator=(EncodedAuthentication&&) noexcept -> EncodedAuthentication& = default;

        /**
         * @brief Borrow the encoded username.
         * @return Bounded view of owned storage.
         */
        [[nodiscard]] auto GetUsername() const noexcept -> std::string_view { return m_username; }

        /**
         * @brief Borrow the encoded password.
         * @return Bounded view of owned storage.
         */
        [[nodiscard]] auto GetPassword() const noexcept -> std::string_view { return m_password; }

        /**
         * @brief Borrow secure username storage.
         * @return Encoded username.
         */
        [[nodiscard]] auto GetUsernameUnderlying() const noexcept -> utils::SecureString const& { return m_username; }

        /**
         * @brief Borrow secure password storage.
         * @return Encoded password.
         */
        [[nodiscard]] auto GetPasswordUnderlying() const noexcept -> utils::SecureString const& { return m_password; }
    };

    /**
     * @brief Map exact protocol names to owned proxy credentials.
     */
    class ProxyAuthentication {
    private:
        std::map<std::string, EncodedAuthentication, std::less<>> m_auths; ///< Transparent protocol lookup.

    public:
        ProxyAuthentication() = default;

        /**
         * @brief Copy protocol credentials.
         * @param auths Entries to own.
         */
        ProxyAuthentication(std::initializer_list<std::pair<std::string const, EncodedAuthentication>> auths) : m_auths{ auths } {}

        /**
         * @brief Copy a standard map.
         * @param auths Entries to own.
         */
        explicit ProxyAuthentication(std::map<std::string, EncodedAuthentication> const& auths) : m_auths{ auths.begin(), auths.end() } {}

        /**
         * @brief Inspect membership without insertion.
         * @param protocol Exact protocol.
         * @return Whether configured.
         */
        [[nodiscard]] auto Has(std::string_view protocol) const -> bool { return m_auths.contains(protocol); }

        /**
         * @brief Borrow encoded username, inserting an empty entry when absent.
         * @param protocol Exact protocol.
         * @return Encoded username.
         */
        [[nodiscard]] auto GetUsername(std::string_view protocol) -> std::string_view { return m_auths[std::string{ protocol }].GetUsername(); }

        /**
         * @brief Borrow encoded password, inserting an empty entry when absent.
         * @param protocol Exact protocol.
         * @return Encoded password.
         */
        [[nodiscard]] auto GetPassword(std::string_view protocol) -> std::string_view { return m_auths[std::string{ protocol }].GetPassword(); }

        /**
         * @brief Borrow existing secure username storage.
         * @param protocol Exact protocol.
         * @return Encoded username.
         * @throws std::out_of_range If absent.
         */
        [[nodiscard]] auto GetUsernameUnderlying(std::string_view protocol) const -> utils::SecureString const& { return find(protocol).GetUsernameUnderlying(); }

        /**
         * @brief Borrow existing secure password storage.
         * @param protocol Exact protocol.
         * @return Encoded password.
         * @throws std::out_of_range If absent.
         */
        [[nodiscard]] auto GetPasswordUnderlying(std::string_view protocol) const -> utils::SecureString const& { return find(protocol).GetPasswordUnderlying(); }

    private:
        auto find(std::string_view protocol) const -> EncodedAuthentication const& {
            auto const entry{ m_auths.find(protocol) };
            if (entry == m_auths.end()) {
                throw std::out_of_range{ "mcr::options::ProxyAuthentication: protocol not configured." };
            }
            return entry->second;
        }
    };

} // namespace mcr::options
