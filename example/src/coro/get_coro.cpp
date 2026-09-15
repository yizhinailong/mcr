/**
 * @file get_coro.cpp
 * @brief Demonstrate mcr::GetCoro with an explicit call to this public request API.
 */
import std;
import mcr;
import mcr_example.support;

/**
 * @brief Await a GET request while owning the URL in the coroutine frame.
 * @param url HTTP endpoint copied into this coroutine.
 * @return Task yielding the completed response.
 */
auto request(std::string url) -> mcr::Task<mcr::Result<mcr::Response>> {
    co_return co_await mcr::GetCoro(
        mcr::Url{
            std::move(url)
    },
        mcr::Parameters{ { "message", "hello world" } },
        mcr::UserAgent{ "mcr_example" },
        mcr::options::Timeout{ std::chrono::seconds{ 10 } }
    );
}

/**
 * @brief Run this request example against the supplied HTTP endpoint.
 * @param argc Number of command-line arguments.
 * @param argv Executable name followed by URL, optional download destination, or --help.
 * @return Zero for success or help, one for failure, and two for invalid arguments.
 */
auto main(int argc, char** argv) -> int {
    return example::run(argc, argv, "get_coro", [](example::Arguments const& args) { return example::print_response(mcr::sync_wait(request(args.url))); }, example::RuntimeKind::Coroutine);
}
