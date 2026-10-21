/**
 * @file test_async_cleanup.cpp
 * @brief Clean up the global async pool from its own task while preserving claimed work.
 */
import std;
import mcr;

/**
 * @brief Verify permanent cleanup from an async worker does not terminate or join itself.
 */
auto main() -> int {
    using namespace std::chrono_literals;
    try {
        mcr::Async::Startup(2, 2).value();
        std::promise<void> started, release;
        auto gate = release.get_future().share();
        auto other = mcr::async([&] {
                         started.set_value();
                         if (gate.wait_for(3s) != std::future_status::ready) {
                             throw std::runtime_error{ "worker cleanup must not wait for another claimed task" };
                         }
                         return 7;
                     }).value();
        if (started.get_future().wait_for(3s) != std::future_status::ready) {
            throw std::runtime_error{ "the other global worker did not start" };
        }
        auto cleanup = mcr::async([] {
                           mcr::Async::Cleanup();
                           return 11;
                       }).value();
        auto cleaned = cleanup.Get();
        release.set_value();
        if (cleaned != 11 || other.Get() != 7 || mcr::async([] {}) || mcr::Async::Startup()) {
            throw std::runtime_error{ "cleanup must preserve active results and permanently reject new work" };
        }
        mcr::Async::Cleanup();
    } catch (std::exception const& error) {
        std::println("test_async_cleanup: {}", error.what());
        return 1;
    }
    return 0;
}
