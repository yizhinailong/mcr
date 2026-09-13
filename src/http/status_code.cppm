/**
 * @file status_code.cppm
 * @brief HTTP status constants and response classification compatible with cpr.
 */
export module mcr.status_code;

export namespace mcr::status {

    // Informational responses.
    inline constexpr long HTTP_CONTINUE{ 100 };           ///< Continue.
    inline constexpr long HTTP_SWITCHING_PROTOCOL{ 101 }; ///< Switching Protocols.
    inline constexpr long HTTP_PROCESSING{ 102 };         ///< Processing.
    inline constexpr long HTTP_EARLY_HINTS{ 103 };        ///< Early Hints.

    // Successful responses.
    inline constexpr long HTTP_OK{ 200 };                            ///< OK.
    inline constexpr long HTTP_CREATED{ 201 };                       ///< Created.
    inline constexpr long HTTP_ACCEPTED{ 202 };                      ///< Accepted.
    inline constexpr long HTTP_NON_AUTHORITATIVE_INFORMATION{ 203 }; ///< Non-Authoritative Information.
    inline constexpr long HTTP_NO_CONTENT{ 204 };                    ///< No Content.
    inline constexpr long HTTP_RESET_CONTENT{ 205 };                 ///< Reset Content.
    inline constexpr long HTTP_PARTIAL_CONTENT{ 206 };               ///< Partial Content.
    inline constexpr long HTTP_MULTI_STATUS{ 207 };                  ///< Multi-Status.
    inline constexpr long HTTP_ALREADY_REPORTED{ 208 };              ///< Already Reported.
    inline constexpr long HTTP_IM_USED{ 226 };                       ///< IM Used.

    // Redirection messages.
    inline constexpr long HTTP_MULTIPLE_CHOICE{ 300 };    ///< Multiple Choices.
    inline constexpr long HTTP_MOVED_PERMANENTLY{ 301 };  ///< Moved Permanently.
    inline constexpr long HTTP_FOUND{ 302 };              ///< Found.
    inline constexpr long HTTP_SEE_OTHER{ 303 };          ///< See Other.
    inline constexpr long HTTP_NOT_MODIFIED{ 304 };       ///< Not Modified.
    inline constexpr long HTTP_USE_PROXY{ 305 };          ///< Use Proxy.
    inline constexpr long HTTP_UNUSED{ 306 };             ///< Unused status retained for cpr compatibility.
    inline constexpr long HTTP_TEMPORARY_REDIRECT{ 307 }; ///< Temporary Redirect.
    inline constexpr long HTTP_PERMANENT_REDIRECT{ 308 }; ///< Permanent Redirect.

    // Client error responses.
    inline constexpr long HTTP_BAD_REQUEST{ 400 };                     ///< Bad Request.
    inline constexpr long HTTP_UNAUTHORIZED{ 401 };                    ///< Unauthorized.
    inline constexpr long HTTP_PAYMENT_REQUIRED{ 402 };                ///< Payment Required.
    inline constexpr long HTTP_FORBIDDEN{ 403 };                       ///< Forbidden.
    inline constexpr long HTTP_NOT_FOUND{ 404 };                       ///< Not Found.
    inline constexpr long HTTP_METHOD_NOT_ALLOWED{ 405 };              ///< Method Not Allowed.
    inline constexpr long HTTP_NOT_ACCEPTABLE{ 406 };                  ///< Not Acceptable.
    inline constexpr long HTTP_PROXY_AUTHENTICATION_REQUIRED{ 407 };   ///< Proxy Authentication Required.
    inline constexpr long HTTP_REQUEST_TIMEOUT{ 408 };                 ///< Request Timeout.
    inline constexpr long HTTP_CONFLICT{ 409 };                        ///< Conflict.
    inline constexpr long HTTP_GONE{ 410 };                            ///< Gone.
    inline constexpr long HTTP_LENGTH_REQUIRED{ 411 };                 ///< Length Required.
    inline constexpr long HTTP_PRECONDITION_FAILED{ 412 };             ///< Precondition Failed.
    inline constexpr long HTTP_PAYLOAD_TOO_LARGE{ 413 };               ///< Payload Too Large.
    inline constexpr long HTTP_URI_TOO_LONG{ 414 };                    ///< URI Too Long.
    inline constexpr long HTTP_UNSUPPORTED_MEDIA_TYPE{ 415 };          ///< Unsupported Media Type.
    inline constexpr long HTTP_REQUESTED_RANGE_NOT_SATISFIABLE{ 416 }; ///< Requested Range Not Satisfiable.
    inline constexpr long HTTP_EXPECTATION_FAILED{ 417 };              ///< Expectation Failed.
    inline constexpr long HTTP_IM_A_TEAPOT{ 418 };                     ///< I'm a teapot.
    inline constexpr long HTTP_MISDIRECTED_REQUEST{ 421 };             ///< Misdirected Request.
    inline constexpr long HTTP_UNPROCESSABLE_ENTITY{ 422 };            ///< Unprocessable Entity.
    inline constexpr long HTTP_LOCKED{ 423 };                          ///< Locked.
    inline constexpr long HTTP_FAILED_DEPENDENCY{ 424 };               ///< Failed Dependency.
    inline constexpr long HTTP_TOO_EARLY{ 425 };                       ///< Too Early.
    inline constexpr long HTTP_UPGRADE_REQUIRED{ 426 };                ///< Upgrade Required.
    inline constexpr long HTTP_PRECONDITION_REQUIRED{ 428 };           ///< Precondition Required.
    inline constexpr long HTTP_TOO_MANY_REQUESTS{ 429 };               ///< Too Many Requests.
    inline constexpr long HTTP_REQUEST_HEADER_FIELDS_TOO_LARGE{ 431 }; ///< Request Header Fields Too Large.
    inline constexpr long HTTP_UNAVAILABLE_FOR_LEGAL_REASONS{ 451 };   ///< Unavailable For Legal Reasons.

    // Server error responses.
    inline constexpr long HTTP_INTERNAL_SERVER_ERROR{ 500 };           ///< Internal Server Error.
    inline constexpr long HTTP_NOT_IMPLEMENTED{ 501 };                 ///< Not Implemented.
    inline constexpr long HTTP_BAD_GATEWAY{ 502 };                     ///< Bad Gateway.
    inline constexpr long HTTP_SERVICE_UNAVAILABLE{ 503 };             ///< Service Unavailable.
    inline constexpr long HTTP_GATEWAY_TIMEOUT{ 504 };                 ///< Gateway Timeout.
    inline constexpr long HTTP_HTTP_VERSION_NOT_SUPPORTED{ 505 };      ///< HTTP Version Not Supported.
    inline constexpr long HTTP_VARIANT_ALSO_NEGOTIATES{ 506 };         ///< Variant Also Negotiates.
    inline constexpr long HTTP_INSUFFICIENT_STORAGE{ 507 };            ///< Insufficient Storage.
    inline constexpr long HTTP_LOOP_DETECTED{ 508 };                   ///< Loop Detected.
    inline constexpr long HTTP_NOT_EXTENDED{ 510 };                    ///< Not Extended.
    inline constexpr long HTTP_NETWORK_AUTHENTICATION_REQUIRED{ 511 }; ///< Network Authentication Required.

    inline constexpr long INFO_CODE_OFFSET{ 100 };                     ///< Inclusive lower bound for informational responses.
    inline constexpr long SUCCESS_CODE_OFFSET{ 200 };                  ///< Inclusive lower bound for successful responses.
    inline constexpr long REDIRECT_CODE_OFFSET{ 300 };                 ///< Inclusive lower bound for redirection messages.
    inline constexpr long CLIENT_ERROR_CODE_OFFSET{ 400 };             ///< Inclusive lower bound for client errors.
    inline constexpr long SERVER_ERROR_CODE_OFFSET{ 500 };             ///< Inclusive lower bound for server errors.
    inline constexpr long MISC_CODE_OFFSET{ 600 };                     ///< Exclusive upper bound for server errors.

    /**
     * @brief Check whether a status belongs to the informational response class.
     * @param code Status code to classify, including unnamed values.
     * @return True for codes from 100 through 199; false otherwise.
     */
    [[nodiscard]] constexpr auto is_informational(long code) noexcept -> bool {
        return code >= INFO_CODE_OFFSET && code < SUCCESS_CODE_OFFSET;
    }

    /**
     * @brief Check whether a status belongs to the successful response class.
     * @param code Status code to classify, including unnamed values.
     * @return True for codes from 200 through 299; false otherwise.
     */
    [[nodiscard]] constexpr auto is_success(long code) noexcept -> bool {
        return code >= SUCCESS_CODE_OFFSET && code < REDIRECT_CODE_OFFSET;
    }

    /**
     * @brief Check whether a status belongs to the redirection response class.
     * @param code Status code to classify, including unnamed values.
     * @return True for codes from 300 through 399; false otherwise.
     */
    [[nodiscard]] constexpr auto is_redirect(long code) noexcept -> bool {
        return code >= REDIRECT_CODE_OFFSET && code < CLIENT_ERROR_CODE_OFFSET;
    }

    /**
     * @brief Check whether a status belongs to the client error response class.
     * @param code Status code to classify, including unnamed values.
     * @return True for codes from 400 through 499; false otherwise.
     */
    [[nodiscard]] constexpr auto is_client_error(long code) noexcept -> bool {
        return code >= CLIENT_ERROR_CODE_OFFSET && code < SERVER_ERROR_CODE_OFFSET;
    }

    /**
     * @brief Check whether a status belongs to the server error response class.
     * @param code Status code to classify, including unnamed values.
     * @return True for codes from 500 through 599; false otherwise.
     */
    [[nodiscard]] constexpr auto is_server_error(long code) noexcept -> bool {
        return code >= SERVER_ERROR_CODE_OFFSET && code < MISC_CODE_OFFSET;
    }

} // namespace mcr::status
