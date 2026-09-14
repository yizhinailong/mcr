/**
 * @file options.cpp
 * @brief Demonstrate mcr::Options with an explicit call to this public request API.
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
    return example::run(argc, argv, "options", [](example::Arguments const& args) {
        auto const response{ mcr::Options(
            mcr::Url{ args.url },
            mcr::Parameters{ { "message", "hello world" } },
            mcr::UserAgent{ "mcr_example" },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }) };
        return example::print_response(response); }, example::RuntimeKind::Synchronous);
}
