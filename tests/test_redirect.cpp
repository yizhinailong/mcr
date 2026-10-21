/**
 * @file test_redirect.cpp
 * @brief Verify redirect defaults, overloads, and flag semantics through the HTTP options module.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"

import std;
import mcr;

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
static_assert(std::to_underlying(Flags::POST_301) == CURL_REDIR_POST_301);
static_assert(std::to_underlying(Flags::POST_302) == CURL_REDIR_POST_302);
static_assert(std::to_underlying(Flags::POST_303) == CURL_REDIR_POST_303);
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

    auto matches(
        mcr::options::Redirect const& option,
        long maximum,
        bool follow,
        bool cont_send_cred,
        Flags post_flags
    ) -> bool {
        return option.maximum == maximum && option.follow == follow && option.cont_send_cred == cont_send_cred && option.post_flags == post_flags;
    }

    auto check_construction() -> bool {
        // cpr's include/cpr/redirect.h initializes post_flags to NONE, matching libcurl.
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
        option.maximum = 0L;
        option.follow = false;
        option.cont_send_cred = true;
        option.post_flags = Flags::POST_302;
        mcr::options::Redirect copied{ option };
        option = mcr::options::Redirect{};
        passed &= check(matches(copied, 0L, false, true, Flags::POST_302), "copy construction must preserve public updates independently");
        copied = option;
        passed &= check(matches(copied, 50L, true, false, Flags::NONE), "assignment must replace all four options");
        return passed;
    }

    auto check_flag_updates() -> bool {
        Flags flags{ Flags::POST_301 };
        bool passed{ check(&(flags |= Flags::POST_302) == &flags && flags == (Flags::POST_301 | Flags::POST_302), "operator|= must combine bits and return its left operand by reference") };
        passed &= check(&(flags &= ~Flags::POST_301) == &flags && flags == Flags::POST_302, "operator&= must allow clearing a selected bit and return its left operand by reference");
        passed &= check(&(flags ^= Flags::POST_ALL) == &flags && flags == (Flags::POST_301 | Flags::POST_303), "operator^= must toggle selected bits and return its left operand by reference");
        (flags |= Flags::POST_302) &= Flags::POST_303;
        passed &= check(flags == Flags::POST_303 && any(flags), "compound assignments must chain and any must be found by argument-dependent lookup");
        flags ^= flags;
        passed &= check(!any(flags), "self xor must clear every bit");
        flags = static_cast<Flags>(0x80);
        flags |= Flags::POST_301;
        flags &= ~Flags::POST_301;
        flags ^= Flags::POST_302;
        passed &= check(std::to_underlying(flags) == 0x82, "compound operations must preserve unnamed bits");
        return passed;
    }

    /**
     * @brief Verify every POST preservation mask through real 301, 302, and 303 responses.
     */
    auto check_post_redirects(mcr::test::HttpServer const& server) -> bool {
        using namespace std::chrono_literals;
        std::string const body{ "post\0bytes", 10 };
        bool passed{ true };
        for (int index{}; index < 3; ++index) {
            auto session = mcr::Session::Create().value();
            session->SetUrl(server.Url(std::format("/redirect/{}", 301 + index))).value();
            session->SetProxies({ { "http", "" }, { "no_proxy", "*" } }).value();
            session->SetTimeout(mcr::options::Timeout{ 3s }).value();
            session->SetBody(mcr::Body{ body }).value();
            auto const default_response = session->Post().value();
            passed &= check(!default_response.error && default_response.header.at("X-Method") == "GET" && default_response.text.empty(), "the default NONE policy must switch POST to GET and discard its body for 301/302/303");
            for (unsigned mask{}; mask <= 8; ++mask) {
                // Include an unknown bit to verify it does not accidentally enable POST preservation.
                auto const flags = static_cast<Flags>(mask == 8 ? 0x80 : mask);
                session->SetRedirect(mcr::options::Redirect{ flags }).value();
                auto const response = session->Post().value();
                bool const preserve = (std::to_underlying(flags) & (1u << index)) != 0;
                passed &= check(!response.error && response.status_code == 200 && response.redirect_count == 1 && response.header.at("X-Method") == (preserve ? "POST" : "GET") && response.text == (preserve ? body : ""), "POSTREDIR must map each flag independently and preserve exactly the requested methods and bodies");
            }
        }
        return passed;
    }

    /**
     * @brief Forward credentials to another hostname only with explicit opt-in.
     */
    auto check_cross_host_credentials(mcr::test::HttpServer const& origin) -> bool {
        using namespace std::chrono_literals;
        mcr::test::HttpServer destination;
        auto url = destination.Url("/echo").Str();
        url.replace(url.find("127.0.0.1"), std::string_view{ "127.0.0.1" }.size(), "localhost");
        auto session = mcr::Session::Create().value();
        session->SetProxies({ { "http", "" }, { "no_proxy", "*" } }).value();
        session->SetTimeout(mcr::options::Timeout{ 3s }).value();
        session->SetAuth({ "user", "password", mcr::options::AuthMode::BASIC }).value();
        session->SetUrl(origin.Url("/echo")).value();
        bool passed{ check(session->Get().value().header.at("X-Request-Authorization") == "Basic dXNlcjpwYXNzd29yZA==", "credentials must be sent to the original host") };
        session->SetUrl(origin.Url("/redirect/302")).value();
        session->SetHeader({ { "X-Redirect-To", url } }).value();
        for (bool const forward : { false, true, false }) {
            session->SetRedirect(mcr::options::Redirect{ true, forward }).value();
            auto const response = session->Get().value();
            passed &= check(!response.error && response.status_code == 200 && response.url.Str() == url && response.header.at("X-Request-Authorization") == (forward ? "Basic dXNlcjpwYXNzd29yZA==" : ""), "cross-host redirects must send credentials only while cont_send_cred is enabled");
        }
        destination.Check();
        return passed;
    }
} // namespace

int main() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ check_construction() };
    passed &= check_flag_updates();
    try {
        mcr::test::HttpServer server;
        passed &= check_post_redirects(server);
        passed &= check_cross_host_credentials(server);
        server.Check();
    } catch (std::exception const& error) {
        passed = check(false, error.what());
    }
    curl_global_cleanup();
    if (!passed) {
        return 1;
    }
    std::println("test_redirect: ok");
    return 0;
}
