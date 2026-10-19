/**
 * @file test_error.cpp
 * @brief Verify error mapping, diagnostics, and result propagation through the library entry module.
 */
#include <curl/curl.h>

import std;
import mcr;

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_error: {}", message);
        }
        return condition;
    }

    auto transfer_result(std::int32_t curl_code, std::string message) -> mcr::Result<std::string> {
        auto status{ mcr::check_curl_error(curl_code, std::move(message)) };
        if (!status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return "complete";
    }

} // namespace

int main() {
    bool passed{ true };

    mcr::Error const default_error;
    passed &= check(
        !default_error &&
            default_error.code == mcr::ErrorCode::OK &&
            default_error.message.empty(),
        "default Error must mean no error"
    );

    mcr::Error const direct_error{ mcr::ErrorCode::COULDNT_CONNECT, "connection refused" };
    passed &= check(
        static_cast<bool>(direct_error) &&
            direct_error.message == "connection refused",
        "Error must preserve its code and diagnostic"
    );

    /**
     * @brief A curl status and its expected library error code.
     * @details Cases include numeric collisions: curl timeout is 28, while cpr timeout is 18.
     * Curl's PARTIAL_FILE is 18 and its HTTP2 is 16, requiring explicit mapping.
     */
    struct MappingCase {
        std::int32_t curl_code;
        mcr::ErrorCode error_code;
    };

    MappingCase const cases[]{
        { CURLE_OK, mcr::ErrorCode::OK },
        { CURLE_UNSUPPORTED_PROTOCOL, mcr::ErrorCode::UNSUPPORTED_PROTOCOL },
        { CURLE_URL_MALFORMAT, mcr::ErrorCode::URL_MALFORMAT },
        { CURLE_COULDNT_RESOLVE_PROXY, mcr::ErrorCode::COULDNT_RESOLVE_PROXY },
        { CURLE_COULDNT_RESOLVE_HOST, mcr::ErrorCode::COULDNT_RESOLVE_HOST },
        { CURLE_COULDNT_CONNECT, mcr::ErrorCode::COULDNT_CONNECT },
        { CURLE_HTTP2, mcr::ErrorCode::HTTP2 },
        { CURLE_PARTIAL_FILE, mcr::ErrorCode::PARTIAL_FILE },
        { CURLE_OPERATION_TIMEDOUT, mcr::ErrorCode::OPERATION_TIMEDOUT },
        { CURLE_PEER_FAILED_VERIFICATION, mcr::ErrorCode::PEER_FAILED_VERIFICATION },
        { CURLE_SETOPT_OPTION_SYNTAX, mcr::ErrorCode::SETOPT_OPTION_SYNTAX },
        { CURLE_HTTP3, mcr::ErrorCode::HTTP3 },
        { CURLE_TOO_LARGE, mcr::ErrorCode::TOO_LARGE },
        { CURLE_FTP_WEIRD_PASS_REPLY, mcr::ErrorCode::UNKNOWN_ERROR },
        { -1, mcr::ErrorCode::UNKNOWN_ERROR },
        { (std::numeric_limits<std::int32_t>::max)(), mcr::ErrorCode::UNKNOWN_ERROR },
    };
    for (auto const& entry : cases) {
        mcr::Error const error{ entry.curl_code, "curl diagnostic" };
        passed &= check(
            error.code == entry.error_code &&
                error.message == "curl diagnostic",
            "curl Error construction must map the code and preserve its diagnostic"
        );
    }

    auto const& mapping{ mcr::get_error_code_to_string_mapping() };
    passed &= check(mapping.size() == 64, "all 64 cpr error codes must have names");
    for (std::uint16_t value{ 0 }; value <= 62; ++value) {
        passed &= check(
            mapping.contains(static_cast<mcr::ErrorCode>(value)),
            "a supported error code is missing its name"
        );
    }
    passed &= check(mcr::to_string(mcr::ErrorCode::OK) == "OK", "success must have its cpr name");
    passed &= check(
        mcr::to_string(mcr::ErrorCode::UNSUPPORTED_PROTOCOL) == "UNSUPPORTED_PROTOCOL",
        "unsupported protocol must have its cpr name"
    );
    passed &= check(
        mcr::to_string(mcr::ErrorCode::OPERATION_TIMEDOUT) == "OPERATION_TIMEDOUT",
        "timeout must have its cpr name"
    );
    passed &= check(
        mcr::to_string(mcr::ErrorCode::UNKNOWN_ERROR) == "UNKNOWN_ERROR",
        "unknown curl errors must have a printable name"
    );
    try {
        (void)mcr::to_string(static_cast<mcr::ErrorCode>(999));
        passed &= check(false, "invalid enum lookup must retain cpr's out_of_range behavior");
    } catch (std::out_of_range const&) {
    }

    auto const success{ mcr::check_curl_error(CURLE_OK, "unused diagnostic") };
    passed &= check(success.has_value(), "CURLE_OK must yield a successful Result<void>");

    std::string diagnostic{ "request timed out" };
    auto const failure{ mcr::check_curl_error(CURLE_OPERATION_TIMEDOUT, diagnostic) };
    diagnostic.clear();
    passed &= check(
        !failure &&
            failure.error().code == mcr::ErrorCode::OPERATION_TIMEDOUT &&
            failure.error().message == "request timed out",
        "failed Result<void> must own its Error diagnostic"
    );

    auto const unknown{ mcr::check_curl_error(-1) };
    passed &= check(
        !unknown &&
            unknown.error().code == mcr::ErrorCode::UNKNOWN_ERROR &&
            unknown.error().message.empty(),
        "unrecognized curl status must fail even without a diagnostic"
    );

    auto const value{ transfer_result(CURLE_OK, {}) };
    passed &= check(value && *value == "complete", "a successful curl check must allow a value result");

    auto const propagated{ transfer_result(CURLE_PARTIAL_FILE, "incomplete response") };
    passed &= check(
        !propagated &&
            propagated.error().code == mcr::ErrorCode::PARTIAL_FILE &&
            propagated.error().message == "incomplete response",
        "curl failure must propagate into Result<T>"
    );

    if (!passed) {
        return 1;
    }
    std::println("test_error: ok");
    return 0;
}
