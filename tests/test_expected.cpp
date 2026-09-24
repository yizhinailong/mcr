/**
 * @file test_expected.cpp
 * @brief Verify explicit failures across synchronous, asynchronous, callback, and coroutine requests.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"

import std;
import mcr;

namespace {
    /**
     * @brief Fail a test while preserving an actionable diagnostic.
     */
    auto require(bool condition, std::string_view message) -> void {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    /**
     * @brief Check failure propagation without consuming an invalid expected value.
     */
    auto check_requests() -> void {
        mcr::test::HttpServer           server;
        mcr::options::HttpVersion const invalid{ static_cast<mcr::options::HttpVersionCode>(255) };
        mcr::options::Proxies const     proxies{
            {     "http",  "" },
            { "no_proxy", "*" }
        };
        auto failed = mcr::Get(server.Url(), proxies, invalid);
        require(!failed && failed.error().code == mcr::ErrorCode::BAD_FUNCTION_ARGUMENT, "configuration failure must return its error code");

        auto submitted = mcr::GetAsync(server.Url(), proxies, invalid);
        require(submitted.has_value(), "an invalid option is checked by the submitted task");
        auto asynchronous = submitted->Get();
        require(!asynchronous && asynchronous.error().code == failed.error().code, "async execution must preserve configuration errors");

        auto callback = mcr::GetCallback([](mcr::Result<mcr::Response> response) {
            return response ? mcr::ErrorCode::OK : response.error().code;
        },
                                         server.Url(),
                                         proxies,
                                         invalid);
        require(callback && callback->Get() == failed.error().code, "continuations must receive failed request results");

        auto coroutine = mcr::sync_wait(mcr::GetCoro(server.Url(), proxies, invalid));
        require(!coroutine && coroutine.error().code == failed.error().code, "coroutines must return configuration failures");
        auto batch = mcr::MultiGet(std::tuple{ server.Url(), proxies }, std::tuple{ server.Url(), proxies, invalid });
        require(!batch && batch.error().code == failed.error().code, "a batch configuration error must stop the batch before transfers");
        require(server.Connections() == 0, "failed configuration must never reach the server");

        auto created = mcr::Session::Create();
        require(created.has_value(), "session creation must return shared ownership");
        auto& session = **created;
        require(!session.SetHttpVersion(invalid), "setters must report invalid input");
        session.SetHttpVersion(mcr::options::HttpVersion{}).value();
        session.SetProxies(proxies).value();
        session.SetUrl(server.Url("/error")).value();
        auto response = session.Get();
        require(response && !response->error && response->status_code == 404, "HTTP errors must retain their complete responses");
        session.SetUrl(mcr::Url{ "invalid-scheme://localhost/" }).value();
        response = session.Get();
        require(response && response->error.code == mcr::ErrorCode::UNSUPPORTED_PROTOCOL, "transport errors must remain available with response metadata");
        session.SetUrl(server.Url("/hello")).value();
        response = session.Get();
        require(response && !response->error && response->status_code == 200, "sessions must recover after configuration and transfer failures");

        auto snapshot = mcr::Response::FromCurl(nullptr, {}, {});
        require(!snapshot && snapshot.error().code == mcr::ErrorCode::BAD_FUNCTION_ARGUMENT, "response factories must reject invalid curl holders");
        server.Check();
    }
} // namespace

/**
 * @brief Run integration checks and shut down both HTTP runtimes before curl cleanup.
 */
int main() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        mcr::Async::Startup(2, 4).value();
        check_requests();
    } catch (std::exception const& error) {
        std::println("test_expected: {}", error.what());
        passed = false;
    }
    mcr::Coro::Cleanup().value();
    mcr::Async::Cleanup();
    curl_global_cleanup();
    return passed ? 0 : 1;
}
