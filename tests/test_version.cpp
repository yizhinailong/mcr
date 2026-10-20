/**
 * @file test_version.cpp
 * @brief Check generated metadata through the public entry module.
 */
#include <curl/curlver.h>
import std;
import mcr;

static_assert(mcr::VERSION_NUM == (mcr::VERSION_MAJOR << 16 | mcr::VERSION_MINOR << 8 | mcr::VERSION_PATCH));
static_assert(mcr::CURL_VERSION_NUM == LIBCURL_VERSION_NUM);
static_assert(!mcr::VERSION.empty());

auto main() -> int {
    auto const manifest{ std::filesystem::path{ __FILE__ }.parent_path().parent_path() / "mcpp.toml" };
    std::ifstream input{ manifest };
    bool package{ false };
    std::string line;
    while (std::getline(input, line)) {
        auto const start{ line.find_first_not_of(" \t") };
        if (start == std::string::npos) {
            continue;
        }
        std::string_view value{ line.data() + start, line.size() - start };
        if (value.starts_with('[')) {
            package = value.starts_with("[package]");
        } else if (package && value.starts_with("version")) {
            auto const quote{ value.find_first_of("\"'") };
            if (quote == std::string_view::npos) {
                return 1;
            }
            auto const end{ value.find(value[quote], quote + 1) };
            auto const version{ value.substr(quote + 1, end - quote - 1) };
            auto const core{ version.substr(0, version.find_first_of("-+")) };
            if (version != mcr::VERSION || core != std::format("{}.{}.{}", mcr::VERSION_MAJOR, mcr::VERSION_MINOR, mcr::VERSION_PATCH)) {
                std::println("test_version: generated constants must match [package].version");
                return 1;
            }
            std::println("test_version: mcr {} (0x{:06x}), curl 0x{:06x}", mcr::VERSION, mcr::VERSION_NUM, mcr::CURL_VERSION_NUM);
            return 0;
        }
    }
    std::println("test_version: could not find the package version");
    return 1;
}
