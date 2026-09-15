/**
 * @file curlmultiholder.cppm
 * @brief Exclusive ownership of a curl multi handle.
 */
module;

#include <curl/curl.h>

export module mcr.curlmultiholder;

export import mcr.error;
import std;

export namespace mcr::curl {

    /**
     * @brief Own a multi handle without owning the easy handles added to it.
     * @note Remove all attached easy handles before destroying or replacing the owned handle.
     * Callback data must remain valid until cleanup finishes. Do not destroy or replace the
     * handle from one of its callbacks. Curl's global initialization and cleanup belong to the caller.
     * The public pointer retains cpr's ownership model; callers must clean up a replaced handle.
     */
    class CurlMultiHolder {
    public:
        CURLM* handle{ nullptr }; ///< Owned multi handle, null after moving out.

        /**
         * @brief Initialize a multi handle.
         * @return Owned handle, or FAILED_INIT if curl_multi_init() fails.
         */
        [[nodiscard]] static auto Create() -> Result<CurlMultiHolder> {
            CurlMultiHolder result;
            result.handle = curl_multi_init();
            if (!result.handle) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::curl::CurlMultiHolder: curl_multi_init failed." }
                };
            }
            return result;
        }

        CurlMultiHolder(CurlMultiHolder const&)                    = delete;
        auto operator=(CurlMultiHolder const&) -> CurlMultiHolder& = delete;

        /**
         * @brief Transfer ownership while preserving options and attached easy handles.
         * @param other Holder whose handle is set to null.
         */
        CurlMultiHolder(CurlMultiHolder&& other) noexcept : handle{ std::exchange(other.handle, nullptr) } {}

        /**
         * @brief Release the owned multi handle; attached easy handles must already be removed.
         */
        ~CurlMultiHolder() {
            releaseHandle();
        }

        /**
         * @brief Release the current multi handle and take ownership of another holder's handle.
         * @param other Source holder; self-move leaves the holder unchanged.
         * @return This holder after the transfer.
         * @note Remove easy handles from the destination's current handle before assigning.
         */
        auto operator=(CurlMultiHolder&& other) noexcept -> CurlMultiHolder& {
            if (this != &other) {
                releaseHandle();
                handle = std::exchange(other.handle, nullptr);
            }
            return *this;
        }

    private:
        /**
         * @brief Create an empty holder for factory initialization.
         */
        CurlMultiHolder() noexcept = default;

        /**
         * @brief Release an owned multi handle and clear its pointer.
         */
        auto releaseHandle() noexcept -> void {
            if (handle) {
                (void)curl_multi_cleanup(std::exchange(handle, nullptr));
            }
        }
    };

} // namespace mcr::curl
