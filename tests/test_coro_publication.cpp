/**
 * @file test_coro_publication.cpp
 * @brief Stress immediate coroutine completion and preserve active transfers on duplicate submission.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"

import std;
import mcr;

namespace {
    using namespace std::chrono_literals;

    /**
     * @brief Fail the test when a runtime invariant is violated.
     */
    auto require(bool condition, std::string_view message) -> void {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    /**
     * @brief Finish immediately after the queued resumption, destroying the scheduling awaiter.
     */
    auto scheduled() -> mcr::Task<void> {
        (co_await mcr::Coro::Schedule()).value();
    }

    /**
     * @brief Repeatedly publish continuations whose coroutine frames can be reclaimed immediately.
     */
    auto stress_publication(mcr::test::HttpServer const& server) -> void {
        auto const before{ server.Connections() };
        std::vector<std::jthread> callers;
        std::mutex error_mutex;
        std::exception_ptr failure;
        for (int caller{}; caller < 4; ++caller) {
            callers.emplace_back([&] {
                try {
                    for (int iteration{}; iteration < 1000; ++iteration) {
                        mcr::sync_wait(scheduled());
                        auto cancelled = mcr::GetCoro(server.Url());
                        require(cancelled.Cancel(), "cold task cancellation must succeed");
                        if (iteration % 2 == 0) {
                            cancelled.Start();
                        }
                        auto response = mcr::sync_wait(std::move(cancelled)).value();
                        require(response.error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "immediately completed cancellation must retain its result");
                    }
                } catch (...) {
                    std::lock_guard lock{ error_mutex };
                    if (!failure) {
                        failure = std::current_exception();
                    }
                }
            });
        }
        callers.clear();
        if (failure) {
            std::rethrow_exception(failure);
        }
        require(server.Connections() == before, "pre-cancelled transfers must never contact the server");
    }

    /**
     * @brief Exercise the internal transfer boundary with a deliberately duplicated session.
     */
    auto transfer(
        std::shared_ptr<mcr::Session> session,
        std::atomic_int& preparations
    ) -> mcr::Task<mcr::Result<mcr::Response>> {
        co_return co_await mcr::detail::CoroTransferAwaiter{
            std::move(session),
            [&preparations](mcr::Session& current) {
                ++preparations;
                return current.PrepareGet();
            },
            {}
        };
    }

    /**
     * @brief Reject a second claim without reconfiguring or losing the first transfer.
     */
    auto duplicate_handle(mcr::test::HttpServer const& server) -> void {
        auto session = mcr::Session::Create().value();
        session->SetUrl(server.Url("/stream")).value();
        session->SetProxies({ { "http", "" }, { "no_proxy", "*" } }).value();
        session->SetTimeout(mcr::options::Timeout{ 5s }).value();
        std::promise<void> headers;
        auto arrived = headers.get_future();
        bool signalled{};
        session->SetHeaderCallback(mcr::HeaderCallback{ [&](auto, auto) {
                   if (!signalled) {
                       signalled = true;
                       headers.set_value();
                   }
                   return true;
               } })
            .value();
        std::atomic_int preparations{};
        auto first = transfer(session, preparations);
        first.Start();
        require(arrived.wait_for(3s) == std::future_status::ready, "the first handle must be active");
        auto second = mcr::sync_wait(transfer(session, preparations));
        require(!second && second.error().code == mcr::ErrorCode::FAILED_INIT, "the duplicate submission must return a multi setup error");
        auto response = mcr::sync_wait(std::move(first)).value();
        require(!response.error && response.status_code == 200 && response.text == std::string(307200, 'x'), "the first submission must keep its handle, body, and continuation");
        require(preparations == 1, "a duplicate must be rejected before preparing the active session");
    }
} // namespace

/**
 * @brief Run publication races and duplicate-handle regressions with bounded network timeouts.
 */
auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    int result{};
    try {
        mcr::test::HttpServer server;
        stress_publication(server);
        duplicate_handle(server);
        server.Check();
    } catch (std::exception const& error) {
        std::println("test_coro_publication: {}", error.what());
        result = 1;
    }
    mcr::Coro::Cleanup().value();
    curl_global_cleanup();
    return result;
}
