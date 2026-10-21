/**
 * @file response.cppm
 * @brief Owned snapshots of HTTP response data and curl transfer metadata.
 */
module;

#include <curl/curl.h>

export module mcr:response;

export import :cert_info;
export import :cookies;
export import :error;
export import :types;

import :curlholder;
import :util;
import std;

export namespace mcr {

    /**
     * @brief Store a completed transfer, including HTTP failures without treating them as curl errors.
     * @note Metadata and certificates are captured at construction, so later Session requests cannot
     * change them. Unlike cpr, a Response does not retain the live easy handle.
     */
    class Response {
    private:
        std::vector<CertInfo> m_cert_infos; ///< Certificate snapshot, empty when unavailable.

    public:
        long status_code{};           ///< HTTP status, or zero when no response was received.
        std::string text;             ///< Buffered response body, including binary bytes.
        Header header;                ///< Case-insensitive headers from the final response.
        Url url;                      ///< Effective URL after redirects.
        double elapsed{};             ///< Total transfer duration in seconds.
        Cookies cookies;              ///< Snapshot of curl's cookie engine.
        Error error;                  ///< Transport error; HTTP 4xx and 5xx do not set this.
        std::string raw_header;       ///< All received header blocks, including redirects.
        std::string status_line;      ///< Final HTTP status line.
        std::string reason;           ///< Final HTTP reason phrase, if present.
        CprOffT uploaded_bytes{};     ///< Uploaded body bytes reported by curl.
        CprOffT downloaded_bytes{};   ///< Downloaded body bytes reported by curl.
        long redirect_count{};        ///< Number of redirects followed.
        std::string primary_ip;       ///< Address of the connected peer.
        std::uint16_t primary_port{}; ///< Port of the connected peer.

        /**
         * @brief Construct an empty response with a successful transport error code.
         */
        Response() = default;

        /**
         * @brief Take ownership of received data and snapshot a completed easy handle.
         * @param curl Valid holder used for the transfer; not retained by this response.
         * @param body Received body bytes.
         * @param headers Received raw header blocks.
         * @param received_cookies Cookies extracted from the handle.
         * @param transfer_error Transport outcome.
         * @return A response snapshot, or BAD_FUNCTION_ARGUMENT when the easy handle is null.
         */
        [[nodiscard]] static auto FromCurl(
            curl::CurlHolder const& curl,
            std::string body,
            std::string headers,
            Cookies received_cookies = {},
            Error transfer_error = {}
        ) -> Result<Response> {
            if (!curl.handle) {
                return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::Response requires an active curl holder." }
                };
            }
            Response result;
            result.text = std::move(body);
            result.cookies = std::move(received_cookies);
            result.error = std::move(transfer_error);
            result.raw_header = std::move(headers);
            result.header = utils::parse_header(result.raw_header, &result.status_line, &result.reason);
            auto* handle{ curl.handle };
            (void)curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &result.status_code);
            (void)curl_easy_getinfo(handle, CURLINFO_TOTAL_TIME, &result.elapsed);
            char* address{ nullptr };
            if (curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &address) == CURLE_OK && address) {
                result.url = Url{ address };
            }
            (void)curl_easy_getinfo(handle, CURLINFO_SIZE_DOWNLOAD_T, &result.downloaded_bytes);
            (void)curl_easy_getinfo(handle, CURLINFO_SIZE_UPLOAD_T, &result.uploaded_bytes);
            (void)curl_easy_getinfo(handle, CURLINFO_REDIRECT_COUNT, &result.redirect_count);
            address = nullptr;
            if (curl_easy_getinfo(handle, CURLINFO_PRIMARY_IP, &address) == CURLE_OK && address) {
                result.primary_ip = address;
            }
            long port{};
            if (curl_easy_getinfo(handle, CURLINFO_PRIMARY_PORT, &port) == CURLE_OK) {
                result.primary_port = static_cast<std::uint16_t>(port);
            }
            curl_certinfo* certificates{ nullptr };
            if (curl_easy_getinfo(handle, CURLINFO_CERTINFO, &certificates) == CURLE_OK && certificates) {
                for (int index{}; index < certificates->num_of_certs; ++index) {
                    CertInfo info;
                    for (auto* entry{ certificates->certinfo[index] }; entry; entry = entry->next) {
                        info.emplace_back(entry->data);
                    }
                    result.m_cert_infos.push_back(std::move(info));
                }
            }
            return result;
        }

        /**
         * @brief Snapshot a nullable shared holder without retaining or copying its ownership.
         * @param curl Holder used for the transfer; a null pointer returns BAD_FUNCTION_ARGUMENT.
         * @param body Received body bytes.
         * @param headers Received raw header blocks.
         * @param received_cookies Cookies extracted from the handle.
         * @param transfer_error Transport outcome.
         * @return An independent response snapshot or an invalid-holder error.
         */
        [[nodiscard]] static auto FromCurl(
            std::shared_ptr<curl::CurlHolder> const& curl,
            std::string body,
            std::string headers,
            Cookies received_cookies = {},
            Error transfer_error = {}
        ) -> Result<Response> {
            if (!curl) {
                return std::unexpected{ Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::Response requires an active curl holder." } };
            }
            return FromCurl(*curl, std::move(body), std::move(headers), std::move(received_cookies), std::move(transfer_error));
        }

        /**
         * @brief Return certificate details captured for this response.
         * @return An independent copy, empty for plain HTTP.
         * @note Retain cpr's value-returning signature; modifying the copy does not change the snapshot.
         */
        [[nodiscard]] auto GetCertInfos() const -> std::vector<CertInfo> { return m_cert_infos; }
    };

} // namespace mcr
