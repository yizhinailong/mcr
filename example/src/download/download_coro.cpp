/**
 * @file download_coro.cpp
 * @brief Download to a file through co_await and wait at the application boundary.
 */
import std;
import mcr;
import mcr_example.support;

/**
 * @brief Own the URL and output path while awaiting a binary download.
 * @param url HTTP download endpoint.
 * @param output Destination opened and closed by DownloadCoro.
 * @return Task yielding the download metadata.
 */
auto download(std::string url, std::filesystem::path output) -> mcr::Task<mcr::Result<mcr::Response>> {
    co_return co_await mcr::DownloadCoro(std::move(output), mcr::Url{ std::move(url) }, mcr::options::Timeout{ std::chrono::seconds{ 10 } });
}

/**
 * @brief Run this request example against the supplied HTTP endpoint.
 * @param argc Number of command-line arguments.
 * @param argv Executable name followed by URL, optional download destination, or --help.
 * @return Zero for success or help, one for failure, and two for invalid arguments.
 */
auto main(int argc, char** argv) -> int {
    return example::run(argc, argv, "download_coro", [](example::Arguments const& args) {
        auto const response{ mcr::sync_wait(download(args.url, args.output)) };
        if (!response) { return example::print_error(response.error()); }
        std::println("Downloaded bytes: {}", std::filesystem::file_size(args.output));
        return example::print_response(response); }, example::RuntimeKind::Coroutine, true);
}
