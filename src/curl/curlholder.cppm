/**
 * @file curlholder.cppm
 * @brief Exclusive ownership of curl transfer resources and URL conversion helpers.
 */
module;

#include <curl/curl.h>

export module mcr.curlholder;

export import mcr.secure_string;

export import mcr.error;
import std;

export namespace mcr::curl {

    /**
     * @brief Own an easy handle, header and resolve lists, MIME data, and an error buffer.
     * @note Public pointers retain cpr's ownership model; callers must release replaced resources
     * and keep externally configured callback data alive until handle cleanup finishes.
     * Moves transfer ownership and bind the handle to the destination's error buffer.
     * Public URL helpers use UrlEncode()/UrlDecode(), and resolveCurlList becomes resolve_curl_list.
     * Curl's process-wide initialization and cleanup are not owned by individual holders.
     */
    struct CurlHolder {
    public:
        CURL*                             handle{ nullptr };            ///< Owned easy handle, null after moving out.
        curl_slist*                       chunk{ nullptr };             ///< Owned request header list.
        curl_slist*                       resolve_curl_list{ nullptr }; ///< Owned hostname resolution override list.
        curl_mime*                        multipart{ nullptr };         ///< Owned multipart MIME data.
        std::array<char, CURL_ERROR_SIZE> error{};                      ///< Error storage registered with the active handle.

        /**
         * @brief Initialize an easy handle and register this holder's error buffer.
         * @note Initialization is serialized among CurlHolder instances, following cpr.
         * Initialization failure is returned as FAILED_INIT.
         * @return Success or the first operation error.
         */
        [[nodiscard]] static auto Create() -> Result<CurlHolder> {
            CurlHolder result;
            {
                std::lock_guard lock{ curlEasyInitMutex() };
                result.handle = curl_easy_init();
            }
            if (!result.handle) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::curl::CurlHolder: curl_easy_init failed." }
                };
            }
            result.bindErrorBuffer();
            return result;
        }

        CurlHolder(CurlHolder const&)                    = delete;
        auto operator=(CurlHolder const&) -> CurlHolder& = delete;

        /**
         * @brief Transfer all resources and copy the existing error text.
         * @param other Holder whose resource pointers are set to null.
         */
        CurlHolder(CurlHolder&& other) noexcept
            : handle{ std::exchange(other.handle, nullptr) },
              chunk{ std::exchange(other.chunk, nullptr) },
              resolve_curl_list{ std::exchange(other.resolve_curl_list, nullptr) },
              multipart{ std::exchange(other.multipart, nullptr) },
              error{ other.error } {
            bindErrorBuffer();
        }

        /**
         * @brief Release owned lists, MIME data, and the easy handle exactly once.
         */
        ~CurlHolder() {
            releaseResources();
        }

        /**
         * @brief Release current resources and take ownership of another holder's resources.
         * @param other Source holder; self-move leaves the holder unchanged.
         * @return This holder after the transfer.
         */
        auto operator=(CurlHolder&& other) noexcept -> CurlHolder& {
            if (this != &other) {
                releaseResources();
                handle            = std::exchange(other.handle, nullptr);
                chunk             = std::exchange(other.chunk, nullptr);
                resolve_curl_list = std::exchange(other.resolve_curl_list, nullptr);
                multipart         = std::exchange(other.multipart, nullptr);
                error             = other.error;
                bindErrorBuffer();
            }
            return *this;
        }

        /**
         * @brief Percent-encode a URL component using curl's byte-oriented escaping rules.
         * @param input Bytes to encode, including embedded nulls; need not be null-terminated.
         * @return Encoded bytes in a SecureString, or an error for invalid state, excessive length, or curl allocation failure.
         * @note Empty views are handled directly so curl cannot fall back to strlen().
         * Curl's temporary allocation is freed even if constructing the result throws.
         */
        [[nodiscard]] auto UrlEncode(std::string_view input) const -> Result<utils::SecureString> {
            auto const length{ checkedLength(input) };
            if (!length) {
                return std::unexpected{ length.error() };
            }
            if (input.empty()) {
                return {};
            }
            std::unique_ptr<char, decltype(&curl_free)> output{ curl_easy_escape(handle, input.data(), *length), &curl_free };
            if (!output) {
                return std::unexpected{
                    Error{ ErrorCode::OUT_OF_MEMORY, "mcr::curl::CurlHolder: URL conversion failed." }
                };
            }
            return utils::SecureString{ output.get() };
        }

        /**
         * @brief Decode percent escapes without translating plus signs into spaces.
         * @param input Bytes to decode; need not be null-terminated.
         * @return Decoded bytes in a SecureString, or an error for invalid state, excessive length, or curl allocation failure.
         * @note Uses curl's output length to retain embedded nulls, unlike cpr's null-terminated copy.
         * Curl's temporary allocation is freed even if constructing the result throws.
         */
        [[nodiscard]] auto UrlDecode(std::string_view input) const -> Result<utils::SecureString> {
            auto const length{ checkedLength(input) };
            if (!length) {
                return std::unexpected{ length.error() };
            }
            if (input.empty()) {
                return {};
            }
            int                                         output_length{ 0 };
            std::unique_ptr<char, decltype(&curl_free)> output{ curl_easy_unescape(handle, input.data(), *length, &output_length), &curl_free };
            if (!output) {
                return std::unexpected{
                    Error{ ErrorCode::OUT_OF_MEMORY, "mcr::curl::CurlHolder: URL conversion failed." }
                };
            }
            return utils::SecureString{ output.get(), static_cast<std::size_t>(output_length) };
        }

    private:
        /**
         * @brief Create an empty holder for factory initialization.
         */
        CurlHolder() noexcept = default;

        /**
         * @brief Obtain the mutex used to serialize easy-handle initialization.
         * @return A function-local mutex that avoids static initialization order dependencies.
         */
        static auto curlEasyInitMutex() -> std::mutex& {
            static std::mutex s_mutex;
            return s_mutex;
        }

        /**
         * @brief Bind an active handle to this holder's error storage after construction or moving.
         */
        auto bindErrorBuffer() noexcept -> void {
            if (handle) {
                // This supported option only stores a pointer and does not allocate.
                (void)curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error.data());
            }
        }

        /**
         * @brief Release each owned resource and clear its pointer, following cpr's cleanup order.
         */
        auto releaseResources() noexcept -> void {
            curl_slist_free_all(std::exchange(chunk, nullptr));
            curl_slist_free_all(std::exchange(resolve_curl_list, nullptr));
            curl_mime_free(std::exchange(multipart, nullptr));
            curl_easy_cleanup(std::exchange(handle, nullptr));
        }

        /**
         * @brief Validate URL conversion state and convert the input length without narrowing loss.
         * @param input Bytes supplied to a URL conversion helper.
         * @return The byte count as curl's int argument.
         */
        auto checkedLength(std::string_view input) const -> Result<int> {
            if (!handle) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::curl::CurlHolder: URL conversion requires an active handle." }
                };
            }
            if (std::cmp_greater(input.size(), (std::numeric_limits<int>::max)())) {
                return std::unexpected{
                    Error{ ErrorCode::TOO_LARGE, "mcr::curl::CurlHolder: URL input exceeds curl's int length limit." }
                };
            }
            return static_cast<int>(input.size());
        }
    };

} // namespace mcr::curl
