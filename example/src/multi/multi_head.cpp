/**
 * @file multi_head.cpp
 * @brief Demonstrate mcr::MultiHead with an explicit call to this public request API.
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
    return example::run(argc, argv, "multi_head", [](example::Arguments const& args) {
        auto first{ std::tuple{
            mcr::Url{ args.url },
            mcr::Parameters{ { "request", "first" } },
            mcr::UserAgent{ "mcr_example" },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }} };
        auto second{ std::tuple{
            mcr::Url{ args.url },
            mcr::Parameters{ { "request", "second" } },
            mcr::UserAgent{ "mcr_example" },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }} };
        auto responses{ mcr::MultiHead(std::move(first), std::move(second)) };
        if (!responses) { return example::print_error(responses.error()); }
        int result{};
        for (auto const& response : *responses) {
            result |= example::print_response(response);
        }
        return result; }, example::RuntimeKind::Synchronous);
}
