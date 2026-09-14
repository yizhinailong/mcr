/**
 * @file download_callback.cpp
 * @brief Download binary bytes into memory through the WriteCallback overload.
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
    return example::run(argc, argv, "download_callback", [](example::Arguments const& args) {
        std::string              bytes;
        mcr::WriteCallback const write{ [&bytes](std::string_view chunk, std::intptr_t) {
            bytes.append(chunk);
            return true;
        } };
        auto const response{ mcr::Download(write, mcr::Url{ args.url }, mcr::options::Timeout{ std::chrono::seconds{ 10 } }) };
        std::println("Downloaded bytes: {}", bytes.size());
        std::print("Content (hex): ");
        for (unsigned char byte : bytes) {
            std::print("{:02x}", byte);
        }
        std::println();
        return example::print_response(response);
    });
}
