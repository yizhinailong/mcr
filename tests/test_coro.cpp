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

    /**
     * @brief Check coroutine template constraints and its public result through import mcr.
     * @tparam M HTTP method, including deliberately invalid enum values.
     * @tparam Ts Option types.
     */
    template <mcr::Method M, typename... Ts>
    concept CoroRequestAvailable = requires(Ts... values) {
        { mcr::request_coro<M>(std::move(values)...) } -> std::same_as<mcr::Task<mcr::Result<mcr::Response>>>;
    };

    static_assert(!CoroRequestAvailable<static_cast<mcr::Method>(-1)>);
    static_assert(!CoroRequestAvailable<static_cast<mcr::Method>(7), mcr::Url>);
    static_assert(!CoroRequestAvailable<static_cast<mcr::Method>(255), mcr::Url, mcr::Body>);

    /**
     * @brief Require coroutine wrappers to move an ordinary option into their owned tuple.
     */
    struct MoveOnlyUrl : mcr::Url {
        /**
         * @brief Own URL bytes before moving the option into a task.
         * @param url URL to move into the option.
         */
        explicit MoveOnlyUrl(mcr::Url url) : mcr::Url{ std::move(url) } {}

        /**
         * @brief Prevent copying this option.
         */
        MoveOnlyUrl(MoveOnlyUrl const&) = delete;

        /**
         * @brief Transfer the owned URL to the coroutine frame.
         */
        MoveOnlyUrl(MoveOnlyUrl&&) = default;
    };

    auto require(bool value, std::string_view message) -> void {
        if (!value) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    auto proxies() -> mcr::options::Proxies {
        return {
            { "http", "" },
            { "no_proxy", "*" }
        };
    }

    /**
     * @brief Exercise the template entry throughout existing runtime and cancellation tests.
     * @tparam Options Additional request option types.
     * @param server Local HTTP fixture.
     * @param path Fixture endpoint.
     * @param options Additional owned options.
     * @return Lazy GET task.
     */
    template <typename... Options>
    auto get(
        HttpServer const& server,
        std::string_view path,
        Options... options
    ) -> mcr::Task<mcr::Result<mcr::Response>> {
        return mcr::request_coro<mcr::method::Get>(server.Url(path), proxies(), mcr::options::Timeout{ 3s }, std::move(options)...);
    }

    /**
     * @brief Compare a coroutine template with its named entry, including empty and upload options.
     * @tparam M Supported HTTP method.
     * @param server Local HTTP fixture.
     * @param method Expected HTTP method.
     * @param named Named coroutine request with a payload body.
     */
    template <mcr::Method M>
    auto check_method(
        HttpServer const& server,
        std::string_view method,
        mcr::Task<mcr::Result<mcr::Response>> named
    ) -> void {
        static_assert(CoroRequestAvailable<M> && CoroRequestAvailable<M, mcr::Url, mcr::Body>);
        auto check = [method](mcr::Task<mcr::Result<mcr::Response>> task, std::string_view body = "payload") {
            auto response = mcr::sync_wait(std::move(task)).value();
            require(!response.error && response.status_code == 200 && response.header.at("X-Method") == method, "coroutine requests must preserve their HTTP method");
            require(response.text == (method == "HEAD" ? "" : body), "coroutine requests must preserve body semantics");
            if constexpr (M == mcr::method::Head) {
                require(response.header.at("Content-Length") == "0", "HEAD coroutine requests must also omit the configured request body");
            }
        };
        check(std::move(named));
        auto args = std::tuple{ server.Url("/echo"), proxies(), mcr::options::Timeout{ 3s }, mcr::Body{ "payload" } };
        auto request = [](auto&&... values) {
            return mcr::request_coro<M>(std::forward<decltype(values)>(values)...);
        };
        check(std::apply(request, args));
        check(std::apply(request, std::move(args)));
        auto empty = mcr::sync_wait(mcr::request_coro<M>());
        require(empty && bool(empty->error) && empty->status_code == 0, "zero coroutine options must report a missing URL as a transfer error");
        auto response = mcr::sync_wait(request(server.Url("/echo"), proxies(), mcr::options::Timeout{ 3s })).value();
        require(!response.error && response.status_code == 200 && response.header.at("X-Method") == method && response.text.empty(), "coroutine templates must also work without a body option");
        for (mcr::CprOffT const size : { mcr::CprOffT{ 7 }, mcr::CprOffT{ -1 } }) {
            mcr::ReadCallback upload{ size, [offset = std::size_t{}](char* buffer, std::size_t& length, auto) mutable {
                                         std::string_view const source{ "payload" };
                                         length = (std::min)(length, source.size() - offset);
                                         std::memcpy(buffer, source.data() + offset, length);
                                         offset += length;
                                         return true;
                                     } };
            constexpr bool UPLOAD{ M == mcr::method::Post || M == mcr::method::Put || M == mcr::method::Patch };
            check(request(server.Url("/echo"), proxies(), mcr::options::Timeout{ 3s }, std::move(upload)), UPLOAD ? "payload" : "");
        }
    }

    /**
     * @brief Exercise all seven named and template coroutine method families.
     * @param server Local HTTP fixture.
     */
    auto methods(HttpServer const& server) -> void {
        auto args = std::tuple{ server.Url("/echo"), proxies(), mcr::options::Timeout{ 3s }, mcr::Body{ "payload" } };
        check_method<mcr::method::Get>(server, "GET", std::apply([](auto... values) { return mcr::GetCoro(std::move(values)...); }, args));
        check_method<mcr::method::Post>(server, "POST", std::apply([](auto... values) { return mcr::PostCoro(std::move(values)...); }, args));
        check_method<mcr::method::Put>(server, "PUT", std::apply([](auto... values) { return mcr::PutCoro(std::move(values)...); }, args));
        check_method<mcr::method::Head>(server, "HEAD", std::apply([](auto... values) { return mcr::HeadCoro(std::move(values)...); }, args));
        check_method<mcr::method::Delete>(server, "DELETE", std::apply([](auto... values) { return mcr::DeleteCoro(std::move(values)...); }, args));
        check_method<mcr::method::Options>(server, "OPTIONS", std::apply([](auto... values) { return mcr::OptionsCoro(std::move(values)...); }, args));
        check_method<mcr::method::Patch>(server, "PATCH", std::apply([](auto... values) { return mcr::PatchCoro(std::move(values)...); }, args));
    }

    /**
     * @brief Check named and template coroutine ownership before initial suspension.
     * @param server Local HTTP fixture.
     */
    auto ownership(HttpServer const& server) -> void {
        mcr::Task<mcr::Result<mcr::Response>> task;
        mcr::Task<mcr::Result<mcr::Response>> template_task;
        auto const before = server.Connections();
        {
            auto url = server.Url("/echo");
            mcr::Body body{ "owned before suspension" };
            mcr::Header header{ { "X-Custom", "one" }, { "X-Empty", "preserved" } };
            mcr::Header const second{ { "x-custom", "two" } };
            task = mcr::PostCoro(server.Url("/echo"), proxies(), body, mcr::Header{ { "X-Custom", "one" } }, mcr::Header{ { "X-Empty", "two" } });
            template_task = mcr::request_coro<mcr::method::Post>(url, proxies(), mcr::options::Timeout{ 3s }, mcr::Body{ "discarded" }, body, header, second, mcr::Header{ { "X-Custom", "last" } }, mcr::Header{});
            require(server.Connections() == before, "creating a coroutine template must not submit network I/O");
            url = server.Url("/error");
            body = mcr::Body{ "changed" };
            header["X-Empty"] = "changed";
        }
        auto response = mcr::sync_wait(std::move(task)).value();
        require(response.text == "owned before suspension", "request options must be owned before initial suspension");
        require(response.header.at("X-Request-X-Custom") == "one" && response.header.at("X-Request-X-Empty") == "two", "header arguments must be merged");
        response = mcr::sync_wait(std::move(template_task)).value();
        require(!response.error && response.status_code == 200 && response.text == "owned before suspension", "coroutine templates must copy lvalues into the cold frame before their sources change or expire");
        require(response.header.at("X-Request-X-Custom") == "last" && response.header.at("X-Request-X-Empty") == "preserved", "coroutine templates must merge all copied header arguments in order");

        auto const url = server.Url("/echo");
        auto const proxy = proxies();
        mcr::Body body{ "before" };
        mcr::Header first{ { "X-Custom", "first" }, { "X-Empty", "preserved" } };
        mcr::Header const second{ { "x-custom", "second" } };
        auto borrowed = mcr::request_coro<mcr::method::Post>(std::cref(url), std::cref(proxy), mcr::Body{ "discarded" }, std::ref(body), std::ref(first), std::cref(second), mcr::Header{});
        body = mcr::Body{ "borrowed after creation" };
        first["X-Empty"] = "changed";
        response = mcr::sync_wait(std::move(borrowed)).value();
        require(response.text == body.Str() && response.header.at("X-Request-X-Custom") == "second" && response.header.at("X-Request-X-Empty") == "changed", "coroutine reference wrappers must borrow options until first startup and retain header merging");

        std::string bytes{ "view\0bytes", 10 };
        auto view = mcr::request_coro<mcr::method::Post>(url, proxy, mcr::BodyView{ std::string_view{ bytes } });
        bytes[0] = 'V';
        response = mcr::sync_wait(std::move(view)).value();
        require(response.text == bytes, "coroutine BodyView must borrow the original binary bytes through the transfer");
        auto moved = mcr::request_coro<mcr::method::Post>(MoveOnlyUrl{ url }, proxy, mcr::Body{ "moved" });
        require(mcr::sync_wait(std::move(moved)).value().text == "moved", "coroutine templates must accept and own noncopyable rvalue options");

        auto retained = std::make_shared<std::atomic_int>(0);
        std::weak_ptr<std::atomic_int> released = retained;
        auto const idle_connections = server.Connections();
        {
            auto cold = mcr::request_coro<mcr::method::Get>(server.Url(), proxy, mcr::HeaderCallback{ [owned = std::move(retained)](std::string_view, std::intptr_t) { ++*owned; return true; } });
            require(!released.expired() && released.lock()->load() == 0 && server.Connections() == idle_connections, "a cold template task must own options without invoking callbacks");
        }
        require(released.expired() && server.Connections() == idle_connections, "discarding a cold coroutine template must release options without sending a request");
    }

    auto sequential(HttpServer const& server) -> mcr::Task<int> {
        auto first = co_await get(server, "/hello");
        auto second = co_await get(server, "/hello");
        co_return first.value().status_code + second.value().status_code;
    }

    /**
     * @brief Record the task's initial thread and return an owned result.
     */
    auto spawned_value(
        std::thread::id& thread,
        std::shared_ptr<int> value
    ) -> mcr::Task<std::unique_ptr<int>> {
        thread = std::this_thread::get_id();
        co_return std::make_unique<int>(*value);
    }

    /**
     * @brief Preserve reference result identity when starting through the runtime.
     */
    auto spawned_reference(int& value) -> mcr::Task<int&> {
        co_return value;
    }

    /**
     * @brief Raise an exception from a task started on the continuation thread.
     */
    auto spawned_failure() -> mcr::Task<int> {
        throw std::runtime_error{ "spawned failure" };
        co_return 0;
    }

    /**
     * @brief Append a step only after the scheduler starts this task.
     */
    auto record_step(std::vector<int>& order, int step) -> mcr::Task<void> {
        order.push_back(step);
        co_return;
    }

    /**
     * @brief Switch threads and yield behind an already queued peer.
     */
    auto scheduled_chain(std::thread::id caller, std::vector<int>& order) -> mcr::Task<void> {
        require(std::this_thread::get_id() == caller, "ordinary task startup must retain its calling thread");
        (co_await mcr::Coro::Schedule()).value();
        require(std::this_thread::get_id() != caller, "Schedule must switch to the continuation thread");
        auto peer = mcr::Coro::Spawn(record_step(order, 2)).value();
        require(order.empty(), "Spawn must enqueue a peer without running it inline");
        order.push_back(1);
        (co_await mcr::Coro::Schedule()).value();
        order.push_back(3);
        co_await std::move(peer);
    }

    /**
     * @brief Hold the continuation thread to observe accepted work before it starts.
     */
    auto hold_scheduler(
        std::promise<void>& entered,
        std::shared_future<void> release
    ) -> mcr::Task<void> {
        entered.set_value();
        require(release.wait_for(3s) == std::future_status::ready, "scheduler test must release its continuation");
        co_return;
    }

    /**
     * @brief Observe cancellation before any task-specific asynchronous operation.
     */
    auto stop_requested() -> mcr::Task<bool> {
        co_return (co_await mcr::this_coro::stop_token).stop_requested();
    }

    /**
     * @brief Preserve scheduling rejection as a Result for the caller.
     */
    auto schedule_result() -> mcr::Task<mcr::Result<void>> {
        co_return co_await mcr::Coro::Schedule();
    }

    /**
     * @brief Reject reuse of moved or consumed scheduling state without registering another resume.
     */
    auto schedule_ownership() -> mcr::Task<void> {
        auto source = mcr::Coro::Schedule();
        static_assert(!std::is_copy_constructible_v<decltype(source)>);
        auto destination = std::move(source);
        try {
            (co_await source).value();
            require(false, "a moved scheduling awaiter must be rejected");
        } catch (std::logic_error const&) {}
        (co_await destination).value();
        try {
            (co_await destination).value();
            require(false, "a consumed scheduling awaiter must be rejected");
        } catch (std::logic_error const&) {}
    }

    /**
     * @brief Count tasks that were accepted concurrently with runtime shutdown.
     */
    auto count_start(std::atomic_int& runs) -> mcr::Task<void> {
        ++runs;
        co_return;
    }

    /**
     * @brief Verify queued startup, result types, exceptions, and cooperative scheduling.
     */
    auto scheduling(HttpServer const& server) -> void {
        std::thread::id started;
        auto value = mcr::Coro::Spawn(spawned_value(started, std::make_shared<int>(17))).value();
        require(*mcr::sync_wait(std::move(value)) == 17 && started != std::this_thread::get_id(), "Spawn must start cold tasks on the continuation thread and retain move-only results");
        int referenced{ 5 };
        mcr::sync_wait(mcr::Coro::Spawn(spawned_reference(referenced)).value()) = 9;
        require(referenced == 9, "Spawn must preserve reference results");
        try {
            (void)mcr::sync_wait(mcr::Coro::Spawn(spawned_failure()).value());
            require(false, "spawned exceptions must reach the result consumer");
        } catch (std::runtime_error const& error) {
            require(std::string_view{ error.what() } == "spawned failure", "Spawn must preserve the original exception");
        }
        try {
            (void)mcr::Coro::Spawn(mcr::Task<void>{});
            require(false, "Spawn must reject empty tasks");
        } catch (std::logic_error const&) {}
        auto completed = spawned_reference(referenced);
        completed.Start();
        require(&mcr::sync_wait(mcr::Coro::Spawn(std::move(completed)).value()) == &referenced, "Spawn must accept an already started task without restarting it");

        std::vector<int> order;
        mcr::sync_wait(scheduled_chain(std::this_thread::get_id(), order));
        require(order == std::vector<int>{ 1, 2, 3 }, "Schedule on the continuation thread must yield behind queued work");
        mcr::sync_wait(schedule_ownership());

        std::promise<void> entered, release;
        auto began = entered.get_future();
        auto held = mcr::Coro::Spawn(hold_scheduler(entered, release.get_future().share())).value();
        require(began.wait_for(3s) == std::future_status::ready, "scheduler must be held before posting cancellable tasks");
        auto before = server.Connections();
        auto request = mcr::Coro::Spawn(get(server, "/hello")).value();
        auto observed = mcr::Coro::Spawn(stop_requested()).value();
        auto owned = std::make_shared<int>(29);
        std::weak_ptr<int> weak = owned;
        std::thread::id abandoned_thread;
        {
            auto abandoned = mcr::Coro::Spawn(spawned_value(abandoned_thread, std::move(owned))).value();
        }
        require(!weak.expired(), "discarding a queued task must leave its cold frame owned by the scheduler");
        require(request.Cancel() && observed.Cancel(), "queued tasks must expose their cancellation sources");
        release.set_value();
        mcr::sync_wait(std::move(held));
        require(mcr::sync_wait(std::move(request)).value().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK && server.Connections() == before, "cancelling a spawned request before startup must prevent network I/O");
        require(mcr::sync_wait(std::move(observed)), "the public stop-token query must observe queued cancellation");
        mcr::sync_wait(schedule_result()).value();
        require(weak.expired() && abandoned_thread == started, "an abandoned queued task must run once and release its frame on the continuation thread");
    }

    auto concurrent(HttpServer const& server) -> void {
        std::mutex threads_mutex;
        std::set<std::thread::id> io_threads;
        auto observer = mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
            std::lock_guard lock{ threads_mutex };
            io_threads.insert(std::this_thread::get_id());
            return true;
        } };
        auto first = get(server, "/barrier", observer);
        auto second = get(server, "/barrier", observer);
        first.Start();
        second.Start();
        require(mcr::sync_wait(std::move(first)).value().status_code == 200 && mcr::sync_wait(std::move(second)).value().status_code == 200, "two requests must reach the server concurrently");
        require(io_threads.size() == 1 && !io_threads.contains(std::this_thread::get_id()), "curl callbacks must share one I/O thread");
        require(mcr::sync_wait(sequential(server)) == 400, "a resumed coroutine must be able to submit and await another request");
        std::vector<mcr::Task<mcr::Result<mcr::Response>>> tasks;
        for (int index{}; index < 40; ++index) {
            tasks.push_back(get(server, "/hello"));
            tasks.back().Start();
        }
        for (auto& task : tasks) {
            require(mcr::sync_wait(std::move(task)).value().status_code == 200, "concurrent submissions must all complete");
        }
    }

    /**
     * @brief Preserve configuration, preparation, transfer and HTTP error layers.
     * @param server Local HTTP fixture.
     */
    auto errors(HttpServer const& server) -> void {
        auto const before = server.Connections();
        auto invalid = mcr::request_coro<mcr::method::Get>(server.Url(), mcr::options::HttpVersion{ static_cast<mcr::options::HttpVersionCode>(255) });
        require(server.Connections() == before, "cold tasks with invalid options must defer configuration until startup");
        auto rejected = mcr::sync_wait(std::move(invalid));
        require(!rejected && rejected.error().code == mcr::ErrorCode::BAD_FUNCTION_ARGUMENT && server.Connections() == before, "coroutine template configuration errors must use Result without contacting the server");
        auto missing_url = mcr::sync_wait(mcr::request_coro<mcr::method::Get>());
        auto named_missing_url = mcr::sync_wait(mcr::GetCoro());
        require(missing_url && named_missing_url && missing_url->error.code == named_missing_url->error.code, "zero-option coroutine templates must match the named entry's error");
        require(mcr::sync_wait(get(server, "/slow", mcr::options::Timeout{ 20ms })).value().error.code == mcr::ErrorCode::OPERATION_TIMEDOUT, "timeouts must remain response transport errors");
        auto http_error = mcr::sync_wait(get(server, "/error")).value();
        require(http_error.status_code == 404 && !http_error.error, "HTTP errors must remain ordinary responses");
        require(mcr::sync_wait(get(server, "/redirect")).value().text == "Hello session!", "redirect behavior must be reused");
        try {
            (void)mcr::sync_wait(get(server, "/hello", mcr::HeaderCallback{ [](std::string_view, std::intptr_t) -> bool {
                                         throw std::runtime_error{ "header failure" };
                                     } }))
                .value();
            require(false, "curl callback exceptions must propagate at co_await");
        } catch (std::runtime_error const& error) {
            require(std::string_view{ error.what() } == "header failure", "original callback exception must be retained");
        }
        auto preparation = mcr::sync_wait(mcr::PostCoro(server.Url("/echo"), proxies(), mcr::Multipart{ { "missing", mcr::File{ "mcr_nonexistent_coro_upload_812905" } } }));
        require(!preparation && preparation.error().code == mcr::ErrorCode::READ_ERROR, "preparation failures must complete the task with an error");
        auto template_preparation = mcr::sync_wait(mcr::request_coro<mcr::method::Post>(server.Url("/echo"), proxies(), mcr::Multipart{ { "missing", mcr::File{ "mcr_nonexistent_coro_upload_812905" } } }));
        require(!template_preparation && template_preparation.error().code == preparation.error().code, "coroutine templates must propagate Prepare method failures through Result");
        require(mcr::sync_wait(get(server, "/hello")).value().status_code == 200, "one failed transfer must not stop the runtime");
    }

    auto parent_request(
        HttpServer const& server,
        mcr::HeaderCallback callback
    ) -> mcr::Task<mcr::Result<mcr::Response>> {
        co_return co_await get(server, "/stream", std::move(callback));
    }

    auto cancellation(HttpServer const& server) -> void {
        auto before = server.Connections();
        auto cancelled = get(server, "/hello");
        require(cancelled.Cancel() && !cancelled.Cancel(), "Cancel must signal once, including before Start");
        require(mcr::sync_wait(std::move(cancelled)).value().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK && server.Connections() == before, "cancellation before submission must not contact the server");

        std::promise<void> arrived;
        auto seen = arrived.get_future();
        std::atomic_bool signalled{};
        auto parent = parent_request(server, mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
                                         if (!signalled.exchange(true)) {
                                             arrived.set_value();
                                         }
                                         return true;
                                     } });
        auto source = parent.GetStopSource();
        parent.Start();
        require(seen.wait_for(3s) == std::future_status::ready, "stream must start before cancellation");
        source.request_stop();
        require(mcr::sync_wait(std::move(parent)).value().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "parent cancellation must wake polling and remove the active child transfer");
    }

    auto continuation_checks(
        HttpServer const& server,
        std::thread::id& resumed
    ) -> mcr::Task<void> {
        (void)co_await get(server, "/hello");
        resumed = std::this_thread::get_id();
        bool wait_rejected{}, cleanup_rejected{};
        try {
            (void)mcr::sync_wait(get(server, "/hello")).value();
        } catch (std::logic_error const&) {
            wait_rejected = true;
        }
        cleanup_rejected = !mcr::Coro::Cleanup();
        require(wait_rejected && cleanup_rejected, "blocking waits and cleanup must be rejected on the continuation thread");
    }

    /**
     * @brief Deliberately occupy the continuation thread while I/O must remain responsive.
     */
    auto held_continuation(
        HttpServer const& server,
        std::promise<void>& entered,
        std::shared_future<void> release
    ) -> mcr::Task<void> {
        (void)co_await get(server, "/hello");
        entered.set_value();
        require(release.wait_for(3s) == std::future_status::ready, "continuation test must be released");
    }

    auto independent_io(HttpServer const& server) -> void {
        std::promise<void> entered, release, header;
        auto began = entered.get_future();
        auto received = header.get_future();
        auto held = held_continuation(server, entered, release.get_future().share());
        held.Start();
        require(began.wait_for(3s) == std::future_status::ready, "continuation must start");
        std::atomic_bool signalled{};
        auto next = get(server, "/hello", mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
                            if (!signalled.exchange(true)) {
                                header.set_value();
                            }
                            return true;
                        } });
        next.Start();
        bool progressed = received.wait_for(2s) == std::future_status::ready;
        release.set_value();
        mcr::sync_wait(std::move(held));
        require(mcr::sync_wait(std::move(next)).value().status_code == 200 && progressed, "a busy continuation must not prevent unrelated network I/O");
    }

    auto downloads(HttpServer const& server) -> void {
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
        auto response = mcr::sync_wait(std::move(download)).value();
        std::ifstream file{ path, std::ios::binary };
        std::string bytes{ std::istreambuf_iterator<char>{ file }, {} };
        require(response.status_code == 200 && !response.error && response.text.empty() && bytes.size() == 256, "download must close its output before publishing metadata");
        for (std::size_t index{}; index < bytes.size(); ++index) {
            require(static_cast<unsigned char>(bytes[index]) == index, "download must preserve binary bytes");
        }
        auto failed = mcr::sync_wait(mcr::DownloadCoro(path / "invalid.bin", server.Url("/binary"), proxies()));
        require(!failed && failed.error().code == mcr::ErrorCode::WRITE_ERROR, "file opening failures must return WRITE_ERROR");
    }

    auto shutdown(HttpServer const& server) -> void {
        std::promise<void> arrived;
        auto seen = arrived.get_future();
        std::promise<void> release;
        auto gate = release.get_future().share();
        std::atomic_bool signalled{};
        auto active = get(server, "/stream", mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
                              if (!signalled.exchange(true)) {
                                  arrived.set_value();
                                  (void)gate.wait_for(3s);
                              }
                              return true;
                          } });
        active.Start();
        require(seen.wait_for(3s) == std::future_status::ready, "request must be active at shutdown");
        std::promise<void> scheduler_entered, scheduler_release;
        auto scheduler_began = scheduler_entered.get_future();
        auto held = mcr::Coro::Spawn(hold_scheduler(scheduler_entered, scheduler_release.get_future().share())).value();
        require(scheduler_began.wait_for(3s) == std::future_status::ready, "scheduler must be held before queuing shutdown work");
        std::vector<int> steps;
        std::vector<mcr::Task<void>> scheduled;
        for (int index{}; index < 8; ++index) {
            scheduled.push_back(mcr::Coro::Spawn(record_step(steps, index)).value());
        }
        auto yielded = schedule_result();
        yielded.Start();
        // Hold the I/O thread inside a bounded callback, making the pending queue deterministic.
        std::atomic_int queued_callbacks{};
        std::vector<mcr::Task<mcr::Result<mcr::Response>>> queued;
        for (int index{}; index < 8; ++index) {
            queued.push_back(get(server, "/hello", mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) {
                                     ++queued_callbacks;
                                     return true;
                                 } }));
            queued.back().Start();
        }
        std::latch race{ 2 };
        std::atomic_int runs{};
        std::vector<mcr::Task<void>> racing;
        std::exception_ptr post_error;
        std::jthread poster{ [&] {
            race.arrive_and_wait();
            try {
                for (int index{}; index < 200; ++index) {
                    auto posted = mcr::Coro::Spawn(count_start(runs));
                    if (posted) {
                        racing.push_back(std::move(*posted));
                    } else {
                        require(posted.error().code == mcr::ErrorCode::FAILED_INIT, "shutdown races must reject startup with FAILED_INIT");
                    }
                }
            } catch (...) {
                post_error = std::current_exception();
            }
        } };
        std::jthread cleanup{ [&] {
            race.arrive_and_wait();
            mcr::Coro::Cleanup().value();
        } };
        bool stopping{};
        auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!stopping && std::chrono::steady_clock::now() < deadline) {
            stopping = !mcr::Coro::Startup();
            std::this_thread::yield();
        }
        release.set_value();
        scheduler_release.set_value();
        cleanup.join();
        poster.join();
        if (post_error) {
            std::rethrow_exception(post_error);
        }
        require(stopping, "cleanup must close submissions before joining I/O");
        mcr::sync_wait(std::move(held));
        for (auto& task : scheduled) {
            mcr::sync_wait(std::move(task));
        }
        require(steps == std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 } && mcr::sync_wait(std::move(yielded)), "cleanup must drain all accepted startup and scheduling work in queue order");
        for (auto& task : racing) {
            mcr::sync_wait(std::move(task));
        }
        require(runs == static_cast<int>(racing.size()), "every startup racing shutdown must either be rejected or execute exactly once");
        require(mcr::sync_wait(std::move(active)).value().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "shutdown must finish active tasks before returning");
        for (auto& task : queued) {
            require(mcr::sync_wait(std::move(task)).value().error.code == mcr::ErrorCode::ABORTED_BY_CALLBACK, "shutdown must complete every queued task");
        }
        require(queued_callbacks == 0, "queued requests must not enter curl after shutdown begins");
        mcr::Coro::Cleanup().value();
        require(!mcr::sync_wait(get(server, "/hello")), "submissions after cleanup must return an error");
        require(!mcr::sync_wait(schedule_result()), "Schedule after cleanup must return an error without suspending");
        auto owned = std::make_shared<int>(23);
        std::weak_ptr<int> weak = owned;
        std::thread::id unstarted;
        auto rejected = mcr::Coro::Spawn(spawned_value(unstarted, std::move(owned)));
        require(!rejected && rejected.error().code == mcr::ErrorCode::FAILED_INIT && weak.expired() && unstarted == std::thread::id{}, "rejected startup must release its cold frame without running the task");
    }
} // namespace

int main() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        mcr::Coro::Startup().value();
        HttpServer server;
        auto before = server.Connections();
        { auto lazy = get(server, "/hello"); }
        require(server.Connections() == before, "discarded cold HTTP tasks must not start requests");
        methods(server);
        ownership(server);
        scheduling(server);
        concurrent(server);
        errors(server);
        cancellation(server);
        independent_io(server);
        downloads(server);
        std::thread::id resumed, io;
        (void)mcr::sync_wait(get(server, "/hello", mcr::HeaderCallback{ [&](std::string_view, std::intptr_t) { io = std::this_thread::get_id(); return true; } })).value();
        mcr::sync_wait(continuation_checks(server, resumed));
        require(resumed != io && resumed != std::this_thread::get_id(), "continuations must run separately from I/O and caller threads");
        shutdown(server);
        server.Check();
    } catch (std::exception const& error) {
        std::println("test_coro: {}", error.what());
        passed = false;
    }
    mcr::Coro::Cleanup().value();
    curl_global_cleanup();
    return passed ? 0 : 1;
}
