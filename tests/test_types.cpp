/**
 * @file test_types.cpp
 * @brief Verify owned string options and case-insensitive headers through the library entry module.
 */
#include <curl/curl.h>

import std;
import mcr;

static_assert(std::is_same_v<mcr::CprOffT, curl_off_t>);
static_assert(std::is_same_v<mcr::CprPfArgT, mcr::CprOffT>);
static_assert(std::is_nothrow_move_constructible_v<mcr::Url>);
static_assert(std::is_nothrow_move_assignable_v<mcr::Url>);
static_assert(!std::is_default_constructible_v<mcr::StringHolder<mcr::Url>>);
static_assert(!std::is_convertible_v<mcr::Url, std::string>);
static_assert(std::is_same_v<decltype(std::declval<mcr::Url const&>() + "/path"), mcr::Url>);
static_assert(std::is_same_v<decltype(std::declval<mcr::Url&>().Str()), std::string const&>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_types: {}", message);
        }
        return condition;
    }

    /**
     * @brief A second option type to verify StringHolder's derived-type support.
     */
    class TestOption : public mcr::StringHolder<TestOption> {
    public:
        explicit TestOption(std::string str) : StringHolder<TestOption>(std::move(str)) {}
    };

    auto check_construction() -> bool {
        bool passed{ true };
        mcr::Url const empty;
        passed &= check(
            empty.Str().empty() && empty.CStr()[0] == '\0',
            "default URL must be empty and null-terminated"
        );

        std::string source{ "https://example.test/api" };
        mcr::Url const from_string = source;
        mcr::Url const from_view = std::string_view{ source }.substr(0, 20);
        mcr::Url const from_c_string = source.c_str();
        source.assign("changed");
        passed &= check(from_string == "https://example.test/api", "string construction must own its text");
        passed &= check(
            from_view == "https://example.test",
            "string view construction must copy exactly the selected range"
        );
        passed &= check(from_c_string == from_string, "C string construction must own its text");

        mcr::Url const from_temporary(std::string(128, 'x'));
        passed &=
            check(from_temporary.Str() == std::string(128, 'x'), "URL must retain a temporary string's contents");

        char const bytes[]{ 'a', '\0', 'b', 'c' };
        mcr::Url const from_bytes(bytes, sizeof(bytes));
        mcr::Url const binary_view(std::string_view{ bytes, sizeof(bytes) });
        passed &= check(
            from_bytes.Str() == std::string(bytes, sizeof(bytes)),
            "byte ranges must preserve embedded nulls without requiring a terminator"
        );
        passed &= check(binary_view == from_bytes, "string views must preserve embedded nulls");
        passed &= check(
            from_bytes.Data()[2] == 'b' && from_bytes.CStr()[sizeof(bytes)] == '\0',
            "byte access must expose owned storage and its terminator"
        );
        passed &= check(
            mcr::Url(bytes, 0).Str().empty() && mcr::Url(std::string_view{}).Str().empty(),
            "zero-length ranges and empty views must produce empty URLs"
        );

        mcr::Url const fragments{ "https://", "example.test", "/api" };
        passed &= check(fragments == from_string, "initializer lists must concatenate fragments in order");
        passed &= check(
            mcr::Url(std::initializer_list<std::string>{}).Str().empty(),
            "empty fragment lists must produce empty URLs"
        );
        mcr::Url const binary_fragments{ std::string(bytes, 3), "suffix" };
        passed &= check(
            binary_fragments.Str() == std::string(bytes, 3) + "suffix",
            "fragment concatenation must preserve embedded nulls"
        );

        mcr::Url copied(from_temporary);
        copied += "/copy";
        passed &= check(
            from_temporary.Str() == std::string(128, 'x') && copied.Str().ends_with("/copy"),
            "copied URLs must have independent storage"
        );
        mcr::Url moved(std::move(copied));
        mcr::Url assigned;
        assigned = moved;
        mcr::Url move_assigned;
        move_assigned = std::move(moved);
        passed &= check(
            assigned == move_assigned && assigned.Str() == std::string(128, 'x') + "/copy",
            "copy and move assignment must preserve the destination text"
        );

        auto converted{ static_cast<std::string>(from_string) };
        converted.clear();
        passed &= check(
            from_string == "https://example.test/api",
            "explicit string conversion must return an independent copy"
        );
        return passed;
    }

    auto check_operators() -> bool {
        bool passed{ true };
        mcr::Url const base{ "https://example.test" };
        std::string const suffix{ "/api" };
        mcr::Url const option_suffix{ suffix };
        passed &=
            check(base + "/api" == "https://example.test/api", "C string concatenation must preserve URL text");
        passed &= check(base + suffix == base + option_suffix, "string and same-type concatenation must agree");
        passed &= check(base == "https://example.test", "concatenation must not mutate its source");

        mcr::Url appended{ base };
        appended += "/api";
        appended += std::string{ "/v1" };
        appended += mcr::Url{ "/items" };
        passed &=
            check(appended == "https://example.test/api/v1/items", "all append overloads must append their text");
        mcr::Url repeated{ "ab" };
        repeated += repeated;
        passed &= check(repeated == "abab", "a holder must support appending itself");

        char equal_text[]{ "https://example.test" };
        passed &= check(
            base == equal_text && !(base != equal_text),
            "C string equality and inequality must compare contents from independent storage"
        );
        passed &= check(
            base != "https://different.test" && !(base == "https://different.test"),
            "different C strings must compare unequal"
        );
        passed &= check(
            base == std::string{ equal_text } && !(base != std::string{ equal_text }),
            "standard string comparisons must agree"
        );
        passed &= check(
            base == mcr::Url{ equal_text } && !(base != mcr::Url{ equal_text }),
            "same-type comparisons must agree"
        );
        passed &=
            check(base != suffix && base != option_suffix, "different strings and options must compare unequal");

        char const bytes[]{ 'a', '\0', 'b' };
        mcr::Url const binary(bytes, sizeof(bytes));
        mcr::Url const other_binary("a\0c", 3);
        passed &= check(
            binary != "a" && binary != other_binary && binary == std::string(bytes, sizeof(bytes)),
            "comparisons must include bytes after embedded nulls"
        );
        mcr::Url binary_appended{ binary };
        binary_appended += binary;
        passed &= check(
            binary_appended.Str() == std::string(bytes, sizeof(bytes)) + std::string(bytes, sizeof(bytes)),
            "holder append must preserve embedded nulls"
        );

        std::ostringstream stream;
        stream << base << '|' << binary;
        passed &= check(
            stream.str() == base.Str() + '|' + binary.Str(),
            "stream output must support chaining and preserve embedded nulls"
        );

        TestOption option{ "custom" };
        static_assert(std::is_same_v<decltype(option + "/suffix"), TestOption>);
        auto const combined{ option + "/suffix" };
        option += combined;
        passed &= check(
            combined == "custom/suffix" && option == "customcustom/suffix",
            "StringHolder must support other derived option types"
        );
        return passed;
    }

    auto check_headers() -> bool {
        bool passed{ true };
        mcr::CaseInsensitiveCompare const compare;
        static_assert(noexcept(compare(std::declval<std::string const&>(), std::declval<std::string const&>())));
        passed &= check(
            !compare("Content-Type", "content-type") && !compare("content-type", "Content-Type"),
            "header names differing only in case must be equivalent"
        );
        passed &= check(
            compare("", "a") && !compare("", "") && !compare("a", ""),
            "empty names must follow lexicographical ordering"
        );
        passed &= check(
            compare("a", "AA") && !compare("AA", "a") && compare("aB", "Ac"),
            "comparison must handle prefixes and differing suffixes"
        );
        passed &= check(
            compare(std::string("A\0b", 3), std::string("a\0c", 3)),
            "comparison must include bytes after embedded nulls"
        );
        std::string const high_byte(1, static_cast<char>(0xff));
        passed &= check(
            !compare(high_byte + 'A', high_byte + 'a') && !compare(high_byte + 'a', high_byte + 'A'),
            "case folding must safely accept bytes outside ASCII"
        );

        mcr::Header headers{
            { "Content-Type", "Application/JSON" },
            { "X-Trace", "Value" },
        };
        passed &= check(
            headers.at("content-type") == "Application/JSON" && headers.at("CONTENT-TYPE") == "Application/JSON",
            "header lookup must ignore name case and preserve value case"
        );
        auto const [entry, inserted]{ headers.emplace("CONTENT-type", "ignored") };
        passed &= check(
            !inserted && entry->second == "Application/JSON" && headers.size() == 2,
            "equivalent names must not create duplicate headers"
        );
        headers["content-TYPE"] = "text/plain";
        passed &= check(
            headers.size() == 2 && headers.at("Content-Type") == "text/plain",
            "subscript must update an equivalent existing key"
        );
        passed &= check(
            headers.find("CONTENT-TYPE")->first == "Content-Type",
            "updates must preserve the original key spelling"
        );
        headers["Accept"] = "";
        passed &= check(headers.at("ACCEPT").empty(), "headers must support empty values");
        std::string_view const bounded{ "!cOnTeNt-TyPe!" };
        passed &= check(headers.find(bounded.substr(1, bounded.size() - 2))->second == "text/plain" && headers.contains(std::string_view{ "ACCEPT" }), "transparent lookup must accept bounded string views without constructing a key");
        passed &= check(compare("A", "B") && !compare("A", "a") && !compare("a", "A") && compare("z", "\xC0") && compare("\xC0", "\xE0"), "ASCII folding must preserve unsigned high bytes independently of the C locale");
        passed &= check(
            headers.erase("x-TRACE") == 1 && !headers.contains("X-Trace"),
            "header erasure must ignore name case"
        );
        passed &=
            check(headers.begin()->first == "Accept", "headers must iterate in case-insensitive lexical order");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_construction() };
    passed &= check_operators();
    passed &= check_headers();
    if (!passed) {
        return 1;
    }
    std::println("test_types: ok");
    return 0;
}
