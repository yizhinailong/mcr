/**
 * @file test_json.cpp
 * @brief Verify JSON module exports, serialized ownership, and encoding failures.
 */
import std;
import mcr.json;

namespace {
    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_json: {}", message);
        }
        return condition;
    }
} // namespace

auto main() -> int {
    try {
        mcr::Json source{
            {   "name",                                                      "Alice" },
            { "active",                                                         true },
            { "values", mcr::Json::array({ 1, nullptr, "\xe4\xbd\xa0\xe5\xa5\xbd" }) }
        };
        mcr::Json const     expected = source;
        mcr::JsonBody const body{ source };
        source["name"] = "changed";
        bool passed{ check(mcr::Json::parse(body.Str()) == expected, "body must retain a snapshot of nested JSON and UTF-8 strings") };
        passed &= check(body.Str().find('\n') == std::string::npos, "default serialization must be compact");
        auto copied{ body };
        auto moved{ std::move(copied) };
        passed &= check(moved.Str() == body.Str(), "copies and moves must preserve serialized bytes");

        for (auto const& value : { mcr::Json(nullptr), mcr::Json(true), mcr::Json(42), mcr::Json(1.25), mcr::Json::array(), mcr::Json::object(), mcr::Json(std::string{ "a\0b\n\"", 5 }) }) {
            mcr::JsonBody const scalar{ value };
            passed &= check(mcr::Json::parse(scalar.Str()) == value, "all JSON root kinds and escaped null bytes must round trip");
        }
        passed &= check(mcr::JsonBody{ mcr::Json(nullptr) }.Str() == "null", "JSON null must produce a nonempty body");
        passed &= check(mcr::JsonBody{ mcr::Json("{\"a\":1}") }.Str() == R"("{\"a\":1}")", "JSON string values must be quoted rather than treated as serialized documents");

        try {
            (void)mcr::JsonBody{ mcr::Json(std::string(1, static_cast<char>(0xff))) };
            passed &= check(false, "invalid UTF-8 must fail during body construction");
        } catch (mcr::Json::type_error const& failure) {
            passed &= check(failure.id == 316, "serialization must retain nlohmann's encoding diagnostic");
        }
        return passed ? 0 : 1;
    } catch (std::exception const& failure) {
        (void)check(false, failure.what());
        return 1;
    }
}
