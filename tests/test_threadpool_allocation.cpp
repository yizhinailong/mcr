/**
 * @file test_threadpool_allocation.cpp
 * @brief Verify task ownership and pool recovery when submission allocations fail.
 */
import std;
import mcr;

namespace {
    thread_local std::ptrdiff_t g_fail_after{ -1 }; ///< Allocations remaining before one failure on the submitting thread.

    /**
     * @brief Require a regression condition to hold.
     * @param condition Condition to check.
     * @param message Failure diagnostic.
     */
    void require(bool condition, std::string_view message) {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    /**
     * @brief Observe task capture release and verify destruction can reenter the pool.
     */
    struct Capture {
        mcr::utils::ThreadPool& pool;     ///< Pool whose mutex must be unlocked during release.
        int&                    releases; ///< Number of times this capture has been destroyed.

        /**
         * @brief Acquire the pool mutex before recording capture destruction.
         */
        ~Capture() {
            (void)pool.GetCurrentThreadNum();
            ++releases;
        }
    };
} // namespace

/**
 * @brief Inject one allocation failure, then allow exception handling to allocate normally.
 * @param bytes Requested allocation size.
 * @return Allocated storage.
 * @throws std::bad_alloc At the selected allocation or when allocation cannot succeed.
 */
void* operator new(std::size_t bytes) {
    if (g_fail_after == 0) {
        g_fail_after = -1;
        throw std::bad_alloc{};
    }
    if (g_fail_after > 0) {
        --g_fail_after;
    }
    while (true) {
        if (auto* storage = std::malloc(bytes == 0 ? 1 : bytes)) {
            return storage;
        }
        auto handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc{};
        }
        handler();
    }
}

/**
 * @brief Route array allocations through the same failure injector.
 * @param bytes Requested allocation size.
 * @return Allocated storage.
 */
void* operator new[](std::size_t bytes) {
    return ::operator new(bytes);
}

/**
 * @brief Release storage allocated by the replacement allocator.
 * @param storage Allocation to release.
 */
void operator delete(void* storage) noexcept {
    std::free(storage);
}

/**
 * @brief Release storage when its allocation size is supplied.
 * @param storage Allocation to release.
 */
void operator delete(void* storage, std::size_t) noexcept {
    ::operator delete(storage);
}

/**
 * @brief Release array storage.
 * @param storage Allocation to release.
 */
void operator delete[](void* storage) noexcept {
    ::operator delete(storage);
}

/**
 * @brief Release array storage when its allocation size is supplied.
 * @param storage Allocation to release.
 */
void operator delete[](void* storage, std::size_t) noexcept {
    ::operator delete(storage);
}

namespace {
    /**
     * @brief Fail a selected submission allocation while queued tasks own unique captures.
     * @param fail_after Allocation index to fail, starting from zero.
     * @return True if submission encountered the injected allocation failure.
     */
    auto check_allocation_failure(std::ptrdiff_t fail_after) -> bool {
        constexpr std::size_t       TASK_COUNT{ 24 };
        std::array<int, TASK_COUNT> releases{};
        int                         runs{};
        mcr::utils::ThreadPool      pool{ 1, 1 };
        pool.Start();
        pool.Pause();
        std::array<std::unique_ptr<Capture>, TASK_COUNT> captures;
        for (auto const& [index, capture] : captures | std::views::enumerate) {
            capture = std::make_unique<Capture>(pool, releases[index]);
        }
        std::vector<std::future<void>> futures;
        futures.reserve(TASK_COUNT);

        bool failed{};
        g_fail_after = fail_after;
        try {
            for (auto& capture : captures) {
                futures.push_back(pool.Submit([owned = std::move(capture), &runs] { ++runs; }));
            }
        } catch (std::bad_alloc const&) {
            failed = true;
        }
        g_fail_after = -1;

        pool.Stop();
        for (auto& future : futures) {
            require(future.wait_for(std::chrono::seconds{ 0 }) == std::future_status::ready, "Stop must complete every accepted future after a submission failure");
            bool canceled{};
            try {
                future.get();
            } catch (std::future_error const& error) {
                canceled = error.code() == std::make_error_code(std::future_errc::broken_promise);
            }
            require(canceled, "accepted pending tasks must retain broken_promise cancellation");
        }
        for (auto& capture : captures) {
            capture.reset();
        }
        require(runs == 0, "failed and canceled submissions must not invoke user code");
        require(std::ranges::all_of(releases, [](int count) { return count == 1; }), "every unique capture must be released exactly once outside the pool mutex");
        require(pool.Submit([] { return 42; }).get() == 42, "submission failure must leave the pool restartable");
        return failed;
    }
} // namespace

/**
 * @brief Exercise each allocation in a batch until the failure index exceeds its allocation count.
 * @return Zero when ownership, cancellation, and recovery checks pass.
 */
int main() {
    try {
        for (std::ptrdiff_t index{}; index < 512; ++index) {
            if (!check_allocation_failure(index)) {
                require(index > 0, "the allocation injector must observe submission allocations");
                std::println("test_threadpool_allocation: ok ({} allocation failure points)", index);
                return 0;
            }
        }
        throw std::runtime_error{ "allocation failure sweep did not finish" };
    } catch (std::exception const& error) {
        g_fail_after = -1;
        std::println("test_threadpool_allocation: {}", error.what());
        return 1;
    }
}
