/**
 * @file support.cppm
 * @brief Shared command-line handling, curl lifetime, and response output for examples.
 */
module;

#include <curl/curl.h>

export module mcr_example.support;
import std;
import mcr;

export namespace example {
    /**
     * @brief Select the runtime needed by an example.
     */
    enum class RuntimeKind {
        Synchronous, ///< Requests complete on the calling thread.
        Async,       ///< Requests use the global thread pool.
        Coroutine    ///< Requests use the curl multi coroutine runtime.
    };

    /**
     * @brief Hold command-line values for the duration of an example.
     */
    struct Arguments {
        std::string           url;    ///< HTTP endpoint supplied by the caller.
        std::filesystem::path output; ///< Destination for file downloads, otherwise empty.
    };

    /**
     * @brief Initialize curl before worker threads and clean it up after they stop.
     */
    class Runtime {
    private:
        RuntimeKind m_kind; ///< Runtime started for this process.

    public:
        /**
         * @brief Initialize curl and start the selected runtime on the main thread.
         * @param kind Runtime used by the example.
         * @throws std::runtime_error If curl cannot be initialized.
         * @throws std::exception If the selected runtime cannot start.
         */
        explicit Runtime(RuntimeKind kind) : m_kind{ kind } {
            if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
                throw std::runtime_error{ "Could not initialize curl." };
            }
            try {
                if (m_kind == RuntimeKind::Async) {
                    mcr::Async::Startup(2, 4).value();
                } else if (m_kind == RuntimeKind::Coroutine) {
                    mcr::Coro::Startup().value();
                }
            } catch (...) {
                curl_global_cleanup();
                throw;
            }
        }

        Runtime(Runtime const&)                    = delete;
        auto operator=(Runtime const&) -> Runtime& = delete;

        /**
         * @brief Stop workers before releasing curl's process-wide resources.
         * @note All example tasks are consumed before this guard is destroyed.
         */
        ~Runtime() {
            if (m_kind == RuntimeKind::Async) {
                mcr::Async::Cleanup();
            } else if (m_kind == RuntimeKind::Coroutine) {
                (void)mcr::Coro::Cleanup();
            }
            curl_global_cleanup();
        }
    };

    /**
     * @brief Print response metadata and body, distinguishing transport and HTTP errors.
     * @param response Completed request.
     * @return Zero for HTTP 2xx and one for transport errors or other HTTP statuses.
     */
    auto print_response(mcr::Response const& response) -> int {
        if (response.error) {
            std::println("Request failed: {}", response.error.message);
            return 1;
        }
        std::println("Status code: {}", response.status_code);
        std::println("Header:");
        for (auto const& [name, value] : response.header) {
            std::println("\t{}: {}", name, value);
        }
        std::println("Text: {}", response.text);
        return mcr::status::is_success(response.status_code) ? 0 : 1;
    }

    /**
     * @brief Print an explicit operation failure and return a failing exit code.
     * @param error Owned library diagnostic.
     * @return One to indicate failure.
     */
    auto print_error(mcr::Error const& error) -> int {
        std::println("Request failed: {}: {}", mcr::to_string(error.code), error.message);
        return 1;
    }

    /**
     * @brief Print an operation result including configuration and local I/O errors.
     * @param response Request result to inspect.
     * @return Zero for a successful HTTP response, otherwise one.
     */
    auto print_response(mcr::Result<mcr::Response> const& response) -> int {
        return response ? print_response(*response) : print_error(response.error());
    }

    /**
     * @brief Parse arguments and run one example with an initialized HTTP runtime.
     * @tparam Handler Callable accepting the parsed arguments and returning an exit code.
     * @param argc Number of command-line arguments.
     * @param argv Executable name followed by URL and, for file downloads, destination.
     * @param name Executable name shown in help.
     * @param handler Example implementation; all its tasks must finish before returning.
     * @param kind Runtime required by the implementation.
     * @param needs_output Whether a file destination must be supplied.
     * @return Zero for success or help, one for failure, and two for invalid arguments.
     */
    template <typename Handler>
    auto run(
        int              argc,
        char**           argv,
        std::string_view name,
        Handler&&        handler,
        RuntimeKind      kind         = RuntimeKind::Synchronous,
        bool             needs_output = false
    ) -> int {
        auto usage = [&] {
            std::println("Usage: {} <URL>{}", name, needs_output ? " <OUTPUT>" : "");
        };
        if (argc == 2 && std::string_view{ argv[1] } == "--help") {
            usage();
            return 0;
        }
        if (argc != (needs_output ? 3 : 2)) {
            usage();
            return 2;
        }
        try {
            Arguments const args{ argv[1], needs_output ? std::filesystem::path{ argv[2] } : std::filesystem::path{} };
            Runtime         runtime{ kind };
            return std::invoke(std::forward<Handler>(handler), args);
        } catch (std::bad_expected_access<mcr::Error> const& error) {
            return print_error(error.error());
        } catch (std::exception const& error) {
            std::println("Example failed: {}", error.what());
            return 1;
        }
    }
} // namespace example
