/**
 * @file test_coro_exit.cpp
 * @brief Verify process exit on the continuation thread without joining or destroying live workers.
 */
#include <curl/curl.h>

import std;
import mcr;

namespace {
    /**
     * @brief Begin static destruction while running accepted continuation work.
     */
    auto exit_on_continuation() -> mcr::Task<void> {
        (co_await mcr::Coro::Schedule()).value();
        std::exit(mcr::Coro::IsRuntimeThread() ? 0 : 2);
    }
} // namespace

/**
 * @brief Fail unless the continuation exits successfully before sync_wait can return.
 */
auto main() -> int {
    if (mcr::Coro::IsRuntimeThread() || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    mcr::sync_wait(exit_on_continuation());
    return 1;
}
