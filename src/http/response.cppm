/**
 * @file response.cppm
 * @brief Owned snapshots of HTTP response data and curl transfer metadata.
 */
module;

#include <curl/curl.h>

export module mcr.response;

export import mcr.cert_info;
export import mcr.cookies;
export import mcr.curlholder;
export import mcr.error;
export import mcr.types;

import mcr.util;
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
        long          status_code{};      ///< HTTP status, or zero when no response was received.
        std::string   text;               ///< Buffered response body, including binary bytes.
        Header        header;             ///< Case-insensitive headers from the final response.
        Url           url;                ///< Effective URL after redirects.
        double        elapsed{};          ///< Total transfer duration in seconds.
        Cookies       cookies;            ///< Snapshot of curl's cookie engine.
        Error         error;              ///< Transport error; HTTP 4xx and 5xx do not set this.
        std::string   raw_header;         ///< All received header blocks, including redirects.
        std::string   status_line;        ///< Final HTTP status line.
        std::string   reason;             ///< Final HTTP reason phrase, if present.
        CprOffT       uploaded_bytes{};   ///< Uploaded body bytes reported by curl.
        CprOffT       downloaded_bytes{}; ///< Downloaded body bytes reported by curl.
        long          redirect_count{};   ///< Number of redirects followed.
        std::string   primary_ip;         ///< Address of the connected peer.
        std::uint16_t primary_port{};     ///< Port of the connected peer.

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
         * @throws std::invalid_argument If the holder or easy handle is null.
         */
        Response(std::shared_ptr<curl::CurlHolder> curl, std::string&& body, std::string&& headers, Cookies&& received_cookies = {}, Error&& transfer_error = {})
            : text{ std::move(body) }, cookies{ std::move(received_cookies) }, error{ std::move(transfer_error) }, raw_header{ std::move(headers) } {
            if (!curl || !curl->handle) {
                throw std::invalid_argument{ "mcr::Response requires an active curl holder." };
            }
            header = utils::parse_header(raw_header, &status_line, &reason);
            auto* handle{ curl->handle };
            (void)curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status_code);
            (void)curl_easy_getinfo(handle, CURLINFO_TOTAL_TIME, &elapsed);
            char* address{ nullptr };
            if (curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &address) == CURLE_OK && address) {
                url = Url{ address };
            }
            (void)curl_easy_getinfo(handle, CURLINFO_SIZE_DOWNLOAD_T, &downloaded_bytes);
            (void)curl_easy_getinfo(handle, CURLINFO_SIZE_UPLOAD_T, &uploaded_bytes);
            (void)curl_easy_getinfo(handle, CURLINFO_REDIRECT_COUNT, &redirect_count);
            address = nullptr;
            if (curl_easy_getinfo(handle, CURLINFO_PRIMARY_IP, &address) == CURLE_OK && address) {
                primary_ip = address;
            }
            long port{};
            if (curl_easy_getinfo(handle, CURLINFO_PRIMARY_PORT, &port) == CURLE_OK) {
                primary_port = static_cast<std::uint16_t>(port);
            }
            curl_certinfo* certificates{ nullptr };
            if (curl_easy_getinfo(handle, CURLINFO_CERTINFO, &certificates) == CURLE_OK && certificates) {
                for (int index{}; index < certificates->num_of_certs; ++index) {
                    CertInfo info;
                    for (auto* entry{ certificates->certinfo[index] }; entry; entry = entry->next) {
                        info.emplace_back(entry->data);
                    }
                    m_cert_infos.push_back(std::move(info));
                }
            }
        }

        /**
         * @brief Return certificate details captured for this response.
         * @return An independent copy, empty for plain HTTP.
         */
        [[nodiscard]] auto GetCertInfos() const -> std::vector<CertInfo> { return m_cert_infos; }
    };

} // namespace mcr
