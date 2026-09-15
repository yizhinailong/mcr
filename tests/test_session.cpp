/**
 * @file test_session.cpp
 * @brief Exercise Session against a controlled loopback HTTP/1.1 server.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"

import std;
import mcr.session;
import mcr.util;

static_assert(!std::is_copy_constructible_v<mcr::Session> && !std::is_move_constructible_v<mcr::Session>);
static_assert(std::is_copy_constructible_v<mcr::Response> && std::is_move_constructible_v<mcr::Response>);
static_assert(std::is_same_v<mcr::AsyncResponse, mcr::utils::AsyncWrapper<mcr::Result<mcr::Response>>>);
static_assert(std::variant_size_v<mcr::Content> == 6);

namespace {
    using namespace std::chrono_literals;
    using namespace std::string_view_literals;

    using mcr::test::HttpServer;

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_session: {}", message);
        }
        return condition;
    }

    auto configure(mcr::Session& session, HttpServer const& server, std::string_view path = "/hello") -> void {
        session.SetOption(server.Url(path)).value();
        session.SetOption(mcr::options::Proxies{
                              {     "http",  "" },
                              { "no_proxy", "*" }
        })
            .value();
        session.SetOption(mcr::options::Timeout{ 3000ms }).value();
        session.SetOption(mcr::options::HttpVersion{ mcr::options::HttpVersionCode::VERSION_1_1 }).value();
    }

    auto check_reuse(HttpServer const& server) -> bool {
        auto  session_owner = mcr::Session::Create().value();
        auto& session       = *session_owner;
        configure(session, server);
        auto const before{ server.Connections() };
        auto       first{ session.Get().value() };
        bool       passed{ check(first.status_code == 200 && !first.error && first.text == "Hello session!", "GET must return body and status") };
        passed &= check(first.url == server.Url() && first.header["content-type"] == "text/plain" && first.reason == "OK" && first.status_line == "HTTP/1.1 200 OK", "response must expose parsed final headers and effective URL");
        passed &= check(first.downloaded_bytes == 14 && first.uploaded_bytes == 0 && first.primary_ip == "127.0.0.1" && first.primary_port != 0 && first.elapsed >= 0 && first.GetCertInfos().empty(), "HTTP metadata must be initialized and plain HTTP certificates must be empty");
        for (int i{}; i < 4; ++i) {
            passed &= check(session.Get().value().text == first.text, "repeated GET must clear prior buffers");
        }
        passed &= check(server.Connections() == before + 1, "sequential requests must reuse the same connection");
        passed &= check(session.Head().value().text.empty() && session.GetDownloadFileLength().value() == 14 && session.Get().value().text == first.text, "HEAD and length probing must not corrupt subsequent GET");
        session.SetUrl(server.Url("/error")).value();
        auto error{ session.Get().value() };
        passed &= check(error.status_code == 404 && !error.error && error.text == "missing" && first.status_code == 200 && first.text == "Hello session!", "HTTP errors are responses and previous response snapshots stay independent");
        session.SetUrl(mcr::Url{ "http://[" }).value();
        auto malformed{ session.Get().value() };
        passed &= check(malformed.error.code == mcr::ErrorCode::URL_MALFORMAT && !malformed.error.message.empty() && malformed.text.empty(), "malformed URL must return an initialized transport failure");
        session.SetUrl(server.Url()).value();
        passed &= check(!session.Get().value().error, "successful reuse must clear previous curl errors");
        return passed;
    }

    auto check_methods_and_content(HttpServer const& server) -> bool {
        auto  session_owner = mcr::Session::Create().value();
        auto& session       = *session_owner;
        configure(session, server, "/echo");
        using Method = mcr::Result<mcr::Response> (mcr::Session::*)();
        std::pair<Method, std::string_view> const methods[]{
            {    &mcr::Session::Post,    "POST" },
            {     &mcr::Session::Get,     "GET" },
            {     &mcr::Session::Put,     "PUT" },
            { &mcr::Session::Options, "OPTIONS" },
            {   &mcr::Session::Patch,   "PATCH" },
            {  &mcr::Session::Delete,  "DELETE" }
        };
        bool              passed{ true };
        std::string const binary{ "first\0last", 10 };
        for (auto const& [method, name] : methods) {
            session.SetBody(mcr::Body{ binary }).value();
            auto response{ (session.*method)().value() };
            passed &= check(!response.error && response.header["X-Method"] == name && response.text == binary && response.uploaded_bytes == 10, "methods must preserve binary bodies and use their own HTTP verb after reuse");
            session.RemoveContent().value();
            response  = (session.*method)().value();
            passed   &= check(!response.error && response.header["X-Method"] == name && response.text.empty(), "RemoveContent must detach the old body for every method");
        }
        session.SetOption(mcr::Payload{
                              { "space key", "x+y" },
                              {     "empty",    "" }
        })
            .value();
        auto payload{ session.Post().value() };
        passed &= check(payload.text == "space key=x%2By&empty=" && payload.header["X-Request-Content-Type"] == "application/x-www-form-urlencoded", "payloads must preserve cpr's raw keys and encode values using the session holder");
        std::string borrowed{ binary };
        session.SetOption(mcr::BodyView{ borrowed }).value();
        passed      &= check(session.Post().value().text == binary, "BodyView must preserve exact byte length");
        borrowed[0]  = 'F';
        passed      &= check(session.Put().value().text == borrowed, "BodyView must observe borrowed storage on the next request");
        session.SetBodyView({}).value();
        passed &= check(session.Post().value().text.empty(), "null empty BodyView must produce a zero-length body without reading stdin");
        session.SetBody(mcr::Body{ "retained" }).value();
        passed &= check(session.Head().value().text.empty() && session.Post().value().text == "retained", "HEAD must suppress but retain configured content");
        session.SetOption(mcr::Multipart{
                              {   "text", std::string_view{ binary } },
                              { "number",                         42 }
        })
            .value();
        auto multipart{ session.Post().value() };
        passed &= check(!multipart.error && multipart.text.contains(binary) && multipart.text.contains("name=\"number\"") && multipart.header["X-Request-Content-Type"].starts_with("multipart/form-data; boundary="), "multipart must encode text fields including embedded nulls");
        passed &= check(session.Get().value().header["X-Method"] == "GET", "GET with retained multipart must remain GET");
        session.SetBody(mcr::Body{ "replacement" }).value();
        passed &= check(session.Post().value().text == "replacement", "switching MIME to a body must detach the MIME tree");
        session.SetMultipart({
                                 { "again", "value" }
        })
            .value();
        passed &= check(session.Post().value().text.contains("name=\"again\""), "switching a body to MIME must detach the old POST fields");
        session.RemoveContent().value();
        passed &= check(session.Get().value().text.empty() && std::holds_alternative<std::monostate>(session.GetContent()), "removing multipart must leave no dangling MIME or body state");
        return passed;
    }

    auto check_options(HttpServer const& server) -> bool {
        auto  session_owner = mcr::Session::Create().value();
        auto& session       = *session_owner;
        configure(session, server, "/echo?existing=1#fragment");
        session.SetOption(mcr::Parameters{
                              {  "a b", "x+y" },
                              { "flag",    "" }
        })
            .value();
        session.SetOption(mcr::Header{
                              { "X-Custom", "old" },
                              {  "X-Empty",    "" }
        })
            .value();
        session.UpdateHeader({
                                 { "x-custom", "new" }
        })
            .value();
        session.GetHeader()["Another"] = "value";
        session.SetOption(mcr::UserAgent{ "session-test" }).value();
        session.SetOption(mcr::options::AcceptEncoding::Create({ mcr::options::AcceptEncodingMethods::disabled }).value()).value();
        session.SetOption(mcr::options::ReserveSize{ 4096 }).value();
        session.SetOption(mcr::options::ConnectTimeout{ 1000ms }).value();
        session.SetOption(mcr::options::LimitRate{ 0, 0 }).value();
        auto response{ session.Get().value() };
        bool passed{ check(response.header["X-Target"] == "/echo?existing=1&a%20b=x%2By&flag" && session.GetFullRequestUrl().value() == server.Url("/echo?existing=1&a%20b=x%2By&flag#fragment").Str(), "parameters must merge with existing queries before fragments") };
        passed &= check(response.header["X-Request-X-Custom"] == "new" && response.header["X-Request-User-Agent"] == "session-test" && response.header["X-Request-Accept-Encoding"].empty(), "header replacement, user agent, and disabled encoding must reach the server");
        passed &= check(std::as_const(session).GetHeader().size() == 3, "case-insensitive header merging must retain unrelated entries");
        session.SetParameters({}).value();
        session.SetUrl(server.Url("/echo")).value();
        session.SetOption(mcr::options::Authentication{ "user", "password", mcr::options::AuthMode::BASIC }).value();
        passed &= check(session.Get().value().header["X-Request-Authorization"] == "Basic dXNlcjpwYXNzd29yZA==", "basic authentication must reach the server");
        session.SetOption(mcr::options::Bearer{ "token" }).value();
        passed &= check(session.Get().value().header["X-Request-Authorization"] == "Bearer token", "bearer authentication must replace basic authentication");
        session.SetUrl(server.Url("/cookie")).value();
        auto cookie{ session.Get().value() };
        session.SetUrl(server.Url("/echo")).value();
        passed &= check(!cookie.cookies.empty() && session.Get().value().header["X-Request-Cookie"] == "stored=yes", "cookies must persist in the session engine");
        session.SetOption(mcr::Cookies{
                              { "explicit", "a b" }
        })
            .value();
        passed &= check(session.Get().value().header["X-Request-Cookie"] == "explicit=a%20b;", "SetCookies must replace stored cookies and retain cpr's trailing separator");
        session.SetUrl(server.Url("/range")).value();
        session.SetOption(mcr::options::Range{ 2, 5 }).value();
        auto range{ session.Get().value() };
        passed &= check(range.status_code == 206 && range.text == "2345", "byte ranges must reach curl");
        session.SetUrl(server.Url("/echo")).value();
        passed &= check(session.Put().value().header["X-Request-Range"].empty(), "PUT must clear a previous range as in cpr");
        session.SetHeader({
                              { "Expect", "100-continue" }
        })
            .value();
        session.SetBody(mcr::Body{ "continue" }).value();
        passed &= check(session.Post().value().text == "continue", "explicit Expect must be preserved and interim headers parsed");
        return passed;
    }

    auto check_redirects_and_failures(HttpServer const& server) -> bool {
        auto  session_owner = mcr::Session::Create().value();
        auto& session       = *session_owner;
        configure(session, server, "/redirect");
        auto redirect{ session.Get().value() };
        bool passed{ check(!redirect.error && redirect.status_code == 200 && redirect.redirect_count == 1 && redirect.url == server.Url() && redirect.text == "Hello session!", "default redirects must follow Location") };
        passed &= check(redirect.raw_header.contains("302 Found") && !redirect.header.contains("X-Intermediate"), "raw headers retain redirects while parsed headers describe the final response");
        session.SetOption(mcr::options::Redirect{ false }).value();
        passed &= check(session.Get().value().status_code == 302, "redirect following may be disabled");
        session.SetRedirect(mcr::options::Redirect{ 1L }).value();
        session.SetUrl(server.Url("/loop")).value();
        passed &= check(session.Get().value().error.code == mcr::ErrorCode::TOO_MANY_REDIRECTS, "redirect limits must surface as transport errors");
        session.SetUrl(server.Url("/partial")).value();
        auto partial{ session.Get().value() };
        passed &= check(partial.error.code == mcr::ErrorCode::PARTIAL_FILE && partial.text == "short", "partial transfer failures must preserve received bytes");
        session.SetUrl(server.Url("/slow")).value();
        session.SetTimeout(mcr::options::Timeout{ 25ms }).value();
        passed &= check(session.Get().value().error.code == mcr::ErrorCode::OPERATION_TIMEDOUT, "timeouts must cancel a delayed local response");
        session.SetTimeout(mcr::options::Timeout{ 3000ms }).value();
        session.SetUrl(server.Url()).value();
        auto cancellation{ std::make_shared<std::atomic_bool>(true) };
        session.SetCancellationParam(cancellation).value();
        int progress{};
        session.SetProgressCallback(mcr::ProgressCallback{ [&](auto, auto, auto, auto, auto) { ++progress; return true; } }).value();
        passed &= check(session.Get().value().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK && progress == 0, "cancellation must precede the progress observer even if the observer is replaced later");
        cancellation->store(false);
        passed &= check(!session.Get().value().error && progress > 0, "cleared cancellation must allow reuse with progress callbacks");
        session.SetCancellationParam(nullptr).value();
        session.SetProgressCallback(mcr::ProgressCallback{ [](auto, auto, auto, auto, auto) { return false; } }).value();
        passed &= check(session.Get().value().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "false progress callbacks must abort");
        session.SetProgressCallback({}).value();
        passed &= check(!session.Get().value().error, "cleared progress callbacks must restore transfers");
        return passed;
    }

    auto check_callbacks(HttpServer const& server) -> bool {
        auto  session_owner = mcr::Session::Create().value();
        auto& session       = *session_owner;
        configure(session, server);
        std::string body, headers;
        session.SetOption(mcr::WriteCallback{ [&](std::string_view data, auto) { body += data; return true; } }).value();
        session.SetOption(mcr::HeaderCallback{ [&](std::string_view data, auto) { headers += data; return true; } }).value();
        auto response{ session.Get().value() };
        bool passed{ check(!response.error && response.text.empty() && body == "Hello session!" && response.raw_header == headers && response.header["Content-Type"] == "text/plain", "callbacks must stream the body and observe headers without losing response metadata") };
        session.SetWriteCallback(mcr::WriteCallback{ [](auto, auto) { return false; } }).value();
        passed &= check(session.Get().value().error.code == mcr::ErrorCode::WRITE_ERROR, "false write callbacks must abort");
        session.SetWriteCallback(mcr::WriteCallback{ [](auto, auto) -> bool { throw std::runtime_error{ "callback failed" }; } }).value();
        try {
            (void)session.Get().value();
            passed &= check(false, "write callback exceptions must be rethrown");
        } catch (std::runtime_error const& error) {
            passed &= check(error.what() == "callback failed"sv, "callback exception identity must be preserved");
        }
        session.SetWriteCallback({}).value();
        passed &= check(session.Get().value().text == "Hello session!", "empty write callback must restore buffering after an exception");
        session.SetHeaderCallback(mcr::HeaderCallback{ [](auto, auto) { return false; } }).value();
        passed &= check(session.Get().value().error.code == mcr::ErrorCode::WRITE_ERROR, "header callbacks must be able to abort");
        session.SetHeaderCallback({}).value();
        session.SetUrl(server.Url("/echo")).value();
        session.SetReadCallback(mcr::ReadCallback{ [](char*, std::size_t& length, auto) { ++length; return true; } }).value();
        passed &= check(session.Post().value().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "oversized producer counts must abort without reading outside the curl buffer");
        session.SetReadCallback(mcr::ReadCallback{ [](char*, std::size_t&, auto) -> bool { throw std::runtime_error{ "read failed" }; } }).value();
        try {
            (void)session.Post().value();
            passed &= check(false, "read callback exceptions must propagate after curl returns");
        } catch (std::runtime_error const& error) {
            passed &= check(error.what() == "read failed"sv, "read callback exception identity must be preserved");
        }
        for (mcr::CprOffT const size : { mcr::CprOffT{ 10 }, mcr::CprOffT{ -1 } }) {
            std::string source{ "read\0bytes", 10 };
            std::size_t offset{};
            session.SetOption(mcr::ReadCallback{ size, [&](char* buffer, std::size_t& length, auto) {
                                                    length = (std::min)(length, source.size() - offset);
                                                    std::memcpy(buffer, source.data() + offset, length);
                                                    offset += length;
                                                    return true;
                                                } })
                .value();
            auto upload{ session.Post().value() };
            passed &= check(!upload.error && upload.text == source, "fixed-length and chunked read uploads must retain binary bytes");
            offset  = 0;
            passed &= check(session.Put().value().text == source, "read uploads must work when switching POST to PUT");
        }
        session.SetReadCallback({}).value();
        passed &= check(session.Post().value().text.empty(), "cleared read callbacks must not read old upload state");
        session.SetUrl(server.Url("/sse")).value();
        std::vector<std::string> events;
        session.SetOption(mcr::ServerSentEventCallback{ [&](mcr::ServerSentEvent&& event, auto) { events.push_back(std::move(event.data)); return true; } }).value();
        auto first{ session.Get().value() };
        auto second{ session.Get().value() };
        passed &= check(!first.error && !second.error && first.text.empty() && events == std::vector<std::string>{ "first", "second", "first", "second" }, "SSE must reset unfinished parser state between requests");
        session.SetWriteCallback({}).value();
        passed &= check(session.Get().value().text.starts_with("id: 7"), "raw write selection must clear SSE consumption");
        int debug_count{};
        session.SetDebugCallback(mcr::DebugCallback{ [&](auto, auto, auto) { ++debug_count; } }).value();
        passed &= check(!session.Get().value().error && debug_count > 0, "debug callbacks must enable curl diagnostics");
        session.SetDebugCallback(mcr::DebugCallback{ [](auto, auto, auto) { throw std::runtime_error{ "debug failed" }; } }).value();
        try {
            (void)session.Get().value();
            passed &= check(false, "debug exceptions must propagate after curl returns");
        } catch (std::runtime_error const& error) {
            passed &= check(error.what() == "debug failed"sv, "debug callback exception identity must be preserved");
        }
        session.SetDebugCallback({}).value();
        passed &= check(!session.Get().value().error, "cleared debug callbacks must permit reuse after an exception");
        return passed;
    }

    /**
     * @brief Remove only this test's temporary file.
     */
    struct TempFile {
        std::filesystem::path path{ std::filesystem::temp_directory_path() / std::format("mcr-session-{}.bin", std::chrono::steady_clock::now().time_since_epoch().count()) };

        ~TempFile() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };

    auto check_multipart_files(HttpServer const& server) -> bool {
        auto  session_owner = mcr::Session::Create().value();
        auto& session       = *session_owner;
        configure(session, server, "/echo");
        TempFile          temporary;
        std::string const binary{ "file\0bytes", 10 };
        {
            std::ofstream file{ temporary.path, std::ios::binary };
            file.write(binary.data(), static_cast<std::streamsize>(binary.size()));
        }
        session.SetMultipart({
                                 { "file", mcr::File{ temporary.path.string(), "override.bin" }, "application/octet-stream" },
                                 { "buffer", mcr::Buffer::Create(binary.begin(), binary.end(), std::filesystem::path{ "buffer.bin" }).value() },
                                 { "empty", mcr::Buffer::Create(binary.begin(), binary.begin(), std::filesystem::path{ "empty.bin" }).value() }
        })
            .value();
        auto       response{ session.Post().value() };
        bool       passed{ check(!response.error && response.text.contains("filename=\"override.bin\"") && response.text.contains("filename=\"buffer.bin\"") && response.text.contains("filename=\"empty.bin\"") && response.text.contains(binary), "MIME must support files, filename overrides, and binary or empty borrowed buffers") };
        auto const first{ response.text.find(binary) };
        passed &= check(first != std::string::npos && response.text.find(binary, first + binary.size()) != std::string::npos, "both file and buffer parts must preserve embedded null bytes");
        session.SetMultipart({
                                 { "missing", mcr::File{ temporary.path.string() + ".missing" } }
        })
            .value();
        auto missing  = session.Post();
        passed       &= check(!missing && missing.error().code == mcr::ErrorCode::READ_ERROR, "missing MIME files must fail preparation");
        session.SetMultipart({
                                 { "recovered", "text" }
        })
            .value();
        passed &= check(session.Post().value().text.contains("name=\"recovered\""), "MIME may be replaced after a failed upload");
        return passed;
    }

    auto check_pool_and_resolve(HttpServer const& server) -> bool {
        auto      pool = mcr::ConnectionPool::Create().value();
        int const before{ server.Connections() };
        for (int index{}; index < 2; ++index) {
            auto  session_owner = mcr::Session::Create().value();
            auto& session       = *session_owner;
            configure(session, server);
            session.SetOption(pool).value();
            if (!check(!session.Get().value().error, "sessions attached to a pool must transfer successfully")) {
                return false;
            }
        }
        bool  passed{ check(server.Connections() == before + 1, "a shared pool must reuse a connection across session lifetimes") };
        auto  session_owner = mcr::Session::Create().value();
        auto& session       = *session_owner;
        configure(session, server);
        auto const address{ server.Url("").Str() };
        auto const port{ static_cast<std::uint16_t>(std::stoul(address.substr(address.rfind(':') + 1))) };
        session.SetOption(mcr::options::Resolve{ "session.test.invalid", "127.0.0.1", { port } }).value();
        session.SetUrl(mcr::Url{ std::format("http://session.test.invalid:{}/hello", port) }).value();
        auto response{ session.Get().value() };
        passed &= check(!response.error && response.text == "Hello session!", "DNS overrides must resolve a controlled hostname to the fixture");
        session.SetOption(std::vector<mcr::options::Resolve>{}).value();
        session.SetUrl(server.Url()).value();
        passed &= check(!session.Get().value().error, "clearing the configured resolve list must permit ordinary loopback requests");
        session.SetProxies({
                               {     "http", address },
                               { "no_proxy",      "" }
        })
            .value();
        session.SetUrl(mcr::Url{ "http://proxy-target.test.invalid/hello" }).value();
        response  = session.Get().value();
        passed   &= check(!response.error && response.header["X-Target"] == "http://proxy-target.test.invalid/hello", "proxy and no_proxy options must route an absolute-form request through the fixture");
        session.SetProxies({
                               { "http", "" }
        })
            .value();
        session.SetUrl(server.Url()).value();
        response  = session.Get().value();
        passed   &= check(!response.error && response.header["X-Target"] == "/hello", "replacing the proxy configuration must restore direct requests");
        return passed;
    }

    auto check_downloads_and_async(HttpServer const& server) -> bool {
        auto session{ mcr::Session::Create().value() };
        configure(*session, server);
        std::string normal, download;
        session->SetWriteCallback(mcr::WriteCallback{ [&](auto data, auto) { normal += data; return true; } }).value();
        session->SetBody(mcr::Body{ "ignored while downloading" }).value();
        auto response{ session->Download(mcr::WriteCallback{ [&](auto data, auto) { download += data; return true; } }).value() };
        bool passed{ check(!response.error && response.text.empty() && download == "Hello session!" && normal.empty(), "downloads must override stored content and body consumers for this transfer") };
        (void)session->Get().value();
        passed &= check(normal == "Hello session!" && download == normal, "ordinary writes must resume after callback downloads");
        session->SetWriteCallback({}).value();
        session->RemoveContent().value();
        TempFile temporary;
        {
            std::ofstream file{ temporary.path, std::ios::binary };
            auto          future{ session->DownloadAsync(file).value() };
            auto          result{ future.Get().value() };
            passed &= check(!result.error && result.text.empty(), "asynchronous file download must complete without buffering the body");
        }
        std::ifstream file{ temporary.path, std::ios::binary };
        std::string   bytes{ std::istreambuf_iterator<char>{ file }, {} };
        file.close();
        passed &= check(bytes == "Hello session!" && session->Get().value().text == bytes, "file download must not leave a dangling stream pointer for subsequent GET");
        std::ofstream closed;
        passed &= check(session->Download(closed).value().error.code == mcr::ErrorCode::WRITE_ERROR, "failed file writes must be reported as transport failures");
        session->PrepareHead().value();
        auto prepared{ session->Complete(curl_easy_perform(session->GetCurlHolder()->handle)).value() };
        passed &= check(!prepared.error && prepared.text.empty() && prepared.status_code == 200, "Prepare/Complete must support externally driven transfers");
        auto callback{ session->GetCallback([prefix = std::make_unique<std::string>("result: ")](mcr::Result<mcr::Response> result_result) { auto result = std::move(result_result).value();  return *prefix + result.text; }).value() };
        passed &= check(callback.Get() == "result: Hello session!", "continuations must support move-only captures");
        auto future{ session->GetAsync().value() };
        session.reset();
        passed &= check(future.Get().value().text == "Hello session!", "async work must keep its session alive after external owners release it");
        static_assert(!std::is_default_constructible_v<mcr::Session>);
        return passed;
    }
} // namespace

namespace {
    /**
     * @brief Expose continuation helpers to small test interceptor functions.
     */
    class FunctionalInterceptor : public mcr::Interceptor {
    public:
        using Interceptor::Proceed;
        std::function<mcr::Result<mcr::Response>(mcr::Session&)> action;

        explicit FunctionalInterceptor(decltype(action) value) : action{ std::move(value) } {}

        auto Intercept(mcr::Session& session) -> mcr::Result<mcr::Response> override { return action(session); }
    };

    class FunctionalMultiInterceptor : public mcr::InterceptorMulti {
    public:
        using InterceptorMulti::PrepareDownloadSession;
        using InterceptorMulti::Proceed;
        std::function<mcr::Result<std::vector<mcr::Response>>(mcr::MultiPerform&)> action;

        explicit FunctionalMultiInterceptor(decltype(action) value) : action{ std::move(value) } {}

        auto Intercept(mcr::MultiPerform& multi) -> mcr::Result<std::vector<mcr::Response>> override { return action(multi); }
    };

    template <typename Fn>
    auto rejects(Fn&& action) -> bool {
        try {
            if constexpr (std::is_void_v<std::invoke_result_t<Fn>>) {
                action();
            } else {
                return !action();
            }
        } catch (std::logic_error const&) {
            return true;
        }
        return false;
    }

    auto make_session(HttpServer const& server, std::string_view path = "/hello") -> std::shared_ptr<mcr::Session> {
        auto result{ mcr::Session::Create().value() };
        configure(*result, server, path);
        return result;
    }

    auto check_proxy_auth(HttpServer const& server) -> bool {
        auto                              encoded = mcr::options::EncodedAuthentication::Create("u$er", "p@ss").value();
        bool                              passed{ check(encoded.GetUsername() == "u%24er" && encoded.GetPassword() == "p%40ss", "credential accessors must retain cpr's percent-encoded storage") };
        mcr::options::ProxyAuthentication auth{
            { "http", encoded }
        };
        passed              &= check(auth.Has("http") && !auth.Has("HTTP") && auth.GetUsername("absent").empty() && auth.Has("absent"), "proxy lookup must retain exact keys and insertion semantics");
        passed              &= check(rejects([&] { std::as_const(auth).GetPasswordUnderlying("missing"); }), "const secure credential lookup must reject absent protocols");
        auto  session_owner  = mcr::Session::Create().value();
        auto& session        = *session_owner;
        configure(session, server);
        session.SetUrl(mcr::Url{ "http://proxy-target.test.invalid/proxy-auth" }).value();
        session.SetProxies({
                               {     "http", server.Url("").Str() },
                               { "no_proxy",                   "" }
        })
            .value();
        session.SetOption(auth).value();
        auto response{ session.Get().value() };
        passed &= check(response.status_code == 200 && response.header["X-Request-Proxy-Authorization"] == "Basic dSRlcjpwQHNz", "special characters must be decoded before curl encodes HTTP proxy authentication");
        session.SetProxyAuth({}).value();
        response  = session.Get().value();
        passed   &= check(response.status_code == 407 && response.header["X-Request-Proxy-Authorization"].empty(), "replacing proxy credentials must remove the previous authorization");
        session.SetProxyAuth(mcr::options::ProxyAuthentication{
                                 { "http", mcr::options::EncodedAuthentication::Create("u$er", "p@ss").value() }
        })
            .value();
        passed &= check(session.Get().value().status_code == 200, "proxy authentication must recover after credentials are restored");
        session.SetUrl(server.Url()).value();
        session.SetProxies({
                               { "http", "" }
        })
            .value();
        passed &= check(session.Get().value().header["X-Request-Proxy-Authorization"].empty(), "direct origin requests must never receive proxy credentials");
        return passed;
    }

    auto check_interceptors(HttpServer const& server) -> bool {
        using I             = FunctionalInterceptor;
        auto  session_owner = mcr::Session::Create().value();
        auto& session       = *session_owner;
        configure(session, server, "/echo");
        std::vector<int> order;
        session.AddInterceptor(std::make_shared<I>([&](mcr::Session& current) {
                   order.push_back(1);
                   current.SetBody(mcr::Body{ "from interceptor" }).value();
                   (void)I::Proceed(current).value();
                   auto response{ I::Proceed(current).value() };
                   order.push_back(3);
                   response.status_code = 299;
                   return response;
               }))
            .value();
        session.AddInterceptor(std::make_shared<I>([&](mcr::Session& current) { order.push_back(2); return I::Proceed(current).value(); })).value();
        auto response{ session.Post().value() };
        bool passed{ check(response.status_code == 299 && response.text == "from interceptor" && order == std::vector<int>{ 1, 2, 2, 3 }, "retries must execute only downstream interceptors and see option changes") };
        order.clear();
        passed                &= check(session.Post().value().status_code == 299 && order == std::vector<int>{ 1, 2, 2, 3 }, "a new request must restart the full chain");
        passed                &= check(rejects([&] { return session.AddInterceptor(nullptr); }), "null interceptors must be rejected");

        auto  synthetic_owner  = mcr::Session::Create().value();
        auto& synthetic        = *synthetic_owner;
        synthetic.SetUrl(mcr::Url{ "http://[" }).value();
        synthetic.AddInterceptor(std::make_shared<I>([](auto&) { mcr::Response result; result.status_code = 204; return result; })).value();
        passed               &= check(synthetic.Get().value().status_code == 204, "a synthetic response must short circuit before curl performs the request");

        auto  throwing_owner  = mcr::Session::Create().value();
        auto& throwing        = *throwing_owner;
        configure(throwing, server);
        int attempts{};
        throwing.AddInterceptor(std::make_shared<I>([&](mcr::Session& current) -> mcr::Response {
                    if (++attempts == 1) {
                        throw std::runtime_error{ "interceptor failure" };
                    }
                    passed &= check(rejects([&] { return current.AddInterceptor(nullptr); }), "active interceptor chains must reject modifications");
                    return I::Proceed(current).value();
                }))
            .value();
        try {
            (void)throwing.Get().value();
            passed &= check(false, "interceptor exceptions must propagate");
        } catch (std::runtime_error const&) {}
        passed              &= check(throwing.Get().value().text == "Hello session!" && attempts == 2, "interceptor cursor must recover after an exception");

        auto  changed_owner  = mcr::Session::Create().value();
        auto& changed        = *changed_owner;
        configure(changed, server, "/echo");
        changed.AddInterceptor(std::make_shared<I>([](mcr::Session& current) {
                   current.SetBody(mcr::Body{ "switched" }).value();
                   return I::Proceed(current, I::ProceedHttpMethod::POST_REQUEST).value();
               }))
            .value();
        response              = changed.Head().value();
        passed               &= check(response.text == "switched" && response.header["X-Method"] == "POST", "interceptors must be able to replace the original method");

        auto  download_owner  = mcr::Session::Create().value();
        auto& download        = *download_owner;
        configure(download, server);
        download.AddInterceptor(std::make_shared<I>([](mcr::Session& current) { (void)I::Proceed(current).value(); return I::Proceed(current).value(); })).value();
        std::string bytes;
        response  = download.Download(mcr::WriteCallback{ [&](auto data, auto) { bytes += data; return true; } }).value();
        passed   &= check(!response.error && response.text.empty() && bytes == "Hello session!Hello session!", "download retries must retain their callback destination");
        TempFile temporary;
        {
            std::ofstream file{ temporary.path, std::ios::binary };
            response = download.Download(file).value();
        }
        passed &= check(!response.error && std::filesystem::file_size(temporary.path) == 28, "download retries must retain their file destination");
        auto asynchronous{ make_session(server) };
        asynchronous->AddInterceptor(std::make_shared<I>([](mcr::Session& current) { auto result{ I::Proceed(current).value() }; result.status_code = 201; return result; }));
        passed &= check(asynchronous->GetAsync().value().Get().value().status_code == 201, "async methods must run the same interceptor chain");
        return passed;
    }

    auto check_multi(HttpServer const& server) -> bool {
        using M = mcr::MultiPerform;
        using H = M::HttpMethod;
        auto first{ make_session(server, "/barrier") };
        auto second{ make_session(server, "/barrier") };
        M    multi;
        multi.AddSession(first).value();
        multi.AddSession(second).value();
        bool passed{ check(first.use_count() == 2 && second.use_count() == 2, "batches must own registered sessions") };
        passed &= check(rejects([&] { return multi.Perform(); }) && rejects([&] { return multi.AddSession(first); }) && rejects([&] { return multi.AddSession(nullptr); }), "undefined methods and invalid registrations must fail before transfer");
        passed &= check(rejects([&] { return first->Get(); }), "registered sessions must reject easy-perform outside their batch");
        M other;
        passed &= check(rejects([&] { return other.AddSession(first); }) && rejects([&] { return other.RemoveSession(first); }), "ownership must survive failed registration and removal in another batch");
        auto responses{ multi.Get().value() };
        passed &= check(responses.size() == 2 && responses[0].status_code == 200 && responses[1].status_code == 200, "both requests must reach the fixture barrier concurrently");
        first->SetUrl(server.Url("/slow")).value();
        second->SetUrl(server.Url("/echo")).value();
        second->SetBody(mcr::Body{ "second response" }).value();
        multi.GetSessions().value().get()[0].second  = H::GET_REQUEST;
        multi.GetSessions().value().get()[1].second  = H::POST_REQUEST;
        responses                                    = multi.Perform().value();
        passed                                      &= check(responses[0].text == "Hello session!" && responses[1].text == "second response" && responses[1].header["X-Method"] == "POST", "mixed-method batch results must retain registration order despite completion order");
        second->SetUrl(mcr::Url{ "http://[" }).value();
        responses  = multi.Perform().value();
        passed    &= check(responses.size() == 2 && !responses[0].error && responses[1].error.code == mcr::ErrorCode::URL_MALFORMAT, "failed transfers must still occupy their registered response position");
        first->SetUrl(server.Url()).value();
        second->SetUrl(server.Url()).value();
        first->SetWriteCallback(mcr::WriteCallback{ [](auto, auto) -> bool { throw std::runtime_error{ "batch callback" }; } }).value();
        try {
            (void)multi.Get().value();
            passed &= check(false, "batch callback exceptions must propagate");
        } catch (std::runtime_error const& error) {
            passed &= check(error.what() == "batch callback"sv, "batch callback exceptions must retain their identity");
        }
        first->SetWriteCallback({}).value();
        passed &= check(multi.Get().value().size() == 2, "all handles must detach after a callback exception so the batch can be reused");
        multi.RemoveSession(second).value();
        passed &= check(!second->Get().value().error && second.use_count() == 1, "removal must immediately release the session and permit easy requests");
        M moved{ std::move(multi) };
        passed &= check(moved.Get().value().size() == 1 && rejects([&] { return first->Get(); }), "moving a batch must transfer its session claims");
        multi.AddSession(second).value();
        passed &= check(multi.Get().value().size() == 1, "moved-from batches must be reusable");
        moved   = std::move(multi);
        passed &= check(!first->Get().value().error && moved.Get().value().size() == 1, "move assignment must release old destination claims and retain source claims");
        moved.GetSessions().value().get().push_back(moved.GetSessions().value().get().front());
        passed &= check(rejects([&] { return moved.Get(); }), "mutable registration edits must be checked for duplicate handles");
        moved.GetSessions().value().get().pop_back();
        moved.RemoveSession(second).value();
        passed &= check(moved.Get().value().empty() && moved.Download().value().empty(), "empty batches and downloads must succeed without indexing nonexistent sessions");
        return passed;
    }

    auto check_multi_interceptors_and_downloads(HttpServer const& server) -> bool {
        using M = mcr::MultiPerform;
        using I = FunctionalMultiInterceptor;
        auto first{ make_session(server) };
        auto second{ make_session(server) };
        M    multi;
        multi.AddSession(first, M::HttpMethod::DOWNLOAD_REQUEST).value();
        multi.AddSession(second, M::HttpMethod::DOWNLOAD_REQUEST).value();
        int calls{};
        multi.AddInterceptor(std::make_shared<I>([&](M& current) { ++calls; (void)I::Proceed(current).value(); return I::Proceed(current).value(); })).value();
        std::vector<int> order;
        multi.AddInterceptor(std::make_shared<I>([&](M& current) { order.push_back(2); auto results{ I::Proceed(current).value() }; results[0].status_code = 299; return results; })).value();
        std::string        bytes;
        mcr::WriteCallback writer{ [&](auto data, auto) { bytes += data; return true; } };
        TempFile           temporary;
        bool               passed{ check(rejects([&] { return multi.Download(writer); }), "download destinations must match the registration count") };
        {
            std::ofstream file{ temporary.path, std::ios::binary };
            auto          responses{ multi.PerformDownload(writer, std::ref(file)).value() };
            passed &= check(responses.size() == 2 && responses[0].status_code == 299 && responses[0].text.empty() && calls == 1 && order == std::vector<int>{ 2, 2 }, "batch retry chains must preserve ordered download responses");
        }
        passed &= check(bytes == "Hello session!Hello session!" && std::filesystem::file_size(temporary.path) == 28, "batch retries must preserve callback and borrowed stream destinations");
        passed &= check(rejects([&] { return multi.Perform(); }), "completed download batches must not retain borrowed destination pointers");
        auto results{ multi.Get().value() };
        passed &= check(results[0].text == "Hello session!" && results[1].text == "Hello session!", "ordinary requests must work after downloads and restart batch interceptors");
        first->AddInterceptor(std::make_shared<FunctionalInterceptor>([](auto&) { mcr::Response response; response.status_code = 400; return response; })).value();
        passed &= check(multi.Get().value()[0].status_code == 299, "batch requests must use batch interceptors instead of individual session interceptors");
        M    throwing;
        auto third{ make_session(server) };
        throwing.AddSession(third).value();
        int attempts{};
        throwing.AddInterceptor(std::make_shared<I>([&](M& current) -> std::vector<mcr::Response> {
                    if (++attempts == 1) {
                        throw std::runtime_error{ "multi interceptor" };
                    }
                    return I::Proceed(current).value();
                }))
            .value();
        try {
            (void)throwing.Get().value();
            passed &= check(false, "batch interceptor exceptions must propagate");
        } catch (std::runtime_error const&) {}
        passed &= check(throwing.Get().value()[0].status_code == 200 && attempts == 2, "batch interceptor cursors must recover after exceptions");
        M    converted;
        auto fourth{ make_session(server) };
        converted.AddSession(fourth).value();
        converted.AddInterceptor(std::make_shared<I>([&](M& current) {
                     current.GetSessions().value().get()[0].second = M::HttpMethod::DOWNLOAD_REQUEST;
                     I::PrepareDownloadSession(current, 0, writer).value();
                     return I::Proceed(current).value();
                 }))
            .value();
        bytes.clear();
        passed &= check(converted.Get().value()[0].text.empty() && bytes == "Hello session!", "batch interceptors must be able to select download methods and destinations");
        return passed;
    }
} // namespace

auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        HttpServer server;
        passed &= check_reuse(server);
        passed &= check_methods_and_content(server);
        passed &= check_options(server);
        passed &= check_redirects_and_failures(server);
        passed &= check_callbacks(server);
        passed &= check_multipart_files(server);
        passed &= check_pool_and_resolve(server);
        passed &= check_downloads_and_async(server);
        passed &= check_proxy_auth(server);
        passed &= check_interceptors(server);
        passed &= check_multi(server);
        passed &= check_multi_interceptors_and_downloads(server);
        mcr::Async::Cleanup();
        server.Check();
    } catch (std::exception const& error) {
        passed = check(false, error.what());
    }
    curl_global_cleanup();
    return passed ? 0 : 1;
}
