/**
 * @file test_async_wrapper.cpp
 * @brief Verify future forwarding, cooperative cancellation, sharing, and ownership transitions.
 */
import std;
import mcr;
import mcr.threadpool;

using PlainResult       = mcr::utils::AsyncWrapper<int>;
using CancellableResult = mcr::utils::AsyncWrapper<int, true>;

static_assert(std::is_same_v<decltype(mcr::utils::AsyncWrapper{ std::future<int>{} }), PlainResult>);
static_assert(std::is_same_v<decltype(mcr::utils::AsyncWrapper{ std::future<int>{}, std::shared_ptr<std::atomic_bool>{} }), CancellableResult>);
static_assert(std::is_default_constructible_v<PlainResult>);
static_assert(!std::is_default_constructible_v<CancellableResult>);
static_assert(!std::is_convertible_v<std::future<int>, PlainResult>);
static_assert(std::derived_from<CancellableResult, PlainResult>);
static_assert(!std::is_copy_constructible_v<PlainResult> && !std::is_copy_assignable_v<PlainResult>);
static_assert(!std::is_copy_constructible_v<CancellableResult> && !std::is_copy_assignable_v<CancellableResult>);
static_assert(std::is_nothrow_move_constructible_v<PlainResult> && std::is_nothrow_move_assignable_v<PlainResult>);
static_assert(std::is_nothrow_move_constructible_v<CancellableResult> && std::is_nothrow_move_assignable_v<CancellableResult>);
static_assert(std::is_same_v<decltype(std::declval<mcr::utils::AsyncWrapper<void>&>().Get()), void>);
static_assert(std::is_same_v<decltype(std::declval<mcr::utils::AsyncWrapper<int&>&>().Get()), int&>);
static_assert(noexcept(std::declval<PlainResult&>().Share()));
static_assert(noexcept(std::declval<CancellableResult const&>().Valid()));
static_assert(std::is_same_v<std::underlying_type_t<mcr::utils::CancellationResult>, std::uint8_t>);
static_assert(static_cast<int>(mcr::utils::CancellationResult::failure) == 0);
static_assert(static_cast<int>(mcr::utils::CancellationResult::success) == 1);
static_assert(static_cast<int>(mcr::utils::CancellationResult::invalid_operation) == 2);

namespace {

    using namespace std::chrono_literals;
    using CancelResult = mcr::utils::CancellationResult;

    auto require(bool condition, std::string_view message) -> void {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    template <typename Function>
    auto expect_logic_error(Function&& function, std::string_view reason) -> void {
        try {
            function();
        } catch (std::logic_error const& error) {
            std::string_view const message{ error.what() };
            require(message.starts_with("mcr::utils::AsyncWrapper::") && message.contains(reason), "invalid/cancelled access must provide the wrapper's operation diagnostic");
            return;
        }
        throw std::runtime_error{ "invalid/cancelled access must throw logic_error" };
    }

    template <typename Wrapper>
    auto check_rejected_access(Wrapper& wrapper, std::string_view reason) -> void {
        require(!wrapper.Valid(), "invalid and cancelled wrappers must report Valid false");
        expect_logic_error([&] { (void)wrapper.Get(); }, reason);
        expect_logic_error([&] { std::as_const(wrapper).Wait(); }, reason);
        expect_logic_error([&] { (void)std::as_const(wrapper).WaitFor(0ms); }, reason);
        expect_logic_error([&] { (void)std::as_const(wrapper).WaitUntil(std::chrono::steady_clock::now()); }, reason);
    }

    template <bool Cancellable, typename T>
    auto wrap(std::future<T>&& future) -> mcr::utils::AsyncWrapper<T, Cancellable> {
        if constexpr (Cancellable) {
            return mcr::utils::AsyncWrapper{ std::move(future), std::make_shared<std::atomic_bool>(false) };
        } else {
            return mcr::utils::AsyncWrapper{ std::move(future) };
        }
    }

    template <bool Cancellable>
    auto check_future_operations() -> void {
        auto invalid{ wrap<Cancellable>(std::future<int>{}) };
        check_rejected_access(invalid, "invalid");
        require(!invalid.Share().valid(), "sharing an invalid future must return an invalid shared future without throwing");

        std::promise<int> promise;
        auto              future{ promise.get_future() };
        auto              wrapper{ wrap<Cancellable>(std::move(future)) };
        require(!future.valid() && wrapper.Valid(), "construction must move exclusive future ownership");
        require(std::as_const(wrapper).WaitFor(0ms) == std::future_status::timeout, "a pending promise must report a relative timeout");
        require(std::as_const(wrapper).WaitUntil(std::chrono::steady_clock::now() - 1s) == std::future_status::timeout, "a pending promise must report a past steady-clock deadline");
        require(wrapper.WaitUntil(std::chrono::system_clock::now() - 1s) == std::future_status::timeout, "WaitUntil must support other clock types");
        promise.set_value(42);
        std::as_const(wrapper).Wait();
        require(wrapper.WaitFor(std::chrono::duration<double>{ 0 }) == std::future_status::ready && wrapper.WaitUntil(std::chrono::steady_clock::now()) == std::future_status::ready && wrapper.Valid(), "waiting on a ready future must preserve its state");
        require(wrapper.Get() == 42, "Get must return the completed result");
        check_rejected_access(wrapper, "invalid");

        std::promise<int>  blocked_promise;
        auto               blocked{ wrap<Cancellable>(blocked_promise.get_future()) };
        std::promise<void> entering_wait;
        auto               entered{ entering_wait.get_future() };
        auto               waiter{ std::async(std::launch::async, [&] {
            entering_wait.set_value();
            std::as_const(blocked).Wait();
            return blocked.Get();
        }) };
        entered.wait();
        bool const was_pending{ waiter.wait_for(0ms) == std::future_status::timeout };
        blocked_promise.set_value(73);
        auto const result{ waiter.get() };
        require(was_pending && result == 73, "Wait must keep a consumer blocked until its promise is fulfilled");

        int  deferred_calls{ 0 };
        auto deferred{ wrap<Cancellable>(std::async(std::launch::deferred, [&] { ++deferred_calls; return 9; })) };
        require(deferred.WaitFor(0ms) == std::future_status::deferred && deferred.WaitUntil(std::chrono::steady_clock::now()) == std::future_status::deferred && deferred_calls == 0, "timed waits must preserve deferred execution");
        deferred.Wait();
        require(deferred_calls == 1 && deferred.Valid() && deferred.Get() == 9, "Wait must run deferred work once without consuming its result");

        std::promise<std::string> shared_promise;
        auto                      shareable{ wrap<Cancellable>(shared_promise.get_future()) };
        auto                      shared{ shareable.Share() };
        auto                      shared_copy{ shared };
        check_rejected_access(shareable, "invalid");
        require(shared.valid() && !shareable.Share().valid(), "Share must transfer the state only once");
        shared_promise.set_value("shared result");
        require(shared.get() == "shared result" && shared_copy.get() == "shared result" && shared.get() == "shared result", "shared futures must permit copied and repeated reads");
    }

    template <bool Cancellable>
    auto check_result_types_and_exceptions() -> void {
        std::promise<void> void_promise;
        auto               void_result{ wrap<Cancellable>(void_promise.get_future()) };
        void_promise.set_value();
        void_result.Get();
        require(!void_result.Valid(), "Get must consume void futures");

        int                referent{ 11 };
        std::promise<int&> reference_promise;
        auto               reference{ wrap<Cancellable>(reference_promise.get_future()) };
        reference_promise.set_value(referent);
        auto& result{ reference.Get() };
        result = 12;
        require(&result == &referent && referent == 12 && !reference.Valid(), "reference results must preserve identity and writability");

        std::promise<std::unique_ptr<int>> owner_promise;
        auto                               owner{ wrap<Cancellable>(owner_promise.get_future()) };
        owner_promise.set_value(std::make_unique<int>(99));
        auto owned{ owner.Get() };
        require(owned && *owned == 99 && !owner.Valid(), "Get must transfer move-only results without copying");

        std::promise<int> failed_promise;
        auto              failed{ wrap<Cancellable>(failed_promise.get_future()) };
        failed_promise.set_exception(std::make_exception_ptr(std::runtime_error{ "task failed" }));
        failed.Wait();
        bool propagated{ false };
        try {
            (void)failed.Get();
        } catch (std::runtime_error const& error) {
            propagated = std::string_view{ error.what() } == "task failed";
        }
        require(propagated && !failed.Valid(), "stored task exceptions must propagate and consume the future");
        check_rejected_access(failed, "invalid");
    }

    auto check_plain_moves() -> void {
        std::promise<int> promise;
        promise.set_value(21);
        PlainResult original{ promise.get_future() };
        PlainResult moved{ std::move(original) };
        check_rejected_access(original, "invalid");
        PlainResult assigned;
        assigned = std::move(moved);
        check_rejected_access(moved, "invalid");
        require(assigned.Get() == 21, "ordinary wrappers must transfer their future through move construction and assignment");
    }

    auto check_cancellation() -> void {
        auto invalid_state{ std::make_shared<std::atomic_bool>(false) };
        {
            CancellableResult invalid{ std::future<int>{}, std::shared_ptr{ invalid_state } };
            check_rejected_access(invalid, "invalid");
            require(!invalid.IsCancelled() && invalid.Cancel() == CancelResult::invalid_operation && !invalid_state->load(), "an invalid future cannot be explicitly cancelled");
        }
        require(invalid_state->load(), "destruction must signal even an originally invalid future, matching cpr");

        std::promise<int> null_promise;
        null_promise.set_value(10);
        auto null_future{ null_promise.get_future() };
        bool null_rejected{ false };
        try {
            CancellableResult invalid{ std::move(null_future), std::shared_ptr<std::atomic_bool>{} };
        } catch (std::invalid_argument const&) {
            null_rejected = true;
        }
        require(null_rejected && null_future.valid() && null_future.get() == 10, "a null cancellation flag must be rejected without consuming the source future");

        auto              state{ std::make_shared<std::atomic_bool>(false) };
        auto              transferred_state{ state };
        std::promise<int> promise;
        auto              future{ promise.get_future() };
        CancellableResult wrapper{ std::move(future), std::move(transferred_state) };
        require(!future.valid() && !transferred_state && wrapper.Valid() && !wrapper.IsCancelled(), "cancellable construction must take both handles without setting the flag");
        int                       progress_calls{ 0 };
        mcr::ProgressCallback     progress{ [&](auto, auto, auto, auto, auto) { ++progress_calls; return true; } };
        mcr::CancellationCallback observer{ std::shared_ptr{ state }, progress };
        require(observer(0, 0, 0, 0) && progress_calls == 1, "the existing cancellation callback must observe the wrapper's shared flag");
        require(wrapper.Cancel() == CancelResult::success && state->load() && wrapper.IsCancelled(), "the first cancellation must signal shared state");
        require(wrapper.Cancel() == CancelResult::invalid_operation && !observer(0, 0, 0, 0) && progress_calls == 1, "repeated cancellation must fail and suppress the progress observer");
        check_rejected_access(wrapper, "cancelled");
        promise.set_value(42);
        auto shared_after_cancel{ wrapper.Share() };
        require(shared_after_cancel.get() == 42 && wrapper.Cancel() == CancelResult::invalid_operation, "Share must retain cpr's ability to transfer an underlying future after cancellation");

        auto              preset{ std::make_shared<std::atomic_bool>(true) };
        std::promise<int> preset_promise;
        preset_promise.set_value(1);
        CancellableResult already_cancelled{ preset_promise.get_future(), std::move(preset) };
        check_rejected_access(already_cancelled, "cancelled");
        require(already_cancelled.Cancel() == CancelResult::invalid_operation, "an initially set flag must be treated as already cancelled");

        auto              external_state{ std::make_shared<std::atomic_bool>(false) };
        std::promise<int> external_promise;
        external_promise.set_value(2);
        CancellableResult external{ external_promise.get_future(), std::shared_ptr{ external_state } };
        std::jthread      cancelling_thread{ [external_state] { external_state->store(true); } };
        cancelling_thread.join();
        check_rejected_access(external, "cancelled");

        auto              ready_state{ std::make_shared<std::atomic_bool>(false) };
        std::promise<int> ready_promise;
        ready_promise.set_value(3);
        CancellableResult ready{ ready_promise.get_future(), std::shared_ptr{ ready_state } };
        require(ready.WaitFor(0ms) == std::future_status::ready && ready.Cancel() == CancelResult::success, "a ready but unconsumed future must still accept cancellation");

        auto consumed_state{ std::make_shared<std::atomic_bool>(false) };
        {
            std::promise<int> consumed_promise;
            consumed_promise.set_value(4);
            CancellableResult consumed{ consumed_promise.get_future(), std::shared_ptr{ consumed_state } };
            require(consumed.Get() == 4 && !consumed.Valid() && !consumed.IsCancelled(), "successful Get must consume the future without cancelling");
            require(consumed.Cancel() == CancelResult::invalid_operation && !consumed_state->load(), "Cancel must not change the flag after Get consumed the future");
        }
        require(consumed_state->load(), "destruction must signal cancellation even after successful Get");

        auto                    shared_state{ std::make_shared<std::atomic_bool>(false) };
        std::shared_future<int> shared;
        std::promise<int>       shared_promise;
        {
            CancellableResult shareable{ shared_promise.get_future(), std::shared_ptr{ shared_state } };
            shared = shareable.Share();
            require(!shareable.Valid() && shareable.Cancel() == CancelResult::invalid_operation && !shared_state->load(), "Share must invalidate cancellation through the wrapper without setting its flag");
        }
        require(shared_state->load(), "destruction after Share must still signal the retained cancellation flag");
        shared_promise.set_value(5);
        require(shared.get() == 5, "a shared future must remain usable after wrapper destruction");
    }

    auto check_cancellable_moves() -> void {
        auto              state{ std::make_shared<std::atomic_bool>(false) };
        std::promise<int> promise;
        promise.set_value(31);
        std::optional<CancellableResult> destination;
        {
            CancellableResult source{ promise.get_future(), std::shared_ptr{ state } };
            destination.emplace(std::move(source));
            check_rejected_access(source, "invalid");
            require(!source.IsCancelled() && source.Cancel() == CancelResult::invalid_operation && !source.Share().valid(), "moved-from cancellable wrappers must be safe to inspect and reject cancellation");
        }
        require(!state->load() && destination->Valid(), "destroying a moved-from wrapper must not cancel the destination");
        auto& same{ *destination };
        same = std::move(*destination);
        require(!state->load() && destination->Get() == 31, "self-move must preserve ownership without cancelling");
        destination.reset();
        require(state->load(), "only the owning wrapper's destructor must signal the flag");

        auto              first_state{ std::make_shared<std::atomic_bool>(false) };
        auto              second_state{ std::make_shared<std::atomic_bool>(false) };
        std::promise<int> first_promise;
        std::promise<int> second_promise;
        first_promise.set_value(1);
        second_promise.set_value(2);
        CancellableResult first{ first_promise.get_future(), std::shared_ptr{ first_state } };
        CancellableResult second{ second_promise.get_future(), std::shared_ptr{ second_state } };
        first = std::move(second);
        require(first_state->load() && !second_state->load() && first.Valid(), "move assignment must cancel the replaced operation and preserve the incoming flag");
        check_rejected_access(second, "invalid");
        second = std::move(first);
        require(!second_state->load() && second.Get() == 2, "a moved-from wrapper must be reusable through move assignment");
    }

    auto check_concurrent_cancellation() -> void {
        auto              state{ std::make_shared<std::atomic_bool>(false) };
        std::promise<int> promise;
        std::promise<int> peer_promise;
        promise.set_value(1);
        peer_promise.set_value(2);
        CancellableResult           wrapper{ promise.get_future(), std::shared_ptr{ state } };
        CancellableResult           peer{ peer_promise.get_future(), std::shared_ptr{ state } };
        std::array<CancelResult, 8> results{};
        std::barrier                start{ static_cast<std::ptrdiff_t>(results.size() + 1) };
        std::vector<std::jthread>   threads;
        for (std::size_t index{ 0 }; index < results.size(); ++index) {
            threads.emplace_back([&, index] {
                start.arrive_and_wait();
                results[index] = index % 2 ? wrapper.Cancel() : peer.Cancel();
            });
        }
        start.arrive_and_wait();
        threads.clear();
        require(std::ranges::count(results, CancelResult::success) == 1 && std::ranges::count(results, CancelResult::invalid_operation) == 7 && wrapper.IsCancelled() && peer.IsCancelled(), "concurrent cancellation of a shared flag must report exactly one successful transition");
    }

    auto cooperative_future(std::shared_ptr<std::atomic_bool> state, std::promise<void>& started, std::atomic_bool& observed) -> std::future<int> {
        return std::async(std::launch::async, [state = std::move(state), &started, &observed] {
            mcr::CancellationCallback callback{ std::shared_ptr{ state } };
            auto const                deadline{ std::chrono::steady_clock::now() + 3s };
            started.set_value();
            while (callback(0, 0, 0, 0) && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            observed.store(state->load());
            return 1;
        });
    }

    auto check_running_task_lifetime() -> void {
        auto               destruction_state{ std::make_shared<std::atomic_bool>(false) };
        std::promise<void> destruction_started;
        auto               destruction_entered{ destruction_started.get_future() };
        std::atomic_bool   destruction_observed{ false };
        {
            CancellableResult wrapper{ cooperative_future(destruction_state, destruction_started, destruction_observed), std::shared_ptr{ destruction_state } };
            destruction_entered.wait();
        }
        require(destruction_state->load() && destruction_observed.load(), "destruction must signal before std::future releases and joins a running cooperative task");

        auto               old_state{ std::make_shared<std::atomic_bool>(false) };
        auto               incoming_state{ std::make_shared<std::atomic_bool>(false) };
        std::promise<void> replacement_started;
        auto               replacement_entered{ replacement_started.get_future() };
        std::atomic_bool   replacement_observed{ false };
        CancellableResult  replacement{ cooperative_future(old_state, replacement_started, replacement_observed), std::shared_ptr{ old_state } };
        replacement_entered.wait();
        std::promise<int> incoming_promise;
        incoming_promise.set_value(77);
        CancellableResult incoming{ incoming_promise.get_future(), std::shared_ptr{ incoming_state } };
        replacement = std::move(incoming);
        require(old_state->load() && replacement_observed.load() && !incoming_state->load() && replacement.Get() == 77, "move assignment must cancel and finish the old task before releasing its future while preserving the incoming result");
    }

    auto check_cancellation_during_wait() -> void {
        auto               state{ std::make_shared<std::atomic_bool>(false) };
        std::promise<void> task_entered;
        auto               entered{ task_entered.get_future() };
        std::promise<void> release_task;
        auto               release{ release_task.get_future() };
        CancellableResult  wrapper{ std::async(std::launch::deferred, [&] {
                                       task_entered.set_value();
                                       release.wait();
                                       return 6;
                                   }),
                                   std::shared_ptr{ state } };
        auto waiter{ std::async(std::launch::async, [&] { wrapper.Wait(); }) };
        entered.wait();
        auto const cancelled{ wrapper.Cancel() };
        bool const still_waiting{ waiter.wait_for(0ms) == std::future_status::timeout };
        release_task.set_value();
        waiter.get();
        require(cancelled == CancelResult::success && still_waiting, "cancellation must not interrupt a wait that already passed its initial cancellation check");
        check_rejected_access(wrapper, "cancelled");
        require(wrapper.Share().get() == 6, "late cancellation must leave the completed underlying result available through Share");
    }

    auto check_thread_pool_results() -> void {
        mcr::utils::ThreadPool pool{ 1, 1 };
        auto                   result{ mcr::utils::AsyncWrapper{ pool.Submit([] { return 84; }) } };
        require(result.Get() == 84, "wrappers must accept futures returned by the existing thread pool");
        (void)pool.Pause();
        auto cancelled{ mcr::utils::AsyncWrapper{ pool.Submit([] { return 0; }) } };
        (void)pool.Stop();
        bool propagated{ false };
        try {
            (void)cancelled.Get();
        } catch (std::future_error const& error) {
            propagated = error.code() == std::make_error_code(std::future_errc::broken_promise);
        }
        require(propagated && !cancelled.Valid(), "broken-promise errors from cancelled pool tasks must propagate and consume the result");
    }

} // namespace

int main() {
    try {
        check_future_operations<false>();
        check_future_operations<true>();
        check_result_types_and_exceptions<false>();
        check_result_types_and_exceptions<true>();
        check_plain_moves();
        check_cancellation();
        check_cancellable_moves();
        check_concurrent_cancellation();
        check_running_task_lifetime();
        check_cancellation_during_wait();
        check_thread_pool_results();
    } catch (std::exception const& error) {
        std::println("test_async_wrapper: {}", error.what());
        return 1;
    }
    std::println("test_async_wrapper: ok");
    return 0;
}
