/**
 * @file test_session_mutation.cpp
 * @brief Reject immediate session mutations from curl callbacks and outside the owning batch.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"

import std;
import mcr;

namespace {
    using namespace std::chrono_literals;

    /**
     * @brief Report a failed assertion.
     */
    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_session_mutation: {}", message);
        }
        return condition;
    }

    /**
     * @brief Check the error returned by a forbidden mutation.
     */
    auto rejected(mcr::Result<void> const& result) -> bool {
        return check(!result && result.error().code == mcr::ErrorCode::RECURSIVE_API_CALL, "mutating a busy curl handle must return RECURSIVE_API_CALL");
    }

    /**
     * @brief Attempt setters that update curl or immediately release curl resources.
     */
    auto check_mutations(mcr::Session& session, mcr::ConnectionPool const& pool) -> bool {
        bool passed{ rejected(session.RemoveContent()) };
        passed &= rejected(session.SetTimeout(mcr::options::Timeout{ 1ms }));
        passed &= rejected(session.SetConnectTimeout(mcr::options::ConnectTimeout{ 1ms }));
        passed &= rejected(session.SetConnectionPool(pool));
        passed &= rejected(session.SetAuth({ "user", "password", mcr::options::AuthMode::BASIC }));
        passed &= rejected(session.SetBearer(mcr::options::Bearer{ "token" }));
        passed &= rejected(session.SetUserAgent(mcr::UserAgent{ "changed" }));
        passed &= rejected(session.SetVerifySsl(mcr::options::VerifySsl{ false }));
        passed &= rejected(session.SetSslOptions({}));
        passed &= rejected(session.SetRedirect(mcr::options::Redirect{ false }));
        passed &= rejected(session.SetCookies({}));
        passed &= rejected(session.SetLowSpeed(mcr::options::LowSpeed{ 1, 1s }));
        passed &= rejected(session.SetUnixSocket(mcr::options::UnixSocket{ "" }));
        passed &= rejected(session.SetDebugCallback({}));
        passed &= rejected(session.SetVerbose(mcr::options::Verbose{ false }));
        passed &= rejected(session.SetInterface(mcr::options::Interface{ "127.0.0.1" }));
        passed &= rejected(session.SetLocalPort(mcr::options::LocalPort{ 0 }));
        passed &= rejected(session.SetLocalPortRange(mcr::options::LocalPortRange{ 1 }));
        passed &= rejected(session.SetHttpVersion({}));
        passed &= rejected(session.SetRange(mcr::options::Range{ 0, 1 }));
        passed &= rejected(session.SetResolve({ "localhost", "127.0.0.1", { 80 } }));
        passed &= rejected(session.SetResolves({}));
        passed &= rejected(session.SetMultiRange(mcr::options::MultiRange{ mcr::options::Range{ 0, 1 } }));
        passed &= rejected(session.SetLimitRate(mcr::options::LimitRate{ 1, 1 }));
        return passed;
    }

    /**
     * @brief Preserve an in-flight MIME upload when callbacks attempt to mutate its session.
     */
    auto check_callbacks(mcr::test::HttpServer const& server) -> bool {
        auto session{ mcr::Session::Create().value() };
        auto pool{ mcr::ConnectionPool::Create().value() };
        session->SetUrl(server.Url("/echo")).value();
        session->SetProxies({ { "http", "" }, { "no_proxy", "*" } }).value();
        session->SetTimeout(mcr::options::Timeout{ 3s }).value();
        session->SetHeader({ { "Expect", "100-continue" } }).value();
        std::string const data(128 * 1024, 'x');
        session->SetMultipart({ { "data", std::string_view{ data } } }).value();
        bool passed{ true }, header_seen{}, progress_seen{}, debug_seen{};
        session->SetHeaderCallback(mcr::HeaderCallback{ [&](std::string_view line, auto) {
                   if (line.starts_with("HTTP/1.1 100")) {
                       header_seen = true;
                       passed &= check_mutations(*session, pool);
                   }
                   return true;
               } })
            .value();
        session->SetProgressCallback(mcr::ProgressCallback{ [&](auto, auto, auto, auto, auto) {
                   if (!progress_seen) {
                       progress_seen = true;
                       passed &= check_mutations(*session, pool);
                   }
                   return true;
               } })
            .value();
        session->SetDebugCallback(mcr::DebugCallback{ [&](auto, auto, auto) {
                   if (!debug_seen) {
                       debug_seen = true;
                       passed &= check_mutations(*session, pool);
                   }
               } })
            .value();
        auto const response{ session->Post().value() };
        passed &= check(header_seen && progress_seen && debug_seen, "all callback phases must execute");
        passed &= check(!response.error && response.status_code == 200 && response.text.contains(data), "rejected mutations must leave the MIME tree and upload intact");
        passed &= check(std::holds_alternative<mcr::Multipart>(session->GetContent()), "RemoveContent must preserve stored content on failure");
        session->SetHeaderCallback({}).value();
        session->SetProgressCallback({}).value();
        session->SetDebugCallback({}).value();
        session->SetVerbose(mcr::options::Verbose{ false }).value();
        session->RemoveContent().value();
        session->SetTimeout(mcr::options::Timeout{ 3s }).value();
        passed &= check(session->Post().value().text.empty(), "setters must work again after completion");

        mcr::MultiPerform multi;
        multi.AddSession(session).value();
        passed &= check_mutations(*session, pool);
        passed &= check(!multi.Get().value().front().error, "the owning batch must still prepare its session");
        multi.RemoveSession(session).value();
        session->SetTimeout(mcr::options::Timeout{ 3s }).value();
        return passed;
    }
} // namespace

/**
 * @brief Run callback and batch ownership mutation regressions against a local server.
 */
auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        mcr::test::HttpServer server;
        passed = check_callbacks(server);
        server.Check();
    } catch (std::exception const& error) {
        passed = check(false, error.what());
    }
    curl_global_cleanup();
    return passed ? 0 : 1;
}
