/**
 * @file multi_post.cpp
 * @brief Demonstrate mcr::MultiPost with an explicit call to this public request API.
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
    return example::run(argc, argv, "multi_post", [](example::Arguments const& args) {
        auto first{ std::tuple{
            mcr::Url{ args.url },
            mcr::Parameters{ { "request", "first" } },
            mcr::Body{ "hello from POST" },
            mcr::Header{ { "Content-Type", "text/plain" } },
            mcr::UserAgent{ "mcr_example" },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }} };
        auto second{ std::tuple{
            mcr::Url{ args.url },
            mcr::Parameters{ { "request", "second" } },
            mcr::Body{ "hello from POST" },
            mcr::Header{ { "Content-Type", "text/plain" } },
            mcr::UserAgent{ "mcr_example" },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }} };
        auto responses{ mcr::MultiPost(std::move(first), std::move(second)) };
        int result{};
        for (auto const& response : responses) {
            result |= example::print_response(response);
        }
        return result; }, example::RuntimeKind::Synchronous);
}
