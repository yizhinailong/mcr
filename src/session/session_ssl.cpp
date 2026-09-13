/**
 * @file session_ssl.cpp
 * @brief Apply owned TLS options without borrowing their storage.
 */
module;
#include <curl/curl.h>
module mcr.session;
import std;

namespace mcr {
    auto Session::SetSslOptions(options::SslOptions const& options) -> void {
        // Some backends reject even the default value for unsupported optional settings.
        auto optional_option = [this](CURLoption option, auto value, bool requested) {
            auto const result{ curl_easy_setopt(m_curl->handle, option, value) };
            if (!requested && (result == CURLE_NOT_BUILT_IN || result == CURLE_UNKNOWN_OPTION)) {
                return;
            }
            checkCurl(result);
        };
        auto string_option = [&](CURLoption option, std::string_view value) {
            optional_option(option, value.empty() ? nullptr : value.data(), !value.empty());
        };
        auto blob_option = [&](CURLoption option, std::string_view value) {
            curl_blob blob{ const_cast<char*>(value.data()), value.size(), CURL_BLOB_COPY };
            optional_option(option, value.empty() ? nullptr : &blob, !value.empty());
        };

        string_option(CURLOPT_SSLCERT, options.cert_file);
        blob_option(CURLOPT_SSLCERT_BLOB, options.cert_file.empty() ? std::string_view{ options.cert_blob } : std::string_view{});
        setOption(CURLOPT_SSLCERTTYPE, options.cert_type.empty() ? "PEM" : options.cert_type.c_str());
        string_option(CURLOPT_SSLKEY, options.key_file);
        blob_option(CURLOPT_SSLKEY_BLOB, options.key_file.empty() ? std::string_view{ options.key_blob } : std::string_view{});
        setOption(CURLOPT_SSLKEYTYPE, options.key_type.empty() ? "PEM" : options.key_type.c_str());
        string_option(CURLOPT_KEYPASSWD, options.key_pass);
        string_option(CURLOPT_PINNEDPUBLICKEY, options.pinned_public_key);
        setOption(CURLOPT_SSL_ENABLE_ALPN, options.enable_alpn ? 1L : 0L);
        setOption(CURLOPT_SSL_VERIFYPEER, options.verify_peer ? 1L : 0L);
        setOption(CURLOPT_SSL_VERIFYHOST, options.verify_host ? 2L : 0L);
        optional_option(CURLOPT_SSL_VERIFYSTATUS, options.verify_status ? 1L : 0L, options.verify_status);
        setOption(CURLOPT_SSLVERSION, options.ssl_version | options.max_version);
        long flags{ options.ssl_no_revoke ? CURLSSLOPT_NO_REVOKE : 0L };
#ifdef _WIN32
        flags |= CURLSSLOPT_NATIVE_CA;
#endif
        setOption(CURLOPT_SSL_OPTIONS, flags);
#if LIBCURL_VERSION_NUM >= 0x080F00
        if (options.ssl_fast_start) {
            throw std::runtime_error{ "mcr::Session: TLS false start was removed in curl 8.15." };
        }
#else
        optional_option(CURLOPT_SSL_FALSESTART, options.ssl_fast_start ? 1L : 0L, options.ssl_fast_start);
#endif

        char* default_ca{ nullptr };
        (void)curl_easy_getinfo(m_curl->handle, CURLINFO_CAINFO, &default_ca);
        setOption(CURLOPT_CAINFO, options.ca_info.empty() ? default_ca : options.ca_info.c_str());
        blob_option(CURLOPT_CAINFO_BLOB, options.ca_buffer.empty() ? options.ca_info_blob : options.ca_buffer);
        char* default_path{ nullptr };
        (void)curl_easy_getinfo(m_curl->handle, CURLINFO_CAPATH, &default_path);
        optional_option(CURLOPT_CAPATH, options.ca_path.empty() ? default_path : options.ca_path.c_str(), !options.ca_path.empty());
        string_option(CURLOPT_CRLFILE, options.crl_file);
        string_option(CURLOPT_SSL_CIPHER_LIST, options.ciphers);
        string_option(CURLOPT_TLS13_CIPHERS, options.tls13_ciphers);
        setOption(CURLOPT_SSL_SESSIONID_CACHE, options.session_id_cache ? 1L : 0L);
    }
} // namespace mcr
