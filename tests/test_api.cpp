/**
 * @file test_api.cpp
 * @brief Exercise one-shot API ownership, HTTP methods, batches, cancellation and downloads.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"
import std;
import mcr.api;

namespace {
    using namespace std::chrono_literals;
    using mcr::test::HttpServer;

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_api: {}", message);
        }
        return condition;
    }

    auto options(HttpServer const& server, std::string_view path = "/echo") {
        return std::tuple{
            server.Url(path),
            mcr::options::Proxies{ { "http", "" }, { "no_proxy", "*" } },
            mcr::options::Timeout{ 3000ms },
            mcr::Body{ "payload" }
        };
    }

    auto check_response(mcr::Response const& response, std::string_view method) -> bool {
        return check(!response.error && response.status_code == 200 && response.header.at("X-Method") == method && response.text == (method == "HEAD" ? "" : "payload"), std::format("{} must preserve method and body semantics", method));
    }

    template <typename Sync, typename Async, typename Callback, typename Multi, typename MultiAsync>
    auto check_family(HttpServer const& server, std::string_view method, Sync sync, Async asynchronous, Callback callback, Multi multi, MultiAsync multi_async) -> bool {
        auto const args{ options(server) };
        bool       passed{ check_response(std::apply(sync, args), method) };
        passed &= check_response(std::apply(asynchronous, args).Get().value(), method);
        auto continuation{ [&](mcr::Result<mcr::Response> response_result) { auto response = std::move(response_result).value();  return check_response(response, method); } };
        passed &= std::apply([&](auto const&... values) { return callback(continuation, values...); }, args).Get();
        auto responses{ multi(args, options(server)) };
        passed &= check(responses.size() == 2, "a synchronous batch must return one response per tuple");
        for (auto const& response : responses) {
            passed &= check_response(response, method);
        }
        auto futures{ multi_async(args, options(server)) };
        passed &= check(futures.size() == 2, "an asynchronous batch must return one future per tuple");
        for (auto& future : futures) {
            passed &= check_response(future.Get().value(), method);
        }
        passed &= check(multi().empty() && multi_async().empty(), "all empty batch overloads must work");
        return passed;
    }

    auto check_methods(HttpServer const& server) -> bool {
        bool passed{ true };
        passed &= check_family(server, "GET", [](auto&&... values) { return mcr::Get(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::GetAsync(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::GetCallback(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiGet(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiGetAsync(std::forward<decltype(values)>(values)...).value(); });
        passed &= check_family(server, "POST", [](auto&&... values) { return mcr::Post(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::PostAsync(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::PostCallback(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiPost(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiPostAsync(std::forward<decltype(values)>(values)...).value(); });
        passed &= check_family(server, "PUT", [](auto&&... values) { return mcr::Put(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::PutAsync(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::PutCallback(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiPut(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiPutAsync(std::forward<decltype(values)>(values)...).value(); });
        passed &= check_family(server, "HEAD", [](auto&&... values) { return mcr::Head(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::HeadAsync(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::HeadCallback(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiHead(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiHeadAsync(std::forward<decltype(values)>(values)...).value(); });
        passed &= check_family(server, "DELETE", [](auto&&... values) { return mcr::Delete(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::DeleteAsync(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::DeleteCallback(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiDelete(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiDeleteAsync(std::forward<decltype(values)>(values)...).value(); });
        passed &= check_family(server, "OPTIONS", [](auto&&... values) { return mcr::Options(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::OptionsAsync(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::OptionsCallback(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiOptions(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiOptionsAsync(std::forward<decltype(values)>(values)...).value(); });
        passed &= check_family(server, "PATCH", [](auto&&... values) { return mcr::Patch(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::PatchAsync(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::PatchCallback(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiPatch(std::forward<decltype(values)>(values)...).value(); }, [](auto&&... values) { return mcr::MultiPatchAsync(std::forward<decltype(values)>(values)...).value(); });

        passed &= check(bool(mcr::Get().value().error) && bool(mcr::GetAsync().value().Get().value().error), "zero options must compile and report a missing URL");
        auto empty_options{ mcr::MultiGet(std::tuple<>{}).value() };
        passed &= check(empty_options.size() == 1 && bool(empty_options[0].error), "an empty option tuple must report a request error");
        return passed;
    }

    auto check_headers_and_ownership(HttpServer const& server) -> bool {
        mcr::Header first{
            { "X-Custom",     "first" },
            {  "X-Empty", "preserved" }
        };
        mcr::Header const second{
            { "x-custom", "second" }
        };
        auto response{ std::apply([&](auto const&... values) {
            return mcr::Get(values..., first, second, mcr::Header{
                                                          { "X-Custom", "last" }
            },
                            mcr::Header{})
                .value();
        },
                                  options(server)) };
        bool passed{ check(response.header["X-Request-X-Custom"] == "last" && response.header["X-Request-X-Empty"] == "preserved", "all cv/ref forms of repeated Header options must merge in order") };
        response  = std::apply([&](auto const&... values) { return mcr::Get(values..., std::ref(first), std::cref(second)).value(); }, options(server));
        passed   &= check(response.header["X-Request-X-Custom"] == "second" && response.header["X-Request-X-Empty"] == "preserved", "explicit reference-wrapped headers must also merge");

        auto* pool{ mcr::GlobalThreadPool::GetInstance() };
        pool->Wait();
        (void)pool->Pause();
        mcr::AsyncResponse                                                      copied;
        std::vector<mcr::utils::AsyncWrapper<mcr::Result<mcr::Response>, true>> batch;
        {
            auto args{ options(server) };
            copied                    = std::apply([&](auto&... values) { return mcr::PostAsync(values..., first).value(); }, args);
            batch                     = mcr::MultiPostAsync(args).value();
            std::get<mcr::Body>(args) = mcr::Body{ "changed" };
            first["X-Custom"]         = "changed";
        }
        (void)pool->Resume();
        response  = copied.Get().value();
        passed   &= check(response.text == "payload" && response.header["X-Request-X-Custom"] == "first" && batch[0].Get().value().text == "payload", "queued async calls must own copies of ordinary lvalue options and tuples");
        return passed;
    }

    /**
     * @brief Verify that rvalue tuples are forwarded without copy-only type erasure.
     */
    struct MoveOnlyUrl : mcr::Url {
        explicit MoveOnlyUrl(mcr::Url url) : mcr::Url{ std::move(url) } {}

        MoveOnlyUrl(MoveOnlyUrl const&) = delete;
        MoveOnlyUrl(MoveOnlyUrl&&)      = default;
    };

    auto check_batches(HttpServer const& server) -> bool {
        HttpServer concurrent;
        auto       batch{ mcr::MultiGet(options(concurrent, "/barrier"), options(concurrent, "/barrier")).value() };
        bool       passed{ check(batch.size() == 2 && batch[0].status_code == 200 && batch[1].status_code == 200, "MultiGet must run requests concurrently") };
        concurrent.Check();
        HttpServer async_concurrent;
        auto       futures{ mcr::MultiGetAsync(options(async_concurrent, "/barrier"), options(async_concurrent, "/barrier")).value() };
        for (auto& future : futures) {
            passed &= check(future.Get().value().status_code == 200, "MultiGetAsync must permit concurrent execution");
        }
        async_concurrent.Check();
        batch         = mcr::MultiGet(options(server, "/error"), std::tuple{ mcr::Url{ "invalid-scheme://localhost/" } }, options(server)).value();
        passed       &= check(batch.size() == 3 && batch[0].status_code == 404 && !batch[0].error && bool(batch[1].error) && batch[2].text == "payload", "HTTP and transfer errors must keep their input positions");
        auto movable  = [&] {
            return std::tuple{
                MoveOnlyUrl{ server.Url() },
                mcr::options::Proxies{ { "http", "" }, { "no_proxy", "*" } },
                mcr::options::Timeout{ 3000ms }
            };
        };
        passed &= check(mcr::MultiGet(movable()).value()[0].status_code == 200 && mcr::MultiGetAsync(movable()).value()[0].Get().value().status_code == 200, "both batch APIs must accept move-only rvalue option tuples");
        return passed;
    }

    auto check_callbacks(HttpServer const& server) -> bool {
        struct LvalueContinuation {
            auto operator()(mcr::Result<mcr::Response> const& response) & -> long { return response.value().status_code; }

            auto operator()(mcr::Result<mcr::Response> const&) && -> long = delete;
        };

        auto args{ options(server) };
        auto moved{ std::apply([](auto const&... values) {
            return mcr::PostCallback([state = std::make_unique<int>(7)](mcr::Result<mcr::Response> response_result) mutable {
                       auto response  = std::move(response_result).value();
                       *state        += static_cast<int>(response.text.size());
                       return std::move(state);
                   },
                                     values...)
                .value();
        },
                               args) };
        bool passed{ check(*moved.Get() == 14, "continuations must support move-only captures and results") };
        passed &= check(mcr::GetCallback(LvalueContinuation{
        },
                                         server.Url(),
                                         mcr::options::Proxies{ { "http", "" } })
                                .value()
                                .Get() == 200,
                        "owned continuations must be invoked as lvalues, as in cpr");
        int  value{};
        auto reference{ std::apply([&](auto const&... values) { return mcr::GetCallback([&](mcr::Result<mcr::Response>) -> int& { return value; }, values...).value(); }, args) };
        static_assert(std::same_as<decltype(reference), mcr::utils::AsyncWrapper<int&, true>>);
        reference.Get() = 17;
        auto no_result{ std::apply([&](auto const&... values) { return mcr::GetCallback([&](mcr::Result<mcr::Response>) { ++value; }, values...).value(); }, args) };
        static_assert(std::same_as<decltype(no_result), mcr::utils::AsyncWrapper<void, true>>);
        no_result.Get();
        passed &= check(value == 18, "continuations must preserve reference and void returns");
        auto throwing{ std::apply([](auto const&... values) { return mcr::GetCallback([](mcr::Result<mcr::Response>) -> int { throw std::runtime_error{ "continuation" }; }, values...).value(); }, args) };
        try {
            (void)throwing.Get();
            passed &= check(false, "continuation exceptions must surface through Get");
        } catch (std::runtime_error const& error) {
            passed &= check(std::string_view{ error.what() } == "continuation", "the original continuation exception must be retained");
        }
        auto preparation{ mcr::GetAsync(mcr::options::HttpVersion{ static_cast<mcr::options::HttpVersionCode>(255) }).value() };
        auto failed  = preparation.Get();
        passed      &= check(!failed && failed.error().code == mcr::ErrorCode::BAD_FUNCTION_ARGUMENT, "async preparation must return a configuration error");
        return passed;
    }

    auto check_cancellation(HttpServer const& server) -> bool {
        auto* pool{ mcr::GlobalThreadPool::GetInstance() };
        pool->Wait();
        (void)pool->Pause();
        int const before{ server.Connections() };
        auto      queued{ mcr::MultiGetAsync(options(server)).value() };
        bool      passed{ check(queued[0].Cancel() == mcr::utils::CancellationResult::success, "queued batch requests must be cancellable") };
        auto      queued_result{ queued[0].Share() };
        (void)pool->Resume();
        passed &= check(queued_result.get().value().status_code == 0 && server.Connections() == before, "cancellation before execution must avoid a network request");

        std::promise<void> received;
        auto               signal{ received.get_future() };
        bool               signalled{ false };
        auto               arguments{ std::tuple_cat(options(server, "/stream"), std::tuple{ mcr::WriteCallback{ [&](std::string_view, std::intptr_t) {
                                           if (!std::exchange(signalled, true)) {
                                               received.set_value();
                                           }
                                           return true;
                                       } } }) };
        auto active{ mcr::MultiGetAsync(std::move(arguments), options(server)).value() };
        passed &= check(signal.wait_for(5s) == std::future_status::ready, "streaming transfer must start before cancellation");
        passed &= check(active[0].Cancel() == mcr::utils::CancellationResult::success, "active batch requests must be cancellable");
        auto cancelled{ active[0].Share().get().value() };
        passed &= check(cancelled.error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK && active[1].Get().value().status_code == 200, "cancelling one transfer must abort it without affecting another request");

        pool->Wait();
        (void)pool->Pause();
        bool called{ false };
        auto callback{
            mcr::GetCallback([&](mcr::Result<mcr::Response> response_result) { auto response = std::move(response_result).value();  called = true; return response.status_code;             },
              server.Url(), mcr::options::Proxies{ { "http", "" } }
              ).value()
        };
        (void)callback.Cancel();
        auto callback_result{ callback.Share() };
        (void)pool->Resume();
        passed &= check(callback_result.get() == 200 && called, "cpr-compatible callback cancellation must not implicitly abort the task");
        return passed;
    }

    struct TempDirectory {
        std::filesystem::path path{ std::filesystem::temp_directory_path() / std::format("mcr-api-{}", std::chrono::steady_clock::now().time_since_epoch().count()) };

        TempDirectory() {
            if (!std::filesystem::create_directory(path)) {
                throw std::runtime_error{ "Could not create an exclusive test directory." };
            }
        }

        ~TempDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    auto read_file(std::filesystem::path const& path) -> std::string {
        std::ifstream file{ path, std::ios::binary };
        return { std::istreambuf_iterator<char>{ file }, {} };
    }

    auto check_downloads(HttpServer const& server) -> bool {
        TempDirectory directory;
        auto          args{ options(server, "/binary") };
        std::string   binary(256, '\0');
        for (std::size_t index{}; index < binary.size(); ++index) {
            binary[index] = static_cast<char>(index);
        }
        std::string bytes;
        auto        callback{ std::apply([&](auto const&... values) { return mcr::Download(mcr::WriteCallback{ [&](std::string_view part, std::intptr_t) { bytes.append(part); return true; } }, values...).value(); }, args) };
        bool        passed{ check(!callback.error && callback.text.empty() && bytes == binary, "callback downloads must preserve every byte") };
        auto const  path{ directory.path / "synchronous.bin" };
        {
            std::ofstream file{ path, std::ios::binary };
            auto          response{ std::apply([&](auto const&... values) { return mcr::Download(file, values...).value(); }, args) };
            passed &= check(!response.error && response.text.empty(), "stream downloads must return transfer metadata");
        }
        passed &= check(read_file(path) == binary, "stream downloads must preserve all binary bytes");
        auto future{ std::apply([&](auto const&... values) { return mcr::DownloadAsync(directory.path / "asynchronous.bin", values...).value(); }, args) };
        passed &= check(!future.Get().value().error && read_file(directory.path / "asynchronous.bin") == binary, "asynchronous downloads must open and close files in binary mode");
        std::ofstream closed;
        auto          closed_result  = mcr::Download(closed, server.Url());
        passed                      &= check(!closed_result && closed_result.error().code == mcr::ErrorCode::WRITE_ERROR, "closed download streams must return WRITE_ERROR");
        auto unwritable              = mcr::DownloadAsync(directory.path, server.Url()).value().Get();
        passed                      &= check(!unwritable && unwritable.error().code == mcr::ErrorCode::WRITE_ERROR, "async file-open failures must return WRITE_ERROR");
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
        passed &= check_methods(server);
        passed &= check_headers_and_ownership(server);
        passed &= check_batches(server);
        passed &= check_callbacks(server);
        passed &= check_cancellation(server);
        passed &= check_downloads(server);
        mcr::GlobalThreadPool::GetInstance()->Wait();
        server.Check();
    } catch (std::exception const& error) {
        passed = check(false, error.what());
    }
    mcr::Async::Cleanup();
    curl_global_cleanup();
    return passed ? 0 : 1;
}
