/**
 * @file test_fixture.cpp
 * @brief Verify independent socket initialization and reusable request barrier generations.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"
import std;
import mcr;

namespace {
    /**
     * @brief Report a fixture regression through the test program's failure result.
     */
    auto require(bool condition, std::string_view message) -> void {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    /**
     * @brief Run a bounded request without consulting proxy environment variables.
     */
    auto request(mcr::test::HttpServer const& server) -> mcr::Response {
        return mcr::Get(server.Url("/barrier"), mcr::options::Proxies{ { "http", "" }, { "no_proxy", "*" } }, mcr::options::Timeout{ 5000 }).value();
    }

    /**
     * @brief Complete two concurrent requests in exactly one barrier generation.
     */
    auto pair(mcr::test::HttpServer const& server) -> void {
        auto first = std::async(std::launch::async, [&] { return request(server); });
        auto second = std::async(std::launch::async, [&] { return request(server); });
        require(first.get().status_code == 200 && second.get().status_code == 200, "each fresh pair must complete together");
    }
} // namespace

/**
 * @brief Construct sockets before curl initialization, then exercise paired and unmatched requests.
 */
auto main() -> int {
    try {
        mcr::test::HttpServer server;
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            return 1;
        }
        bool passed{ true };
        try {
            pair(server);
            pair(server);
            require(request(server).status_code == 503, "an unmatched request must not reuse arrivals from earlier pairs");
            pair(server);
        } catch (std::exception const& error) {
            std::println("test_fixture: {}", error.what());
            passed = false;
        }
        try {
            server.Check();
        } catch (std::exception const& error) {
            std::println("test_fixture worker: {}", error.what());
            passed = false;
        }
        curl_global_cleanup();
        return passed ? 0 : 1;
    } catch (std::exception const& error) {
        std::println("test_fixture: {}", error.what());
        return 1;
    }
}
