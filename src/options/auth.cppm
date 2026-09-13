/**
 * @file auth.cppm
 * @brief HTTP authentication modes, owned credentials, and bearer tokens.
 */
module;

#include <curl/curlver.h>

export module mcr.auth;

import mcr.secure_string;
import std;

export namespace mcr::options {

    /**
     * @brief Authentication policies retaining cpr's uint8_t names and ordinal values.
     */
    enum class AuthMode : std::uint8_t {
        BASIC,     ///< HTTP Basic authentication.
        DIGEST,    ///< HTTP Digest authentication.
        NTLM,      ///< NTLM authentication.
        NEGOTIATE, ///< Negotiate/SPNEGO authentication.
        ANY,       ///< Let curl choose among the authentication methods it supports.
        ANYSAFE,   ///< Let curl choose among supported methods other than Basic.
    };

    /**
     * @brief Own raw username:password bytes and an authentication policy, following cpr.
     * @note Uses utils::SecureString storage; its allocator wipes released heap allocations.
     * Credentials and mode are retained without encoding, normalization, or validation.
     */
    class Authentication {
    private:
        utils::SecureString m_auth_string; ///< Owned credentials, including the colon separator.
        AuthMode            m_auth_mode;   ///< Selected authentication policy.

    public:
        /**
         * @brief Copy the input views with one colon inserted between them.
         * @param username Username bytes; the view need not be null-terminated.
         * @param password Password bytes; empty values and embedded nulls are retained.
         * @param auth_mode Authentication policy to store verbatim.
         * @throws std::bad_alloc If allocating credential storage fails.
         * @throws std::length_error If the combined credentials exceed string capacity.
         */
        Authentication(std::string_view username, std::string_view password, AuthMode auth_mode)
            : m_auth_string{ username }, m_auth_mode{ auth_mode } {
            m_auth_string += ':';
            m_auth_string += password;
        }

        /**
         * @brief Borrow the null-terminated credential string.
         * @return A nonnull pointer to owned bytes; C-string consumers stop at the first embedded null.
         * @note Assignment, moving, or destruction can invalidate the returned pointer.
         */
        [[nodiscard]] auto GetAuthString() const noexcept -> char const* {
            return m_auth_string.c_str();
        }

        /**
         * @brief Read the selected authentication policy.
         * @return The mode supplied at construction or copied from another authentication object.
         */
        [[nodiscard]] auto GetAuthMode() const noexcept -> AuthMode {
            return m_auth_mode;
        }
    };

#if LIBCURL_VERSION_NUM >= 0x073D00 // HTTP bearer authentication was added in 7.61.0.
    /**
     * @brief Own raw bearer token bytes and allow derived classes to customize token access.
     * @note Available when built with curl headers at least 7.61.0, following cpr.
     * No authorization prefix, encoding, or validation is applied to the token.
     */
    class Bearer {
    public:
        /**
         * @brief Copy a token view into owned secure-string storage.
         * @param token Token bytes, including empty views, without requiring null termination.
         * @throws std::bad_alloc If allocating token storage fails.
         * @throws std::length_error If the token exceeds string capacity.
         */
        Bearer(std::string_view token) : m_token_string{ token } {}

        /**
         * @brief Copy token bytes into independent secure-string storage.
         * @param other Source token.
         */
        Bearer(Bearer const& other)                        = default;

        /**
         * @brief Move the token using secure-string move semantics.
         * @param other Source token.
         */
        Bearer(Bearer&& other) noexcept                    = default;

        /**
         * @brief Destroy the token, including derived state when deleted through a base pointer.
         */
        virtual ~Bearer() noexcept                         = default;

        /**
         * @brief Move token storage.
         * @param other Source token.
         * @return This object after assignment.
         */
        auto operator=(Bearer&& other) noexcept -> Bearer& = default;

        /**
         * @brief Copy token bytes.
         * @param other Source token.
         * @return This object after assignment.
         */
        auto operator=(Bearer const& other) -> Bearer&     = default;

        /**
         * @brief Borrow the null-terminated token string; derived classes may override this accessor.
         * @return For the base implementation, a nonnull pointer into owned token storage.
         * @note Assignment, moving, derived mutation, or destruction can invalidate the pointer.
         * Embedded null bytes are stored, but C-string consumers see only their preceding prefix.
         */
        [[nodiscard]] virtual auto GetToken() const noexcept -> char const* {
            return m_token_string.c_str();
        }

    protected:
        utils::SecureString m_token_string; ///< Owned token bytes, available for derived customization.
    };
#endif

} // namespace mcr::options
