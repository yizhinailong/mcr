/**
 * @file test_accept_encoding.cpp
 * @brief Verify encoding names, deduplication, empty sets, and disabled validation through the HTTP options module.
 */
import std;
import mcr.http;

using Methods = mcr::options::AcceptEncodingMethods;

static_assert(std::is_same_v<std::underlying_type_t<Methods>, std::uint8_t>);
static_assert(std::is_convertible_v<std::initializer_list<Methods>, mcr::options::AcceptEncoding>);
static_assert(std::is_convertible_v<std::initializer_list<std::string>, mcr::options::AcceptEncoding>);
static_assert(noexcept(std::declval<mcr::options::AcceptEncoding const&>().Empty()));
static_assert(std::is_same_v<decltype(std::declval<mcr::options::AcceptEncoding const&>().GetString()), std::string>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_accept_encoding: {}", message);
        }
        return condition;
    }

    auto has_names(mcr::options::AcceptEncoding const& option, std::vector<std::string> expected) -> bool {
        std::string const        text{ option.GetString() };
        std::vector<std::string> actual;
        for (auto const part : text | std::views::split(std::string_view{ ", " })) {
            actual.emplace_back(part.begin(), part.end());
        }
        std::ranges::sort(actual);
        std::ranges::sort(expected);
        return actual == expected;
    }

    auto check_empty_and_names() -> bool {
        bool passed{ true };
        for (mcr::options::AcceptEncoding const& option : { mcr::options::AcceptEncoding{}, mcr::options::AcceptEncoding(std::initializer_list<Methods>{}), mcr::options::AcceptEncoding(std::initializer_list<std::string>{}) }) {
            passed &= check(option.Empty() && option.GetString().empty() && !option.Disabled(), "default and empty lists must produce a safe, empty option without disabling encodings");
        }

        std::array<std::string_view, 5> const names{ "identity", "deflate", "zlib", "gzip", "disabled" };
        passed &= check(mcr::options::ACCEPT_ENCODING_METHODS_STRING_MAP.size() == names.size(), "the exported map must contain all five built-in names");
        for (std::size_t index{ 0 }; index < names.size(); ++index) {
            auto const                method{ static_cast<Methods>(index) };
            mcr::options::AcceptEncoding const option{ method, method };
            passed &= check(!option.Empty() && option.GetString() == names[index], "enum values must retain cpr's numeric mapping and deduplicate repeated methods");
            passed &= check(option.Disabled() == (method == Methods::disabled), "only the disabled enum must disable encoding handling");
        }
        mcr::options::AcceptEncoding const built_in{ Methods::deflate, Methods::gzip, Methods::zlib, Methods::gzip };
        passed &= check(has_names(built_in, { "deflate", "gzip", "zlib" }) && !built_in.Disabled(), "enum lists must serialize unique names with comma-space separators in any order");
        mcr::options::AcceptEncoding const custom{ "gzip", "br", "GZIP", "gzip", "x-custom;q=0.5" };
        passed &= check(has_names(custom, { "gzip", "br", "GZIP", "x-custom;q=0.5" }) && !custom.Disabled(), "custom names must be case-sensitive, deduplicated, and preserved verbatim");
        mcr::options::AcceptEncoding const empty_name{ std::string{} };
        passed &= check(!empty_name.Empty() && empty_name.GetString().empty() && !empty_name.Disabled(), "one empty name must remain distinct from an empty set");
        mcr::options::AcceptEncoding const with_empty_name{ "", "gzip" };
        passed &= check(has_names(with_empty_name, { "", "gzip" }), "an empty name must retain its element separator");

        std::string         name{ "br" };
        mcr::options::AcceptEncoding owned{ name };
        name    = "gzip";
        passed &= check(owned.GetString() == "br", "custom names must be copied into owned storage");
        mcr::options::AcceptEncoding copied{ owned };
        owned = mcr::options::AcceptEncoding{ "deflate" };
        mcr::options::AcceptEncoding const moved{ std::move(copied) };
        passed &= check(moved.GetString() == "br" && owned.GetString() == "deflate", "copying, moving, and assignment must preserve independent options");
        std::string const binary_name{ "x\0y", 3 };
        passed &= check(mcr::options::AcceptEncoding{ binary_name }.GetString() == binary_name, "custom names must retain embedded null bytes");
        return passed;
    }

    auto check_validation() -> bool {
        mcr::options::AcceptEncoding const disabled{ "disabled", "disabled" };
        bool                      passed{ check(disabled.Disabled() && disabled.GetString() == "disabled", "duplicate disabled strings must be accepted as one sentinel") };
        mcr::options::AcceptEncoding const distinct{ "Disabled", " disabled", "disabled " };
        passed &= check(!distinct.Disabled(), "disabled detection must not normalize spelling or whitespace");

        for (mcr::options::AcceptEncoding const& option : {
                 mcr::options::AcceptEncoding{ Methods::disabled, Methods::gzip },
                 mcr::options::AcceptEncoding{        "disabled",        "gzip" },
                 mcr::options::AcceptEncoding{        "disabled",            "" }
        }) {
            std::string const text{ option.GetString() };
            passed &= check(!option.Empty(), "mixed disabled options must remain constructible and serializable before validation");
            try {
                (void)option.Disabled();
                passed &= check(false, "Disabled must reject a sentinel combined with another distinct name");
            } catch (std::invalid_argument const& error) {
                passed &= check(std::string_view{ error.what() } == "AcceptEncoding does not accept any other values if 'disabled' is present. You set the following encodings: " + text, "validation diagnostics must retain cpr's message and list the stored encodings");
            }
            passed &= check(option.GetString() == text, "validation failure must leave the stored names unchanged");
        }

        try {
            mcr::options::AcceptEncoding const invalid{ Methods::gzip, static_cast<Methods>(255) };
            passed &= check(false, "an unrecognized enum value must throw during construction");
        } catch (std::out_of_range const&) {
        }
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_empty_and_names() };
    passed &= check_validation();
    if (!passed) {
        return 1;
    }
    std::println("test_accept_encoding: ok");
    return 0;
}
