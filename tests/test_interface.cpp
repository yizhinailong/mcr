/**
 * @file test_interface.cpp
 * @brief Verify interface selector ownership and inherited string operations through import mcr.
 */
import std;
import mcr;

static_assert(std::derived_from<mcr::options::Interface, mcr::StringHolder<mcr::options::Interface>>);
static_assert(std::is_convertible_v<std::string, mcr::options::Interface>);
static_assert(std::is_convertible_v<std::string_view, mcr::options::Interface>);
static_assert(std::is_convertible_v<char const*, mcr::options::Interface>);
static_assert(!std::is_convertible_v<mcr::options::Interface, std::string>);
static_assert(!std::is_convertible_v<mcr::Url, mcr::options::Interface>);
static_assert(std::is_nothrow_move_constructible_v<mcr::options::Interface>);
static_assert(std::is_nothrow_move_assignable_v<mcr::options::Interface>);
static_assert(std::is_same_v<decltype(std::declval<mcr::options::Interface const&>() + "0"), mcr::options::Interface>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_interface: {}", message);
        }
        return condition;
    }

    auto check_construction() -> bool {
        mcr::options::Interface const empty;
        bool passed{ check(empty.Str().empty() && empty.CStr()[0] == '\0' && mcr::options::Interface{ "" } == empty && mcr::options::Interface{ std::string_view{} } == empty && mcr::options::Interface(std::initializer_list<std::string>{}) == empty, "default, empty C string, view, and fragments must preserve cpr's unspecified interface option") };

        std::string source{ "eth0/suffix" };
        mcr::options::Interface const from_string = source;
        mcr::options::Interface const from_view = std::string_view{ source }.substr(0, 4);
        mcr::options::Interface const from_c_string = source.c_str();
        source.assign("changed");
        passed &= check(from_string == "eth0/suffix" && from_c_string == from_string && from_view == "eth0", "all text constructors must own their input and respect view boundaries");
        mcr::options::Interface const from_temporary(std::string(128, 'x'));
        passed &= check(from_temporary.Str() == std::string(128, 'x'), "a moved temporary string must remain available after its source is destroyed");

        char const bytes[]{ 'e', 't', 'h', '\0', '0' };
        mcr::options::Interface const from_bytes(bytes, sizeof(bytes));
        mcr::options::Interface const binary_view(std::string_view{ bytes, sizeof(bytes) });
        passed &= check(from_bytes.Str() == std::string(bytes, sizeof(bytes)) && binary_view == from_bytes && from_bytes.Data()[4] == '0' && from_bytes.CStr()[sizeof(bytes)] == '\0' && mcr::options::Interface(bytes, 0) == empty, "byte ranges must retain nulls and support nonterminated and zero-length input");
        mcr::options::Interface const fragments{ "if!", "eth", "0" };
        mcr::options::Interface const binary_fragments{ std::string(bytes, sizeof(bytes)), "tail" };
        passed &= check(fragments == "if!eth0" && binary_fragments.Str() == from_bytes.Str() + "tail", "fragment construction must join all bytes without separators");
        passed &= check(mcr::options::Interface{ " selector that need not exist " } == " selector that need not exist ", "constructing a selector must not trim, validate, or resolve it");

        mcr::options::Interface copied{ from_view };
        copied += "-copy";
        mcr::options::Interface moved{ std::move(copied) };
        mcr::options::Interface assigned;
        assigned = moved;
        mcr::options::Interface move_assigned;
        move_assigned = std::move(moved);
        assigned += "-changed";
        passed &= check(from_view == "eth0" && move_assigned == "eth0-copy" && assigned == "eth0-copy-changed", "copy and move construction and assignment must preserve independent destination values");
        return passed;
    }

    auto check_operations() -> bool {
        mcr::options::Interface const prefix{ "eth" };
        std::string const suffix{ "0" };
        mcr::options::Interface const option_suffix{ suffix };
        bool passed{ check(prefix + "0" == "eth0" && prefix + suffix == "eth0" && prefix + option_suffix == "eth0" && prefix == "eth", "inherited concatenation must return Interface values without changing the source") };
        mcr::options::Interface appended{ "if!" };
        appended += "et";
        appended += std::string{ "h" };
        appended += option_suffix;
        passed &= check(appended == "if!eth0", "all inherited append overloads must work with Interface");
        char equal_text[]{ "if!eth0" };
        passed &= check(appended == equal_text && !(appended != equal_text) && appended == std::string{ equal_text } && appended == mcr::options::Interface{ equal_text } && appended != "eth0", "inherited comparisons must compare selector contents");
        auto converted{ static_cast<std::string>(appended) };
        converted.clear();
        passed &= check(appended == "if!eth0", "explicit string conversion must return an independent copy");
        mcr::options::Interface const binary("eth\0tail", 8);
        std::ostringstream stream;
        stream << appended << '|' << binary;
        passed &= check(stream.str() == appended.Str() + '|' + binary.Str(), "inherited stream output must preserve selector bytes, including embedded nulls");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_construction() };
    passed &= check_operations();
    if (!passed) {
        return 1;
    }
    std::println("test_interface: ok");
    return 0;
}
