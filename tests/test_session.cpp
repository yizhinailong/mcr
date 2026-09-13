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
static_assert(std::is_same_v<mcr::AsyncResponse, mcr::utils::AsyncWrapper<mcr::Response>>);
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
        session.SetOption(server.Url(path));
        session.SetOption(mcr::options::Proxies{
            {     "http",  "" },
            { "no_proxy", "*" }
        });
        session.SetOption(mcr::options::Timeout{ 3000ms });
        session.SetOption(mcr::options::HttpVersion{ mcr::options::HttpVersionCode::VERSION_1_1 });
    }

    auto check_reuse(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server);
        auto const before{ server.Connections() };
        auto       first{ session.Get() };
        bool       passed{ check(first.status_code == 200 && !first.error && first.text == "Hello session!", "GET must return body and status") };
        passed &= check(first.url == server.Url() && first.header["content-type"] == "text/plain" && first.reason == "OK" && first.status_line == "HTTP/1.1 200 OK", "response must expose parsed final headers and effective URL");
        passed &= check(first.downloaded_bytes == 14 && first.uploaded_bytes == 0 && first.primary_ip == "127.0.0.1" && first.primary_port != 0 && first.elapsed >= 0 && first.GetCertInfos().empty(), "HTTP metadata must be initialized and plain HTTP certificates must be empty");
        for (int i{}; i < 4; ++i) {
            passed &= check(session.Get().text == first.text, "repeated GET must clear prior buffers");
        }
        passed &= check(server.Connections() == before + 1, "sequential requests must reuse the same connection");
        passed &= check(session.Head().text.empty() && session.GetDownloadFileLength() == 14 && session.Get().text == first.text, "HEAD and length probing must not corrupt subsequent GET");
        session.SetUrl(server.Url("/error"));
        auto error{ session.Get() };
        passed &= check(error.status_code == 404 && !error.error && error.text == "missing" && first.status_code == 200 && first.text == "Hello session!", "HTTP errors are responses and previous response snapshots stay independent");
        session.SetUrl(mcr::Url{ "http://[" });
        auto malformed{ session.Get() };
        passed &= check(malformed.error.code == mcr::ErrorCode::URL_MALFORMAT && !malformed.error.message.empty() && malformed.text.empty(), "malformed URL must return an initialized transport failure");
        session.SetUrl(server.Url());
        passed &= check(!session.Get().error, "successful reuse must clear previous curl errors");
        return passed;
    }

    auto check_methods_and_content(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server, "/echo");
        using Method = mcr::Response (mcr::Session::*)();
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
            session.SetBody(mcr::Body{ binary });
            auto response{ (session.*method)() };
            passed &= check(!response.error && response.header["X-Method"] == name && response.text == binary && response.uploaded_bytes == 10, "methods must preserve binary bodies and use their own HTTP verb after reuse");
            session.RemoveContent();
            response  = (session.*method)();
            passed   &= check(!response.error && response.header["X-Method"] == name && response.text.empty(), "RemoveContent must detach the old body for every method");
        }
        session.SetOption(mcr::Payload{
            { "space key", "x+y" },
            {     "empty",    "" }
        });
        auto payload{ session.Post() };
        passed &= check(payload.text == "space key=x%2By&empty=" && payload.header["X-Request-Content-Type"] == "application/x-www-form-urlencoded", "payloads must preserve cpr's raw keys and encode values using the session holder");
        std::string borrowed{ binary };
        session.SetOption(mcr::BodyView{ borrowed });
        passed      &= check(session.Post().text == binary, "BodyView must preserve exact byte length");
        borrowed[0]  = 'F';
        passed      &= check(session.Put().text == borrowed, "BodyView must observe borrowed storage on the next request");
        session.SetBodyView({});
        passed &= check(session.Post().text.empty(), "null empty BodyView must produce a zero-length body without reading stdin");
        session.SetBody(mcr::Body{ "retained" });
        passed &= check(session.Head().text.empty() && session.Post().text == "retained", "HEAD must suppress but retain configured content");
        session.SetOption(mcr::Multipart{
            {   "text", std::string_view{ binary } },
            { "number",                         42 }
        });
        auto multipart{ session.Post() };
        passed &= check(!multipart.error && multipart.text.contains(binary) && multipart.text.contains("name=\"number\"") && multipart.header["X-Request-Content-Type"].starts_with("multipart/form-data; boundary="), "multipart must encode text fields including embedded nulls");
        passed &= check(session.Get().header["X-Method"] == "GET", "GET with retained multipart must remain GET");
        session.SetBody(mcr::Body{ "replacement" });
        passed &= check(session.Post().text == "replacement", "switching MIME to a body must detach the MIME tree");
        session.SetMultipart({
            { "again", "value" }
        });
        passed &= check(session.Post().text.contains("name=\"again\""), "switching a body to MIME must detach the old POST fields");
        session.RemoveContent();
        passed &= check(session.Get().text.empty() && std::holds_alternative<std::monostate>(session.GetContent()), "removing multipart must leave no dangling MIME or body state");
        return passed;
    }

    auto check_options(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server, "/echo?existing=1#fragment");
        session.SetOption(mcr::Parameters{
            {  "a b", "x+y" },
            { "flag",    "" }
        });
        session.SetOption(mcr::Header{
            { "X-Custom", "old" },
            {  "X-Empty",    "" }
        });
        session.UpdateHeader({
            { "x-custom", "new" }
        });
        session.GetHeader()["Another"] = "value";
        session.SetOption(mcr::UserAgent{ "session-test" });
        session.SetOption(mcr::options::AcceptEncoding{ mcr::options::AcceptEncodingMethods::disabled });
        session.SetOption(mcr::options::ReserveSize{ 4096 });
        session.SetOption(mcr::options::ConnectTimeout{ 1000ms });
        session.SetOption(mcr::options::LimitRate{ 0, 0 });
        auto response{ session.Get() };
        bool passed{ check(response.header["X-Target"] == "/echo?existing=1&a%20b=x%2By&flag" && session.GetFullRequestUrl() == server.Url("/echo?existing=1&a%20b=x%2By&flag#fragment").Str(), "parameters must merge with existing queries before fragments") };
        passed &= check(response.header["X-Request-X-Custom"] == "new" && response.header["X-Request-User-Agent"] == "session-test" && response.header["X-Request-Accept-Encoding"].empty(), "header replacement, user agent, and disabled encoding must reach the server");
        passed &= check(std::as_const(session).GetHeader().size() == 3, "case-insensitive header merging must retain unrelated entries");
        session.SetParameters({});
        session.SetUrl(server.Url("/echo"));
        session.SetOption(mcr::options::Authentication{ "user", "password", mcr::options::AuthMode::BASIC });
        passed &= check(session.Get().header["X-Request-Authorization"] == "Basic dXNlcjpwYXNzd29yZA==", "basic authentication must reach the server");
        session.SetOption(mcr::options::Bearer{ "token" });
        passed &= check(session.Get().header["X-Request-Authorization"] == "Bearer token", "bearer authentication must replace basic authentication");
        session.SetUrl(server.Url("/cookie"));
        auto cookie{ session.Get() };
        session.SetUrl(server.Url("/echo"));
        passed &= check(!cookie.cookies.empty() && session.Get().header["X-Request-Cookie"] == "stored=yes", "cookies must persist in the session engine");
        session.SetOption(mcr::Cookies{
            { "explicit", "a b" }
        });
        passed &= check(session.Get().header["X-Request-Cookie"] == "explicit=a%20b;", "SetCookies must replace stored cookies and retain cpr's trailing separator");
        session.SetUrl(server.Url("/range"));
        session.SetOption(mcr::options::Range{ 2, 5 });
        auto range{ session.Get() };
        passed &= check(range.status_code == 206 && range.text == "2345", "byte ranges must reach curl");
        session.SetUrl(server.Url("/echo"));
        passed &= check(session.Put().header["X-Request-Range"].empty(), "PUT must clear a previous range as in cpr");
        session.SetHeader({
            { "Expect", "100-continue" }
        });
        session.SetBody(mcr::Body{ "continue" });
        passed &= check(session.Post().text == "continue", "explicit Expect must be preserved and interim headers parsed");
        return passed;
    }

    auto check_redirects_and_failures(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server, "/redirect");
        auto redirect{ session.Get() };
        bool passed{ check(!redirect.error && redirect.status_code == 200 && redirect.redirect_count == 1 && redirect.url == server.Url() && redirect.text == "Hello session!", "default redirects must follow Location") };
        passed &= check(redirect.raw_header.contains("302 Found") && !redirect.header.contains("X-Intermediate"), "raw headers retain redirects while parsed headers describe the final response");
        session.SetOption(mcr::options::Redirect{ false });
        passed &= check(session.Get().status_code == 302, "redirect following may be disabled");
        session.SetRedirect(mcr::options::Redirect{ 1L });
        session.SetUrl(server.Url("/loop"));
        passed &= check(session.Get().error.code == mcr::ErrorCode::TOO_MANY_REDIRECTS, "redirect limits must surface as transport errors");
        session.SetUrl(server.Url("/partial"));
        auto partial{ session.Get() };
        passed &= check(partial.error.code == mcr::ErrorCode::PARTIAL_FILE && partial.text == "short", "partial transfer failures must preserve received bytes");
        session.SetUrl(server.Url("/slow"));
        session.SetTimeout(mcr::options::Timeout{ 25ms });
        passed &= check(session.Get().error.code == mcr::ErrorCode::OPERATION_TIMEDOUT, "timeouts must cancel a delayed local response");
        session.SetTimeout(mcr::options::Timeout{ 3000ms });
        session.SetUrl(server.Url());
        auto cancellation{ std::make_shared<std::atomic_bool>(true) };
        session.SetCancellationParam(cancellation);
        int progress{};
        session.SetProgressCallback(mcr::ProgressCallback{ [&](auto, auto, auto, auto, auto) { ++progress; return true; } });
        passed &= check(session.Get().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK && progress == 0, "cancellation must precede the progress observer even if the observer is replaced later");
        cancellation->store(false);
        passed &= check(!session.Get().error && progress > 0, "cleared cancellation must allow reuse with progress callbacks");
        session.SetCancellationParam(nullptr);
        session.SetProgressCallback(mcr::ProgressCallback{ [](auto, auto, auto, auto, auto) { return false; } });
        passed &= check(session.Get().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "false progress callbacks must abort");
        session.SetProgressCallback({});
        passed &= check(!session.Get().error, "cleared progress callbacks must restore transfers");
        return passed;
    }

    auto check_callbacks(HttpServer const& server) -> bool {
        mcr::Session session;
        configure(session, server);
        std::string body, headers;
        session.SetOption(mcr::WriteCallback{ [&](std::string_view data, auto) { body += data; return true; } });
        session.SetOption(mcr::HeaderCallback{ [&](std::string_view data, auto) { headers += data; return true; } });
        auto response{ session.Get() };
        bool passed{ check(!response.error && response.text.empty() && body == "Hello session!" && response.raw_header == headers && response.header["Content-Type"] == "text/plain", "callbacks must stream the body and observe headers without losing response metadata") };
        session.SetWriteCallback(mcr::WriteCallback{ [](auto, auto) { return false; } });
        passed &= check(session.Get().error.code == mcr::ErrorCode::WRITE_ERROR, "false write callbacks must abort");
        session.SetWriteCallback(mcr::WriteCallback{ [](auto, auto) -> bool { throw std::runtime_error{ "callback failed" }; } });
        try {
            (void)session.Get();
            passed &= check(false, "write callback exceptions must be rethrown");
        } catch (std::runtime_error const& error) {
            passed &= check(error.what() == "callback failed"sv, "callback exception identity must be preserved");
        }
        session.SetWriteCallback({});
        passed &= check(session.Get().text == "Hello session!", "empty write callback must restore buffering after an exception");
        session.SetHeaderCallback(mcr::HeaderCallback{ [](auto, auto) { return false; } });
        passed &= check(session.Get().error.code == mcr::ErrorCode::WRITE_ERROR, "header callbacks must be able to abort");
        session.SetHeaderCallback({});
        session.SetUrl(server.Url("/echo"));
        session.SetReadCallback(mcr::ReadCallback{ [](char*, std::size_t& length, auto) { ++length; return true; } });
        passed &= check(session.Post().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "oversized producer counts must abort without reading outside the curl buffer");
        session.SetReadCallback(mcr::ReadCallback{ [](char*, std::size_t&, auto) -> bool { throw std::runtime_error{ "read failed" }; } });
        try {
            (void)session.Post();
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
                                                } });
            auto upload{ session.Post() };
            passed &= check(!upload.error && upload.text == source, "fixed-length and chunked read uploads must retain binary bytes");
            offset  = 0;
            passed &= check(session.Put().text == source, "read uploads must work when switching POST to PUT");
        }
        session.SetReadCallback({});
        passed &= check(session.Post().text.empty(), "cleared read callbacks must not read old upload state");
        session.SetUrl(server.Url("/sse"));
        std::vector<std::string> events;
        session.SetOption(mcr::ServerSentEventCallback{ [&](mcr::ServerSentEvent&& event, auto) { events.push_back(std::move(event.data)); return true; } });
        auto first{ session.Get() };
        auto second{ session.Get() };
        passed &= check(!first.error && !second.error && first.text.empty() && events == std::vector<std::string>{ "first", "second", "first", "second" }, "SSE must reset unfinished parser state between requests");
        session.SetWriteCallback({});
        passed &= check(session.Get().text.starts_with("id: 7"), "raw write selection must clear SSE consumption");
        int debug_count{};
        session.SetDebugCallback(mcr::DebugCallback{ [&](auto, auto, auto) { ++debug_count; } });
        passed &= check(!session.Get().error && debug_count > 0, "debug callbacks must enable curl diagnostics");
        session.SetDebugCallback(mcr::DebugCallback{ [](auto, auto, auto) { throw std::runtime_error{ "debug failed" }; } });
        try {
            (void)session.Get();
            passed &= check(false, "debug exceptions must propagate after curl returns");
        } catch (std::runtime_error const& error) {
            passed &= check(error.what() == "debug failed"sv, "debug callback exception identity must be preserved");
        }
        session.SetDebugCallback({});
        passed &= check(!session.Get().error, "cleared debug callbacks must permit reuse after an exception");
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
        mcr::Session session;
        configure(session, server, "/echo");
        TempFile          temporary;
        std::string const binary{ "file\0bytes", 10 };
        {
            std::ofstream file{ temporary.path, std::ios::binary };
            file.write(binary.data(), static_cast<std::streamsize>(binary.size()));
        }
        session.SetMultipart({
            { "file", mcr::File{ temporary.path.string(), "override.bin" }, "application/octet-stream" },
            { "buffer", mcr::Buffer{ binary.begin(), binary.end(), std::filesystem::path{ "buffer.bin" } } },
            { "empty", mcr::Buffer{ binary.begin(), binary.begin(), std::filesystem::path{ "empty.bin" } } }
        });
        auto       response{ session.Post() };
        bool       passed{ check(!response.error && response.text.contains("filename=\"override.bin\"") && response.text.contains("filename=\"buffer.bin\"") && response.text.contains("filename=\"empty.bin\"") && response.text.contains(binary), "MIME must support files, filename overrides, and binary or empty borrowed buffers") };
        auto const first{ response.text.find(binary) };
        passed &= check(first != std::string::npos && response.text.find(binary, first + binary.size()) != std::string::npos, "both file and buffer parts must preserve embedded null bytes");
        session.SetMultipart({
            { "missing", mcr::File{ temporary.path.string() + ".missing" } }
        });
        try {
            (void)session.Post();
            passed &= check(false, "a missing MIME file must fail preparation instead of uploading an empty field");
        } catch (std::runtime_error const&) {}
        session.SetMultipart({
            { "recovered", "text" }
        });
        passed &= check(session.Post().text.contains("name=\"recovered\""), "MIME may be replaced after a failed upload");
        return passed;
    }

    auto check_pool_and_resolve(HttpServer const& server) -> bool {
        mcr::ConnectionPool pool;
        int const           before{ server.Connections() };
        for (int index{}; index < 2; ++index) {
            mcr::Session session;
            configure(session, server);
            session.SetOption(pool);
            if (!check(!session.Get().error, "sessions attached to a pool must transfer successfully")) {
                return false;
            }
        }
        bool         passed{ check(server.Connections() == before + 1, "a shared pool must reuse a connection across session lifetimes") };
        mcr::Session session;
        configure(session, server);
        auto const address{ server.Url("").Str() };
        auto const port{ static_cast<std::uint16_t>(std::stoul(address.substr(address.rfind(':') + 1))) };
        session.SetOption(mcr::options::Resolve{ "session.test.invalid", "127.0.0.1", { port } });
        session.SetUrl(mcr::Url{ std::format("http://session.test.invalid:{}/hello", port) });
        auto response{ session.Get() };
        passed &= check(!response.error && response.text == "Hello session!", "DNS overrides must resolve a controlled hostname to the fixture");
        session.SetOption(std::vector<mcr::options::Resolve>{});
        session.SetUrl(server.Url());
        passed &= check(!session.Get().error, "clearing the configured resolve list must permit ordinary loopback requests");
        session.SetProxies({
            {     "http", address },
            { "no_proxy",      "" }
        });
        session.SetUrl(mcr::Url{ "http://proxy-target.test.invalid/hello" });
        response  = session.Get();
        passed   &= check(!response.error && response.header["X-Target"] == "http://proxy-target.test.invalid/hello", "proxy and no_proxy options must route an absolute-form request through the fixture");
        session.SetProxies({
            { "http", "" }
        });
        session.SetUrl(server.Url());
        response  = session.Get();
        passed   &= check(!response.error && response.header["X-Target"] == "/hello", "replacing the proxy configuration must restore direct requests");
        return passed;
    }

    auto check_downloads_and_async(HttpServer const& server) -> bool {
        auto session{ std::make_shared<mcr::Session>() };
        configure(*session, server);
        std::string normal, download;
        session->SetWriteCallback(mcr::WriteCallback{ [&](auto data, auto) { normal += data; return true; } });
        session->SetBody(mcr::Body{ "ignored while downloading" });
        auto response{ session->Download(mcr::WriteCallback{ [&](auto data, auto) { download += data; return true; } }) };
        bool passed{ check(!response.error && response.text.empty() && download == "Hello session!" && normal.empty(), "downloads must override stored content and body consumers for this transfer") };
        (void)session->Get();
        passed &= check(normal == "Hello session!" && download == normal, "ordinary writes must resume after callback downloads");
        session->SetWriteCallback({});
        session->RemoveContent();
        TempFile temporary;
        {
            std::ofstream file{ temporary.path, std::ios::binary };
            auto          future{ session->DownloadAsync(file) };
            auto          result{ future.Get() };
            passed &= check(!result.error && result.text.empty(), "asynchronous file download must complete without buffering the body");
        }
        std::ifstream file{ temporary.path, std::ios::binary };
        std::string   bytes{ std::istreambuf_iterator<char>{ file }, {} };
        file.close();
        passed &= check(bytes == "Hello session!" && session->Get().text == bytes, "file download must not leave a dangling stream pointer for subsequent GET");
        std::ofstream closed;
        passed &= check(session->Download(closed).error.code == mcr::ErrorCode::WRITE_ERROR, "failed file writes must be reported as transport failures");
        session->PrepareHead();
        auto prepared{ session->Complete(curl_easy_perform(session->GetCurlHolder()->handle)) };
        passed &= check(!prepared.error && prepared.text.empty() && prepared.status_code == 200, "Prepare/Complete must support externally driven transfers");
        auto callback{ session->GetCallback([prefix = std::make_unique<std::string>("result: ")](mcr::Response result) { return *prefix + result.text; }) };
        passed &= check(callback.Get() == "result: Hello session!", "continuations must support move-only captures");
        auto future{ session->GetAsync() };
        session.reset();
        passed &= check(future.Get().text == "Hello session!", "async work must keep its session alive after external owners release it");
        mcr::Session stack;
        try {
            (void)stack.GetAsync();
            passed &= check(false, "async stack sessions must be rejected");
        } catch (std::runtime_error const&) {}
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
        std::function<mcr::Response(mcr::Session&)> action;

        explicit FunctionalInterceptor(decltype(action) value) : action{ std::move(value) } {}

        auto Intercept(mcr::Session& session) -> mcr::Response override { return action(session); }
    };

    class FunctionalMultiInterceptor : public mcr::InterceptorMulti {
    public:
        using InterceptorMulti::PrepareDownloadSession;
        using InterceptorMulti::Proceed;
        std::function<std::vector<mcr::Response>(mcr::MultiPerform&)> action;

        explicit FunctionalMultiInterceptor(decltype(action) value) : action{ std::move(value) } {}

        auto Intercept(mcr::MultiPerform& multi) -> std::vector<mcr::Response> override { return action(multi); }
    };

    template <typename Fn>
    auto rejects(Fn&& action) -> bool {
        try {
            action();
        } catch (std::logic_error const&) {
            return true;
        }
        return false;
    }

    auto make_session(HttpServer const& server, std::string_view path = "/hello") -> std::shared_ptr<mcr::Session> {
        auto result{ std::make_shared<mcr::Session>() };
        configure(*result, server, path);
        return result;
    }

    auto check_proxy_auth(HttpServer const& server) -> bool {
        mcr::options::EncodedAuthentication encoded{ "u$er", "p@ss" };
        bool                       passed{ check(encoded.GetUsername() == "u%24er" && encoded.GetPassword() == "p%40ss", "credential accessors must retain cpr's percent-encoded storage") };
        mcr::options::ProxyAuthentication   auth{
            { "http", encoded }
        };
        passed &= check(auth.Has("http") && !auth.Has("HTTP") && auth.GetUsername("absent").empty() && auth.Has("absent"), "proxy lookup must retain exact keys and insertion semantics");
        passed &= check(rejects([&] { (void)std::as_const(auth).GetPasswordUnderlying("missing"); }), "const secure credential lookup must reject absent protocols");
        mcr::Session session;
        configure(session, server);
        session.SetUrl(mcr::Url{ "http://proxy-target.test.invalid/proxy-auth" });
        session.SetProxies({
            {     "http", server.Url("").Str() },
            { "no_proxy",                   "" }
        });
        session.SetOption(auth);
        auto response{ session.Get() };
        passed &= check(response.status_code == 200 && response.header["X-Request-Proxy-Authorization"] == "Basic dSRlcjpwQHNz", "special characters must be decoded before curl encodes HTTP proxy authentication");
        session.SetProxyAuth({});
        response  = session.Get();
        passed   &= check(response.status_code == 407 && response.header["X-Request-Proxy-Authorization"].empty(), "replacing proxy credentials must remove the previous authorization");
        session.SetProxyAuth(mcr::options::ProxyAuthentication{
            { "http", mcr::options::EncodedAuthentication{ "u$er", "p@ss" } }
        });
        passed &= check(session.Get().status_code == 200, "proxy authentication must recover after credentials are restored");
        session.SetUrl(server.Url());
        session.SetProxies({
            { "http", "" }
        });
        passed &= check(session.Get().header["X-Request-Proxy-Authorization"].empty(), "direct origin requests must never receive proxy credentials");
        return passed;
    }

    auto check_interceptors(HttpServer const& server) -> bool {
        using I = FunctionalInterceptor;
        mcr::Session session;
        configure(session, server, "/echo");
        std::vector<int> order;
        session.AddInterceptor(std::make_shared<I>([&](mcr::Session& current) {
            order.push_back(1);
            current.SetBody(mcr::Body{ "from interceptor" });
            (void)I::Proceed(current);
            auto response{ I::Proceed(current) };
            order.push_back(3);
            response.status_code = 299;
            return response;
        }));
        session.AddInterceptor(std::make_shared<I>([&](mcr::Session& current) { order.push_back(2); return I::Proceed(current); }));
        auto response{ session.Post() };
        bool passed{ check(response.status_code == 299 && response.text == "from interceptor" && order == std::vector<int>{ 1, 2, 2, 3 }, "retries must execute only downstream interceptors and see option changes") };
        order.clear();
        passed &= check(session.Post().status_code == 299 && order == std::vector<int>{ 1, 2, 2, 3 }, "a new request must restart the full chain");
        passed &= check(rejects([&] { session.AddInterceptor(nullptr); }), "null interceptors must be rejected");

        mcr::Session synthetic;
        synthetic.SetUrl(mcr::Url{ "http://[" });
        synthetic.AddInterceptor(std::make_shared<I>([](auto&) { mcr::Response result; result.status_code = 204; return result; }));
        passed &= check(synthetic.Get().status_code == 204, "a synthetic response must short circuit before curl performs the request");

        mcr::Session throwing;
        configure(throwing, server);
        int attempts{};
        throwing.AddInterceptor(std::make_shared<I>([&](mcr::Session& current) -> mcr::Response {
            if (++attempts == 1) {
                throw std::runtime_error{ "interceptor failure" };
            }
            passed &= check(rejects([&] { current.AddInterceptor(nullptr); }), "active interceptor chains must reject modifications");
            return I::Proceed(current);
        }));
        try {
            (void)throwing.Get();
            passed &= check(false, "interceptor exceptions must propagate");
        } catch (std::runtime_error const&) {}
        passed &= check(throwing.Get().text == "Hello session!" && attempts == 2, "interceptor cursor must recover after an exception");

        mcr::Session changed;
        configure(changed, server, "/echo");
        changed.AddInterceptor(std::make_shared<I>([](mcr::Session& current) {
            current.SetBody(mcr::Body{ "switched" });
            return I::Proceed(current, I::ProceedHttpMethod::POST_REQUEST);
        }));
        response  = changed.Head();
        passed   &= check(response.text == "switched" && response.header["X-Method"] == "POST", "interceptors must be able to replace the original method");

        mcr::Session download;
        configure(download, server);
        download.AddInterceptor(std::make_shared<I>([](mcr::Session& current) { (void)I::Proceed(current); return I::Proceed(current); }));
        std::string bytes;
        response  = download.Download(mcr::WriteCallback{ [&](auto data, auto) { bytes += data; return true; } });
        passed   &= check(!response.error && response.text.empty() && bytes == "Hello session!Hello session!", "download retries must retain their callback destination");
        TempFile temporary;
        {
            std::ofstream file{ temporary.path, std::ios::binary };
            response = download.Download(file);
        }
        passed &= check(!response.error && std::filesystem::file_size(temporary.path) == 28, "download retries must retain their file destination");
        auto asynchronous{ make_session(server) };
        asynchronous->AddInterceptor(std::make_shared<I>([](mcr::Session& current) { auto result{ I::Proceed(current) }; result.status_code = 201; return result; }));
        passed &= check(asynchronous->GetAsync().Get().status_code == 201, "async methods must run the same interceptor chain");
        return passed;
    }

    auto check_multi(HttpServer const& server) -> bool {
        using M = mcr::MultiPerform;
        using H = M::HttpMethod;
        auto first{ make_session(server, "/barrier") };
        auto second{ make_session(server, "/barrier") };
        M    multi;
        multi.AddSession(first);
        multi.AddSession(second);
        bool passed{ check(first.use_count() == 2 && second.use_count() == 2, "batches must own registered sessions") };
        passed &= check(rejects([&] { (void)multi.Perform(); }) && rejects([&] { multi.AddSession(first); }) && rejects([&] { multi.AddSession(nullptr); }), "undefined methods and invalid registrations must fail before transfer");
        passed &= check(rejects([&] { (void)first->Get(); }), "registered sessions must reject easy-perform outside their batch");
        M other;
        passed &= check(rejects([&] { other.AddSession(first); }) && rejects([&] { other.RemoveSession(first); }), "ownership must survive failed registration and removal in another batch");
        auto responses{ multi.Get() };
        passed &= check(responses.size() == 2 && responses[0].status_code == 200 && responses[1].status_code == 200, "both requests must reach the fixture barrier concurrently");
        first->SetUrl(server.Url("/slow"));
        second->SetUrl(server.Url("/echo"));
        second->SetBody(mcr::Body{ "second response" });
        multi.GetSessions()[0].second  = H::GET_REQUEST;
        multi.GetSessions()[1].second  = H::POST_REQUEST;
        responses                      = multi.Perform();
        passed                        &= check(responses[0].text == "Hello session!" && responses[1].text == "second response" && responses[1].header["X-Method"] == "POST", "mixed-method batch results must retain registration order despite completion order");
        second->SetUrl(mcr::Url{ "http://[" });
        responses  = multi.Perform();
        passed    &= check(responses.size() == 2 && !responses[0].error && responses[1].error.code == mcr::ErrorCode::URL_MALFORMAT, "failed transfers must still occupy their registered response position");
        first->SetUrl(server.Url());
        second->SetUrl(server.Url());
        first->SetWriteCallback(mcr::WriteCallback{ [](auto, auto) -> bool { throw std::runtime_error{ "batch callback" }; } });
        try {
            (void)multi.Get();
            passed &= check(false, "batch callback exceptions must propagate");
        } catch (std::runtime_error const& error) {
            passed &= check(error.what() == "batch callback"sv, "batch callback exceptions must retain their identity");
        }
        first->SetWriteCallback({});
        passed &= check(multi.Get().size() == 2, "all handles must detach after a callback exception so the batch can be reused");
        multi.RemoveSession(second);
        passed &= check(!second->Get().error && second.use_count() == 1, "removal must immediately release the session and permit easy requests");
        M moved{ std::move(multi) };
        passed &= check(moved.Get().size() == 1 && rejects([&] { (void)first->Get(); }), "moving a batch must transfer its session claims");
        multi.AddSession(second);
        passed &= check(multi.Get().size() == 1, "moved-from batches must be reusable");
        moved   = std::move(multi);
        passed &= check(!first->Get().error && moved.Get().size() == 1, "move assignment must release old destination claims and retain source claims");
        moved.GetSessions().push_back(moved.GetSessions().front());
        passed &= check(rejects([&] { (void)moved.Get(); }), "mutable registration edits must be checked for duplicate handles");
        moved.GetSessions().pop_back();
        moved.RemoveSession(second);
        passed &= check(moved.Get().empty() && moved.Download().empty(), "empty batches and downloads must succeed without indexing nonexistent sessions");
        return passed;
    }

    auto check_multi_interceptors_and_downloads(HttpServer const& server) -> bool {
        using M = mcr::MultiPerform;
        using I = FunctionalMultiInterceptor;
        auto first{ make_session(server) };
        auto second{ make_session(server) };
        M    multi;
        multi.AddSession(first, M::HttpMethod::DOWNLOAD_REQUEST);
        multi.AddSession(second, M::HttpMethod::DOWNLOAD_REQUEST);
        int calls{};
        multi.AddInterceptor(std::make_shared<I>([&](M& current) { ++calls; (void)I::Proceed(current); return I::Proceed(current); }));
        std::vector<int> order;
        multi.AddInterceptor(std::make_shared<I>([&](M& current) { order.push_back(2); auto results{ I::Proceed(current) }; results[0].status_code = 299; return results; }));
        std::string        bytes;
        mcr::WriteCallback writer{ [&](auto data, auto) { bytes += data; return true; } };
        TempFile           temporary;
        bool               passed{ check(rejects([&] { (void)multi.Download(writer); }), "download destinations must match the registration count") };
        {
            std::ofstream file{ temporary.path, std::ios::binary };
            auto          responses{ multi.PerformDownload(writer, std::ref(file)) };
            passed &= check(responses.size() == 2 && responses[0].status_code == 299 && responses[0].text.empty() && calls == 1 && order == std::vector<int>{ 2, 2 }, "batch retry chains must preserve ordered download responses");
        }
        passed &= check(bytes == "Hello session!Hello session!" && std::filesystem::file_size(temporary.path) == 28, "batch retries must preserve callback and borrowed stream destinations");
        passed &= check(rejects([&] { (void)multi.Perform(); }), "completed download batches must not retain borrowed destination pointers");
        auto results{ multi.Get() };
        passed &= check(results[0].text == "Hello session!" && results[1].text == "Hello session!", "ordinary requests must work after downloads and restart batch interceptors");
        first->AddInterceptor(std::make_shared<FunctionalInterceptor>([](auto&) { mcr::Response response; response.status_code = 400; return response; }));
        passed &= check(multi.Get()[0].status_code == 299, "batch requests must use batch interceptors instead of individual session interceptors");
        M    throwing;
        auto third{ make_session(server) };
        throwing.AddSession(third);
        int attempts{};
        throwing.AddInterceptor(std::make_shared<I>([&](M& current) -> std::vector<mcr::Response> {
            if (++attempts == 1) {
                throw std::runtime_error{ "multi interceptor" };
            }
            return I::Proceed(current);
        }));
        try {
            (void)throwing.Get();
            passed &= check(false, "batch interceptor exceptions must propagate");
        } catch (std::runtime_error const&) {}
        passed &= check(throwing.Get()[0].status_code == 200 && attempts == 2, "batch interceptor cursors must recover after exceptions");
        M    converted;
        auto fourth{ make_session(server) };
        converted.AddSession(fourth);
        converted.AddInterceptor(std::make_shared<I>([&](M& current) {
            current.GetSessions()[0].second = M::HttpMethod::DOWNLOAD_REQUEST;
            I::PrepareDownloadSession(current, 0, writer);
            return I::Proceed(current);
        }));
        bytes.clear();
        passed &= check(converted.Get()[0].text.empty() && bytes == "Hello session!", "batch interceptors must be able to select download methods and destinations");
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
