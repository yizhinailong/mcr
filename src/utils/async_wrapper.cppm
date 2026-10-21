/**
 * @file async_wrapper.cppm
 * @brief Movable future results with optional cooperative cancellation.
 */
export module mcr:async_wrapper;

import std;

export namespace mcr::utils {

    /**
     * @brief Cancellation outcomes retaining cpr's names and numeric values.
     */
    enum class [[nodiscard]] CancellationResult : std::uint8_t {
        failure = 0,           ///< Reserved for compatibility; the flag-based wrapper does not return it.
        success = 1,           ///< This call changed the cancellation flag from false to true.
        invalid_operation = 2, ///< No cancellable future remains, or cancellation was already requested.
    };

    /**
     * @brief Own a future result with optional cooperative cancellation, following cpr.
     * @tparam RetType Future result type, including void, references, and move-only values.
     * @tparam is_cancellable Whether the wrapper owns a shared cancellation flag.
     */
    template <typename RetType, bool is_cancellable = false>
    class AsyncWrapper;

    /**
     * @brief Own a future with checked access and standard future lifetime semantics.
     * @tparam RetType Future result type.
     * @note Get and Share consume the future. Destruction and move assignment may wait when
     * releasing the last reference to a running std::async state, just as std::future does.
     */
    template <typename RetType>
    class AsyncWrapper<RetType, false> {
    private:
        std::future<RetType> m_future; ///< Exclusively owned future handle.

    public:
        /**
         * @brief Construct an invalid wrapper with no shared future state.
         */
        AsyncWrapper() = default;

        /**
         * @brief Take ownership of a future, leaving its source invalid.
         * @param future Future to move; an invalid future is accepted.
         */
        explicit AsyncWrapper(
            std::future<RetType>&& future
        ) noexcept : m_future{ std::move(future) } {}

        AsyncWrapper(AsyncWrapper const&) = delete;
        auto operator=(AsyncWrapper const&) -> AsyncWrapper& = delete;
        AsyncWrapper(AsyncWrapper&&) noexcept = default;
        auto operator=(AsyncWrapper&&) noexcept -> AsyncWrapper& = default;
        ~AsyncWrapper() = default;

        /**
         * @brief Wait for and consume the stored result, preserving reference and void results.
         * @return The future's result, or no value for a void result.
         * @throws std::logic_error If the future has no shared state.
         * @note Exceptions stored in the future propagate and also consume its state.
         */
        [[nodiscard]] auto Get() -> RetType {
            checkValid("mcr::utils::AsyncWrapper::Get: associated future is invalid.");
            return m_future.get();
        }

        /**
         * @brief Check for a shared future state.
         * @return Whether Get or a wait operation may be called.
         */
        [[nodiscard]] auto Valid() const noexcept -> bool {
            return m_future.valid();
        }

        /**
         * @brief Wait until the future is ready without consuming its result.
         * @throws std::logic_error If the future has no shared state.
         */
        auto Wait() const -> void {
            checkValid("mcr::utils::AsyncWrapper::Wait: associated future is invalid.");
            m_future.wait();
        }

        /**
         * @brief Wait for a relative timeout without consuming the future.
         * @tparam Rep Duration representation type.
         * @tparam Period Duration tick period.
         * @param timeout_duration Maximum time to wait.
         * @return ready, timeout, or deferred, as reported by std::future.
         * @throws std::logic_error If the future has no shared state.
         */
        template <typename Rep, typename Period>
        auto WaitFor(
            std::chrono::duration<Rep, Period> const& timeout_duration
        ) const -> std::future_status {
            checkValid("mcr::utils::AsyncWrapper::WaitFor: associated future is invalid.");
            return m_future.wait_for(timeout_duration);
        }

        /**
         * @brief Wait until an absolute deadline without consuming the future.
         * @tparam Clock Deadline clock type.
         * @tparam Duration Deadline duration type.
         * @param timeout_time Deadline at which to stop waiting.
         * @return ready, timeout, or deferred, as reported by std::future.
         * @throws std::logic_error If the future has no shared state.
         */
        template <typename Clock, typename Duration>
        auto WaitUntil(
            std::chrono::time_point<Clock, Duration> const& timeout_time
        ) const -> std::future_status {
            checkValid("mcr::utils::AsyncWrapper::WaitUntil: associated future is invalid.");
            return m_future.wait_until(timeout_time);
        }

        /**
         * @brief Transfer this future handle into a shared future.
         * @return A shared future, invalid if this wrapper had no shared state.
         * @note This wrapper becomes invalid. No validity or cancellation check is performed, as in cpr.
         */
        auto Share() noexcept -> std::shared_future<RetType> {
            return m_future.share();
        }

    private:
        /**
         * @brief Reject operations on an invalid future.
         * @param message Diagnostic for the operation.
         */
        auto checkValid(char const* message) const -> void {
            if (!m_future.valid()) {
                throw std::logic_error{ message };
            }
        }
    };

    /**
     * @brief Combine a future with a shared flag observed by a cooperating task.
     * @tparam RetType Future result type.
     * @note Cancellation is checked before Get and wait operations; it does not interrupt a wait
     * already in progress or forcibly stop a task. Share retains the base future behavior.
     * Concurrent Cancel calls are supported while the wrapper and future handle remain unchanged.
     * Get, Share, moving, and destruction must be synchronized with other accesses to the wrapper.
     */
    template <typename RetType>
    class AsyncWrapper<RetType, true> : public AsyncWrapper<RetType, false> {
    private:
        using Base = AsyncWrapper<RetType, false>;
        std::shared_ptr<std::atomic_bool> m_cancellation_state; ///< Shared cancellation flag; null after moving out.

    public:
        /**
         * @brief Take ownership of a future and a shared cancellation flag.
         * @param future Future to move; an invalid future is accepted.
         * @param cancellation_state Nonnull flag also observed by the task or CancellationCallback.
         * @throws std::invalid_argument If the flag is null; the future is not consumed in that case.
         * @note A flag already set to true is accepted and produces a cancelled wrapper.
         */
        AsyncWrapper(
            std::future<RetType>&& future,
            std::shared_ptr<std::atomic_bool>&& cancellation_state
        ) : m_cancellation_state{ std::move(cancellation_state) } {
            if (!m_cancellation_state) {
                throw std::invalid_argument{ "mcr::utils::AsyncWrapper: cancellation state must not be null." };
            }
            Base::operator=(Base{ std::move(future) });
        }

        AsyncWrapper(AsyncWrapper const&) = delete;
        auto operator=(AsyncWrapper const&) -> AsyncWrapper& = delete;
        AsyncWrapper(AsyncWrapper&&) noexcept = default;

        /**
         * @brief Cancel the previously owned operation and take ownership from another wrapper.
         * @param other Source wrapper; self-move leaves this wrapper unchanged.
         * @return This wrapper after the transfer.
         * @note The old flag is set before releasing its future, which may wait for a running task.
         */
        auto operator=(AsyncWrapper&& other) noexcept -> AsyncWrapper& {
            if (this != &other) {
                requestCancellation();
                Base::operator=(std::move(other));
                m_cancellation_state = std::move(other.m_cancellation_state);
            }
            return *this;
        }

        /**
         * @brief Signal cancellation before releasing the future, including after Get or Share.
         */
        ~AsyncWrapper() {
            requestCancellation();
        }

        /**
         * @brief Check cancellation, then wait for and consume the result.
         * @return The future's result, or no value for a void result.
         * @throws std::logic_error If cancelled or the future has no shared state.
         * @note A task exception propagates from the future when access is permitted.
         */
        [[nodiscard]] auto Get() -> RetType {
            checkCancelled("mcr::utils::AsyncWrapper::Get: request is cancelled.");
            return Base::Get();
        }

        /**
         * @brief Check whether result access is available.
         * @return True for an uncancelled, valid future.
         */
        [[nodiscard]] auto Valid() const noexcept -> bool {
            return !IsCancelled() && Base::Valid();
        }

        /**
         * @brief Check cancellation, then wait without consuming the result.
         * @throws std::logic_error If cancelled or the future has no shared state.
         */
        auto Wait() const -> void {
            checkCancelled("mcr::utils::AsyncWrapper::Wait: request is cancelled.");
            Base::Wait();
        }

        /**
         * @brief Check cancellation before a relative timed wait.
         * @tparam Rep Duration representation type.
         * @tparam Period Duration tick period.
         * @param timeout_duration Maximum time to wait.
         * @return ready, timeout, or deferred, as reported by std::future.
         * @throws std::logic_error If cancelled or the future has no shared state.
         */
        template <typename Rep, typename Period>
        auto WaitFor(
            std::chrono::duration<Rep, Period> const& timeout_duration
        ) const -> std::future_status {
            checkCancelled("mcr::utils::AsyncWrapper::WaitFor: request is cancelled.");
            return Base::WaitFor(timeout_duration);
        }

        /**
         * @brief Check cancellation before waiting for an absolute deadline.
         * @tparam Clock Deadline clock type.
         * @tparam Duration Deadline duration type.
         * @param timeout_time Deadline at which to stop waiting.
         * @return ready, timeout, or deferred, as reported by std::future.
         * @throws std::logic_error If cancelled or the future has no shared state.
         */
        template <typename Clock, typename Duration>
        auto WaitUntil(
            std::chrono::time_point<Clock, Duration> const& timeout_time
        ) const -> std::future_status {
            checkCancelled("mcr::utils::AsyncWrapper::WaitUntil: request is cancelled.");
            return Base::WaitUntil(timeout_time);
        }

        /**
         * @brief Request cooperative cancellation using one atomic flag transition.
         * @return success for the first request; invalid_operation if already cancelled,
         * moved from, or the future was invalid, consumed, or shared.
         * @note A ready but unconsumed future can still be cancelled, matching cpr.
         */
        auto Cancel() noexcept -> CancellationResult {
            if (!Base::Valid() || !m_cancellation_state) {
                return CancellationResult::invalid_operation;
            }
            return m_cancellation_state->exchange(true) ? CancellationResult::invalid_operation : CancellationResult::success;
        }

        /**
         * @brief Inspect the shared flag.
         * @return Whether cancellation was requested; false after moving out.
         */
        [[nodiscard]] auto IsCancelled() const noexcept -> bool {
            return m_cancellation_state && m_cancellation_state->load();
        }

    private:
        /**
         * @brief Request cancellation even if the future was consumed, shared, or originally invalid.
         */
        auto requestCancellation() noexcept -> void {
            if (m_cancellation_state) {
                m_cancellation_state->store(true);
            }
        }

        /**
         * @brief Reject result access after cancellation.
         * @param message Diagnostic for the operation.
         */
        auto checkCancelled(char const* message) const -> void {
            if (IsCancelled()) {
                throw std::logic_error{ message };
            }
        }
    };

    /**
     * @brief Deduce a wrapper without cancellation from its future.
     * @tparam RetType Future result type.
     */
    template <typename RetType>
    AsyncWrapper(std::future<RetType>&&) -> AsyncWrapper<RetType, false>;

    /**
     * @brief Deduce a cancellable wrapper from its future and flag.
     * @tparam RetType Future result type.
     */
    template <typename RetType>
    AsyncWrapper(
        std::future<RetType>&&,
        std::shared_ptr<std::atomic_bool>&&
    ) -> AsyncWrapper<RetType, true>;

} // namespace mcr::utils
