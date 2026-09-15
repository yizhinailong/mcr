/**
 * @file multi_put_async.cpp
 * @brief Demonstrate mcr::MultiPutAsync with an explicit call to this public request API.
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
    return example::run(argc, argv, "multi_put_async", [](example::Arguments const& args) {
        auto first{ std::tuple{
            mcr::Url{ args.url },
            mcr::Parameters{ { "request", "first" } },
            mcr::Body{ "hello from PUT" },
            mcr::Header{ { "Content-Type", "text/plain" } },
            mcr::UserAgent{ "mcr_example" },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }} };
        auto second{ std::tuple{
            mcr::Url{ args.url },
            mcr::Parameters{ { "request", "second" } },
            mcr::Body{ "hello from PUT" },
            mcr::Header{ { "Content-Type", "text/plain" } },
            mcr::UserAgent{ "mcr_example" },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }} };
        auto pending{ mcr::MultiPutAsync(std::move(first), std::move(second)) };
        if (!pending) { return example::print_error(pending.error()); }
        int result{};
        for (auto& task : *pending) {
            result |= example::print_response(task.Get());
        }
        return result; }, example::RuntimeKind::Async);
}
