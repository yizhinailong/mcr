/**
 * @file sse.cppm
 * @brief Incremental Server-Sent Event parsing and callback adaptation.
 */
export module mcr.sse;

import std;

export namespace mcr {

    /**
     * @brief An owned Server-Sent Event with cpr-compatible fields and defaults.
     */
    struct ServerSentEvent {
        std::optional<std::string> id;                 ///< ID supplied in this event, including an explicitly empty ID.
        std::string                event{ "message" }; ///< Event type; an absent or empty type is dispatched as "message".
        std::string                data;               ///< Data fields joined with newlines, preserving empty fields.
        std::optional<std::size_t> retry;              ///< Valid retry interval in milliseconds supplied in this event.

        /**
         * @brief Construct an empty message with no ID or retry interval.
         */
        ServerSentEvent() = default;
    };

    /**
     * @brief Parse SSE fields incrementally and dispatch complete events synchronously.
     * @note Parse() and Reset() follow the project's public method naming convention.
     * ID and retry metadata are local to each event, as in cpr; reconnection state belongs to the caller.
     * Unlike the reference implementation, this parser handles CR line endings and an initial UTF-8 BOM,
     * preserves empty data fields, defaults empty event types to "message", and rejects partial retry numbers.
     * UTF-8 payload bytes are preserved without decoding or validating them.
     * @see https://html.spec.whatwg.org/multipage/server-sent-events.html#parsing-an-event-stream
     */
    class ServerSentEventParser {
    private:
        std::string     m_buffer;             ///< Incoming bytes, including a consumed prefix until the next Parse().
        std::size_t     m_line_start{ 0 };    ///< Start of the next unprocessed line.
        std::size_t     m_scan_position{ 0 }; ///< Next byte to search, avoiding rescanning partial lines.
        bool            m_skip_lf{ false };   ///< Whether a preceding CR may be followed by a matching LF.
        bool            m_at_start{ true };   ///< Whether an initial UTF-8 BOM may still arrive.
        ServerSentEvent m_current_event;      ///< Fields accumulated since the last blank line.

    public:
        /**
         * @brief Construct a parser ready for a new stream.
         */
        ServerSentEventParser() = default;

        /**
         * @brief Append a chunk and deliver each event terminated by a blank line.
         * @param data Incoming bytes, which may split a field, CRLF pair, or UTF-8 sequence.
         * An empty chunk can resume buffered input but does not signal end-of-stream.
         * @param callback Consumer invoked with an owned event; false stops parsing immediately.
         * @return True to continue receiving data, or false when the consumer requests an abort.
         * @throws std::bad_function_call If callback is empty when an event is dispatched.
         * @note Callback exceptions propagate. A delivered event is consumed even when its callback
         * returns false or throws; remaining bytes are retained for the next Parse().
         * @pre Calls on one parser must be serialized, and callbacks must not reenter or reset that parser.
         */
        [[nodiscard]] auto Parse(std::string_view data, std::function<bool(ServerSentEvent&&)> const& callback) -> bool {
            m_buffer.erase(0, m_line_start);
            m_scan_position -= m_line_start;
            m_line_start     = 0;
            m_buffer.append(data);

            if (m_at_start) {
                constexpr std::string_view BOM{ "\xEF\xBB\xBF" };
                if (m_buffer.size() < BOM.size() && BOM.starts_with(m_buffer)) {
                    return true;
                }
                if (m_buffer.starts_with(BOM)) {
                    m_line_start    = BOM.size();
                    m_scan_position = m_line_start;
                }
                m_at_start = false;
            }

            while (m_line_start < m_buffer.size()) {
                if (m_skip_lf) {
                    m_skip_lf = false;
                    if (m_buffer[m_line_start] == '\n') {
                        ++m_line_start;
                        m_scan_position = m_line_start;
                        continue;
                    }
                }

                auto const line_end{ m_buffer.find_first_of("\r\n", m_scan_position) };
                if (line_end == std::string::npos) {
                    m_scan_position = m_buffer.size();
                    break;
                }
                std::string_view const line{ m_buffer.data() + m_line_start, line_end - m_line_start };
                m_skip_lf       = m_buffer[line_end] == '\r';
                m_line_start    = line_end + 1;
                m_scan_position = m_line_start;
                if (!processLine(line, callback)) {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief Discard buffered bytes and event fields and prepare for a new stream.
         * @note Incomplete events are discarded without invoking a callback.
         */
        auto Reset() -> void {
            m_buffer.clear();
            m_line_start    = 0;
            m_scan_position = 0;
            m_skip_lf       = false;
            m_at_start      = true;
            m_current_event = ServerSentEvent{};
        }

    private:
        /**
         * @brief Interpret one complete line without its line terminator.
         * @param line Borrowed text of the line.
         * @param callback Consumer for an event completed by this line.
         * @return Whether parsing should continue.
         */
        auto processLine(std::string_view line, std::function<bool(ServerSentEvent&&)> const& callback) -> bool {
            if (line.empty()) {
                return dispatchEvent(callback);
            }
            if (line.front() == ':') {
                return true;
            }

            auto const       colon{ line.find(':') };
            auto const       field{ line.substr(0, colon) };
            std::string_view value;
            if (colon != std::string_view::npos) {
                value = line.substr(colon + 1);
                if (value.starts_with(' ')) {
                    value.remove_prefix(1);
                }
            }

            if (field == "event") {
                m_current_event.event = value;
            } else if (field == "data") {
                m_current_event.data += value;
                m_current_event.data += '\n';
            } else if (field == "id") {
                if (value.find('\0') == std::string_view::npos) {
                    m_current_event.id = std::string{ value };
                }
            } else if (field == "retry" && !value.empty()) {
                std::size_t retry_value{ 0 };
                auto const* end{ value.data() + value.size() };
                auto const [ptr, error]{ std::from_chars(value.data(), end, retry_value) };
                if (error == std::errc{} && ptr == end) {
                    m_current_event.retry = retry_value;
                }
            }
            return true;
        }

        /**
         * @brief Consume the current event and deliver it if it contains any data fields.
         * @param callback Consumer invoked after parser event state has been reset.
         * @return True for a block without data, otherwise the consumer's result.
         */
        auto dispatchEvent(std::function<bool(ServerSentEvent&&)> const& callback) -> bool {
            auto event{ std::exchange(m_current_event, ServerSentEvent{}) };
            if (event.data.empty()) {
                return true;
            }
            event.data.pop_back();
            if (event.event.empty()) {
                event.event = "message";
            }
            return callback(std::move(event));
        }
    };

    /**
     * @brief Adapt raw SSE chunks to an event callback with cpr-compatible user data.
     */
    class ServerSentEventCallback {
    private:
        ServerSentEventParser m_parser; ///< Parser retaining incomplete events between raw chunks.

    public:
        /**
         * @brief Construct a callback that accepts and discards events.
         */
        ServerSentEventCallback() = default;

        /**
         * @brief Store an event callback and its user data.
         * @param callback_param Consumer returning false to abort the transfer.
         * @param userdata_param Opaque value passed unchanged to the consumer.
         */
        ServerSentEventCallback(std::function<bool(ServerSentEvent&&, std::intptr_t)> callback_param, std::intptr_t userdata_param = 0)
            : userdata{ userdata_param }, callback{ std::move(callback_param) } {}

        /**
         * @brief Forward an event and the current user data to the stored callback.
         * @param event Event to deliver; consumers may move its contents into their own storage.
         * @return The callback result, or true when no callback is installed.
         * @note Callback exceptions propagate to the caller.
         */
        [[nodiscard]] auto operator()(ServerSentEvent&& event) const -> bool {
            if (!callback) {
                return true;
            }
            return callback(std::move(event), userdata);
        }

        /**
         * @brief Parse a raw transfer chunk and invoke the stored callback for complete events.
         * @param data Incoming bytes, which may contain incomplete or multiple events.
         * @return True to continue receiving data, or false when the callback requests an abort.
         * @note Callback exceptions propagate; buffered input follows ServerSentEventParser::Parse().
         */
        [[nodiscard]] auto HandleData(std::string_view data) -> bool {
            return m_parser.Parse(data, [this](ServerSentEvent&& event) { return (*this)(std::move(event)); });
        }

        std::intptr_t                                         userdata{}; ///< Publicly mutable opaque value passed to the callback.
        std::function<bool(ServerSentEvent&&, std::intptr_t)> callback;   ///< Consumer; an empty function accepts events.
    };

} // namespace mcr
