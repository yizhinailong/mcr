/**
 * @file util.cppm
 * @brief HTTP metadata parsing, curl callback adapters, and URL conversion helpers.
 */
module;

#include <curl/curl.h>

export module mcr.util;

export import mcr.callback;
export import mcr.cookies;
export import mcr.curlholder;
export import mcr.secure_string;
export import mcr.sse;
export import mcr.types;

export import mcr.error;
import std;

namespace mcr::utils::detail {

    /**
     * @brief Remove trailing HTTP whitespace.
     * @param text Borrowed header text.
     * @return The remaining borrowed text.
     */
    auto trim_header_end(std::string_view text) noexcept -> std::string_view {
        auto const end{ text.find_last_not_of("\t\n\r ") };
        return end == std::string_view::npos ? std::string_view{} : text.substr(0, end + 1);
    }

} // namespace mcr::utils::detail

/**
 * @brief Utilities following cpr::util, with snake_case free-function names.
 * @note Callback adapters borrow their data and callback pointers, which must remain valid
 * throughout invocation. As in cpr, exceptions propagate on direct calls; callbacks installed
 * in curl must not throw across the C library boundary. Buffer sizes must fit std::size_t.
 */
export namespace mcr::utils {

    /**
     * @brief Parse the last response header block, replacing duplicate names without regard to case.
     * @param headers Raw header bytes, accepting LF and CRLF line endings.
     * @param status_line Optional output receiving the last status line without trailing whitespace.
     * @param reason Optional output receiving the last reason phrase, or empty when absent.
     * @return Owned header names and trimmed values from the last response block, including trailers.
     * @note Outputs remain unchanged when no status line occurs. Status lines are never header fields,
     * and a response without a reason clears a previous reason, correcting cpr's stale output.
     */
    [[nodiscard]] auto parse_header(std::string_view headers, std::string* status_line = nullptr, std::string* reason = nullptr) -> Header {
        Header      result;
        std::string parsed_status;
        std::string parsed_reason;
        bool        found_status{ false };
        while (!headers.empty()) {
            auto const end{ headers.find('\n') };
            auto       line{ headers.substr(0, end) };
            headers.remove_prefix(end == std::string_view::npos ? headers.size() : end + 1);
            if (line.starts_with("HTTP/")) {
                found_status = true;
                line         = detail::trim_header_end(line);
                if (status_line) {
                    parsed_status = line;
                }
                if (reason) {
                    parsed_reason.clear();
                    auto const version_end{ line.find_first_of("\t ") };
                    if (version_end != std::string_view::npos) {
                        auto const code_begin{ line.find_first_not_of("\t ", version_end) };
                        auto const code_end{ line.find_first_of("\t ", code_begin) };
                        if (code_end != std::string_view::npos) {
                            parsed_reason = line.substr(line.find_first_not_of("\t ", code_end));
                        }
                    }
                }
                result.clear();
                continue;
            }
            auto const colon{ line.find(':') };
            if (colon == std::string_view::npos) {
                continue;
            }
            auto       value{ line.substr(colon + 1) };
            auto const first{ value.find_first_not_of("\t ") };
            value.remove_prefix(first == std::string_view::npos ? value.size() : first);
            result[std::string{ line.substr(0, colon) }] = detail::trim_header_end(value);
        }
        // Delay output writes so either output may reuse the string backing the input view.
        if (found_status) {
            if (status_line) {
                *status_line = std::move(parsed_status);
            }
            if (reason) {
                *reason = std::move(parsed_reason);
            }
        }
        return result;
    }

    /**
     * @brief Split bytes with cpr's std::getline semantics.
     * @param input Bytes to split, including embedded nulls.
     * @param delimiter Byte separating fields.
     * @return Owned fields; leading and interior empty fields are retained, a trailing one is omitted,
     * and empty input produces no fields.
     */
    [[nodiscard]] auto split(std::string_view input, char delimiter) -> std::vector<std::string> {
        std::vector<std::string> result;
        while (!input.empty()) {
            auto const end{ input.find(delimiter) };
            result.emplace_back(input.substr(0, end));
            input.remove_prefix(end == std::string_view::npos ? input.size() : end + 1);
        }
        return result;
    }

    /**
     * @brief Recognize the ASCII word true without regard to case or locale.
     * @param input Bytes to compare without trimming whitespace.
     * @return True only for a four-byte case variation of "true".
     */
    [[nodiscard]] auto is_true(std::string_view input) noexcept -> bool {
        constexpr std::string_view TRUE_TEXT{ "true" };
        return std::ranges::equal(input, TRUE_TEXT, [](unsigned char actual, unsigned char expected) {
            return (actual >= 'A' && actual <= 'Z' ? actual + ('a' - 'A') : actual) == expected;
        });
    }

    /**
     * @brief Parse a decimal Unix timestamp in seconds into the platform's time_t.
     * @param timestamp Text accepting leading whitespace, a sign, and a numeric prefix, as in cpr.
     * @return The parsed value without narrowing overflow.
     * @note Cookie expiration uses seconds, despite the reference header's "unix ms" comment.
     */
    [[nodiscard]] auto s_timestamp_to_t(std::string_view timestamp) -> Result<std::time_t> {
        while (!timestamp.empty() && std::isspace(static_cast<unsigned char>(timestamp.front()))) {
            timestamp.remove_prefix(1);
        }
        bool const negative{ !timestamp.empty() && timestamp.front() == '-' };
        if (!timestamp.empty() && (timestamp.front() == '+' || negative)) {
            timestamp.remove_prefix(1);
        }
        if (timestamp.empty()) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::utils::s_timestamp_to_t: missing timestamp." }
            };
        }
        std::uintmax_t magnitude{};
        auto const     parsed = std::from_chars(timestamp.data(), timestamp.data() + timestamp.size(), magnitude);
        if (parsed.ec != std::errc{}) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::utils::s_timestamp_to_t: invalid or overflowing timestamp." }
            };
        }
        auto const maximum = static_cast<std::uintmax_t>((std::numeric_limits<std::time_t>::max)());
        if constexpr (std::is_signed_v<std::time_t>) {
            if (magnitude > maximum + (negative ? 1U : 0U)) {
                return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::utils::s_timestamp_to_t: timestamp exceeds time_t range." }
                };
            }
            if (negative && magnitude == maximum + 1U) {
                return (std::numeric_limits<std::time_t>::min)();
            }
        } else if (magnitude > maximum) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::utils::s_timestamp_to_t: timestamp exceeds time_t range." }
            };
        }
        auto const value = static_cast<std::time_t>(magnitude);
        return negative ? static_cast<std::time_t>(-value) : value;
    }

    /**
     * @brief Copy curl's tab-separated Netscape cookie records into an ordered collection.
     * @param raw_cookies Borrowed list, or null for an empty collection; each node contains text.
     * @return Owned cookies retaining order, duplicate names, domain text, and encoding enabled.
     * @note Missing fields are padded with empty strings and extra fields are ignored, as in cpr.
     * The caller retains list ownership. Expirations must also fit system_clock::time_point.
     */
    [[nodiscard]] auto parse_cookies(curl_slist const* raw_cookies) -> Result<Cookies> {
        constexpr std::size_t COOKIE_FIELD_COUNT{ 7 };
        Cookies               result;
        for (auto const* node{ raw_cookies }; node; node = node->next) {
            auto fields{ split(node->data, '\t') };
            fields.resize(COOKIE_FIELD_COUNT);
            auto timestamp = s_timestamp_to_t(fields[4]);
            if (!timestamp) {
                return std::unexpected{ std::move(timestamp.error()) };
            }
            auto const expires{ std::chrono::system_clock::from_time_t(*timestamp) };
            result.emplace_back(
                Cookie{
                    std::move(fields[5]),
                    std::move(fields[6]),
                    std::move(fields[0]),
                    is_true(fields[1]),
                    std::move(fields[2]),
                    is_true(fields[3]),
                    expires,
                }
            );
        }
        return result;
    }

    /**
     * @brief Fill a curl upload buffer through a ReadCallback.
     * @param ptr Writable buffer of at least size * nitems bytes.
     * @param size Element size supplied by curl.
     * @param nitems Available element count.
     * @param read Borrowed producer, which may reduce the byte count to indicate a short read or EOF.
     * @return The producer's updated byte count on success, or CURL_READFUNC_ABORT on cancellation.
     */
    auto read_user_function(char* ptr, std::size_t size, std::size_t nitems, ReadCallback const* read) -> std::size_t {
        auto count{ size * nitems };
        return (*read)(ptr, count) ? count : CURL_READFUNC_ABORT;
    }

    /**
     * @brief Forward raw response headers without allocating a copy.
     * @param ptr Borrowed header bytes.
     * @param size Element size supplied by curl.
     * @param nmemb Element count.
     * @param header Borrowed header consumer.
     * @return size * nmemb if accepted, or zero if cancelled.
     */
    auto header_user_function(char* ptr, std::size_t size, std::size_t nmemb, HeaderCallback const* header) -> std::size_t {
        auto const count{ size * nmemb };
        return (*header)({ ptr, count }) ? count : 0;
    }

    /**
     * @brief Append a binary response chunk to a string.
     * @param ptr Borrowed response bytes.
     * @param size Element size supplied by curl.
     * @param nmemb Element count.
     * @param data Borrowed nonnull pointer to the destination std::string.
     * @return The number of bytes appended.
     */
    auto write_function(char* ptr, std::size_t size, std::size_t nmemb, void* data) -> std::size_t {
        auto const count{ size * nmemb };
        static_cast<std::string*>(data)->append(ptr, count);
        return count;
    }

    /**
     * @brief Write a response chunk to an output file.
     * @param ptr Borrowed response bytes.
     * @param size Element size supplied by curl.
     * @param nmemb Element count; size * nmemb must fit std::streamsize.
     * @param file Borrowed output stream; open it in binary mode to preserve bytes.
     * @return The chunk size on success, or zero when the stream reports failure.
     * @throws std::ios_base::failure If the stream is configured to throw on a write failure.
     * @note Unlike cpr, stream failure is not reported as successful consumption. Buffered errors
     * discovered only on flush or close still need to be checked by the caller.
     */
    auto write_file_function(char* ptr, std::size_t size, std::size_t nmemb, std::ofstream* file) -> std::size_t {
        auto const count{ size * nmemb };
        file->write(ptr, static_cast<std::streamsize>(count));
        return *file ? count : 0;
    }

    /**
     * @brief Forward a raw response body without allocating a copy.
     * @param ptr Borrowed response bytes.
     * @param size Element size supplied by curl.
     * @param nmemb Element count.
     * @param write Borrowed body consumer.
     * @return size * nmemb if accepted, or zero if cancelled.
     */
    auto write_user_function(char* ptr, std::size_t size, std::size_t nmemb, WriteCallback const* write) -> std::size_t {
        auto const count{ size * nmemb };
        return (*write)({ ptr, count }) ? count : 0;
    }

    /**
     * @brief Feed a raw chunk into the callback's persistent SSE parser.
     * @param ptr Borrowed event-stream bytes, possibly ending in an incomplete event.
     * @param size Element size supplied by curl.
     * @param nmemb Element count.
     * @param sse Borrowed callback retaining parsing state between invocations.
     * @return size * nmemb if accepted, or zero if an event callback cancels.
     */
    auto write_sse_function(char* ptr, std::size_t size, std::size_t nmemb, ServerSentEventCallback* sse) -> std::size_t {
        auto const count{ size * nmemb };
        return sse->HandleData({ ptr, count }) ? count : 0;
    }

    /**
     * @brief Adapt a progress observer or cancellation callback to curl's return convention.
     * @tparam T Callable accepting the four curl-compatible counters and returning a boolean.
     * @param progress Borrowed observer.
     * @param download_total Total download byte count.
     * @param download_now Downloaded byte count.
     * @param upload_total Total upload byte count.
     * @param upload_now Uploaded byte count.
     * @return Zero to continue, or one to abort; never CURL_PROGRESSFUNC_CONTINUE.
     */
    template <typename T = ProgressCallback>
    auto progress_user_function(
        T const*  progress,
        CprPfArgT download_total,
        CprPfArgT download_now,
        CprPfArgT upload_total,
        CprPfArgT upload_now
    ) -> int {
        constexpr int CANCEL_RETURN{ 1 };
        static_assert(CANCEL_RETURN != CURL_PROGRESSFUNC_CONTINUE);
        return (*progress)(download_total, download_now, upload_total, upload_now) ? 0 : CANCEL_RETURN;
    }

    /**
     * @brief Forward curl diagnostic categories and borrowed bytes to a DebugCallback.
     * @param handle Curl handle, unused by this adapter.
     * @param type Diagnostic category.
     * @param data Borrowed diagnostic bytes, including embedded nulls.
     * @param size Byte count.
     * @param debug Borrowed diagnostic consumer.
     * @return Zero, as required by curl's debug callback contract.
     */
    auto debug_user_function(CURL* /*handle*/, curl_infotype type, char* data, std::size_t size, DebugCallback const* debug) -> int {
        (*debug)(static_cast<DebugCallback::InfoType>(type), { data, size });
        return 0;
    }

    /**
     * @brief Percent-encode a URL component using a temporary CurlHolder.
     * @param input Bytes to encode, including embedded nulls; need not be null-terminated.
     * @return Encoded secure storage, or an empty string for empty input or curl conversion failure.
     * @note Reuse CurlHolder::UrlEncode() for repeated conversions to avoid handle creation overhead.
     */
    [[nodiscard]] auto url_encode(std::string_view input) -> Result<SecureString> {
        auto holder = curl::CurlHolder::Create();
        if (!holder) {
            return std::unexpected{ std::move(holder.error()) };
        }
        return holder->UrlEncode(input);
    }

    /**
     * @brief Decode percent escapes using a temporary CurlHolder, leaving plus signs unchanged.
     * @param input Bytes to decode; need not be null-terminated.
     * @return Decoded secure storage retaining embedded nulls, or empty on empty input or curl failure.
     * @note Reuse CurlHolder::UrlDecode() for repeated conversions. Curl global initialization and
     * cleanup remain the caller's responsibility, as for CurlHolder.
     */
    [[nodiscard]] auto url_decode(std::string_view input) -> Result<SecureString> {
        auto holder = curl::CurlHolder::Create();
        if (!holder) {
            return std::unexpected{ std::move(holder.error()) };
        }
        return holder->UrlDecode(input);
    }

} // namespace mcr::utils
