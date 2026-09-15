/**
 * @file put_callback.cpp
 * @brief Demonstrate mcr::PutCallback with an explicit call to this public request API.
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
    return example::run(argc, argv, "put_callback", [](example::Arguments const& args) {
        auto completion{ mcr::PutCallback(
            [](mcr::Result<mcr::Response> response) {
return example::print_response(response);
            },
            mcr::Url{ args.url },
            mcr::Parameters{ { "message", "hello world" } },
            mcr::Body{ "hello from PUT" },
            mcr::Header{ { "Content-Type", "text/plain" } },
            mcr::UserAgent{ "mcr_example" },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }) };
        if (!completion) { return example::print_error(completion.error()); }
        return completion->Get(); }, example::RuntimeKind::Async);
}
