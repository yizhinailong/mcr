/**
 * @file test_utf8_paths.cpp
 * @brief Read and upload Unicode filenames and convert native TLS paths to UTF-8.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"

import std;
import mcr;

namespace {
    /**
     * @brief Fail a path interoperability assertion.
     */
    auto require(bool condition, std::string_view message) -> void {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    /**
     * @brief Produce UTF-8 option bytes from a native test path.
     */
    auto path_bytes(std::filesystem::path const& path) -> std::string {
        auto const bytes = path.u8string();
        return { reinterpret_cast<char const*>(bytes.data()), bytes.size() };
    }

    /**
     * @brief Own one uniquely created directory and its Unicode file.
     */
    struct TemporaryFile {
        std::filesystem::path directory; ///< Unique directory owned by this fixture.
        std::filesystem::path file;      ///< Unicode input file owned by this fixture.

        /**
         * @brief Create a Unicode filename using the native filesystem API.
         */
        TemporaryFile() {
            directory = std::filesystem::temp_directory_path() / std::filesystem::path{ u8"mcr-路径" };
            directory += std::format("-{}", std::chrono::steady_clock::now().time_since_epoch().count());
            require(std::filesystem::create_directory(directory), "the test directory must be unique");
            file = directory / std::filesystem::path{ u8"正文-é-😀.bin" };
        }

        /**
         * @brief Remove only the file and directory created by this fixture.
         */
        ~TemporaryFile() {
            std::error_code error;
            std::filesystem::remove(file, error);
            std::filesystem::remove(directory, error);
        }
    };

    /**
     * @brief Preserve UTF-8 across Body, MIME, Buffer, and native TLS option boundaries.
     */
    auto check_paths(mcr::test::HttpServer const& server) -> void {
        using namespace std::chrono_literals;
        TemporaryFile temporary;
        std::string const bytes{ "binary\0payload", 14 };
        {
            std::ofstream output{ temporary.file, std::ios::binary };
            output.exceptions(std::ios::badbit | std::ios::failbit);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        auto const input = path_bytes(temporary.file);
        auto body = mcr::Body::FromFile(mcr::File{ input }).value();
        require(body.Str() == bytes, "Body must open UTF-8 path bytes through a native path");
        auto session = mcr::Session::Create().value();
        session->SetUrl(server.Url("/echo")).value();
        session->SetProxies({ { "http", "" }, { "no_proxy", "*" } }).value();
        session->SetTimeout(mcr::options::Timeout{ 3s }).value();
        session->SetMultipart({ { "file", mcr::File{ input } }, { "buffer", mcr::Buffer::Create(bytes.begin(), bytes.end(), temporary.file.filename()).value() } }).value();
        auto response = session->Post().value();
        auto const filename = "filename=\"" + path_bytes(temporary.file.filename()) + "\"";
        auto const first = response.text.find(filename);
        require(!response.error && response.status_code == 200 && response.text.contains(bytes) && first != std::string::npos && response.text.find(filename, first + filename.size()) != std::string::npos, "file and buffer parts must send Unicode filenames as UTF-8 and retain their binary bytes");
        auto const tls = mcr::options::Ssl(mcr::options::ssl::PemCert{ temporary.file }, mcr::options::ssl::PemKey{ temporary.file }, mcr::options::ssl::CaInfo{ temporary.file }, mcr::options::ssl::CaPath{ temporary.directory }, mcr::options::ssl::Crl{ temporary.file });
        require(tls.cert_file == input && tls.key_file == input && tls.ca_info == input && tls.crl_file == input && tls.ca_path == path_bytes(temporary.directory), "native certificate, key, CA, and CRL paths must be passed to curl as UTF-8");
    }
} // namespace

/**
 * @brief Run Unicode filesystem integration checks against a local HTTP fixture.
 */
auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    int result{};
    try {
        mcr::test::HttpServer server;
        check_paths(server);
        server.Check();
    } catch (std::exception const& error) {
        std::println("test_utf8_paths: {}", error.what());
        result = 1;
    }
    curl_global_cleanup();
    return result;
}
