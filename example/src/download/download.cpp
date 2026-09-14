/**
 * @file download.cpp
 * @brief Download binary bytes into a caller-owned output stream.
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
    return example::run(argc, argv, "download", [](example::Arguments const& args) {
        std::ofstream file{ args.output, std::ios::binary | std::ios::trunc };
        auto const response{ mcr::Download(file,
            mcr::Url{ args.url },
            mcr::options::Timeout{ std::chrono::seconds{ 10 } }) };
        file.close();
        if (file.fail()) {
            throw std::runtime_error{ "Could not finish writing the output file." };
        }
        std::println("Downloaded bytes: {}", std::filesystem::file_size(args.output));
        return example::print_response(response); }, example::RuntimeKind::Synchronous, true);
}
