/**
 * @file test_json_api.cpp
 * @brief Exercise JSON requests through the public module, sessions, and asynchronous APIs.
 */
#include "fixtures/http_server.hpp"
import std;
import mcr;

namespace {
    using namespace std::chrono_literals;
    using mcr::test::HttpServer;

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_json_api: {}", message);
        }
        return condition;
    }

    auto configure(mcr::Session& session, HttpServer const& server) -> void {
        session.SetUrl(server.Url("/json/echo")).value();
        session.SetProxies(mcr::options::Proxies{
                               {     "http",  "" },
                               { "no_proxy", "*" }
        })
            .value();
        session.SetTimeout(mcr::options::Timeout{ 3000ms }).value();
    }

    auto json_response(mcr::Response const& response, mcr::Json const& expected, std::string_view method = "POST") -> bool {
        auto parsed = response.TryJson();
        return check(!response.error && response.status_code == 200 && response.header.at("X-Method") == method && response.header.at("X-Request-Content-Type") == "application/json" && parsed && *parsed == expected, "JSON transfers must preserve the method, inferred media type, and value");
    }

    auto check_session(HttpServer const& server) -> bool {
        auto  session_owner = mcr::Session::Create().value();
        auto& session       = *session_owner;
        configure(session, server);
        mcr::Json const document{
            { "message", "hello" }
        };
        mcr::JsonBody source{ document };
        session.SetJsonBody(source).value();
        source = mcr::JsonBody{ mcr::Json(nullptr) };
        bool passed{ json_response(session.Post().value(), document) };
        passed &= check(std::holds_alternative<mcr::JsonBody>(session.GetContent()) && !session.GetHeader().contains("Content-Type"), "session must own its JSON body and keep inferred headers out of persistent options");
        passed &= json_response(session.Put().value(), document, "PUT");
        passed &= json_response(session.Patch().value(), document, "PATCH");

        auto response{ session.Head().value() };
        passed &= check(!response.error && response.text.empty() && response.header.at("X-Request-Content-Type").empty(), "HEAD must ignore stored JSON and its inferred media type");
        std::string downloaded;
        response  = session.Download(mcr::WriteCallback{ [&](std::string_view bytes, std::intptr_t) {
                              downloaded.append(bytes);
                              return true;
                           } })
                        .value();
        passed   &= check(!response.error && downloaded.empty() && response.text.empty() && response.header.at("X-Request-Content-Type").empty(), "Download must ignore stored JSON and its inferred media type");
        passed   &= json_response(session.Post().value(), document);

        session.SetHeader(mcr::Header{
                              { "content-type", "application/vnd.example+json" }
        })
            .value();
        response  = session.Post().value();
        passed   &= check(!response.error && response.header.at("X-Request-Content-Type") == "application/vnd.example+json", "explicit media types must override the inferred type case-insensitively");
        session.SetHeader(mcr::Header{}).value();
        passed &= json_response(session.Post().value(), document);

        session.SetBody(mcr::Body{ "plain" }).value();
        response  = session.Post().value();
        passed   &= check(!response.error && response.text == "plain" && response.header.at("X-Request-Content-Type") != "application/json", "plain bodies must replace JSON without inheriting its media type");
        session.SetJsonBody(mcr::JsonBody{ document }).value();
        session.SetPayload(mcr::Payload{
                               { "field", "value" }
        })
            .value();
        response  = session.Post().value();
        passed   &= check(!response.error && response.text == "field=value" && response.header.at("X-Request-Content-Type") == "application/x-www-form-urlencoded", "form payloads must replace JSON and retain form media types");
        session.SetJsonBody(mcr::JsonBody{ document }).value();
        session.SetBodyView(mcr::BodyView{ "borrowed" }).value();
        response  = session.Post().value();
        passed   &= check(!response.error && response.text == "borrowed" && response.header.at("X-Request-Content-Type") != "application/json", "borrowed bodies must clear the inferred JSON media type");
        session.SetJsonBody(mcr::JsonBody{ document }).value();
        session.SetMultipart(mcr::Multipart{
                                 { "field", "value" }
        })
            .value();
        response  = session.Post().value();
        passed   &= check(!response.error && response.text.find("value") != std::string::npos && response.header.at("X-Request-Content-Type").starts_with("multipart/form-data;"), "multipart must replace JSON and generate its own boundary and media type");
        session.SetJsonBody(mcr::JsonBody{ document }).value();
        session.RemoveContent().value();
        response  = session.Post().value();
        passed   &= check(!response.error && response.text.empty() && response.header.at("X-Request-Content-Type") != "application/json", "removing content must remove the inferred JSON media type");
        session.SetJsonBody(mcr::JsonBody{ mcr::Json(nullptr) }).value();
        passed &= json_response(session.Post().value(), mcr::Json(nullptr));

        session.SetUrl(server.Url("/json/error")).value();
        response     = session.Post().value();
        auto parsed  = response.TryJson();
        passed      &= check(!response.error && response.status_code == 422 && parsed && parsed->at("error") == "invalid input", "HTTP errors with structured JSON media types must remain parseable");
        session.SetUrl(server.Url("/json/empty")).value();
        response  = session.Get().value();
        passed   &= check(!response.error && response.status_code == 204 && response.text.empty() && !response.TryJson(), "204 must remain a successful transfer with no JSON document");
        session.SetUrl(server.Url("/json/invalid")).value();
        response  = session.Get().value();
        passed   &= check(!response.error && response.status_code == 200 && !response.TryJson() && response.text == "{invalid", "a JSON media type must not hide malformed response data");
        return passed;
    }

    auto check_options(HttpServer const& server) -> bool {
        auto const                  url{ server.Url("/echo") };
        mcr::options::Proxies const proxies{
            {     "http",  "" },
            { "no_proxy", "*" }
        };
        mcr::options::Timeout const timeout{ 3000ms };
        mcr::Json const             document{
            { "count", 7 }
        };
        mcr::JsonBody const body{ document };
        mcr::Header const   header{
            { "cOnTeNt-TyPe", "application/custom+json" }
        };
        auto before{ mcr::Post(url, proxies, timeout, header, body).value() };
        auto after{ mcr::Post(url, proxies, timeout, body, header).value() };
        bool passed{ check(!before.error && !after.error && before.header.at("X-Request-Content-Type") == "application/custom+json" && after.header.at("X-Request-Content-Type") == "application/custom+json" && before.Json() == document && after.Json() == document, "explicit headers must win regardless of their position relative to JsonBody") };
        auto response{
            mcr::Post(url, proxies, timeout, body, mcr::Header{ { "Content-Type", "" } }
                ).value()
        };
        passed      &= check(!response.error && response.header.at("X-Request-Content-Type").empty() && response.Json() == document, "explicit empty media types must suppress inference");
        response     = mcr::Post(url, proxies, timeout, mcr::Body{
                                                            "discarded"
        },
                                 body,
                                 mcr::Header{ { "X-Custom", "kept" } })
                           .value();
        passed      &= json_response(response, document);
        passed      &= check(response.header.at("X-Request-X-Custom") == "kept" && response.header.at("Content-Type") == "text/plain", "unrelated headers must coexist with inferred types, and parsing may ignore the response media type");
        response     = mcr::Post(url, proxies, timeout, body, mcr::Body{ "last" }).value();
        passed      &= check(!response.error && response.text == "last" && response.header.at("X-Request-Content-Type") != "application/json", "the last content option must win in one-shot APIs");

        auto first   = std::tuple{ url, proxies, timeout, body };
        auto second  = std::tuple{ url, proxies, timeout, mcr::JsonBody{ mcr::Json::array({ 1, 2 }) } };
        auto batch{ mcr::MultiPost(first, std::move(second)).value() };
        passed &= check(batch.size() == 2, "JSON batches must retain one response per request");
        passed &= json_response(batch.at(0), document);
        passed &= json_response(batch.at(1), mcr::Json::array({ 1, 2 }));
        auto callback{ mcr::PostCallback([](mcr::Result<mcr::Response> response_result) { auto response = std::move(response_result).value();  return response.TryJson(); }, url, proxies, timeout, body).value() };
        auto parsed{ callback.Get() };
        passed &= check(parsed && *parsed == document, "continuations must be able to return parsed JSON results");
        return passed;
    }

    auto check_async_ownership(HttpServer const& server) -> bool {
        auto* pool{ mcr::GlobalThreadPool::GetInstance() };
        pool->Wait();
        (void)pool->Pause();
        mcr::AsyncResponse                                                      copied;
        mcr::AsyncResponse                                                      moved;
        std::vector<mcr::utils::AsyncWrapper<mcr::Result<mcr::Response>, true>> batch;
        try {
            mcr::Json value{
                { "state", "original" }
            };
            mcr::JsonBody body{ value };
            auto          args = std::tuple{
                server.Url("/json/echo"),
                mcr::options::Proxies{ { "http", "" }, { "no_proxy", "*" } },
                mcr::options::Timeout{ 3000ms }
            };
            copied         = std::apply([&](auto const&... options) { return mcr::PostAsync(options..., body).value(); }, args);
            batch          = mcr::MultiPostAsync(std::tuple_cat(args, std::tuple{ body })).value();
            moved          = std::apply([&](auto const&... options) { return mcr::PostAsync(options..., mcr::JsonBody{ value }).value(); }, args);
            value["state"] = "changed";
            body           = mcr::JsonBody{ value };
        } catch (...) {
            (void)pool->Resume();
            throw;
        }
        (void)pool->Resume();
        mcr::Json const expected{
            { "state", "original" }
        };
        bool passed{ json_response(copied.Get().value(), expected) };
        passed &= json_response(moved.Get().value(), expected);
        passed &= json_response(batch.at(0).Get().value(), expected);
        return passed;
    }
} // namespace

auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        mcr::Async::Startup(2, 4).value();
        HttpServer server;
        passed &= check_session(server);
        passed &= check_options(server);
        passed &= check_async_ownership(server);
        mcr::GlobalThreadPool::GetInstance()->Wait();
        server.Check();
    } catch (std::exception const& failure) {
        passed = check(false, failure.what());
    }
    mcr::Async::Cleanup();
    curl_global_cleanup();
    return passed ? 0 : 1;
}
