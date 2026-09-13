/**
 * @file cookies.cppm
 * @brief Owned cookie metadata and ordered request cookie serialization.
 */
export module mcr.cookies;

export import mcr.curlholder;

import std;

export namespace mcr {

    inline constexpr std::size_t EXPIRES_STRING_SIZE{ 100 }; ///< Date buffer size retained for cpr compatibility.

    /**
     * @brief Store cookie metadata without normalizing or validating its contents.
     * @note A default-constructed cookie has an empty path; the name/value constructor defaults to "/",
     * matching cpr. Both constructors use the Unix epoch as the default expiration time.
     */
    class Cookie {
    private:
        std::string                           m_name;                        ///< Owned cookie name.
        std::string                           m_value;                       ///< Owned cookie value.
        std::string                           m_domain;                      ///< Domain associated with the cookie.
        bool                                  m_include_subdomains{ false }; ///< Whether the cookie applies to subdomains.
        std::string                           m_path;                        ///< Path associated with the cookie.
        bool                                  m_https_only{ false };         ///< Whether the cookie is restricted to HTTPS.
        std::chrono::system_clock::time_point m_expires{};                   ///< Expiration time, defaulting to the Unix epoch.

    public:
        /**
         * @brief Construct a cookie with empty text fields and default metadata.
         */
        Cookie() = default;

        /**
         * @brief Take ownership of a cookie's name, value, and optional metadata.
         * @param name Cookie name, stored verbatim.
         * @param value Cookie value, including any embedded nulls.
         * @param domain Cookie domain, empty by default.
         * @param include_subdomains Whether the cookie applies to subdomains.
         * @param path Cookie path, defaulting to "/".
         * @param https_only Whether the cookie is restricted to HTTPS.
         * @param expires Expiration time, defaulting to the Unix epoch.
         */
        Cookie(
            std::string                           name,
            std::string                           value,
            std::string                           domain             = {},
            bool                                  include_subdomains = false,
            std::string                           path               = "/",
            bool                                  https_only         = false,
            std::chrono::system_clock::time_point expires            = std::chrono::system_clock::from_time_t(0)
        ) : m_name{ std::move(name) }, m_value{ std::move(value) }, m_domain{ std::move(domain) }, m_include_subdomains{ include_subdomains }, m_path{ std::move(path) }, m_https_only{ https_only }, m_expires{ expires } {}

        /**
         * @brief Get the stored domain.
         * @return A reference to the owned domain text.
         */
        [[nodiscard]] auto GetDomain() const noexcept -> std::string const& {
            return m_domain;
        }

        /**
         * @brief Check subdomain scope.
         * @return Whether the cookie applies to subdomains.
         */
        [[nodiscard]] auto IsIncludingSubdomains() const noexcept -> bool {
            return m_include_subdomains;
        }

        /**
         * @brief Get the stored path.
         * @return A reference to the owned path text.
         */
        [[nodiscard]] auto GetPath() const noexcept -> std::string const& {
            return m_path;
        }

        /**
         * @brief Check transport restrictions.
         * @return Whether the cookie requires HTTPS.
         */
        [[nodiscard]] auto IsHttpsOnly() const noexcept -> bool {
            return m_https_only;
        }

        /**
         * @brief Get the expiration time.
         * @return The stored time point without precision loss.
         */
        [[nodiscard]] auto GetExpires() const noexcept -> std::chrono::system_clock::time_point {
            return m_expires;
        }

        /**
         * @brief Format the expiration time as an English GMT date with whole-second precision.
         * @return Text such as "Thu, 01 Jan 1970 00:00:00 GMT".
         * @note Chrono formatting replaces cpr's platform-specific gmtime and locale-sensitive put_time.
         * Fractional seconds are rounded down; dates before the epoch are supported.
         */
        [[nodiscard]] auto GetExpiresString() const -> std::string {
            return std::format("{:%a, %d %b %Y %T} GMT", std::chrono::floor<std::chrono::seconds>(m_expires));
        }

        /**
         * @brief Get the cookie name.
         * @return A reference to the owned name text.
         */
        [[nodiscard]] auto GetName() const noexcept -> std::string const& {
            return m_name;
        }

        /**
         * @brief Get the cookie value.
         * @return A reference to the owned value text.
         */
        [[nodiscard]] auto GetValue() const noexcept -> std::string const& {
            return m_value;
        }
    };

    /**
     * @brief Store cookies in insertion order, preserving duplicate names as in cpr.
     * @note Standard container member names are retained for iteration and existing cpr usage.
     * Iterators and references follow std::vector invalidation rules.
     */
    class Cookies {
    private:
        std::vector<Cookie> m_cookies; ///< Owned cookies in insertion order.

    public:
        bool encode{ true };                                        ///< Whether request serialization percent-encodes cookie names and unquoted values.

        using iterator       = std::vector<Cookie>::iterator;       ///< Mutable cookie iterator.
        using const_iterator = std::vector<Cookie>::const_iterator; ///< Read-only cookie iterator.

        /**
         * @brief Construct an empty collection with the requested encoding mode.
         * @param encode_param Whether to percent-encode names and unquoted values.
         */
        Cookies(bool encode_param = true) : encode{ encode_param } {}

        /**
         * @brief Copy cookies in the supplied order without deduplication.
         * @param cookies Cookies to store.
         * @param encode_param Whether to percent-encode names and unquoted values.
         */
        Cookies(std::initializer_list<Cookie> const& cookies, bool encode_param = true)
            : m_cookies{ cookies }, encode{ encode_param } {}

        /**
         * @brief Copy one cookie into a collection.
         * @param cookie Cookie to store.
         * @param encode_param Whether to percent-encode names and unquoted values.
         */
        Cookies(Cookie const& cookie, bool encode_param = true) : m_cookies{ cookie }, encode{ encode_param } {}

        /**
         * @brief Access a cookie by its insertion position.
         * @param pos Zero-based position.
         * @return A mutable reference to the cookie.
         * @pre pos must be less than the number of cookies, as with std::vector::operator[].
         */
        auto operator[](std::size_t pos) -> Cookie& {
            return m_cookies[pos];
        }

        /**
         * @brief Find the first cookie with an exactly matching name.
         * @param key Name to look up; a string view is accepted without copying the search key.
         * @return A mutable reference to the first matching cookie.
         * @throws std::out_of_range If no cookie has the requested name; no cookie is inserted.
         */
        auto operator[](std::string_view key) -> Cookie& {
            auto const found{ std::ranges::find(m_cookies, key, &Cookie::GetName) };
            if (found == m_cookies.end()) {
                throw std::out_of_range{ std::format("Cookie: {} does not exist", key) };
            }
            return *found;
        }

        /**
         * @brief Serialize cookie name/value pairs for a request.
         * @param holder Curl handle owner used for percent encoding when encode is true.
         * @return Ordered "name=value; " pairs, including the trailing separator, or an empty string.
         * @note Names and unquoted values are encoded when requested. Values beginning and ending
         * with a double quote are copied verbatim, including a single double-quote character, as in cpr.
         * Domain, path, HTTPS, and expiration metadata are not included in the request header.
         * Exceptions from CurlHolder::UrlEncode() propagate.
         */
        [[nodiscard]] auto GetEncoded(curl::CurlHolder const& holder) const -> std::string {
            std::string result;
            for (auto const& cookie : m_cookies) {
                if (encode) {
                    result += holder.UrlEncode(cookie.GetName());
                } else {
                    result += cookie.GetName();
                }
                result += '=';

                auto const& value{ cookie.GetValue() };
                if (encode && !(value.starts_with('"') && value.ends_with('"'))) {
                    result += holder.UrlEncode(value);
                } else {
                    result += value;
                }
                result += "; ";
            }
            return result;
        }

        /**
         * @brief Begin mutable iteration.
         * @return An iterator to the first cookie.
         */
        auto begin() noexcept -> iterator { return m_cookies.begin(); }

        /**
         * @brief End mutable iteration.
         * @return An iterator past the last cookie.
         */
        auto end() noexcept -> iterator { return m_cookies.end(); }

        /**
         * @brief Begin read-only iteration.
         * @return An iterator to the first cookie.
         */
        [[nodiscard]] auto begin() const noexcept -> const_iterator { return m_cookies.begin(); }

        /**
         * @brief End read-only iteration.
         * @return An iterator past the last cookie.
         */
        [[nodiscard]] auto end() const noexcept -> const_iterator { return m_cookies.end(); }

        /**
         * @brief Begin read-only iteration.
         * @return An iterator to the first cookie.
         */
        [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return m_cookies.cbegin(); }

        /**
         * @brief End read-only iteration.
         * @return An iterator past the last cookie.
         */
        [[nodiscard]] auto cend() const noexcept -> const_iterator { return m_cookies.cend(); }

        /**
         * @brief Append a copy, retaining cpr's single-cookie overload.
         * @param cookie Cookie to copy.
         */
        auto emplace_back(Cookie const& cookie) -> void { m_cookies.emplace_back(cookie); }

        /**
         * @brief Check whether the collection is empty.
         * @return True when no cookies are stored.
         */
        [[nodiscard]] auto empty() const noexcept -> bool { return m_cookies.empty(); }

        /**
         * @brief Append a copy at the end.
         * @param cookie Cookie to copy.
         */
        auto push_back(Cookie const& cookie) -> void { m_cookies.push_back(cookie); }

        /**
         * @brief Remove the last cookie.
         * @pre The collection must not be empty.
         */
        auto pop_back() -> void { m_cookies.pop_back(); }
    };

} // namespace mcr
