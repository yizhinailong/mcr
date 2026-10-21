/**
 * @file test_coro_exit_io.cpp
 * @brief Verify process exit inside an I/O callback without waiting for that same I/O thread.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"

import std;
import mcr;

/**
 * @brief Begin static destruction from a real curl response callback.
 */
auto main() -> int {
    if (mcr::Coro::IsRuntimeThread() || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    mcr::test::HttpServer server;
    auto request{ mcr::GetCoro(
        server.Url("/hello"),
        mcr::options::Proxies{ { "http", "" }, { "no_proxy", "*" } },
        mcr::options::Timeout{ 3000 },
        mcr::HeaderCallback{ [](auto, auto) -> bool {
            std::exit(mcr::Coro::IsRuntimeThread() ? 0 : 2);
        } }
    ) };
    (void)mcr::sync_wait(std::move(request));
    server.Check();
    return 1;
}
