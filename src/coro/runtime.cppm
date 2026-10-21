/**
 * @file runtime.cppm
 * @brief Curl multi transfers and queued task execution on a separate continuation thread.
 */
module;
#include <curl/curl.h>

export module mcr:coro_runtime;

import :task;
import :session;
import :curlholder;
import :curlmultiholder;
import std;

export namespace mcr::detail {
    class CoroRuntime;

    /**
     * @brief Wake the runtime when a task's stop token is signalled.
     */
    struct WakeCoroRuntime {
        CoroRuntime* runtime;
        auto operator()() const noexcept -> void;
    };

    /**
     * @brief Link accepted work in the runtime's serialized continuation queue.
     */
    struct CoroWork {
        std::shared_ptr<CoroWork> next; ///< Intrusive queue link, allocated before publication.

        /**
         * @brief Release the concrete work item through its queue owner.
         */
        virtual ~CoroWork() = default;

        /**
         * @brief Resume outside runtime locks on the continuation thread.
         */
        virtual auto Run() noexcept -> void = 0;
    };

    /**
     * @brief Schedule a continuation and optionally own its unstarted frame.
     */
    struct CoroResume : CoroWork {
        std::coroutine_handle<> continuation{}; ///< Frame to resume exactly once.
        bool owns_frame{ false };               ///< Destroy an unstarted frame if posting fails.

        /**
         * @brief Construct empty work before installing a continuation.
         */
        CoroResume() = default;
        CoroResume(CoroResume const&) = delete;
        auto operator=(CoroResume const&) -> CoroResume& = delete;

        /**
         * @brief Release a cold task if the runtime did not accept ownership.
         */
        ~CoroResume() override {
            if (owns_frame && continuation) {
                continuation.destroy();
            }
        }

        /**
         * @brief Transfer execution to the task before it can destroy its frame.
         */
        auto Run() noexcept -> void override {
            auto handle = std::exchange(continuation, {});
            handle.resume();
        }
    };

    /**
     * @brief Own one transfer until its awaiting coroutine has resumed.
     */
    struct CoroTransfer : CoroWork {
        std::shared_ptr<Session> session;
        std::function<Result<void>(Session&)> prepare;
        std::stop_token stop;
        std::optional<std::stop_callback<WakeCoroRuntime>> wake;
        std::coroutine_handle<> continuation;
        CURLcode result{ CURLE_OK };
        std::exception_ptr error;
        Result<void> setup_result;                  ///< Configuration failure returned before publication.
        CURLMcode multi_error{ CURLM_OK };          ///< Multi failure recorded without allocating diagnostics on the I/O thread.
        bool prepared{ false };
        std::shared_ptr<CoroTransfer> pending_next; ///< Intrusive link used before submission to curl.

        /**
         * @brief Own a session, preparation operation, and cancellation token before publication.
         */
        CoroTransfer(
            std::shared_ptr<Session> owned_session,
            std::function<Result<void>(Session&)> prepare_request,
            std::stop_token token
        ) : session{ std::move(owned_session) }, prepare{ std::move(prepare_request) }, stop{ token } {}

        /**
         * @brief Disconnect cancellation before resuming outside curl's callback stack.
         */
        auto Run() noexcept -> void override {
            wake.reset();
            auto handle = std::exchange(continuation, {});
            handle.resume();
        }
    };

    /**
     * @brief Serialize curl operations on one I/O thread and resume tasks on a second thread.
     * @note Submission, cancellation, and shutdown synchronize the multi handle's lifetime.
     * Internal transfer entry points accept only exclusively owned, freshly configured sessions.
     */
    class CoroRuntime {
        std::mutex m_mutex;
        std::mutex m_cleanup_mutex;
        std::condition_variable m_completion_ready;
        std::condition_variable m_io_ready; ///< Sleep without polling while no transfers are active.
        std::unique_ptr<curl::CurlMultiHolder> m_multi;
        std::shared_ptr<CoroTransfer> m_pending_head;
        std::shared_ptr<CoroTransfer> m_pending_tail;
        std::shared_ptr<CoroWork> m_completed_head;
        std::shared_ptr<CoroWork> m_completed_tail;
        Result<void> m_initialization; ///< Curl initialization result, retained for later submissions.
        bool m_stopping{ false };
        bool m_io_done{ false };
        inline static std::atomic_bool s_created{ false }; ///< Whether the lazily created runtime has been constructed.
        std::jthread m_io_thread;
        std::jthread m_completion_thread;

    public:
        /**
         * @brief Create the runtime once, on first use.
         */
        static auto Instance() -> CoroRuntime& {
            /**
             * @brief Join at normal exit, retaining live state if exit begins on a runtime thread.
             */
            struct Owner {
                CoroRuntime* runtime{ new CoroRuntime }; ///< Remains a static root during runtime-thread process exit.

                ~Owner() {
                    if (is_coro_runtime_thread()) {
                        runtime->requestStop();
                    } else {
                        delete runtime;
                    }
                }
            };

            static Owner owner;
            return *owner.runtime;
        }

        /**
         * @brief Check whether Instance() has constructed the runtime without creating it.
         * @return True once construction has started, including after cleanup.
         */
        [[nodiscard]] static auto Created() noexcept -> bool { return s_created.load(std::memory_order_acquire); }

        CoroRuntime(CoroRuntime const&) = delete;
        auto operator=(CoroRuntime const&) -> CoroRuntime& = delete;

        /**
         * @brief Destroy the runtime at exit, normally after Cleanup() has already run.
         * @note The singleton owner deletes only outside runtime threads. Process exit on a runtime
         * thread retains the live runtime until termination instead of joining or freeing its own state.
         */
        ~CoroRuntime() {
            Teardown();
        }

        /**
         * @brief Reject startup after permanent cleanup.
         * @return Success or the first operation error.
         */
        auto CheckRunning() -> Result<void> {
            std::lock_guard lock{ m_mutex };
            if (m_stopping) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::Coro: runtime has been cleaned up." }
                };
            }
            return m_initialization;
        }

        /**
         * @brief Publish a transfer; no operation after publication may throw.
         * @param transfer Transfer whose continuation and cancellation callback are already installed.
         * @return Success or the first operation error.
         */
        auto Submit(std::shared_ptr<CoroTransfer> transfer) -> Result<void> {
            std::lock_guard lock{ m_mutex };
            if (m_stopping) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::Coro: runtime has been cleaned up." }
                };
            }
            if (!m_initialization) {
                return m_initialization;
            }
            if (m_pending_tail) {
                m_pending_tail->pending_next = transfer;
            } else {
                m_pending_head = transfer;
            }
            m_pending_tail = std::move(transfer);
            m_io_ready.notify_one();
            (void)curl_multi_wakeup(m_multi->handle);
            return {};
        }

        /**
         * @brief Queue work without invoking it inline, synchronizing acceptance with shutdown.
         * @param work Work whose frame and ownership are installed before publication.
         * @return Success or an initialization/shutdown error; failed work is never queued.
         */
        auto Post(std::shared_ptr<CoroWork> work) -> Result<void> {
            {
                std::lock_guard lock{ m_mutex };
                if (m_stopping) {
                    return std::unexpected{
                        Error{ ErrorCode::FAILED_INIT, "mcr::Coro: runtime has been cleaned up." }
                    };
                }
                if (!m_initialization) {
                    return m_initialization;
                }
                append(m_completed_head, m_completed_tail, std::move(work));
            }
            m_completion_ready.notify_one();
            return {};
        }

        /**
         * @brief Wake polling without racing destruction of the multi handle.
         */
        auto Wake() noexcept -> void {
            std::lock_guard lock{ m_mutex };
            if (m_multi) {
                (void)curl_multi_wakeup(m_multi->handle);
            }
        }

        /**
         * @brief Cancel unfinished transfers, drain continuations, join threads, and release curl.
         * @note Repeated calls are harmless; later submissions fail. Call before curl_global_cleanup.
         * @return Success or the first operation error.
         */
        auto Cleanup() -> Result<void> {
            if (is_coro_runtime_thread()) {
                return std::unexpected{
                    Error{ ErrorCode::RECURSIVE_API_CALL, "mcr::Coro::Cleanup: must be called outside runtime threads." }
                };
            }
            Teardown();
            return {};
        }

    private:
        /**
         * @brief Stop accepting work and wake both idle waiting and active curl polling.
         */
        auto requestStop() noexcept -> void {
            {
                std::lock_guard lock{ m_mutex };
                m_stopping = true;
                if (m_multi) {
                    (void)curl_multi_wakeup(m_multi->handle);
                }
            }
            m_io_ready.notify_all();
        }

        /**
         * @brief Initialize curl and start the I/O and continuation workers.
         */
        CoroRuntime() {
            s_created.store(true, std::memory_order_release);
            auto holder = curl::CurlMultiHolder::Create();
            if (!holder) {
                m_initialization = std::unexpected{ std::move(holder.error()) };
                return;
            }
            m_multi = std::make_unique<curl::CurlMultiHolder>(std::move(*holder));
            try {
                m_completion_thread = std::jthread{ [this] { runCompletions(); } };
                m_io_thread = std::jthread{ [this] { runIo(); } };
            } catch (...) {
                {
                    std::lock_guard lock{ m_mutex };
                    m_io_done = true;
                }
                m_completion_ready.notify_all();
                if (m_completion_thread.joinable()) {
                    m_completion_thread.join();
                }
                throw;
            }
        }

        /**
         * @brief Cancel unfinished transfers, join threads, and release curl without reporting an outcome.
         * @note Shared by Cleanup and the destructor; callers must already be outside runtime threads.
         */
        auto Teardown() -> void {
            std::lock_guard cleanup_lock{ m_cleanup_mutex };
            requestStop();
            if (m_io_thread.joinable()) {
                m_io_thread.join();
            }
            if (m_completion_thread.joinable()) {
                m_completion_thread.join();
            }
            std::lock_guard lock{ m_mutex };
            m_multi.reset();
        }

        /**
         * @brief Append without allocating; caller owns the relevant queue lock.
         */
        static auto append(
            std::shared_ptr<CoroWork>& head,
            std::shared_ptr<CoroWork>& tail,
            std::shared_ptr<CoroWork> value
        ) noexcept -> void {
            if (tail) {
                tail->next = value;
            } else {
                head = value;
            }
            tail = std::move(value);
        }

        /**
         * @brief Publish an outcome only after removing the easy handle from the multi handle.
         */
        auto complete(
            std::shared_ptr<CoroTransfer> transfer,
            CURLcode result,
            std::exception_ptr error = {},
            CURLMcode multi_error = CURLM_OK
        ) noexcept -> void {
            transfer->result = result;
            transfer->error = std::move(error);
            transfer->multi_error = multi_error;
            {
                std::lock_guard lock{ m_mutex };
                append(m_completed_head, m_completed_tail, std::move(transfer));
            }
            m_completion_ready.notify_one();
        }

        /**
         * @brief Resume outside all runtime locks and outside curl's callback stack.
         */
        auto runCompletions() noexcept -> void {
            coro_runtime_thread = true;
            for (;;) {
                std::shared_ptr<CoroWork> work;
                {
                    std::unique_lock lock{ m_mutex };
                    m_completion_ready.wait(lock, [this] { return m_completed_head || m_io_done; });
                    if (!m_completed_head) {
                        break;
                    }
                    work = std::move(m_completed_head);
                    m_completed_head = std::move(work->next);
                    if (!m_completed_head) {
                        m_completed_tail.reset();
                    }
                }
                work->Run();
            }
            coro_runtime_thread = false;
        }

        /**
         * @brief Drive all active HTTP transfers and collect each completion independently.
         */
        auto runIo() noexcept -> void {
            coro_runtime_thread = true;
            std::unordered_map<CURL*, std::shared_ptr<CoroTransfer>> active;
            std::exception_ptr failure;
            CURLMcode multi_error{ CURLM_OK };
            try {
                for (;;) {
                    std::shared_ptr<CoroTransfer> pending;
                    {
                        std::unique_lock lock{ m_mutex };
                        if (active.empty()) {
                            m_io_ready.wait(lock, [this] { return m_stopping || m_pending_head; });
                        }
                        if (m_stopping) {
                            break;
                        }
                        pending = std::move(m_pending_head);
                        m_pending_tail.reset();
                    }
                    while (pending) {
                        auto transfer = std::move(pending);
                        pending = std::move(transfer->pending_next);
                        if (transfer->stop.stop_requested()) {
                            complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK);
                            continue;
                        }
                        auto* handle = transfer->session->GetCurlHolder()->handle;
                        bool inserted{};
                        try {
                            // Claim the handle before preparing it: a duplicate must not change an active transfer.
                            auto const entry = active.try_emplace(handle, transfer);
                            inserted = entry.second;
                            if (!inserted) {
                                complete(std::move(transfer), CURLE_FAILED_INIT, {}, CURLM_ADDED_ALREADY);
                                continue;
                            }
                            transfer->setup_result = transfer->prepare(*transfer->session);
                            if (!transfer->setup_result) {
                                active.erase(handle);
                                complete(std::move(transfer), CURLE_FAILED_INIT);
                                continue;
                            }
                            transfer->prepared = true;
                            auto const added = curl_multi_add_handle(m_multi->handle, handle);
                            if (added != CURLM_OK) {
                                active.erase(handle);
                                complete(std::move(transfer), CURLE_FAILED_INIT, {}, added);
                            }
                        } catch (...) {
                            if (inserted) {
                                active.erase(handle);
                            }
                            complete(std::move(transfer), CURLE_FAILED_INIT, std::current_exception());
                        }
                    }
                    for (auto it = active.begin(); it != active.end();) {
                        if (it->second->stop.stop_requested()) {
                            auto* handle = it->first;
                            auto transfer = std::move(it->second);
                            it = active.erase(it);
                            (void)curl_multi_remove_handle(m_multi->handle, handle);
                            complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK);
                        } else {
                            ++it;
                        }
                    }
                    int running{};
                    multi_error = curl_multi_perform(m_multi->handle, &running);
                    if (multi_error != CURLM_OK) {
                        break;
                    }
                    int queued{};
                    while (auto* message = curl_multi_info_read(m_multi->handle, &queued)) {
                        if (message->msg == CURLMSG_DONE) {
                            auto* handle = message->easy_handle;
                            auto result = message->data.result;
                            auto node = active.extract(handle);
                            if (!node.empty()) {
                                (void)curl_multi_remove_handle(m_multi->handle, handle);
                                complete(std::move(node.mapped()), result);
                            }
                        }
                    }
                    if (active.empty()) {
                        continue;
                    }
                    multi_error = curl_multi_poll(m_multi->handle, nullptr, 0, 100, nullptr);
                    if (multi_error != CURLM_OK) {
                        break;
                    }
                }
            } catch (...) {
                failure = std::current_exception();
            }
            std::shared_ptr<CoroTransfer> pending;
            {
                std::lock_guard lock{ m_mutex };
                m_stopping = true;
                pending = std::move(m_pending_head);
                m_pending_tail.reset();
            }
            for (auto& [handle, transfer] : active) {
                (void)curl_multi_remove_handle(m_multi->handle, handle);
                complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK, failure, multi_error);
            }
            while (pending) {
                auto transfer = std::move(pending);
                pending = std::move(transfer->pending_next);
                complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK, failure, multi_error);
            }
            {
                std::lock_guard lock{ m_mutex };
                m_io_done = true;
            }
            m_completion_ready.notify_all();
            coro_runtime_thread = false;
        }
    };

    auto WakeCoroRuntime::operator()() const noexcept -> void {
        runtime->Wake();
    }

    /**
     * @brief Resume an existing coroutine through the runtime's serialized queue.
     */
    class CoroScheduleAwaiter {
        std::shared_ptr<CoroResume> m_work{ std::make_shared<CoroResume>() }; ///< Published continuation storage.
        Result<void> m_status;                                                ///< Rejection returned without suspending or transferring execution.

    public:
        /**
         * @brief Prepare a single-use queued resumption before its coroutine suspends.
         */
        CoroScheduleAwaiter() = default;

        CoroScheduleAwaiter(CoroScheduleAwaiter const&) = delete;
        CoroScheduleAwaiter(CoroScheduleAwaiter&&) = default;

        /**
         * @brief Validate ownership and enqueue even when already on the continuation thread.
         * @throws std::logic_error If the awaiter was moved or already consumed.
         */
        auto await_ready() const -> bool {
            if (!m_work) {
                throw std::logic_error{ "mcr::Coro::Schedule: awaiter has been consumed." };
            }
            return false;
        }

        /**
         * @brief Publish only fully initialized state and avoid accessing this after acceptance.
         * @param continuation Already suspended coroutine to resume.
         * @return Whether the runtime accepted responsibility for resumption.
         * @note [expr.await] suspends before this call. Acceptance may destroy the awaiter before return.
         */
        auto await_suspend(std::coroutine_handle<> continuation) -> bool {
            auto work = std::move(m_work);
            work->continuation = continuation;
            auto status = CoroRuntime::Instance().Post(std::move(work));
            if (!status) {
                m_status = std::move(status);
                return false;
            }
            // Only locals may be accessed after acceptance, including during their destruction.
            return true;
        }

        /**
         * @brief Report success on the continuation thread or rejection on the calling thread.
         */
        auto await_resume() -> Result<void> { return std::move(m_status); }
    };

    /**
     * @brief Await one exclusively owned session through the shared curl multi runtime.
     */
    class CoroTransferAwaiter {
        std::shared_ptr<CoroTransfer> m_transfer;

    public:
        /**
         * @brief Own all transfer state before publishing a continuation to the runtime.
         * @param session Session exclusively used by this transfer.
         * @param prepare Request preparation performed on the I/O thread.
         * @param token Cooperative cancellation token.
         */
        CoroTransferAwaiter(
            std::shared_ptr<Session> session,
            std::function<Result<void>(Session&)> prepare,
            std::stop_token token
        )
            : m_transfer{ std::make_shared<CoroTransfer>(std::move(session), std::move(prepare), token) } {}

        /**
         * @brief Always suspend to serialize curl access through the runtime.
         */
        auto await_ready() const noexcept -> bool { return false; }

        /**
         * @brief Publish fully initialized state without accessing the awaiter after acceptance.
         * @param continuation Already suspended coroutine to resume.
         * @return Whether the runtime accepted responsibility for resumption.
         * @note The local shared owner survives even if completion destroys this awaiter before return.
         */
        auto await_suspend(std::coroutine_handle<> continuation) -> bool {
            auto transfer = m_transfer;
            auto& runtime = CoroRuntime::Instance();
            transfer->continuation = continuation;
            transfer->wake.emplace(transfer->stop, WakeCoroRuntime{ &runtime });
            auto status = runtime.Submit(transfer);
            if (!status) {
                transfer->setup_result = std::move(status);
                return false;
            }
            return true;
        }

        /**
         * @brief Consume the transfer outcome on the continuation thread.
         * @return Response or setup error.
         * @throws Any exception captured while preparing the transfer or running user callbacks.
         */
        auto await_resume() -> Result<Response> {
            if (m_transfer->error) {
                std::rethrow_exception(m_transfer->error);
            }
            if (!m_transfer->setup_result) {
                return std::unexpected{ std::move(m_transfer->setup_result.error()) };
            }
            if (m_transfer->multi_error != CURLM_OK) {
                return std::unexpected{
                    Error{ error_code_from_curl_multi(m_transfer->multi_error), std::string{ "mcr::Coro: " } + curl_multi_strerror(m_transfer->multi_error) }
                };
            }
            if (!m_transfer->prepared) {
                Response response;
                response.error = Error{ static_cast<std::int32_t>(m_transfer->result), "Coroutine request cancelled before transfer." };
                return response;
            }
            return m_transfer->session->Complete(m_transfer->result);
        }
    };
} // namespace mcr::detail

export namespace mcr {
    /**
     * @brief Manage the coroutine runtime independently of the future-based Async runtime.
     */
    class Coro {
    public:
        /**
         * @brief Check whether blocking waits and cleanup are forbidden on this thread.
         * @return True on the runtime's I/O and continuation threads.
         */
        [[nodiscard]] static auto IsRuntimeThread() noexcept -> bool { return detail::is_coro_runtime_thread(); }

        /**
         * @brief Start a cold task on the continuation thread and retain its result and cancellation.
         * @tparam T Value, lvalue reference, or void result type.
         * @param task Task consumed by this call; started tasks keep their existing execution.
         * @return Started task or an initialization/shutdown error before starting a cold task.
         * @throws std::logic_error If the task is empty or has already been consumed.
         * @note Accepted work runs outside runtime locks. External awaiters may change subsequent threads.
         */
        template <typename T>
        [[nodiscard]] static auto Spawn(Task<T> task) -> Result<Task<T>> {
            if (!task.m_state) {
                throw std::logic_error{ "mcr::Coro::Spawn: task is empty." };
            }
            if (task.m_coroutine) {
                auto work = std::make_shared<detail::CoroResume>();
                work->continuation = std::exchange(task.m_coroutine, {});
                work->owns_frame = true;
                if (auto status = detail::CoroRuntime::Instance().Post(std::move(work)); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
            return std::move(task);
        }

        /**
         * @brief Yield to the continuation thread using a queued resumption.
         * @return Awaiter yielding Result<void>; rejection continues on the calling thread.
         * @note The waiting coroutine must remain alive until the queued resumption runs.
         */
        [[nodiscard]] static auto Schedule() -> detail::CoroScheduleAwaiter { return {}; }

        /**
         * @brief Start lazily and return FAILED_INIT after permanent cleanup.
         * @return Success or the first operation error.
         */
        [[nodiscard]] static auto Startup() -> Result<void> { return detail::CoroRuntime::Instance().CheckRunning(); }

        /**
         * @brief Cancel outstanding HTTP transfers and wait for their continuations to finish.
         * @note Call outside tasks before curl_global_cleanup; this permanently closes the runtime.
         * @return Success or the first operation error.
         */
        [[nodiscard]] static auto Cleanup() -> Result<void> { return detail::CoroRuntime::Instance().Cleanup(); }
    };
} // namespace mcr
