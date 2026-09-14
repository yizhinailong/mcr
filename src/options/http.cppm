/**
 * @file http.cppm
 * @brief HTTP protocol versions, response encodings, redirects, and transfer ranges.
 */
export module mcr.http;

import std;

export namespace mcr::options {

    /**
     * @brief Select an HTTP protocol policy using cpr's enum names and ordinal values.
     * @note These values require mapping to CURL_HTTP_VERSION_* before use with libcurl.
     * All policies are declared; actual protocol support depends on the linked curl backend.
     */
    enum class HttpVersionCode : std::uint8_t {
        VERSION_NONE,    ///< Let libcurl choose the protocol version.
        VERSION_1_0,     ///< Request HTTP/1.0.
        VERSION_1_1,     ///< Request HTTP/1.1.
        VERSION_2_0,     ///< Attempt HTTP/2 with fallback to HTTP/1.1 if negotiation fails.
        VERSION_2_0_TLS, ///< Attempt HTTP/2 for HTTPS with HTTP/1.1 fallback; use HTTP/1.1 for plain HTTP.
        /**
         * @brief Use HTTP/2 directly for plain HTTP, without HTTP/1.1 Upgrade.
         * @note Requires server support. HTTPS negotiates with ALPN; since curl 8.10.0,
         * only HTTP/2 is offered for HTTPS with this policy.
         */
        VERSION_2_0_PRIOR_KNOWLEDGE,
        VERSION_3_0,      ///< Attempt HTTP/3 with fallback to earlier HTTP versions.
        VERSION_3_0_ONLY, ///< Attempt HTTP/3 without falling back to earlier HTTP versions.
    };

    /**
     * @brief Store an HTTP protocol preference, defaulting to libcurl's choice.
     * @note The option stores its code verbatim; it does not validate or apply it to a request.
     */
    class HttpVersion {
    public:
        HttpVersionCode code{ HttpVersionCode::VERSION_NONE }; ///< Publicly mutable protocol preference.

        /**
         * @brief Let libcurl choose the HTTP protocol version.
         */
        HttpVersion() = default;

        /**
         * @brief Store a protocol preference without implicit conversion from the enum.
         * @param code_param Protocol policy, preserved without validation.
         */
        constexpr explicit HttpVersion(HttpVersionCode code_param) noexcept : code{ code_param } {}
    };

    /**
     * @brief Built-in encoding names and the disabled sentinel, retaining cpr's enum names and values.
     */
    enum class AcceptEncodingMethods : std::uint8_t {
        identity, ///< Request an unencoded response.
        deflate,  ///< Request deflate encoding.
        zlib,     ///< Request the literal zlib encoding name, as in cpr.
        gzip,     ///< Request gzip encoding.
        disabled, ///< Disable automatic Accept-Encoding handling.
    };

    /**
     * @brief Map built-in methods to their literal encoding names.
     * @note Exposes cpr's AcceptEncodingMethodsStringMap using the project's constant naming convention.
     */
    inline std::map<AcceptEncodingMethods, std::string> const ACCEPT_ENCODING_METHODS_STRING_MAP{
        { AcceptEncodingMethods::identity, "identity" },
        {  AcceptEncodingMethods::deflate,  "deflate" },
        {     AcceptEncodingMethods::zlib,     "zlib" },
        {     AcceptEncodingMethods::gzip,     "gzip" },
        { AcceptEncodingMethods::disabled, "disabled" },
    };

    /**
     * @brief Store encoding preferences with cpr's deduplication and disabled-sentinel semantics.
     * @note Public queries use Empty(), GetString(), and Disabled() following the project's naming style.
     * Custom strings are stored verbatim and output order is unspecified, as in cpr.
     */
    class AcceptEncoding {
    private:
        std::unordered_set<std::string> m_methods; ///< Owned encoding names, deduplicated by exact string equality.

    public:
        /**
         * @brief Construct an empty option representing all encodings supported by curl.
         */
        AcceptEncoding() = default;

        /**
         * @brief Store the names of the selected built-in encodings.
         * @param methods Built-in encodings; duplicates are ignored.
         * @throws std::out_of_range If a method is not a defined AcceptEncodingMethods value.
         * @note Mixing disabled with another method is checked by Disabled(), not during construction.
         */
        AcceptEncoding(std::initializer_list<AcceptEncodingMethods> const& methods) {
            for (auto const method : methods) {
                m_methods.insert(ACCEPT_ENCODING_METHODS_STRING_MAP.at(method));
            }
        }

        /**
         * @brief Copy custom encoding names without normalization or validation.
         * @param methods Encoding names, including empty strings; exact duplicates are ignored.
         * @note The exact string "disabled" has the same sentinel meaning as the enum value.
         */
        AcceptEncoding(std::initializer_list<std::string> const& methods) : m_methods{ methods } {}

        /**
         * @brief Check whether no encoding names were supplied.
         * @return True for an empty set, which selects curl's supported encodings rather than disabling them.
         */
        [[nodiscard]] auto Empty() const noexcept -> bool {
            return m_methods.empty();
        }

        /**
         * @brief Join encoding names with a comma and a space in unspecified order.
         * @return An owned string, or an empty string when no methods are stored.
         * @note Unlike cpr's getString(), this function safely handles an empty set.
         * An empty stored name still contributes an element and any required separator.
         */
        [[nodiscard]] auto GetString() const -> std::string {
            return m_methods | std::views::join_with(std::string_view{ ", " }) | std::ranges::to<std::string>();
        }

        /**
         * @brief Check whether automatic Accept-Encoding handling is disabled.
         * @return True if "disabled" is the only distinct name; false if it is absent.
         * @throws std::invalid_argument If "disabled" is combined with any other distinct name.
         */
        [[nodiscard]] auto Disabled() const -> bool {
            if (!m_methods.contains(ACCEPT_ENCODING_METHODS_STRING_MAP.at(AcceptEncodingMethods::disabled))) {
                return false;
            }
            if (m_methods.size() != 1) {
                throw std::invalid_argument{ "AcceptEncoding does not accept any other values if 'disabled' is present. You set the following encodings: " + GetString() };
            }
            return true;
        }
    };

    /**
     * @brief Flags selecting which redirects preserve POST, following cpr's bit values.
     * @note Bit operations and any() also support constant evaluation and do not throw.
     */
    enum class PostRedirectFlags : std::uint8_t {
        POST_301 = 0x1 << 0,                       ///< Preserve POST after a 301 redirect.
        POST_302 = 0x1 << 1,                       ///< Preserve POST after a 302 redirect.
        POST_303 = 0x1 << 2,                       ///< Preserve POST after a 303 redirect.
        POST_ALL = POST_301 | POST_302 | POST_303, ///< Preserve POST for all three redirect statuses.
        NONE     = 0x0                             ///< Use the default POST redirect behavior.
    };

    /**
     * @brief Combine the bits of two flag values.
     * @param lhs First flag value.
     * @param rhs Second flag value.
     * @return Bits set in either operand.
     */
    [[nodiscard]] constexpr auto operator|(PostRedirectFlags lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags {
        return static_cast<PostRedirectFlags>(std::to_underlying(lhs) | std::to_underlying(rhs));
    }

    /**
     * @brief Select the bits shared by two flag values.
     * @param lhs First flag value.
     * @param rhs Second flag value.
     * @return Bits set in both operands.
     */
    [[nodiscard]] constexpr auto operator&(PostRedirectFlags lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags {
        return static_cast<PostRedirectFlags>(std::to_underlying(lhs) & std::to_underlying(rhs));
    }

    /**
     * @brief Select the bits that differ between two flag values.
     * @param lhs First flag value.
     * @param rhs Second flag value.
     * @return Bits set in exactly one operand.
     */
    [[nodiscard]] constexpr auto operator^(PostRedirectFlags lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags {
        return static_cast<PostRedirectFlags>(std::to_underlying(lhs) ^ std::to_underlying(rhs));
    }

    /**
     * @brief Invert all eight bits, including unnamed bits, as in cpr.
     * @param flag Flag value to invert.
     * @return The full uint8_t complement, without masking to POST_ALL.
     */
    [[nodiscard]] constexpr auto operator~(PostRedirectFlags flag) noexcept -> PostRedirectFlags {
        return static_cast<PostRedirectFlags>(~std::to_underlying(flag));
    }

    /**
     * @brief Add the right operand's bits to the left operand.
     * @param lhs Flag value to update.
     * @param rhs Bits to add.
     * @return A reference to lhs after the update.
     */
    constexpr auto operator|=(PostRedirectFlags& lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags& {
        return lhs = lhs | rhs;
    }

    /**
     * @brief Keep only the left operand's bits selected by the right operand.
     * @param lhs Flag value to update.
     * @param rhs Bits to retain.
     * @return A reference to lhs after the update.
     */
    constexpr auto operator&=(PostRedirectFlags& lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags& {
        return lhs = lhs & rhs;
    }

    /**
     * @brief Toggle the right operand's bits in the left operand.
     * @param lhs Flag value to update.
     * @param rhs Bits to toggle.
     * @return A reference to lhs after the update.
     */
    constexpr auto operator^=(PostRedirectFlags& lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags& {
        return lhs = lhs ^ rhs;
    }

    /**
     * @brief Check whether any bit is set, including unnamed bits.
     * @param flag Flag value to inspect.
     * @return True when flag differs from NONE.
     */
    [[nodiscard]] constexpr auto any(PostRedirectFlags flag) noexcept -> bool {
        return flag != PostRedirectFlags::NONE;
    }

    /**
     * @brief Store redirect options with cpr-compatible defaults and constructor overloads.
     * @note Values are stored without validation; maximum uses long to match curl's argument.
     */
    class Redirect {
    public:
        long              maximum{ 50L };                        ///< Maximum redirects to follow; zero refuses redirects and -1 means unlimited.
        bool              follow{ true };                        ///< Whether to follow 3xx redirect responses.
        bool              cont_send_cred{ false };               ///< Whether to continue sending authentication credentials when the hostname changes.
        PostRedirectFlags post_flags{ PostRedirectFlags::NONE }; ///< Which redirect statuses preserve POST.

        /**
         * @brief Follow up to 50 redirects with default credential and POST handling.
         */
        Redirect() = default;

        /**
         * @brief Construct a complete set of redirect options.
         * @param maximum_param Maximum redirects to follow, including zero and -1.
         * @param follow_param Whether to follow redirects.
         * @param cont_send_cred_param Whether to forward authentication credentials across hostnames.
         * @param post_flags_param Redirect statuses for which to preserve POST.
         */
        Redirect(
            long              maximum_param,
            bool              follow_param,
            bool              cont_send_cred_param,
            PostRedirectFlags post_flags_param
        ) : maximum{ maximum_param },
            follow{ follow_param },
            cont_send_cred{ cont_send_cred_param },
            post_flags{ post_flags_param } {}

        /**
         * @brief Set the redirect limit while keeping the remaining defaults.
         * @param maximum_param Maximum redirects to follow, including zero and -1.
         */
        explicit Redirect(long maximum_param) : maximum{ maximum_param } {}

        /**
         * @brief Set whether redirects are followed while keeping the remaining defaults.
         * @param follow_param Whether to follow redirects.
         */
        explicit Redirect(bool follow_param) : follow{ follow_param } {}

        /**
         * @brief Set redirect following and credential forwarding with default limit and POST handling.
         * @param follow_param Whether to follow redirects.
         * @param cont_send_cred_param Whether to forward authentication credentials across hostnames.
         */
        Redirect(bool follow_param, bool cont_send_cred_param)
            : follow{ follow_param }, cont_send_cred{ cont_send_cred_param } {}

        /**
         * @brief Set POST preservation flags while keeping the remaining defaults.
         * @param post_flags_param Redirect statuses for which to preserve POST.
         */
        explicit Redirect(PostRedirectFlags post_flags_param) : post_flags{ post_flags_param } {}
    };

    /**
     * @brief Store a transfer range, following cpr's optional endpoint defaults.
     * @note Negative endpoints are retained in storage and omitted from the formatted text.
     * Endpoint order and protocol validity are not checked.
     */
    class Range {
    public:
        /**
         * @brief Construct a range with optional start and finish positions.
         * @param resume_from_param Starting position, defaulting to zero when absent.
         * @param finish_at_param Ending position, defaulting to -1 when absent.
         */
        explicit Range(std::optional<std::int64_t> resume_from_param = std::nullopt, std::optional<std::int64_t> finish_at_param = std::nullopt)
            : resume_from{ resume_from_param.value_or(0) }, finish_at{ finish_at_param.value_or(-1) } {}

        std::int64_t resume_from; ///< Publicly mutable starting position; any negative value omits the start.
        std::int64_t finish_at;   ///< Publicly mutable ending position; any negative value omits the finish.

        /**
         * @brief Format the current endpoints as from-to, omitting negative endpoint numbers.
         * @return Owned text such as "0-", "2-3", "-500", or "-", without a bytes= prefix.
         */
        [[nodiscard]] auto Str() const -> std::string {
            std::string result;
            if (resume_from >= 0) {
                result = std::to_string(resume_from);
            }
            result += '-';
            if (finish_at >= 0) {
                result += std::to_string(finish_at);
            }
            return result;
        }
    };

    /**
     * @brief Own an ordered sequence of ranges, following cpr's MultiRange interface.
     * @note Ranges are copied without sorting, merging, deduplication, or validation.
     */
    class MultiRange {
    private:
        std::vector<Range> m_ranges; ///< Owned range values in the supplied order.

    public:
        /**
         * @brief Copy a list of ranges, including an empty list.
         * @param ranges Range values to retain in order.
         */
        MultiRange(std::initializer_list<Range> ranges) : m_ranges{ ranges } {}

        /**
         * @brief Join the stored range strings with a comma and a space.
         * @return Owned text with no trailing separator, or an empty string for an empty list.
         */
        [[nodiscard]] auto Str() const -> std::string {
            return m_ranges | std::views::transform(&Range::Str) | std::views::join_with(std::string_view{ ", " }) | std::ranges::to<std::string>();
        }
    };

} // namespace mcr::options
