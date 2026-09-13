/**
 * @file test_json_response.cpp
 * @brief Verify on-demand JSON parsing and diagnostics independently of transport metadata.
 */
import std;
import mcr.response;

namespace {
    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_json_response: {}", message);
        }
        return condition;
    }
} // namespace

auto main() -> int {
    try {
        mcr::Response response;
        response.text                   = R"({"message":"missing","code":404})";
        response.status_code            = 404;
        response.header["Content-Type"] = "text/plain";
        auto value                      = response.Json();
        value["code"]                   = 200;
        auto parsed{ response.TryJson() };
        bool passed{ check(parsed && parsed->at("code").get<int>() == 404 && response.text == R"({"message":"missing","code":404})" && !response.error, "parsing must ignore HTTP status and media type and return independent values") };
        response.text  = "null";
        passed        &= check(response.Json().is_null() && response.TryJson()->is_null(), "changing text must be observed without a stale cache, including valid null");
        response.header.clear();
        response.text  = "[1,true,\"text\"]";
        passed        &= check(response.Json().is_array(), "parsing must work without a Content-Type header");

        for (auto const& invalid : {
                 std::string{},
                 std::string{ " \r\n\t" },
                 std::string{ "{" },
                 std::string{ "<html>error</html>" },
                 std::string{ "{} trailing" },
                 std::string{ "[1,]" },
                 std::string{ "/*comment*/{}" },
                 std::string{ "\"a\0b\"", 5 },
                 std::string{ '"', static_cast<char>(0xff), '"' }
        }) {
            response.text  = invalid;
            parsed         = response.TryJson();
            passed        &= check(!parsed && parsed.error().id == 101 && parsed.error().byte.has_value() && *parsed.error().byte >= 1 && !parsed.error().message.empty(), "malformed documents must return owned parse diagnostics with a one-based byte position");
            passed        &= check(response.text == invalid && !response.error, "JSON failures must not overwrite text or transport errors");
            try {
                (void)response.Json();
                passed &= check(false, "Json must throw for malformed documents");
            } catch (mcr::Json::parse_error const& failure) {
                passed &= check(failure.byte == parsed.error().byte && failure.id == parsed.error().id, "throwing and expected interfaces must agree on diagnostics");
            }
        }
        auto saved{ parsed.error() };
        response.text   = "1e10000";
        parsed          = response.TryJson();
        passed         &= check(!parsed && parsed.error().id == 406 && !parsed.error().byte && !parsed.error().message.empty(), "numeric overflow must also become a JSON diagnostic without a fabricated byte position");
        response.text   = "{}";
        response.error  = mcr::Error{ mcr::ErrorCode::PARTIAL_FILE, "original transport failure" };
        passed         &= check(response.TryJson().has_value() && response.error.code == mcr::ErrorCode::PARTIAL_FILE && response.error.message == "original transport failure" && !saved.message.empty(), "explicit parsing must preserve transport metadata and earlier diagnostics must own their messages");
        return passed ? 0 : 1;
    } catch (std::exception const& failure) {
        (void)check(false, failure.what());
        return 1;
    }
}
