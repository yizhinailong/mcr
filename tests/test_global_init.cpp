/**
 * @file test_global_init.cpp
 * @brief Verify scoped curl initialization and ordered shutdown of process-wide runtimes.
 */
#include "fixtures/http_server.hpp"

import std;
import mcr;

namespace {
    /**
     * @brief Fail the test with a message when a condition does not hold.
     * @param condition Checked condition.
     * @param message Failure description.
     */
    auto require(bool condition, std::string_view message) -> void {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    /**
     * @brief Request the fixture from the coroutine runtime.
     * @param url Fixture URL.
     * @return Response or request preparation error.
     */
    auto fetch(mcr::Url url) -> mcr::Task<mcr::Result<mcr::Response>> {
        co_return co_await mcr::GetCoro(std::move(url));
    }
} // namespace

/**
 * @brief Use every runtime under nested guards, then check final cleanup is permanent.
 */
auto main() -> int {
    try {
        mcr::test::HttpServer server;
        {
            auto outer = mcr::GlobalInit::Create();
            require(outer.has_value(), "first guard must initialize curl");
            {
                auto inner = mcr::GlobalInit::Create();
                require(inner.has_value(), "nested guard must succeed");
                auto moved = std::move(*inner);
            }

            auto response = mcr::Get(server.Url());
            require(response && !response->error && response->status_code == 200, "sync request must succeed under the guard");

            mcr::Async::Startup(1, 2).value();
            auto async_response = mcr::async([url = server.Url()] { return mcr::Get(url); }).value().Get();
            require(async_response && async_response->status_code == 200, "releasing a nested guard must keep Async running");

            auto coro_response = mcr::sync_wait(fetch(server.Url()));
            require(coro_response && coro_response->status_code == 200, "releasing a nested guard must keep Coro running");

            auto pool = mcr::ConnectionPool::Create().value();
            auto session = mcr::Session::Create().value();
            session->SetUrl(server.Url()).value();
            session->SetConnectionPool(pool).value();
            auto pooled = session->Get();
            require(pooled && pooled->status_code == 200, "pooled request must succeed under the guard");
        }

        require(!mcr::Async::Startup(), "the last guard must clean up Async");
        require(!mcr::Coro::Startup(), "the last guard must clean up Coro");
        auto again = mcr::GlobalInit::Create();
        require(!again && again.error().code == mcr::ErrorCode::FAILED_INIT, "Create must fail after final cleanup");
        server.Check();
    } catch (std::exception const& error) {
        std::println("test_global_init: {}", error.what());
        return 1;
    }
    return 0;
}
