/**
 * @file error.cppm
 * @brief Error codes, diagnostics, and expected-based results for HTTP operations.
 */
module;

#include <curl/curl.h>

export module mcr.error;

import std;

export namespace mcr {

    /**
     * @brief Error codes for HTTP operations, preserving cpr's names and values.
     * @note These values differ from CURLcode. Use error_code_from_curl() to convert curl errors.
     */
    enum class ErrorCode : std::uint16_t {
        OK                       = 0, ///< No error occurred.
        UNSUPPORTED_PROTOCOL     = 1,
        FAILED_INIT              = 2,
        URL_MALFORMAT            = 3,
        NOT_BUILT_IN             = 4,
        COULDNT_RESOLVE_PROXY    = 5,
        COULDNT_RESOLVE_HOST     = 6,
        COULDNT_CONNECT          = 7,
        WEIRD_SERVER_REPLY       = 8,
        REMOTE_ACCESS_DENIED     = 9,
        HTTP2                    = 10,
        PARTIAL_FILE             = 11,
        QUOTE_ERROR              = 12,
        HTTP_RETURNED_ERROR      = 13,
        WRITE_ERROR              = 14,
        UPLOAD_FAILED            = 15,
        READ_ERROR               = 16,
        OUT_OF_MEMORY            = 17,
        OPERATION_TIMEDOUT       = 18,
        RANGE_ERROR              = 19,
        HTTP_POST_ERROR          = 20,
        SSL_CONNECT_ERROR        = 21,
        BAD_DOWNLOAD_RESUME      = 22,
        FILE_COULDNT_READ_FILE   = 23,
        FUNCTION_NOT_FOUND       = 24,
        ABORTED_BY_CALLBACK      = 25,
        BAD_FUNCTION_ARGUMENT    = 26,
        INTERFACE_FAILED         = 27,
        TOO_MANY_REDIRECTS       = 28,
        UNKNOWN_OPTION           = 29,
        SETOPT_OPTION_SYNTAX     = 30,
        GOT_NOTHING              = 31,
        SSL_ENGINE_NOTFOUND      = 32,
        SSL_ENGINE_SETFAILED     = 33,
        SEND_ERROR               = 34,
        RECV_ERROR               = 35,
        SSL_CERTPROBLEM          = 36,
        SSL_CIPHER               = 37,
        PEER_FAILED_VERIFICATION = 38,
        BAD_CONTENT_ENCODING     = 39,
        FILESIZE_EXCEEDED        = 40,
        USE_SSL_FAILED           = 41,
        SEND_FAIL_REWIND         = 42,
        SSL_ENGINE_INITFAILED    = 43,
        LOGIN_DENIED             = 44,
        SSL_CACERT_BADFILE       = 45,
        SSL_SHUTDOWN_FAILED      = 46,
        AGAIN                    = 47,
        SSL_CRL_BADFILE          = 48,
        SSL_ISSUER_ERROR         = 49,
        CHUNK_FAILED             = 50,
        NO_CONNECTION_AVAILABLE  = 51,
        SSL_PINNEDPUBKEYNOTMATCH = 52,
        SSL_INVALIDCERTSTATUS    = 53,
        HTTP2_STREAM             = 54,
        RECURSIVE_API_CALL       = 55,
        AUTH_ERROR               = 56,
        HTTP3                    = 57,
        QUIC_CONNECT_ERROR       = 58,
        PROXY                    = 59,
        SSL_CLIENTCERT           = 60,
        UNRECOVERABLE_POLL       = 61,
        TOO_LARGE                = 62,
        UNKNOWN_ERROR            = 1000, ///< An unsupported or unrecognized curl error occurred.
    };

    /**
     * @brief Get the mapping from error codes to their symbolic names.
     * @return A reference to the immutable mapping, valid until program shutdown.
     * @note The function-local static follows cpr's workaround for MSVC /MT double destruction.
     */
    [[nodiscard]] inline auto get_error_code_to_string_mapping()
        -> std::unordered_map<ErrorCode, std::string> const& {
        static std::unordered_map<ErrorCode, std::string> const s_mapping{
            {                       ErrorCode::OK,                       "OK" },
            {     ErrorCode::UNSUPPORTED_PROTOCOL,     "UNSUPPORTED_PROTOCOL" },
            {              ErrorCode::FAILED_INIT,              "FAILED_INIT" },
            {            ErrorCode::URL_MALFORMAT,            "URL_MALFORMAT" },
            {             ErrorCode::NOT_BUILT_IN,             "NOT_BUILT_IN" },
            {    ErrorCode::COULDNT_RESOLVE_PROXY,    "COULDNT_RESOLVE_PROXY" },
            {     ErrorCode::COULDNT_RESOLVE_HOST,     "COULDNT_RESOLVE_HOST" },
            {          ErrorCode::COULDNT_CONNECT,          "COULDNT_CONNECT" },
            {       ErrorCode::WEIRD_SERVER_REPLY,       "WEIRD_SERVER_REPLY" },
            {     ErrorCode::REMOTE_ACCESS_DENIED,     "REMOTE_ACCESS_DENIED" },
            {                    ErrorCode::HTTP2,                    "HTTP2" },
            {             ErrorCode::PARTIAL_FILE,             "PARTIAL_FILE" },
            {              ErrorCode::QUOTE_ERROR,              "QUOTE_ERROR" },
            {      ErrorCode::HTTP_RETURNED_ERROR,      "HTTP_RETURNED_ERROR" },
            {              ErrorCode::WRITE_ERROR,              "WRITE_ERROR" },
            {            ErrorCode::UPLOAD_FAILED,            "UPLOAD_FAILED" },
            {               ErrorCode::READ_ERROR,               "READ_ERROR" },
            {            ErrorCode::OUT_OF_MEMORY,            "OUT_OF_MEMORY" },
            {       ErrorCode::OPERATION_TIMEDOUT,       "OPERATION_TIMEDOUT" },
            {              ErrorCode::RANGE_ERROR,              "RANGE_ERROR" },
            {          ErrorCode::HTTP_POST_ERROR,          "HTTP_POST_ERROR" },
            {        ErrorCode::SSL_CONNECT_ERROR,        "SSL_CONNECT_ERROR" },
            {      ErrorCode::BAD_DOWNLOAD_RESUME,      "BAD_DOWNLOAD_RESUME" },
            {   ErrorCode::FILE_COULDNT_READ_FILE,   "FILE_COULDNT_READ_FILE" },
            {       ErrorCode::FUNCTION_NOT_FOUND,       "FUNCTION_NOT_FOUND" },
            {      ErrorCode::ABORTED_BY_CALLBACK,      "ABORTED_BY_CALLBACK" },
            {    ErrorCode::BAD_FUNCTION_ARGUMENT,    "BAD_FUNCTION_ARGUMENT" },
            {         ErrorCode::INTERFACE_FAILED,         "INTERFACE_FAILED" },
            {       ErrorCode::TOO_MANY_REDIRECTS,       "TOO_MANY_REDIRECTS" },
            {           ErrorCode::UNKNOWN_OPTION,           "UNKNOWN_OPTION" },
            {     ErrorCode::SETOPT_OPTION_SYNTAX,     "SETOPT_OPTION_SYNTAX" },
            {              ErrorCode::GOT_NOTHING,              "GOT_NOTHING" },
            {      ErrorCode::SSL_ENGINE_NOTFOUND,      "SSL_ENGINE_NOTFOUND" },
            {     ErrorCode::SSL_ENGINE_SETFAILED,     "SSL_ENGINE_SETFAILED" },
            {               ErrorCode::SEND_ERROR,               "SEND_ERROR" },
            {               ErrorCode::RECV_ERROR,               "RECV_ERROR" },
            {          ErrorCode::SSL_CERTPROBLEM,          "SSL_CERTPROBLEM" },
            {               ErrorCode::SSL_CIPHER,               "SSL_CIPHER" },
            { ErrorCode::PEER_FAILED_VERIFICATION, "PEER_FAILED_VERIFICATION" },
            {     ErrorCode::BAD_CONTENT_ENCODING,     "BAD_CONTENT_ENCODING" },
            {        ErrorCode::FILESIZE_EXCEEDED,        "FILESIZE_EXCEEDED" },
            {           ErrorCode::USE_SSL_FAILED,           "USE_SSL_FAILED" },
            {         ErrorCode::SEND_FAIL_REWIND,         "SEND_FAIL_REWIND" },
            {    ErrorCode::SSL_ENGINE_INITFAILED,    "SSL_ENGINE_INITFAILED" },
            {             ErrorCode::LOGIN_DENIED,             "LOGIN_DENIED" },
            {       ErrorCode::SSL_CACERT_BADFILE,       "SSL_CACERT_BADFILE" },
            {      ErrorCode::SSL_SHUTDOWN_FAILED,      "SSL_SHUTDOWN_FAILED" },
            {                    ErrorCode::AGAIN,                    "AGAIN" },
            {          ErrorCode::SSL_CRL_BADFILE,          "SSL_CRL_BADFILE" },
            {         ErrorCode::SSL_ISSUER_ERROR,         "SSL_ISSUER_ERROR" },
            {             ErrorCode::CHUNK_FAILED,             "CHUNK_FAILED" },
            {  ErrorCode::NO_CONNECTION_AVAILABLE,  "NO_CONNECTION_AVAILABLE" },
            { ErrorCode::SSL_PINNEDPUBKEYNOTMATCH, "SSL_PINNEDPUBKEYNOTMATCH" },
            {    ErrorCode::SSL_INVALIDCERTSTATUS,    "SSL_INVALIDCERTSTATUS" },
            {             ErrorCode::HTTP2_STREAM,             "HTTP2_STREAM" },
            {       ErrorCode::RECURSIVE_API_CALL,       "RECURSIVE_API_CALL" },
            {               ErrorCode::AUTH_ERROR,               "AUTH_ERROR" },
            {                    ErrorCode::HTTP3,                    "HTTP3" },
            {       ErrorCode::QUIC_CONNECT_ERROR,       "QUIC_CONNECT_ERROR" },
            {                    ErrorCode::PROXY,                    "PROXY" },
            {           ErrorCode::SSL_CLIENTCERT,           "SSL_CLIENTCERT" },
            {       ErrorCode::UNRECOVERABLE_POLL,       "UNRECOVERABLE_POLL" },
            {                ErrorCode::TOO_LARGE,                "TOO_LARGE" },
            {            ErrorCode::UNKNOWN_ERROR,            "UNKNOWN_ERROR" },
        };
        return s_mapping;
    }

    /**
     * @brief Get the symbolic name of an error code.
     * @param code Error code to look up.
     * @return A copy of the error code's name.
     * @throws std::out_of_range If code is not a defined ErrorCode value.
     * @note Preserves cpr's lookup behavior and exposes the function in namespace mcr.
     */
    [[nodiscard]] inline auto to_string(ErrorCode code) -> std::string {
        return get_error_code_to_string_mapping().at(code);
    }

    /**
     * @brief Convert a curl status to the corresponding library error code.
     * @param curl_code Numeric CURLcode returned by a curl operation.
     * @return The mapped error code, or ErrorCode::UNKNOWN_ERROR for unsupported or unrecognized codes.
     */
    [[nodiscard]] constexpr auto error_code_from_curl(std::int32_t curl_code) noexcept -> ErrorCode {
        switch (curl_code) {
            case CURLE_OK                      : return ErrorCode::OK;
            case CURLE_UNSUPPORTED_PROTOCOL    : return ErrorCode::UNSUPPORTED_PROTOCOL;
            case CURLE_FAILED_INIT             : return ErrorCode::FAILED_INIT;
            case CURLE_URL_MALFORMAT           : return ErrorCode::URL_MALFORMAT;
            case CURLE_NOT_BUILT_IN            : return ErrorCode::NOT_BUILT_IN;
            case CURLE_COULDNT_RESOLVE_PROXY   : return ErrorCode::COULDNT_RESOLVE_PROXY;
            case CURLE_COULDNT_RESOLVE_HOST    : return ErrorCode::COULDNT_RESOLVE_HOST;
            case CURLE_COULDNT_CONNECT         : return ErrorCode::COULDNT_CONNECT;
            case CURLE_WEIRD_SERVER_REPLY      : return ErrorCode::WEIRD_SERVER_REPLY;
            case CURLE_REMOTE_ACCESS_DENIED    : return ErrorCode::REMOTE_ACCESS_DENIED;
            case CURLE_HTTP2                   : return ErrorCode::HTTP2;
            case CURLE_PARTIAL_FILE            : return ErrorCode::PARTIAL_FILE;
            case CURLE_QUOTE_ERROR             : return ErrorCode::QUOTE_ERROR;
            case CURLE_HTTP_RETURNED_ERROR     : return ErrorCode::HTTP_RETURNED_ERROR;
            case CURLE_WRITE_ERROR             : return ErrorCode::WRITE_ERROR;
            case CURLE_UPLOAD_FAILED           : return ErrorCode::UPLOAD_FAILED;
            case CURLE_READ_ERROR              : return ErrorCode::READ_ERROR;
            case CURLE_OUT_OF_MEMORY           : return ErrorCode::OUT_OF_MEMORY;
            case CURLE_OPERATION_TIMEDOUT      : return ErrorCode::OPERATION_TIMEDOUT;
            case CURLE_RANGE_ERROR             : return ErrorCode::RANGE_ERROR;
            case CURLE_HTTP_POST_ERROR         : return ErrorCode::HTTP_POST_ERROR;
            case CURLE_SSL_CONNECT_ERROR       : return ErrorCode::SSL_CONNECT_ERROR;
            case CURLE_BAD_DOWNLOAD_RESUME     : return ErrorCode::BAD_DOWNLOAD_RESUME;
            case CURLE_FILE_COULDNT_READ_FILE  : return ErrorCode::FILE_COULDNT_READ_FILE;
            case CURLE_FUNCTION_NOT_FOUND      : return ErrorCode::FUNCTION_NOT_FOUND;
            case CURLE_ABORTED_BY_CALLBACK     : return ErrorCode::ABORTED_BY_CALLBACK;
            case CURLE_BAD_FUNCTION_ARGUMENT   : return ErrorCode::BAD_FUNCTION_ARGUMENT;
            case CURLE_INTERFACE_FAILED        : return ErrorCode::INTERFACE_FAILED;
            case CURLE_TOO_MANY_REDIRECTS      : return ErrorCode::TOO_MANY_REDIRECTS;
            case CURLE_UNKNOWN_OPTION          : return ErrorCode::UNKNOWN_OPTION;
            case CURLE_SETOPT_OPTION_SYNTAX    : return ErrorCode::SETOPT_OPTION_SYNTAX;
            case CURLE_GOT_NOTHING             : return ErrorCode::GOT_NOTHING;
            case CURLE_SSL_ENGINE_NOTFOUND     : return ErrorCode::SSL_ENGINE_NOTFOUND;
            case CURLE_SSL_ENGINE_SETFAILED    : return ErrorCode::SSL_ENGINE_SETFAILED;
            case CURLE_SEND_ERROR              : return ErrorCode::SEND_ERROR;
            case CURLE_RECV_ERROR              : return ErrorCode::RECV_ERROR;
            case CURLE_SSL_CERTPROBLEM         : return ErrorCode::SSL_CERTPROBLEM;
            case CURLE_SSL_CIPHER              : return ErrorCode::SSL_CIPHER;
            case CURLE_PEER_FAILED_VERIFICATION: return ErrorCode::PEER_FAILED_VERIFICATION;
            case CURLE_BAD_CONTENT_ENCODING    : return ErrorCode::BAD_CONTENT_ENCODING;
            case CURLE_FILESIZE_EXCEEDED       : return ErrorCode::FILESIZE_EXCEEDED;
            case CURLE_USE_SSL_FAILED          : return ErrorCode::USE_SSL_FAILED;
            case CURLE_SEND_FAIL_REWIND        : return ErrorCode::SEND_FAIL_REWIND;
            case CURLE_SSL_ENGINE_INITFAILED   : return ErrorCode::SSL_ENGINE_INITFAILED;
            case CURLE_LOGIN_DENIED            : return ErrorCode::LOGIN_DENIED;
            case CURLE_SSL_CACERT_BADFILE      : return ErrorCode::SSL_CACERT_BADFILE;
            case CURLE_SSL_SHUTDOWN_FAILED     : return ErrorCode::SSL_SHUTDOWN_FAILED;
            case CURLE_AGAIN                   : return ErrorCode::AGAIN;
            case CURLE_SSL_CRL_BADFILE         : return ErrorCode::SSL_CRL_BADFILE;
            case CURLE_SSL_ISSUER_ERROR        : return ErrorCode::SSL_ISSUER_ERROR;
            case CURLE_CHUNK_FAILED            : return ErrorCode::CHUNK_FAILED;
            case CURLE_NO_CONNECTION_AVAILABLE : return ErrorCode::NO_CONNECTION_AVAILABLE;
            case CURLE_SSL_PINNEDPUBKEYNOTMATCH: return ErrorCode::SSL_PINNEDPUBKEYNOTMATCH;
            case CURLE_SSL_INVALIDCERTSTATUS   : return ErrorCode::SSL_INVALIDCERTSTATUS;
            case CURLE_HTTP2_STREAM            : return ErrorCode::HTTP2_STREAM;
            case CURLE_RECURSIVE_API_CALL      : return ErrorCode::RECURSIVE_API_CALL;
            case CURLE_AUTH_ERROR              : return ErrorCode::AUTH_ERROR;
            case CURLE_HTTP3                   : return ErrorCode::HTTP3;
            case CURLE_QUIC_CONNECT_ERROR      : return ErrorCode::QUIC_CONNECT_ERROR;
            case CURLE_PROXY                   : return ErrorCode::PROXY;
            case CURLE_SSL_CLIENTCERT          : return ErrorCode::SSL_CLIENTCERT;
            case CURLE_UNRECOVERABLE_POLL      : return ErrorCode::UNRECOVERABLE_POLL;
            case CURLE_TOO_LARGE               : return ErrorCode::TOO_LARGE;
            default                            : return ErrorCode::UNKNOWN_ERROR;
        }
    }

    /**
     * @brief An error code and its owned diagnostic message.
     */
    struct Error {
        ErrorCode   code{ ErrorCode::OK }; ///< Error classification; defaults to success.
        std::string message;               ///< Caller-supplied diagnostic, which may be empty.

        /**
         * @brief Construct an Error with ErrorCode::OK and an empty message.
         */
        Error() = default;

        /**
         * @brief Construct an error from a library error code.
         * @param error_code Error classification to store.
         * @param error_message Diagnostic to take ownership of; defaults to an empty string.
         */
        explicit Error(ErrorCode error_code, std::string error_message = {})
            : code{ error_code }, message{ std::move(error_message) } {}

        /**
         * @brief Construct an error by mapping a curl status.
         * @param curl_code Numeric CURLcode to convert with error_code_from_curl().
         * @param error_message Diagnostic to take ownership of; defaults to an empty string.
         */
        explicit Error(std::int32_t curl_code, std::string error_message = {})
            : code{ error_code_from_curl(curl_code) }, message{ std::move(error_message) } {}

        /**
         * @brief Check whether this object describes a failure.
         * @return True if code differs from ErrorCode::OK; false otherwise.
         */
        [[nodiscard]] explicit operator bool() const noexcept {
            return code != ErrorCode::OK;
        }
    };

    /**
     * @brief An operation result containing a value on success or an Error on failure.
     * @tparam T Success value type; void represents an operation with no return value.
     * @note The bool conversion follows std::expected: true means success.
     */
    template <typename T = void>
    using Result = std::expected<T, Error>;

    /**
     * @brief Convert a curl status into an expected-based operation result.
     * @param curl_code Numeric CURLcode returned by a curl operation.
     * @param error_message Diagnostic to preserve on failure, such as the contents of CURLOPT_ERRORBUFFER.
     * @return A successful Result<void> for CURLE_OK;
     * otherwise an unexpected Error with the mapped code and supplied message.
     */
    [[nodiscard]] inline auto check_curl_error(std::int32_t curl_code, std::string error_message = {}) -> Result<void> {
        if (curl_code == CURLE_OK) {
            return {};
        }
        return std::unexpected{
            Error{ curl_code, std::move(error_message) }
        };
    }

} // namespace mcr
