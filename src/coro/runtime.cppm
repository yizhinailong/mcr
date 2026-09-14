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
        void         operator()() const noexcept;
    };

    /**
     * @brief Own one transfer until its awaiting coroutine has resumed.
     */
    struct CoroTransfer {
        std::shared_ptr<Session>                           session;
        std::function<void(Session&)>                      prepare;
        std::stop_token                                    stop;
        std::optional<std::stop_callback<WakeCoroRuntime>> wake;
        std::coroutine_handle<>                            continuation;
        CURLcode                                           result{ CURLE_OK };
        std::exception_ptr                                 error;
        bool                                               prepared{ false };
        std::shared_ptr<CoroTransfer>                      next; ///< Intrusive pending/completion queue link; no allocation at completion.

        CoroTransfer(std::shared_ptr<Session> owned_session, std::function<void(Session&)> prepare_request, std::stop_token token)
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
        std::unique_ptr<curl::CurlMultiHolder> m_multi{ std::make_unique<curl::CurlMultiHolder>() };
        std::shared_ptr<CoroTransfer>          m_pending_head;
        std::shared_ptr<CoroTransfer>          m_pending_tail;
        std::shared_ptr<CoroTransfer>          m_completed_head;
        std::shared_ptr<CoroTransfer>          m_completed_tail;
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

        ~CoroRuntime() { Cleanup(); }

        /**
         * @brief Reject startup after permanent cleanup.
         */
        void CheckRunning() {
            std::lock_guard lock{ m_mutex };
            if (m_stopping) {
                throw std::logic_error{ "mcr::Coro: runtime has been cleaned up." };
            }
        }

        /**
         * @brief Publish a transfer; no operation after publication may throw.
         * @param transfer Transfer whose continuation and cancellation callback are already installed.
         * @throws std::logic_error If shutdown has begun.
         */
        void Submit(std::shared_ptr<CoroTransfer> transfer) {
            std::lock_guard lock{ m_mutex };
            if (m_stopping) {
                throw std::logic_error{ "mcr::GetCoro: runtime has been cleaned up." };
            }
            append(m_pending_head, m_pending_tail, std::move(transfer));
            (void)curl_multi_wakeup(m_multi->handle);
        }

        /**
         * @brief Wake polling without racing destruction of the multi handle.
         */
        void Wake() noexcept {
            std::lock_guard lock{ m_mutex };
            if (m_multi) {
                (void)curl_multi_wakeup(m_multi->handle);
            }
        }

        /**
         * @brief Cancel unfinished transfers, drain continuations, join threads, and release curl.
         * @throws std::logic_error If invoked on an I/O or continuation thread.
         * @note Repeated calls are harmless; later submissions fail. Call before curl_global_cleanup.
         */
        void Cleanup() {
            if (coro_runtime_thread) {
                throw std::logic_error{ "mcr::Coro::Cleanup: must be called outside runtime threads." };
            }
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

    private:
        CoroRuntime() {
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
         * @brief Append without allocating; caller owns the relevant queue lock.
         */
        static void append(std::shared_ptr<CoroTransfer>& head, std::shared_ptr<CoroTransfer>& tail, std::shared_ptr<CoroTransfer> value) noexcept {
            if (tail) {
                tail->next = value;
            } else {
                head = value;
            }
            tail = std::move(value);
        }

        /**
         * @brief Turn a multi error into an exception propagated to all affected tasks.
         */
        static void checkMulti(CURLMcode result) {
            if (result != CURLM_OK) {
                throw std::runtime_error{ std::string{ "mcr::Coro: " } + curl_multi_strerror(result) };
            }
        }

        /**
         * @brief Publish an outcome only after removing the easy handle from the multi handle.
         */
        void complete(std::shared_ptr<CoroTransfer> transfer, CURLcode result, std::exception_ptr error = {}) noexcept {
            transfer->result = result;
            transfer->error  = std::move(error);
            {
                std::lock_guard lock{ m_mutex };
                append(m_completed_head, m_completed_tail, std::move(transfer));
            }
            m_completion_ready.notify_one();
        }

        /**
         * @brief Resume outside all runtime locks and outside curl's callback stack.
         */
        void runCompletions() noexcept {
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
        void runIo() noexcept {
            coro_runtime_thread = true;
            std::unordered_map<CURL*, std::shared_ptr<CoroTransfer>> active;
            std::exception_ptr                                       failure;
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
                            transfer->prepare(*transfer->session);
                            transfer->prepared = true;
                            active.emplace(handle, transfer);
                            checkMulti(curl_multi_add_handle(m_multi->handle, handle));
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
                    checkMulti(curl_multi_perform(m_multi->handle, &running));
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
                    checkMulti(curl_multi_poll(m_multi->handle, nullptr, 0, 100, nullptr));
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
                complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK, failure);
            }
            while (pending) {
                auto transfer = std::move(pending);
                pending       = std::move(transfer->next);
                complete(std::move(transfer), CURLE_ABORTED_BY_CALLBACK, failure);
            }
            {
                std::lock_guard lock{ m_mutex };
                m_io_done = true;
            }
            m_completion_ready.notify_all();
            coro_runtime_thread = false;
        }
    };

    void WakeCoroRuntime::operator()() const noexcept {
        runtime->Wake();
    }

    /**
     * @brief Await one exclusively owned session through the shared curl multi runtime.
     */
    class CoroTransferAwaiter {
        std::shared_ptr<CoroTransfer> m_transfer;

    public:
        CoroTransferAwaiter(std::shared_ptr<Session> session, std::function<void(Session&)> prepare, std::stop_token token)
            : m_transfer{ std::make_shared<CoroTransfer>(std::move(session), std::move(prepare), token) } {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> continuation) {
            auto  transfer         = m_transfer;
            auto& runtime          = CoroRuntime::Instance();
            transfer->continuation = continuation;
            transfer->wake.emplace(transfer->stop, WakeCoroRuntime{ &runtime });
            runtime.Submit(std::move(transfer));
        }

        auto await_resume() -> Response {
            if (m_transfer->error) {
                std::rethrow_exception(m_transfer->error);
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
         * @brief Start lazily; throw if the runtime has already been permanently cleaned up.
         */
        static void Startup() { detail::CoroRuntime::Instance().CheckRunning(); }

        /**
         * @brief Cancel outstanding HTTP transfers and wait for their continuations to finish.
         * @throws std::logic_error If called from a runtime thread.
         * @note Call outside tasks before curl_global_cleanup; this permanently closes the runtime.
         */
        static void Cleanup() { detail::CoroRuntime::Instance().Cleanup(); }
    };
} // namespace mcr
