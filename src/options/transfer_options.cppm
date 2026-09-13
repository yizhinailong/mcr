/**
 * @file transfer_options.cppm
 * @brief Common request options for timing, rates, connection selection, and diagnostics.
 */
export module mcr.transfer_options;

import std;

export namespace mcr::options {

    /**
     * @brief A request timeout stored in milliseconds, following cpr's Timeout interface.
     * @note Zero disables the overall request timeout when supplied to curl.
     * Negative values are preserved; this type does not validate curl option semantics.
     */
    class Timeout {
    public:
        /**
         * @brief Convert a chrono duration to whole milliseconds, truncating toward zero.
         * @tparam Rep Source duration's representation type.
         * @tparam Period Source duration's tick period.
         * @param duration Duration to convert with std::chrono::duration_cast.
         * @pre The duration and conversion arithmetic must be representable by duration_cast.
         * Floating-point durations must be finite and convert within the milliseconds representation.
         */
        template <typename Rep, typename Period>
        Timeout(std::chrono::duration<Rep, Period> const& duration)
            : ms{ std::chrono::duration_cast<std::chrono::milliseconds>(duration) } {}

        /**
         * @brief Construct a timeout from an integer millisecond count.
         * @param milliseconds Milliseconds to store, including zero or negative values.
         */
        Timeout(std::int32_t milliseconds) : Timeout{ std::chrono::milliseconds{ milliseconds } } {}

        /**
         * @brief Convert the stored duration to the long argument required by curl.
         * @return The stored millisecond count without narrowing loss.
         * @throws std::overflow_error If the count exceeds the maximum long value.
         * @throws std::underflow_error If the count is below the minimum long value.
         */
        [[nodiscard]] auto Milliseconds() const -> long {
            auto const count{ ms.count() };
            if (std::cmp_greater(count, (std::numeric_limits<long>::max)())) {
                throw std::overflow_error{ std::format("mcr::options::Timeout: timeout value overflow: {} ms.", count) };
            }
            if (std::cmp_less(count, (std::numeric_limits<long>::min)())) {
                throw std::underflow_error{ std::format("mcr::options::Timeout: timeout value underflow: {} ms.", count) };
            }
            return static_cast<long>(count);
        }

        std::chrono::milliseconds ms; ///< Stored duration, publicly mutable as in cpr.
    };

    /**
     * @brief Distinguish connection timeouts from overall request timeouts, following cpr.
     * @note Inherits public ms storage and checked Milliseconds() conversion from Timeout.
     * When applied to CURLOPT_CONNECTTIMEOUT_MS, zero selects curl's default connection timeout.
     */
    class ConnectTimeout : public Timeout {
    public:
        /**
         * @brief Construct a connection timeout from a whole-millisecond duration.
         * @param duration Millisecond count to preserve, including zero and negative values.
         */
        ConnectTimeout(std::chrono::milliseconds const& duration) : Timeout{ duration } {}

        /**
         * @brief Implicitly construct a connection timeout from an integer millisecond count.
         * @param milliseconds Milliseconds to forward to the base timeout.
         */
        ConnectTimeout(std::int32_t milliseconds) : Timeout{ milliseconds } {}
    };

    /**
     * @brief Store the minimum transfer rate and duration used to detect a slow connection.
     * @note Values are stored verbatim, including zero and negative values, without validation.
     * The option follows cpr's chrono interface and omits its deprecated integer-time constructor.
     */
    class LowSpeed {
    public:
        /**
         * @brief Construct a low-speed option with an explicit duration in seconds.
         * @param limit_param Minimum transfer rate in bytes per second.
         * @param time_param Duration during which the transfer rate may remain below the limit.
         */
        LowSpeed(std::int32_t limit_param, std::chrono::seconds time_param)
            : limit{ limit_param }, time{ time_param } {}

        std::int32_t         limit; ///< Publicly mutable minimum transfer rate in bytes per second.
        std::chrono::seconds time;  ///< Publicly mutable observation duration in whole seconds.
    };

    /**
     * @brief Store independent download and upload limits, following cpr's LimitRate option.
     * @note Zero represents an unlimited rate when passed to curl. Negative values are
     * preserved without validation; constructing this option does not throttle a transfer.
     */
    class LimitRate {
    public:
        /**
         * @brief Construct an option with download and upload rate limits.
         * @param downrate_param Download limit in bytes per second, stored without modification.
         * @param uprate_param Upload limit in bytes per second, stored without modification.
         */
        LimitRate(std::int64_t downrate_param, std::int64_t uprate_param)
            : downrate{ downrate_param }, uprate{ uprate_param } {}

        std::int64_t downrate{ 0 }; ///< Publicly mutable download rate limit in bytes per second.
        std::int64_t uprate{ 0 };   ///< Publicly mutable upload rate limit in bytes per second.
    };

    /**
     * @brief Store a local source port, following cpr's LocalPort option.
     * @note Construction stores the supplied uint16_t value without validation or socket binding.
     */
    class LocalPort {
    private:
        std::uint16_t m_local_port; ///< Stored local port, including zero.

    public:
        /**
         * @brief Implicitly construct an option from a local port number.
         * @param local_port Port value to preserve without modification.
         */
        LocalPort(std::uint16_t local_port) : m_local_port{ local_port } {}

        /**
         * @brief Implicitly retrieve the stored port number.
         * @return The original uint16_t value.
         */
        [[nodiscard]] operator std::uint16_t() const {
            return m_local_port;
        }
    };

    /**
     * @brief Store a local port range value, following cpr's LocalPortRange option.
     * @note Construction stores the supplied uint16_t value without validation or port probing.
     */
    class LocalPortRange {
    private:
        std::uint16_t m_local_port_range; ///< Stored local port range value, including zero.

    public:
        /**
         * @brief Implicitly construct an option from a local port range value.
         * @param local_port_range Range value to preserve without modification.
         */
        LocalPortRange(std::uint16_t local_port_range) : m_local_port_range{ local_port_range } {}

        /**
         * @brief Implicitly retrieve the stored port range value.
         * @return The original uint16_t value.
         */
        [[nodiscard]] operator std::uint16_t() const {
            return m_local_port_range;
        }
    };

    /**
     * @brief An immutable Unix domain socket path, following cpr's UnixSocket interface.
     */
    class UnixSocket {
    private:
        std::string const m_unix_socket; ///< Owned socket path, immutable after construction.

    public:
        /**
         * @brief Take ownership of a Unix domain socket path.
         * @param unix_socket Socket path to store without validation.
         */
        UnixSocket(std::string unix_socket) : m_unix_socket{ std::move(unix_socket) } {}

        /**
         * @brief Get the stored socket path as a null-terminated string.
         * @return A pointer to the owned path, valid for this object's lifetime.
         */
        [[nodiscard]] auto GetUnixSocketString() const noexcept -> char const* {
            return m_unix_socket.data();
        }
    };

    /**
     * @brief Store a custom address mapping, following cpr's Resolve option.
     * @note Construction stores text verbatim without address validation or DNS resolution.
     */
    class Resolve {
    public:
        std::string             host;  ///< Hostname to override.
        std::string             addr;  ///< Address text associated with the hostname.
        std::set<std::uint16_t> ports; ///< Ports to which the mapping applies.

        /**
         * @brief Take ownership of a mapping, using ports 80 and 443 when none are supplied.
         * @param host_param Hostname to override, stored without normalization.
         * @param addr_param Address text to store without validation.
         * @param ports_param Target ports; an omitted or empty set selects both 80 and 443.
         * @note Default ports are inserted only during this constructor, not after public field updates.
         */
        Resolve(
            std::string             host_param,
            std::string             addr_param,
            std::set<std::uint16_t> ports_param = { 80U, 443U }
        ) : host{ std::move(host_param) }, addr{ std::move(addr_param) }, ports{ std::move(ports_param) } {
            if (ports.empty()) {
                ports = { 80U, 443U };
            }
        }
    };

    /**
     * @brief Enable or disable verbose transfer diagnostics, following cpr's Verbose interface.
     */
    class Verbose {
    public:
        /**
         * @brief Construct an option that enables verbose diagnostics.
         */
        Verbose() = default;

        /**
         * @brief Construct an option with the requested verbosity.
         * @param enabled Whether to enable verbose diagnostics.
         */
        Verbose(bool enabled) : verbose{ enabled } {}

        bool verbose{ true }; ///< Whether verbose diagnostics are enabled; defaults to true.
    };

    /**
     * @brief Store a response capacity hint, following cpr's ReserveSize interface.
     * @note Construction only stores the size; it does not allocate memory or validate capacity.
     */
    class ReserveSize {
    public:
        /**
         * @brief Construct an option with the requested response capacity.
         * @param size_param Number of bytes to reserve, including zero.
         */
        ReserveSize(std::size_t size_param) : size{ size_param } {}

        std::size_t size{ 0 }; ///< Requested response string capacity in bytes.
    };

} // namespace mcr::options
