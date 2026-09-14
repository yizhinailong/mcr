/**
 * @file test_coro.cpp
 * @brief Exercise coroutine HTTP methods, concurrent I/O, cancellation, and runtime shutdown.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"
import std;
import mcr;

namespace {
    using namespace std::chrono_literals;
    using mcr::test::HttpServer;

    void require(bool value, std::string_view message) {
        if (!value) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    auto proxies() -> mcr::options::Proxies {
        return {
            {     "http",  "" },
            { "no_proxy", "*" }
        };
    }

    template <typename... Options>
    auto get(HttpServer const& server, std::string_view path, Options... options) -> mcr::Task<mcr::Response> {
        return mcr::GetCoro(server.Url(path), proxies(), mcr::options::Timeout{ 3s }, std::move(options)...);
    }

    void methods(HttpServer const& server) {
        auto args  = std::tuple{ server.Url("/echo"), proxies(), mcr::options::Timeout{ 3s }, mcr::Body{ "payload" } };
        auto check = [](mcr::Task<mcr::Response> task, std::string_view method) {
            auto response = mcr::sync_wait(std::move(task));
            require(!response.error && response.status_code == 200 && response.header.at("X-Method") == method, "coroutine requests must preserve their HTTP method");
            require(response.text == (method == "HEAD" ? "" : "payload"), "coroutine requests must preserve body semantics");
        };
        check(std::apply([](auto... values) { return mcr::GetCoro(std::move(values)...); }, args), "GET");
        check(std::apply([](auto... values) { return mcr::PostCoro(std::move(values)...); }, args), "POST");
        check(std::apply([](auto... values) { return mcr::PutCoro(std::move(values)...); }, args), "PUT");
        check(std::apply([](auto... values) { return mcr::HeadCoro(std::move(values)...); }, args), "HEAD");
        check(std::apply([](auto... values) { return mcr::DeleteCoro(std::move(values)...); }, args), "DELETE");
        check(std::apply([](auto... values) { return mcr::OptionsCoro(std::move(values)...); }, args), "OPTIONS");
        check(std::apply([](auto... values) { return mcr::PatchCoro(std::move(values)...); }, args), "PATCH");
    }

    void ownership(HttpServer const& server) {
        mcr::Task<mcr::Response> task;
        {
            mcr::Body body{ "owned before suspension" };
            task = mcr::PostCoro(server.Url("/echo"), proxies(), body, mcr::Header{
                                                                           { "X-Custom", "one" }
            },
                                 mcr::Header{ { "X-Empty", "two" } });
            body = mcr::Body{ "changed" };
        }
        auto response = mcr::sync_wait(std::move(task));
        require(response.text == "owned before suspension", "request options must be owned before initial suspension");
        require(response.header.at("X-Request-X-Custom") == "one" && response.header.at("X-Request-X-Empty") == "two", "header arguments must be merged");
        auto json = mcr::sync_wait(mcr::PostCoro(server.Url("/json/echo"), proxies(), mcr::JsonBody{ mcr::Json{ { "value", 42 } } }));
        require(json.Json()["value"] == 42, "JSON requests must use the existing serialization and response APIs");
    }

    auto sequential(HttpServer const& server) -> mcr::Task<int> {
        auto first  = co_await get(server, "/hello");
        auto second = co_await get(server, "/hello");
        co_return first.status_code + second.status_code;
    }

    void concurrent(HttpServer const& server) {
        std::mutex                threads_mutex;
        std::set<std::thread::id> io_threads;
        auto                      observer = mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
            std::lock_guard lock{ threads_mutex };
            io_threads.insert(std::this_thread::get_id());
            return true;
        } };
        auto first  = get(server, "/barrier", observer);
        auto second = get(server, "/barrier", observer);
        first.Start();
        second.Start();
        require(mcr::sync_wait(std::move(first)).status_code == 200 && mcr::sync_wait(std::move(second)).status_code == 200, "two requests must reach the server concurrently");
        require(io_threads.size() == 1 && !io_threads.contains(std::this_thread::get_id()), "curl callbacks must share one I/O thread");
        require(mcr::sync_wait(sequential(server)) == 400, "a resumed coroutine must be able to submit and await another request");
        std::vector<mcr::Task<mcr::Response>> tasks;
        for (int index{}; index < 40; ++index) {
            tasks.push_back(get(server, "/hello"));
            tasks.back().Start();
        }
        for (auto& task : tasks) {
            require(mcr::sync_wait(std::move(task)).status_code == 200, "concurrent submissions must all complete");
        }
    }

    void errors(HttpServer const& server) {
        require(mcr::sync_wait(get(server, "/slow", mcr::options::Timeout{ 20ms })).error.code == mcr::ErrorCode::OPERATION_TIMEDOUT, "timeouts must remain response transport errors");
        auto http_error = mcr::sync_wait(get(server, "/error"));
        require(http_error.status_code == 404 && !http_error.error, "HTTP errors must remain ordinary responses");
        require(mcr::sync_wait(get(server, "/redirect")).text == "Hello session!", "redirect behavior must be reused");
        try {
            (void)mcr::sync_wait(get(server, "/hello", mcr::HeaderCallback{ [](std::string_view, std::intptr_t) -> bool {
                                         throw std::runtime_error{ "header failure" };
                                     } }));
            require(false, "curl callback exceptions must propagate at co_await");
        } catch (std::runtime_error const& error) {
            require(std::string_view{ error.what() } == "header failure", "original callback exception must be retained");
        }
        bool preparation_failed{};
        try {
            (void)mcr::sync_wait(mcr::PostCoro(server.Url("/echo"), proxies(), mcr::Multipart{
                                                                                   { "missing", mcr::File{ "mcr_nonexistent_coro_upload_812905" } }
            }));
        } catch (std::runtime_error const&) {
            preparation_failed = true;
        }
        require(preparation_failed, "preparation failures must complete the task");
        require(mcr::sync_wait(get(server, "/hello")).status_code == 200, "one failed transfer must not stop the runtime");
    }

    auto parent_request(HttpServer const& server, mcr::HeaderCallback callback) -> mcr::Task<mcr::Response> {
        co_return co_await get(server, "/stream", std::move(callback));
    }

    void cancellation(HttpServer const& server) {
        auto before    = server.Connections();
        auto cancelled = get(server, "/hello");
        require(cancelled.Cancel() && !cancelled.Cancel(), "Cancel must signal once, including before Start");
        require(mcr::sync_wait(std::move(cancelled)).error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK && server.Connections() == before, "cancellation before submission must not contact the server");

        std::promise<void> arrived;
        auto               seen = arrived.get_future();
        std::atomic_bool   signalled{};
        auto               parent = parent_request(server, mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
                                         if (!signalled.exchange(true)) {
                                             arrived.set_value();
                                         }
                                         return true;
                                                   } });
        auto               source = parent.GetStopSource();
        parent.Start();
        require(seen.wait_for(3s) == std::future_status::ready, "stream must start before cancellation");
        source.request_stop();
        require(mcr::sync_wait(std::move(parent)).error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "parent cancellation must wake polling and remove the active child transfer");
    }

    auto continuation_checks(HttpServer const& server, std::thread::id& resumed) -> mcr::Task<void> {
        (void)co_await get(server, "/hello");
        resumed = std::this_thread::get_id();
        bool wait_rejected{}, cleanup_rejected{};
        try {
            (void)mcr::sync_wait(get(server, "/hello"));
        } catch (std::logic_error const&) {
            wait_rejected = true;
        }
        try {
            mcr::Coro::Cleanup();
        } catch (std::logic_error const&) {
            cleanup_rejected = true;
        }
        require(wait_rejected && cleanup_rejected, "blocking waits and cleanup must be rejected on the continuation thread");
    }

    /**
     * @brief Deliberately occupy the continuation thread while I/O must remain responsive.
     */
    auto held_continuation(HttpServer const& server, std::promise<void>& entered, std::shared_future<void> release) -> mcr::Task<void> {
        (void)co_await get(server, "/hello");
        entered.set_value();
        require(release.wait_for(3s) == std::future_status::ready, "continuation test must be released");
    }

    void independent_io(HttpServer const& server) {
        std::promise<void> entered, release, header;
        auto               began    = entered.get_future();
        auto               received = header.get_future();
        auto               held     = held_continuation(server, entered, release.get_future().share());
        held.Start();
        require(began.wait_for(3s) == std::future_status::ready, "continuation must start");
        std::atomic_bool signalled{};
        auto             next = get(server, "/hello", mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
                            if (!signalled.exchange(true)) {
                                header.set_value();
                            }
                            return true;
                                    } });
        next.Start();
        bool progressed = received.wait_for(2s) == std::future_status::ready;
        release.set_value();
        mcr::sync_wait(std::move(held));
        require(mcr::sync_wait(std::move(next)).status_code == 200 && progressed, "a busy continuation must not prevent unrelated network I/O");
    }

    void downloads(HttpServer const& server) {
        auto path = std::filesystem::temp_directory_path() / std::format("mcr_coro_{}.bin", std::chrono::steady_clock::now().time_since_epoch().count());

        struct RemoveFile {
            std::filesystem::path path;

            ~RemoveFile() {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        } cleanup{ path };

        auto download = mcr::DownloadCoro(path, server.Url("/binary"), proxies(), mcr::options::Timeout{ 3s });
        require(!std::filesystem::exists(path), "a cold download must not create its destination");
        auto          response = mcr::sync_wait(std::move(download));
        std::ifstream file{ path, std::ios::binary };
        std::string   bytes{ std::istreambuf_iterator<char>{ file }, {} };
        require(response.status_code == 200 && !response.error && response.text.empty() && bytes.size() == 256, "download must close its output before publishing metadata");
        for (std::size_t index{}; index < bytes.size(); ++index) {
            require(static_cast<unsigned char>(bytes[index]) == index, "download must preserve binary bytes");
        }
        bool failed{};
        try {
            (void)mcr::sync_wait(mcr::DownloadCoro(path / "invalid.bin", server.Url("/binary"), proxies()));
        } catch (std::runtime_error const&) {
            failed = true;
        }
        require(failed, "file opening failures must propagate through the task");
    }

    void shutdown(HttpServer const& server) {
        std::promise<void> arrived;
        auto               seen = arrived.get_future();
        std::promise<void> release;
        auto               gate = release.get_future().share();
        std::atomic_bool   signalled{};
        auto               active = get(server, "/stream", mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
                              if (!signalled.exchange(true)) {
                                  arrived.set_value();
                                  (void)gate.wait_for(3s);
                              }
                              return true;
                                        } });
        active.Start();
        require(seen.wait_for(3s) == std::future_status::ready, "request must be active at shutdown");
        // Hold the I/O thread inside a bounded callback, making the pending queue deterministic.
        std::atomic_int                       queued_callbacks{};
        std::vector<mcr::Task<mcr::Response>> queued;
        for (int index{}; index < 8; ++index) {
            queued.push_back(get(server, "/hello", mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
                                     ++queued_callbacks;
                                     return true;
                                 } }));
            queued.back().Start();
        }
        std::jthread cleanup{ [] { mcr::Coro::Cleanup(); } };
        bool         stopping{};
        auto         deadline = std::chrono::steady_clock::now() + 2s;
        while (!stopping && std::chrono::steady_clock::now() < deadline) {
            try {
                mcr::Coro::Startup();
            } catch (std::logic_error const&) {
                stopping = true;
            }
            std::this_thread::yield();
        }
        release.set_value();
        cleanup.join();
        require(stopping, "cleanup must close submissions before joining I/O");
        require(mcr::sync_wait(std::move(active)).error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "shutdown must finish active tasks before returning");
        for (auto& task : queued) {
            require(mcr::sync_wait(std::move(task)).error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "shutdown must complete every queued task");
        }
        require(queued_callbacks == 0, "queued requests must not enter curl after shutdown begins");
        mcr::Coro::Cleanup();
        try {
            (void)mcr::sync_wait(get(server, "/hello"));
            require(false, "submissions after cleanup must fail");
        } catch (std::logic_error const&) {}
    }
} // namespace

int main() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        mcr::Coro::Startup();
        HttpServer server;
        auto       before = server.Connections();
        { auto lazy = get(server, "/hello"); }
        require(server.Connections() == before, "discarded cold HTTP tasks must not start requests");
        methods(server);
        ownership(server);
        concurrent(server);
        errors(server);
        cancellation(server);
        independent_io(server);
        downloads(server);
        std::thread::id resumed, io;
        (void)mcr::sync_wait(get(server, "/hello", mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) { io = std::this_thread::get_id(); return true; } }));
        mcr::sync_wait(continuation_checks(server, resumed));
        require(resumed != io && resumed != std::this_thread::get_id(), "continuations must run separately from I/O and caller threads");
        shutdown(server);
        server.Check();
    } catch (std::exception const& error) {
        std::println("test_coro: {}", error.what());
        passed = false;
    }
    mcr::Coro::Cleanup();
    curl_global_cleanup();
    return passed ? 0 : 1;
}
