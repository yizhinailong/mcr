/**
 * @file api.cppm
 * @brief One-shot HTTP requests, coroutines, asynchronous continuations and tuple-based batches.
 */
export module mcr.api;
export import mcr.session;
export import mcr.task;
import mcr.coro_runtime;
import std;

namespace mcr::detail {
    /**
     * @brief Unwrap an explicitly borrowed option.
     * @tparam T Option type.
     * @param value Borrowed option.
     * @return Referenced option.
     */
    template <typename T>
    auto unwrap_option(std::reference_wrapper<T> value) -> T& {
        return value.get();
    }

    /**
     * @brief Preserve an ordinary option's value category.
     * @tparam T Option type.
     * @param value Option to forward.
     * @return Forwarded option.
     */
    template <typename T>
    auto unwrap_option(T&& value) -> T&& {
        return std::forward<T>(value);
    }

    /**
     * @brief Apply options in order, merging all Header arguments.
     * @tparam Ts Option types.
     * @param session Destination session.
     * @param options Options to apply.
     * @return Success or the first operation error.
     */
    template <typename... Ts>
    auto set_options(Session& session, Ts&&... options) -> Result<void> {
        bool has_header{ false };
        auto set = [&](auto&& argument) -> Result<void> {
            auto&& option{ unwrap_option(std::forward<decltype(argument)>(argument)) };
            if constexpr (std::same_as<std::remove_cvref_t<decltype(option)>, Header>) {
                if (has_header) {
                    if (auto status = session.UpdateHeader(option); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                } else {
                    if (auto status = session.SetHeader(option); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                    has_header = true;
                }
            } else {
                if (auto status = session.SetOption(std::forward<decltype(option)>(option)); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
            return {};
        };
        Result<void> status;
        ((status ? status = set(std::forward<Ts>(options)) : status), ...);
        return status;
    }

    /**
     * @brief Expand one tuple of request options.
     * @tparam Tuple Tuple type.
     * @param session Destination session.
     * @param options Tuple to expand.
     * @return Success or the first operation error.
     */
    template <typename Tuple>
    auto apply_options(Session& session, Tuple&& options) -> Result<void> {
        return std::apply([&](auto&&... values) { return set_options(session, std::forward<decltype(values)>(values)...); }, std::forward<Tuple>(options));
    }

    /**
     * @brief Configure and execute a temporary session.
     * @tparam Action Session method.
     * @tparam Ts Option types.
     * @param options Options to apply.
     * @return Independent response.
     */
    template <auto Action, typename... Ts>
    auto request(Ts&&... options) -> Result<Response> {
        auto owned_session = Session::Create();
        if (!owned_session) {
            return std::unexpected{ std::move(owned_session.error()) };
        }
        auto& session = **owned_session;
        if (auto status = set_options(session, std::forward<Ts>(options)...); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return std::invoke(Action, session);
    }

    /**
     * @brief Submit owned request options to the pool.
     * @tparam Action Session method.
     * @tparam Ts Option types.
     * @param options Options to own.
     * @return Asynchronous response.
     */
    template <auto Action, typename... Ts>
    auto request_async(Ts&&... options) -> Result<AsyncResponse> {
        return mcr::async([](auto... values) { return request<Action>(std::move(values)...); }, std::forward<Ts>(options)...);
    }

    /**
     * @brief Own request options in a lazy coroutine frame and transfer through curl multi.
     * @tparam Prepare Session preparation method, without synchronous network I/O.
     * @tparam Tuple Owned option tuple type.
     * @param options Options captured at the public API call, before initial suspension.
     * @return A single-consumer response task.
     */
    template <auto Prepare, typename Tuple>
    auto request_coro(Tuple options) -> Task<Result<Response>> {
        auto created_session = Session::Create();
        if (!created_session) {
            co_return std::unexpected{ std::move(created_session.error()) };
        }
        auto session = std::move(*created_session);
        if (auto status = apply_options(*session, std::move(options)); !status) {
            co_return std::unexpected{ std::move(status.error()) };
        }
        auto token = co_await TaskStopToken{};
        co_return co_await CoroTransferAwaiter{ std::move(session), Prepare, token };
    }

    /**
     * @brief Submit a request followed by a continuation.
     * @tparam Action Session method.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Continuation to own.
     * @param options Options to own.
     * @return Future preserving the continuation's result type.
     */
    template <auto Action, typename Then, typename... Ts>
    auto request_callback(Then&& then, Ts&&... options) {
        return mcr::async<true>(
            [](auto handler, auto... values) -> decltype(auto) {
                return std::invoke(handler, request<Action>(std::move(values)...));
            },
            std::forward<Then>(then),
            std::forward<Ts>(options)...
        );
    }

    /**
     * @brief Register one configured session.
     * @tparam Tuple Option tuple type.
     * @param multi Destination batch.
     * @param options One request's options.
     * @return Success or the first operation error.
     */
    template <typename Tuple>
    auto add_request(MultiPerform& multi, Tuple&& options) -> Result<void> {
        auto created_session = Session::Create();
        if (!created_session) {
            return std::unexpected{ std::move(created_session.error()) };
        }
        auto session = std::move(*created_session);
        if (auto status = apply_options(*session, std::forward<Tuple>(options)); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return multi.AddSession(session);
    }

    /**
     * @brief Perform tuple-based requests concurrently.
     * @tparam Action Batch method.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request.
     * @return Responses in argument order.
     */
    template <auto Action, typename... Tuples>
    auto multi_request(Tuples&&... options) -> Result<std::vector<Response>> {
        MultiPerform multi;
        Result<void> status;
        ((status ? status = add_request(multi, std::forward<Tuples>(options)) : status), ...);
        if (!status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return std::invoke(Action, multi);
    }

    /**
     * @brief Submit one cooperatively cancellable request.
     * @tparam Action Session method.
     * @tparam Tuple Option tuple type.
     * @param options Options copied or moved into the task.
     * @return Response future sharing the task's cancellation flag.
     */
    template <auto Action, typename Tuple>
    auto cancellable_request(Tuple&& options) -> Result<utils::AsyncWrapper<Result<Response>, true>> {
        auto* pool{ GlobalThreadPool::GetInstance() };
        if (!pool) {
            return std::unexpected{
                Error{ ErrorCode::FAILED_INIT, "mcr::MultiAsync: global thread pool has been cleaned up." }
            };
        }
        auto cancelled{ std::make_shared<std::atomic_bool>(false) };
        auto future{ pool->Submit([cancelled, values = std::forward<Tuple>(options)]() mutable -> Result<Response> {
            if (cancelled->load()) {
                return Response{};
            }
            auto owned_session = Session::Create();
            if (!owned_session) {
                return std::unexpected{ std::move(owned_session.error()) };
            }
            auto& session = **owned_session;
            if (auto status = session.SetCancellationParam(cancelled); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = apply_options(session, std::move(values)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return std::invoke(Action, session);
        }) };
        if (!future) {
            return std::unexpected{ std::move(future.error()) };
        }
        return utils::AsyncWrapper<Result<Response>, true>{ std::move(*future), std::move(cancelled) };
    }

    /**
     * @brief Submit independently cancellable requests in argument order.
     * @tparam Action Session method.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per task.
     * @return One future per request.
     */
    template <auto Action, typename... Tuples>
    auto multi_async(Tuples&&... options) -> Result<std::vector<utils::AsyncWrapper<Result<Response>, true>>> {
        std::vector<utils::AsyncWrapper<Result<Response>, true>> responses;
        responses.reserve(sizeof...(Tuples));
        auto submit = [&](auto&& value) -> Result<void> {
            auto request = cancellable_request<Action>(std::forward<decltype(value)>(value));
            if (!request) {
                return std::unexpected{ std::move(request.error()) };
            }
            responses.push_back(std::move(*request));
            return {};
        };
        Result<void> status;
        ((status ? status = submit(std::forward<Tuples>(options)) : status), ...);
        if (!status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return responses;
    }
} // namespace mcr::detail

export namespace mcr {

    /**
     * @brief Perform one GET request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Get(Ts&&... options) -> Result<Response> {
        return detail::request<&Session::Get>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one GET request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto GetAsync(Ts... options) -> Result<AsyncResponse> {
        return detail::request_async<&Session::Get>(std::move(options)...);
    }

    /**
     * @brief Create a lazy GET request driven by curl multi.
     * @tparam Ts Request option types.
     * @param options Options copied or moved now; views and reference wrappers remain borrowed.
     * @return Task yielding a Response; cancellation produces ABORTED_BY_CALLBACK.
     */
    template <typename... Ts>
    [[nodiscard]] auto GetCoro(Ts... options) -> Task<Result<Response>> {
        return detail::request_coro<&Session::PrepareGet>(std::tuple{ std::move(options)... });
    }

    /**
     * @brief Submit GET followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving Result<Response>, including configuration failures.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto GetCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Get>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent GET requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiGet(Tuples&&... options) -> Result<std::vector<Response>> {
        return detail::multi_request<&MultiPerform::Get>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable GET requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiGetAsync(Tuples&&... options) -> Result<std::vector<utils::AsyncWrapper<Result<Response>, true>>> {
        return detail::multi_async<&Session::Get>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one POST request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Post(Ts&&... options) -> Result<Response> {
        return detail::request<&Session::Post>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one POST request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto PostAsync(Ts... options) -> Result<AsyncResponse> {
        return detail::request_async<&Session::Post>(std::move(options)...);
    }

    /**
     * @brief Create a lazy POST request driven by curl multi.
     * @tparam Ts Request option types.
     * @param options Owned options; underlying borrowed data must outlive the request.
     * @return Task yielding a Response or propagating a preparation/callback exception.
     */
    template <typename... Ts>
    [[nodiscard]] auto PostCoro(Ts... options) -> Task<Result<Response>> {
        return detail::request_coro<&Session::PreparePost>(std::tuple{ std::move(options)... });
    }

    /**
     * @brief Submit POST followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving Result<Response>, including configuration failures.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto PostCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Post>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent POST requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiPost(Tuples&&... options) -> Result<std::vector<Response>> {
        return detail::multi_request<&MultiPerform::Post>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable POST requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiPostAsync(Tuples&&... options) -> Result<std::vector<utils::AsyncWrapper<Result<Response>, true>>> {
        return detail::multi_async<&Session::Post>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one PUT request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Put(Ts&&... options) -> Result<Response> {
        return detail::request<&Session::Put>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one PUT request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto PutAsync(Ts... options) -> Result<AsyncResponse> {
        return detail::request_async<&Session::Put>(std::move(options)...);
    }

    /**
     * @brief Create a lazy PUT request driven by curl multi.
     * @tparam Ts Request option types.
     * @param options Owned options; underlying borrowed data must outlive the request.
     * @return Single-consumer response task.
     */
    template <typename... Ts>
    [[nodiscard]] auto PutCoro(Ts... options) -> Task<Result<Response>> {
        return detail::request_coro<&Session::PreparePut>(std::tuple{ std::move(options)... });
    }

    /**
     * @brief Submit PUT followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving Result<Response>, including configuration failures.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto PutCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Put>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent PUT requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiPut(Tuples&&... options) -> Result<std::vector<Response>> {
        return detail::multi_request<&MultiPerform::Put>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable PUT requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiPutAsync(Tuples&&... options) -> Result<std::vector<utils::AsyncWrapper<Result<Response>, true>>> {
        return detail::multi_async<&Session::Put>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one HEAD request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Head(Ts&&... options) -> Result<Response> {
        return detail::request<&Session::Head>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one HEAD request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto HeadAsync(Ts... options) -> Result<AsyncResponse> {
        return detail::request_async<&Session::Head>(std::move(options)...);
    }

    /**
     * @brief Create a lazy HEAD request driven by curl multi.
     * @tparam Ts Request option types.
     * @param options Owned options; underlying borrowed data must outlive the request.
     * @return Single-consumer response task with an empty body.
     */
    template <typename... Ts>
    [[nodiscard]] auto HeadCoro(Ts... options) -> Task<Result<Response>> {
        return detail::request_coro<&Session::PrepareHead>(std::tuple{ std::move(options)... });
    }

    /**
     * @brief Submit HEAD followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving Result<Response>, including configuration failures.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto HeadCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Head>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent HEAD requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiHead(Tuples&&... options) -> Result<std::vector<Response>> {
        return detail::multi_request<&MultiPerform::Head>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable HEAD requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiHeadAsync(Tuples&&... options) -> Result<std::vector<utils::AsyncWrapper<Result<Response>, true>>> {
        return detail::multi_async<&Session::Head>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one DELETE request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Delete(Ts&&... options) -> Result<Response> {
        return detail::request<&Session::Delete>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one DELETE request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto DeleteAsync(Ts... options) -> Result<AsyncResponse> {
        return detail::request_async<&Session::Delete>(std::move(options)...);
    }

    /**
     * @brief Create a lazy DELETE request driven by curl multi.
     * @tparam Ts Request option types.
     * @param options Owned options; underlying borrowed data must outlive the request.
     * @return Single-consumer response task.
     */
    template <typename... Ts>
    [[nodiscard]] auto DeleteCoro(Ts... options) -> Task<Result<Response>> {
        return detail::request_coro<&Session::PrepareDelete>(std::tuple{ std::move(options)... });
    }

    /**
     * @brief Submit DELETE followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving Result<Response>, including configuration failures.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto DeleteCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Delete>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent DELETE requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiDelete(Tuples&&... options) -> Result<std::vector<Response>> {
        return detail::multi_request<&MultiPerform::Delete>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable DELETE requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiDeleteAsync(Tuples&&... options) -> Result<std::vector<utils::AsyncWrapper<Result<Response>, true>>> {
        return detail::multi_async<&Session::Delete>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one OPTIONS request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Options(Ts&&... options) -> Result<Response> {
        return detail::request<&Session::Options>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one OPTIONS request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto OptionsAsync(Ts... options) -> Result<AsyncResponse> {
        return detail::request_async<&Session::Options>(std::move(options)...);
    }

    /**
     * @brief Create a lazy OPTIONS request driven by curl multi.
     * @tparam Ts Request option types.
     * @param options Owned options; underlying borrowed data must outlive the request.
     * @return Single-consumer response task.
     */
    template <typename... Ts>
    [[nodiscard]] auto OptionsCoro(Ts... options) -> Task<Result<Response>> {
        return detail::request_coro<&Session::PrepareOptions>(std::tuple{ std::move(options)... });
    }

    /**
     * @brief Submit OPTIONS followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving Result<Response>, including configuration failures.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto OptionsCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Options>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent OPTIONS requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiOptions(Tuples&&... options) -> Result<std::vector<Response>> {
        return detail::multi_request<&MultiPerform::Options>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable OPTIONS requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiOptionsAsync(Tuples&&... options) -> Result<std::vector<utils::AsyncWrapper<Result<Response>, true>>> {
        return detail::multi_async<&Session::Options>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Perform one PATCH request.
     * @tparam Ts Option types.
     * @param options Options applied in order; repeated headers are merged.
     * @return Independent response snapshot.
     */
    template <typename... Ts>
    auto Patch(Ts&&... options) -> Result<Response> {
        return detail::request<&Session::Patch>(std::forward<Ts>(options)...);
    }

    /**
     * @brief Submit one PATCH request with owned options.
     * @tparam Ts Option types.
     * @param options Options copied or moved into the task.
     * @return Response future.
     * @note Views and explicit reference wrappers still borrow their underlying data.
     */
    template <typename... Ts>
    auto PatchAsync(Ts... options) -> Result<AsyncResponse> {
        return detail::request_async<&Session::Patch>(std::move(options)...);
    }

    /**
     * @brief Create a lazy PATCH request driven by curl multi.
     * @tparam Ts Request option types.
     * @param options Owned options; underlying borrowed data must outlive the request.
     * @return Single-consumer response task.
     */
    template <typename... Ts>
    [[nodiscard]] auto PatchCoro(Ts... options) -> Task<Result<Response>> {
        return detail::request_coro<&Session::PreparePatch>(std::tuple{ std::move(options)... });
    }

    /**
     * @brief Submit PATCH followed by a continuation.
     * @tparam Then Continuation type.
     * @tparam Ts Option types.
     * @param then Callable receiving Result<Response>, including configuration failures.
     * @param options Options copied or moved into the task.
     * @return Cancellable wrapper for the continuation result.
     * @note As in cpr::async, cancellation restricts result access; it does not abort this request.
     */
    template <typename Then, typename... Ts>
    auto PatchCallback(Then then, Ts... options) {
        return detail::request_callback<&Session::Patch>(std::move(then), std::move(options)...);
    }

    /**
     * @brief Perform concurrent PATCH requests.
     * @tparam Tuples Option tuple types.
     * @param options One tuple per request; zero tuples is allowed.
     * @return Responses in argument order.
     */
    template <typename... Tuples>
    auto MultiPatch(Tuples&&... options) -> Result<std::vector<Response>> {
        return detail::multi_request<&MultiPerform::Patch>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Submit independently cancellable PATCH requests.
     * @tparam Tuples Option tuple types.
     * @param options Tuples copied or moved into tasks; reference elements remain borrowed.
     * @return Futures in argument order; cancellation is observed before execution and during transfer.
     */
    template <typename... Tuples>
    auto MultiPatchAsync(Tuples&&... options) -> Result<std::vector<utils::AsyncWrapper<Result<Response>, true>>> {
        return detail::multi_async<&Session::Patch>(std::forward<Tuples>(options)...);
    }

    /**
     * @brief Download into a borrowed output stream.
     * @tparam Ts Option types.
     * @param file Open stream, normally in binary mode.
     * @param options Request options.
     * @return Metadata with an empty response body.
     */
    template <typename... Ts>
    auto Download(std::ofstream& file, Ts&&... options) -> Result<Response> {
        if (!file.is_open() || !file.good()) {
            return std::unexpected{
                Error{ ErrorCode::WRITE_ERROR, "mcr::Download: output stream is not writable." }
            };
        }
        auto owned_session = Session::Create();
        if (!owned_session) {
            return std::unexpected{ std::move(owned_session.error()) };
        }
        auto& session = **owned_session;
        if (auto status = detail::set_options(session, std::forward<Ts>(options)...); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return session.Download(file);
    }

    /**
     * @brief Download into a copied callback.
     * @tparam Ts Option types.
     * @param write Body consumer.
     * @param options Request options.
     * @return Metadata with an empty response body.
     */
    template <typename... Ts>
    auto Download(WriteCallback const& write, Ts&&... options) -> Result<Response> {
        auto owned_session = Session::Create();
        if (!owned_session) {
            return std::unexpected{ std::move(owned_session.error()) };
        }
        auto& session = **owned_session;
        if (auto status = detail::set_options(session, std::forward<Ts>(options)...); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return session.Download(write);
    }

    /**
     * @brief Download asynchronously to a file opened in binary truncation mode.
     * @tparam Ts Option types.
     * @param local_path Destination owned by the task.
     * @param options Request options owned by the task.
     * @return Future; opening or closing failures surface through Get().
     * @note Failed transfers may leave a partial file.
     */
    template <typename... Ts>
    auto DownloadAsync(std::filesystem::path local_path, Ts... options) -> Result<AsyncResponse> {
        return mcr::async(
            [](std::filesystem::path path, auto... values) -> Result<Response> {
                std::ofstream file{ path, std::ios::binary | std::ios::trunc };
                auto          response{ Download(file, std::move(values)...) };
                if (!response) {
                    return response;
                }
                file.close();
                if (file.fail()) {
                    return std::unexpected{
                        Error{ ErrorCode::WRITE_ERROR, "mcr::DownloadAsync: could not finish writing the output file." }
                    };
                }
                return response;
            },
            std::move(local_path),
            std::move(options)...
        );
    }

    /**
     * @brief Create a lazy curl multi download owning its destination path and options.
     * @tparam Ts Request option types.
     * @param local_path File opened in binary truncation mode when the task starts.
     * @param options Owned options; views and reference wrappers retain borrowed lifetimes.
     * @return Task yielding response metadata after closing the file.
     * @note Failed or cancelled transfers may leave a partial file.
     */
    template <typename... Ts>
    [[nodiscard]] auto DownloadCoro(std::filesystem::path local_path, Ts... options) -> Task<Result<Response>> {
        std::ofstream file{ local_path, std::ios::binary | std::ios::trunc };
        if (!file.is_open() || !file.good()) {
            co_return std::unexpected{
                Error{ ErrorCode::WRITE_ERROR, "mcr::DownloadCoro: output stream is not writable." }
            };
        }
        auto created_session = Session::Create();
        if (!created_session) {
            co_return std::unexpected{ std::move(created_session.error()) };
        }
        auto session = std::move(*created_session);
        if (auto status = detail::set_options(*session, std::move(options)...); !status) {
            co_return std::unexpected{ std::move(status.error()) };
        }
        auto token    = co_await detail::TaskStopToken{};
        auto response = co_await detail::CoroTransferAwaiter{
            std::move(session),
            [&file](Session& current) { return current.PrepareDownload(file); },
            token
        };
        file.close();
        if (file.fail()) {
            co_return std::unexpected{
                Error{ ErrorCode::WRITE_ERROR, "mcr::DownloadCoro: could not finish writing the output file." }
            };
        }
        co_return response;
    }
} // namespace mcr
