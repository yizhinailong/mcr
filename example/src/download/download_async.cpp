/**
 * @file download_async.cpp
 * @brief Download to a file through a future and observe completion with Get().
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
    return example::run(argc, argv, "download_async", [](example::Arguments const& args) {
        auto pending{ mcr::DownloadAsync(args.output,
            mcr::Url{ args.url },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }) };
        if (!pending) { return example::print_error(pending.error()); }
        auto const response{ pending->Get() };
        if (!response) { return example::print_error(response.error()); }
        std::println("Downloaded bytes: {}", std::filesystem::file_size(args.output));
        return example::print_response(response); }, example::RuntimeKind::Async, true);
}
