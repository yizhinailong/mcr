/**
 * @file get_async.cpp
 * @brief Demonstrate mcr::GetAsync with an explicit call to this public request API.
 */
import std;
import mcr;
import mcr_example.support;

/**
 * @brief Run this request example against the supplied HTTP endpoint.
 * @param argc Number of command-line arguments.
 * @param argv Executable name followed by URL, optional download destination, or --help.
 * @return Zero for success or help, one for failure, and two for invalid arguments.
 */
auto main(int argc, char** argv) -> int {
    return example::run(argc, argv, "get_async", [](example::Arguments const& args) {
        auto pending{ mcr::GetAsync(
            mcr::Url{ args.url },
            mcr::Parameters{ { "message", "hello world" } },
            mcr::UserAgent{ "mcr_example" },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }) };
        if (!pending) { return example::print_error(pending.error()); }
        std::println("Request submitted; waiting for its response.");
        return example::print_response(pending->Get()); }, example::RuntimeKind::Async);
}
