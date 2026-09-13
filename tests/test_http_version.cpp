/**
 * @file test_http_version.cpp
 * @brief Verify HTTP version availability, public option semantics, and cpr ordinal compatibility.
 */
#include <curl/curlver.h>

import std;
import mcr.http;

using Code = mcr::options::HttpVersionCode;

static_assert(std::is_same_v<std::underlying_type_t<Code>, std::uint8_t>);
static_assert(std::is_same_v<decltype(mcr::options::HttpVersion::code), Code>);
static_assert(std::is_nothrow_default_constructible_v<mcr::options::HttpVersion>);
static_assert(std::is_nothrow_constructible_v<mcr::options::HttpVersion, Code>);
static_assert(!std::is_convertible_v<Code, mcr::options::HttpVersion>);
static_assert(!std::is_convertible_v<mcr::options::HttpVersion, Code>);
static_assert(!std::is_constructible_v<mcr::options::HttpVersion, int>);
static_assert(!std::is_convertible_v<Code, long>);
static_assert(mcr::options::HttpVersion{}.code == Code::VERSION_NONE);
static_assert(mcr::options::HttpVersion{ Code::VERSION_1_1 }.code == Code::VERSION_1_1);

namespace {

    template <typename Enum>
    concept HasHttp2 = requires { Enum::VERSION_2_0; };
    template <typename Enum>
    concept HasHttp2Tls = requires { Enum::VERSION_2_0_TLS; };
    template <typename Enum>
    concept HasHttp2PriorKnowledge = requires { Enum::VERSION_2_0_PRIOR_KNOWLEDGE; };
    template <typename Enum>
    concept HasHttp3 = requires { Enum::VERSION_3_0; };
    template <typename Enum>
    concept HasHttp3Only = requires { Enum::VERSION_3_0_ONLY; };

    static_assert(HasHttp2<Code> == (LIBCURL_VERSION_NUM >= 0x072100));
    static_assert(HasHttp2Tls<Code> == (LIBCURL_VERSION_NUM >= 0x072F00));
    static_assert(HasHttp2PriorKnowledge<Code> == (LIBCURL_VERSION_NUM >= 0x073100));
    static_assert(HasHttp3<Code> == (LIBCURL_VERSION_NUM >= 0x074200));
    static_assert(HasHttp3Only<Code> == (LIBCURL_VERSION_NUM >= 0x075800));

    // cpr uses contiguous ordinals. In particular HTTP/3 is 6/7, not curl's 30/31.
    constexpr Code CODES[]{
        Code::VERSION_NONE,
        Code::VERSION_1_0,
        Code::VERSION_1_1,
#if LIBCURL_VERSION_NUM >= 0x072100
        Code::VERSION_2_0,
#endif
#if LIBCURL_VERSION_NUM >= 0x072F00
        Code::VERSION_2_0_TLS,
#endif
#if LIBCURL_VERSION_NUM >= 0x073100
        Code::VERSION_2_0_PRIOR_KNOWLEDGE,
#endif
#if LIBCURL_VERSION_NUM >= 0x074200
        Code::VERSION_3_0,
#endif
#if LIBCURL_VERSION_NUM >= 0x075800
        Code::VERSION_3_0_ONLY,
#endif
    };

    static_assert([] {
        for (std::size_t i{ 0 }; i < std::size(CODES); ++i) {
            if (std::to_underlying(CODES[i]) != i || mcr::options::HttpVersion{ CODES[i] }.code != CODES[i]) {
                return false;
            }
        }
        return true;
    }());

} // namespace

int main() {
    for (auto const code : CODES) {
        mcr::options::HttpVersion original{ code };
        mcr::options::HttpVersion copied{ original };
        original.code = static_cast<Code>(255);
        mcr::options::HttpVersion moved{ std::move(copied) };
        mcr::options::HttpVersion assigned;
        assigned = moved;
        mcr::options::HttpVersion move_assigned;
        move_assigned = std::move(assigned);
        if (move_assigned.code != code || original.code != static_cast<Code>(255)) {
            std::println("test_http_version: copies and public field updates must retain independent codes");
            return 1;
        }
    }
    mcr::options::HttpVersion const unnamed{ static_cast<Code>(255) };
    if (std::to_underlying(unnamed.code) != 255) {
        std::println("test_http_version: construction must preserve unnamed codes without validation, matching cpr");
        return 1;
    }
    std::println("test_http_version: ok");
    return 0;
}
