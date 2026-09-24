/**
 * @file runtime.cppm
 * @brief Curl multi transfers with a separate coroutine continuation thread.
 */
module;
#include <curl/curl.h>

export module mcr.coro_runtime;

import mcr.task;
import mcr.session;
import mcr.curlholder;
import mcr.curlmultiholder;
import std;

export namespace mcr::detail {
    class CoroRuntime;

    /**
     * @brief Wake the runtime when a task's stop token is signalled.
     */
    struct WakeCoroRuntime {
        CoroRuntime* runtime;
        auto         operator()() const noexcept -> void;
    };

    /**
     * @brief Own one transfer until its awaiting coroutine has resumed.
     */
    struct CoroTransfer {
        std::shared_ptr<Session>                           session;
        std::function<Result<void>(Session&)>              prepare;
        std::stop_token                                    stop;
        std::optional<std::stop_callback<WakeCoroRuntime>> wake;
        std::coroutine_handle<>                            continuation;
        CURLcode                                           result{ CURLE_OK };
        std::exception_ptr                                 error;
        Result<void>                                       setup_result;            ///< Configuration failure returned before publication.
        CURLMcode                                          multi_error{ CURLM_OK }; ///< Multi failure recorded without allocating diagnostics on the I/O thread.
        bool                                               prepared{ false };
        std::shared_ptr<CoroTransfer>                      next;                    ///< Intrusive pending/completion queue link; no allocation at completion.

        CoroTransfer(std::shared_ptr<Session> owned_session, std::function<Result<void>(Session&)> prepare_request, std::stop_token token)
            : session{ std::move(owned_session) }, prepare{ std::move(prepare_request) }, stop{ token } {}
    };

    /**
     * @brief Serialize curl operations on one I/O thread and resume tasks on a second thread.
     * @note Submission, cancellation, and shutdown synchronize the multi handle's lifetime.
     * Internal transfer entry points accept only exclusively owned, freshly configured sessions.
     */
    class CoroRuntime {
        std::mutex                             m_mutex;
        std::mutex                             m_cleanup_mutex;
        std::condition_variable                m_completion_ready;
        std::unique_ptr<curl::CurlMultiHolder> m_multi;
        std::shared_ptr<CoroTransfer>          m_pending_head;
        std::shared_ptr<CoroTransfer>          m_pending_tail;
        std::shared_ptr<CoroTransfer>          m_completed_head;
        std::shared_ptr<CoroTransfer>          m_completed_tail;
        Result<void>                           m_initialization; ///< Curl initialization result, retained for later submissions.
        bool                                   m_stopping{ false };
        bool                                   m_io_done{ false };
        std::jthread                           m_io_thread;
        std::jthread                           m_completion_thread;

    public:
        /**
         * @brief Create the runtime once, on first use.
         */
        static auto Instance() -> CoroRuntime& {
            static CoroRuntime runtime;
            return runtime;
        }

        CoroRuntime(CoroRuntime const&)                    = delete;
        auto operator=(CoroRuntime const&) -> CoroRuntime& = delete;

        /**
         * @brief Destroy the runtime at exit, normally after Cleanup() has already run.
         * @note Teardown is unconditional here because a destructor cannot report the recursive-call error.
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
                    Error{ ErrorCode::FAILED_INIT, "mcr::GetCoro: runtime has been cleaned up." }
                };
            }
            if (!m_initialization) {
                return m_initialization;
            }
            append(m_pending_head, m_pending_tail, std::move(transfer));
            (void)curl_multi_wakeup(m_multi->handle);
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
            if (coro_runtime_thread) {
                return std::unexpected{
                    Error{ ErrorCode::RECURSIVE_API_CALL, "mcr::Coro::Cleanup: must be called outside runtime threads." }
                };
            }
            Teardown();
            return {};
        }

    private:
        CoroRuntime() {
            auto holder = curl::CurlMultiHolder::Create();
            if (!holder) {
                m_initialization = std::unexpected{ std::move(holder.error()) };
                return;
            }
            m_multi = std::make_unique<curl::CurlMultiHolder>(std::move(*holder));
            try {
                m_completion_thread = std::jthread{ [this] { runCompletions(); } };
                m_io_thread         = std::jthread{ [this] { runIo(); } };
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
            {
                std::lock_guard lock{ m_mutex };
                m_stopping = true;
                if (m_multi) {
                    (void)curl_multi_wakeup(m_multi->handle);
                }
            }
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
            std::shared_ptr<CoroTransfer>& head,
            std::shared_ptr<CoroTransfer>& tail,
            std::shared_ptr<CoroTransfer>  value
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
            CURLcode                      result,
            std::exception_ptr            error       = {},
            CURLMcode                     multi_error = CURLM_OK
        ) noexcept -> void {
            transfer->result      = result;
            transfer->error       = std::move(error);
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
                std::shared_ptr<CoroTransfer> transfer;
                {
                    std::unique_lock lock{ m_mutex };
                    m_completion_ready.wait(lock, [this] { return m_completed_head || m_io_done; });
                    if (!m_completed_head) {
                        break;
                    }
                    transfer         = std::move(m_completed_head);
                    m_completed_head = std::move(transfer->next);
                    if (!m_completed_head) {
                        m_completed_tail.reset();
                    }
                }
                transfer->wake.reset();
                transfer->continuation.resume();
            }
            coro_runtime_thread = false;
        }

        /**
         * @brief Drive all active HTTP transfers and collect each completion independently.
         */
        auto runIo() noexcept -> void {
            coro_runtime_thread = true;
            std::unordered_map<CURL*, std::shared_ptr<CoroTransfer>> active;
            std::exception_ptr                                       failure;
            CURLMcode                                                multi_error{ CURLM_OK };
            try {
                for (;;) {
                    std::shared_ptr<CoroTransfer> pending;
                    {
                        std::lock_guard lock{ m_mutex };
                        if (m_stopping) {
                            break;
                        }
                        pending = std::move(m_pending_head);
                        m_pending_tail.reset();
                    }
                    while (pending) {
                        auto transfer = std::move(pending);
                        pending       = std::move(transfer->next);
                        if (transfer->stop.stop_requested()) {
                            complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK);
                            continue;
                        }
                        auto* handle = transfer->session->GetCurlHolder()->handle;
                        try {
                            transfer->setup_result = transfer->prepare(*transfer->session);
                            if (!transfer->setup_result) {
                                complete(std::move(transfer), CURLE_FAILED_INIT);
                                continue;
                            }
                            transfer->prepared = true;
                            active.emplace(handle, transfer);
                            auto const added = curl_multi_add_handle(m_multi->handle, handle);
                            if (added != CURLM_OK) {
                                active.erase(handle);
                                complete(std::move(transfer), CURLE_FAILED_INIT, {}, added);
                            }
                        } catch (...) {
                            active.erase(handle);
                            complete(std::move(transfer), CURLE_FAILED_INIT, std::current_exception());
                        }
                    }
                    for (auto it = active.begin(); it != active.end();) {
                        if (it->second->stop.stop_requested()) {
                            auto* handle   = it->first;
                            auto  transfer = std::move(it->second);
                            it             = active.erase(it);
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
                            auto  result = message->data.result;
                            auto  node   = active.extract(handle);
                            if (!node.empty()) {
                                (void)curl_multi_remove_handle(m_multi->handle, handle);
                                complete(std::move(node.mapped()), result);
                            }
                        }
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
                pending    = std::move(m_pending_head);
                m_pending_tail.reset();
            }
            for (auto& [handle, transfer] : active) {
                (void)curl_multi_remove_handle(m_multi->handle, handle);
                complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK, failure, multi_error);
            }
            while (pending) {
                auto transfer = std::move(pending);
                pending       = std::move(transfer->next);
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
     * @brief Await one exclusively owned session through the shared curl multi runtime.
     */
    class CoroTransferAwaiter {
        std::shared_ptr<CoroTransfer> m_transfer;

    public:
        CoroTransferAwaiter(std::shared_ptr<Session> session, std::function<Result<void>(Session&)> prepare, std::stop_token token)
            : m_transfer{ std::make_shared<CoroTransfer>(std::move(session), std::move(prepare), token) } {}

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> continuation) {
            auto  transfer         = m_transfer;
            auto& runtime          = CoroRuntime::Instance();
            transfer->continuation = continuation;
            transfer->wake.emplace(transfer->stop, WakeCoroRuntime{ &runtime });
            auto status = runtime.Submit(transfer);
            if (!status) {
                transfer->setup_result = std::move(status);
                return false;
            }
            return true;
        }

        auto await_resume() -> Result<Response> {
            if (m_transfer->error) {
                std::rethrow_exception(m_transfer->error);
            }
            if (!m_transfer->setup_result) {
                return std::unexpected{ std::move(m_transfer->setup_result.error()) };
            }
            if (m_transfer->multi_error != CURLM_OK) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, std::string{ "mcr::Coro: " } + curl_multi_strerror(m_transfer->multi_error) }
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
