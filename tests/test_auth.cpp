/**
 * @file test_auth.cpp
 * @brief Verify authentication modes, exact credential bytes, view lengths, and ownership.
 */
import std;
import mcr.auth;

using Mode = mcr::options::AuthMode;

static_assert(std::is_same_v<std::underlying_type_t<Mode>, std::uint8_t>);
static_assert(!std::is_default_constructible_v<mcr::options::Authentication>);
static_assert(!std::is_constructible_v<mcr::options::Authentication, std::string_view, std::string_view>);
static_assert(std::is_constructible_v<mcr::options::Authentication, std::string_view, std::string_view, Mode>);
static_assert(std::is_same_v<decltype(std::declval<mcr::options::Authentication const&>().GetAuthString()), char const*>);
static_assert(std::is_same_v<decltype(std::declval<mcr::options::Authentication const&>().GetAuthMode()), Mode>);
static_assert(noexcept(std::declval<mcr::options::Authentication const&>().GetAuthString()));
static_assert(noexcept(std::declval<mcr::options::Authentication const&>().GetAuthMode()));
static_assert(std::is_nothrow_move_constructible_v<mcr::options::Authentication>);
static_assert(std::is_nothrow_move_assignable_v<mcr::options::Authentication>);
static_assert(std::to_underlying(Mode::BASIC) == 0);
static_assert(std::to_underlying(Mode::DIGEST) == 1);
static_assert(std::to_underlying(Mode::NTLM) == 2);
static_assert(std::to_underlying(Mode::NEGOTIATE) == 3);
static_assert(std::to_underlying(Mode::ANY) == 4);
static_assert(std::to_underlying(Mode::ANYSAFE) == 5);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_auth: {}", message);
        }
        return condition;
    }

    auto matches(mcr::options::Authentication const& auth, std::string_view expected, Mode mode) -> bool {
        auto const* bytes{ auth.GetAuthString() };
        return bytes && std::string_view{ bytes, expected.size() } == expected && bytes[expected.size()] == '\0' && auth.GetAuthMode() == mode;
    }

    auto check_construction() -> bool {
        bool passed{ true };
        for (Mode const mode : { Mode::BASIC, Mode::DIGEST, Mode::NTLM, Mode::NEGOTIATE, Mode::ANY, Mode::ANYSAFE }) {
            mcr::options::Authentication const auth  = { "user", "password", mode };
            passed                         &= check(matches(auth, "user:password", mode), "every mode must retain the same unencoded credentials and its own policy");
        }
        passed &= check(matches(mcr::options::Authentication{ {}, {}, Mode::BASIC }, ":", Mode::BASIC), "empty views must still produce the colon separator");
        passed &= check(matches(mcr::options::Authentication{ {}, "password", Mode::NTLM }, ":password", Mode::NTLM), "an empty username must be preserved");
        passed &= check(matches(mcr::options::Authentication{ "user", {}, Mode::NEGOTIATE }, "user:", Mode::NEGOTIATE), "an empty password must be preserved");
        passed &= check(matches(mcr::options::Authentication{ " user:name ", "p:a ss+%&", Mode::DIGEST }, " user:name :p:a ss+%&", Mode::DIGEST), "colons, whitespace, and reserved bytes must remain unescaped");

        std::array<char, 5> const username{ 'u', 's', 'e', 'r', 'x' };
        std::array<char, 5> const password{ 'p', 'a', 's', 's', 'x' };
        mcr::options::Authentication const bounded{
            { username.data(), 4 },
            { password.data(), 4 },
            Mode::BASIC
        };
        passed &= check(matches(bounded, "user:pass", Mode::BASIC), "input views must respect their lengths without requiring null terminators");
        passed &= check(matches(mcr::options::Authentication{
                                    { username.data(), 0 },
                                    { password.data(), 0 },
                                    Mode::BASIC
        },
                                ":",
                                Mode::BASIC),
                        "zero-length views must not fall back to reading input strings");
        std::string const utf8{ "\xE4\xB8\xAD" };
        passed &= check(matches(mcr::options::Authentication{ utf8, utf8, Mode::ANY }, utf8 + ":" + utf8, Mode::ANY), "UTF-8 credentials must retain their original bytes");

        std::string const         binary_user{ "u\0x", 3 };
        std::string const         binary_password{ "p\0q", 3 };
        mcr::options::Authentication const binary{ binary_user, binary_password, Mode::BASIC };
        passed &= check(matches(binary, binary_user + ":" + binary_password, Mode::BASIC), "owned credentials must preserve all embedded null bytes");
        passed &= check(std::string_view{ binary.GetAuthString() } == "u", "C-string consumers must observe only the prefix before an embedded null");
        auto const unnamed{ static_cast<Mode>(255) };
        passed &= check(matches(mcr::options::Authentication{ "user", "pass", unnamed }, "user:pass", unnamed), "unnamed modes must be retained without validation, as in cpr");
        return passed;
    }

    auto check_ownership() -> bool {
        bool passed{ true };
        for (std::size_t const length : { 1U, 512U }) {
            std::string         username(length, 'u');
            std::string         password(length, 'p');
            std::string const   expected{ username + ":" + password };
            mcr::options::Authentication original{ username, password, Mode::ANYSAFE };
            username.assign(length, 'x');
            password.assign(length, 'y');
            passed &= check(matches(original, expected, Mode::ANYSAFE), "both short and heap-backed credentials must own their input views");

            mcr::options::Authentication copied{ original };
            original  = mcr::options::Authentication{ "other", "value", Mode::BASIC };
            passed   &= check(matches(copied, expected, Mode::ANYSAFE) && matches(original, "other:value", Mode::BASIC), "copy construction must retain independent credential storage and mode");
            mcr::options::Authentication moved{ std::move(copied) };
            passed   &= check(matches(moved, expected, Mode::ANYSAFE) && copied.GetAuthString() != nullptr, "move construction must preserve credentials and leave the source queryable");
            copied     = mcr::options::Authentication{ "reused", "source", Mode::DIGEST };
            original  = moved;
            moved     = std::move(copied);
            passed   &= check(matches(original, expected, Mode::ANYSAFE) && matches(moved, "reused:source", Mode::DIGEST), "copy and move assignment must replace both credential bytes and mode");
        }
        auto retained = [] {
            std::string username{ "temporary user" };
            std::string password{ "temporary password" };
            return mcr::options::Authentication{ username, password, Mode::BASIC };
        }();
        passed &= check(matches(retained, "temporary user:temporary password", Mode::BASIC), "credentials must remain valid after the input strings are destroyed");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_construction() };
    passed &= check_ownership();
    if (!passed) {
        return 1;
    }
    std::println("test_auth: ok");
    return 0;
}
