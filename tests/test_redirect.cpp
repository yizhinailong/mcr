/**
 * @file test_redirect.cpp
 * @brief Verify redirect defaults, overloads, and flag semantics through the HTTP options module.
 */
import std;
import mcr.http;

using Flags = mcr::options::PostRedirectFlags;

static_assert(std::is_same_v<std::underlying_type_t<Flags>, std::uint8_t>);
static_assert(std::is_same_v<decltype(mcr::options::Redirect::maximum), long>);
static_assert(std::is_default_constructible_v<mcr::options::Redirect>);
static_assert(!std::is_convertible_v<long, mcr::options::Redirect>);
static_assert(!std::is_convertible_v<bool, mcr::options::Redirect>);
static_assert(!std::is_convertible_v<Flags, mcr::options::Redirect>);
static_assert(!std::is_constructible_v<mcr::options::Redirect, int>); // long and bool overloads are ambiguous, as in cpr.

static_assert(std::to_underlying(Flags::POST_301) == 1);
static_assert(std::to_underlying(Flags::POST_302) == 2);
static_assert(std::to_underlying(Flags::POST_303) == 4);
static_assert(std::to_underlying(Flags::POST_ALL) == 7);
static_assert(std::to_underlying(Flags::NONE) == 0);
static_assert((Flags::POST_301 | Flags::POST_302 | Flags::POST_303) == Flags::POST_ALL);
static_assert((Flags::POST_ALL & Flags::POST_302) == Flags::POST_302);
static_assert((Flags::POST_301 & Flags::POST_303) == Flags::NONE);
static_assert((Flags::POST_ALL ^ Flags::POST_302) == (Flags::POST_301 | Flags::POST_303));
static_assert((Flags::POST_ALL ^ Flags::POST_ALL) == Flags::NONE);
static_assert(std::to_underlying(~Flags::NONE) == 0xFF);
static_assert(std::to_underlying(~Flags::POST_ALL) == 0xF8);
static_assert(~~Flags::POST_301 == Flags::POST_301);
static_assert(!mcr::options::any(Flags::NONE));
static_assert(mcr::options::any(Flags::POST_301));
static_assert(mcr::options::any(static_cast<Flags>(0x80)));
static_assert(std::to_underlying(static_cast<Flags>(0x80) | Flags::POST_301) == 0x81);
static_assert((static_cast<Flags>(0x81) & static_cast<Flags>(0x80)) == static_cast<Flags>(0x80));
static_assert((static_cast<Flags>(0x81) ^ Flags::POST_301) == static_cast<Flags>(0x80));

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_redirect: {}", message);
        }
        return condition;
    }

    auto matches(mcr::options::Redirect const& option, long maximum, bool follow, bool cont_send_cred, Flags post_flags) -> bool {
        return option.maximum == maximum && option.follow == follow && option.cont_send_cred == cont_send_cred && option.post_flags == post_flags;
    }

    auto check_construction() -> bool {
        bool passed{ check(matches(mcr::options::Redirect{}, 50L, true, false, Flags::NONE), "default options must match cpr") };
        for (long const maximum : { -1L, 0L, 1L, (std::numeric_limits<long>::min)(), (std::numeric_limits<long>::max)() }) {
            passed &= check(matches(mcr::options::Redirect{ maximum }, maximum, true, false, Flags::NONE), "limit construction must preserve long values and leave other defaults intact");
        }
        for (bool const follow : { false, true }) {
            passed &= check(matches(mcr::options::Redirect{ follow }, 50L, follow, false, Flags::NONE), "bool construction must set follow without changing the limit");
            for (bool const cont_send_cred : { false, true }) {
                passed &= check(matches(mcr::options::Redirect{ follow, cont_send_cred }, 50L, follow, cont_send_cred, Flags::NONE), "two-bool construction must preserve both independent settings");
                passed &= check(matches(mcr::options::Redirect{ -1L, follow, cont_send_cred, Flags::POST_ALL }, -1L, follow, cont_send_cred, Flags::POST_ALL), "full construction must store all four settings");
            }
        }
        for (Flags const flags : { Flags::NONE, Flags::POST_301, Flags::POST_302, Flags::POST_303, Flags::POST_ALL, Flags::POST_301 | Flags::POST_303, static_cast<Flags>(0x80) }) {
            passed &= check(matches(mcr::options::Redirect{ flags }, 50L, true, false, flags), "flag construction must preserve named and unnamed bits with other defaults intact");
        }

        mcr::options::Redirect option{};
        option.maximum        = 0L;
        option.follow         = false;
        option.cont_send_cred = true;
        option.post_flags     = Flags::POST_302;
        mcr::options::Redirect copied{ option };
        option   = mcr::options::Redirect{};
        passed &= check(matches(copied, 0L, false, true, Flags::POST_302), "copy construction must preserve public updates independently");
        copied  = option;
        passed &= check(matches(copied, 50L, true, false, Flags::NONE), "assignment must replace all four options");
        return passed;
    }

    auto check_flag_updates() -> bool {
        Flags flags{ Flags::POST_301 };
        bool  passed{ check(&(flags |= Flags::POST_302) == &flags && flags == (Flags::POST_301 | Flags::POST_302), "operator|= must combine bits and return its left operand by reference") };
        passed                     &= check(&(flags &= ~Flags::POST_301) == &flags && flags == Flags::POST_302, "operator&= must allow clearing a selected bit and return its left operand by reference");
        passed                     &= check(&(flags ^= Flags::POST_ALL) == &flags && flags == (Flags::POST_301 | Flags::POST_303), "operator^= must toggle selected bits and return its left operand by reference");
        (flags |= Flags::POST_302) &= Flags::POST_303;
        passed                     &= check(flags == Flags::POST_303 && any(flags), "compound assignments must chain and any must be found by argument-dependent lookup");
        flags                      ^= flags;
        passed                     &= check(!any(flags), "self xor must clear every bit");
        flags                       = static_cast<Flags>(0x80);
        flags                      |= Flags::POST_301;
        flags                      &= ~Flags::POST_301;
        flags                      ^= Flags::POST_302;
        passed                     &= check(std::to_underlying(flags) == 0x82, "compound operations must preserve unnamed bits");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_construction() };
    passed &= check_flag_updates();
    if (!passed) {
        return 1;
    }
    std::println("test_redirect: ok");
    return 0;
}
