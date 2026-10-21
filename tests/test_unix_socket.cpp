/**
 * @file test_unix_socket.cpp
 * @brief Verify HTTP transport over a real Unix domain socket on supported curl builds.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"
#ifdef _WIN32
    #include <afunix.h>
#else
    #include <sys/un.h>
#endif
import std;
import mcr;

namespace {
    /**
     * @brief Fail a socket fixture or HTTP assertion with a readable diagnostic.
     */
    auto require(bool condition, std::string_view message) -> void {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    /**
     * @brief Remove only this test's unique socket path after its listener closes.
     */
    struct SocketPath {
        std::filesystem::path path{ std::filesystem::temp_directory_path() / std::format("mcr-u-{}-{}.sock", std::chrono::steady_clock::now().time_since_epoch().count(), std::random_device{}()) }; ///< Owned socket node.

        /**
         * @brief Unlink the socket node on successful and failed construction paths.
         */
        ~SocketPath() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };

    /**
     * @brief Serve one real HTTP request without opening a TCP listener or resolving DNS.
     */
    class UnixServer {
    private:
        SocketPath m_path;                                               ///< Unlinked after the listener is closed.
        mcr::test::Socket m_listener{ socket(AF_UNIX, SOCK_STREAM, 0) }; ///< Owned domain socket.
        std::string m_request;                                           ///< Worker-owned until Check joins it.
        std::exception_ptr m_failure;                                    ///< Worker failure, inspected after joining.
        std::jthread m_worker;                                           ///< Joins before listener and path destruction.

    public:
        /**
         * @brief Bind a unique local socket and start a bounded one-request worker.
         */
        UnixServer() {
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            auto const text = Path();
            require(text.size() < sizeof(address.sun_path), "temporary Unix socket path must fit sockaddr_un");
            std::memcpy(address.sun_path, text.data(), text.size());
            require(bind(m_listener.handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(m_listener.handle, 1) == 0, "Unix fixture must bind and listen");
            m_worker = std::jthread{ [this](std::stop_token stop) {
                try {
                    while (!stop.stop_requested()) {
                        if (!mcr::test::readable(m_listener.handle)) {
                            continue;
                        }
                        mcr::test::Socket client{ accept(m_listener.handle, nullptr, nullptr) };
                        while (!stop.stop_requested() && !m_request.contains("\r\n\r\n")) {
                            if (!mcr::test::readable(client.handle)) {
                                continue;
                            }
                            std::array<char, 2048> buffer{};
                            auto const count = recv(client.handle, buffer.data(), static_cast<int>(buffer.size()), 0);
                            require(count > 0, "Unix fixture must receive HTTP request headers");
                            m_request.append(buffer.data(), static_cast<std::size_t>(count));
                        }
                        std::string_view response{ "HTTP/1.1 200 OK\r\nContent-Length: 9\r\nConnection: close\r\n\r\nunix-body" };
                        while (!response.empty() && !stop.stop_requested()) {
#ifdef MSG_NOSIGNAL
                            constexpr int FLAGS{ MSG_NOSIGNAL };
#else
                            constexpr int FLAGS{ 0 };
#endif
                            auto const count = send(client.handle, response.data(), static_cast<int>(response.size()), FLAGS);
                            require(count > 0, "Unix fixture must send its response");
                            response.remove_prefix(static_cast<std::size_t>(count));
                        }
                        return;
                    }
                } catch (...) {
                    m_failure = std::current_exception();
                }
            } };
        }

        /**
         * @brief Stop and join even after a client configuration failure.
         */
        ~UnixServer() {
            m_worker.request_stop();
            if (m_worker.joinable()) {
                m_worker.join();
            }
        }

        /**
         * @brief Return the UTF-8 socket path expected by libcurl.
         */
        auto Path() const -> std::string {
            auto const text = m_path.path.u8string();
            return { reinterpret_cast<char const*>(text.data()), text.size() };
        }

        /**
         * @brief Join the completed worker and inspect the request independently of curl metadata.
         */
        auto Check() -> void {
            m_worker.request_stop();
            m_worker.join();
            if (m_failure) {
                std::rethrow_exception(m_failure);
            }
            require(m_request.starts_with("GET /unix HTTP/1.1\r\n") && m_request.contains("Host: unix.test"), "the Unix listener must receive the expected method, path, and virtual host");
        }
    };
} // namespace

/**
 * @brief Verify supported Unix transport or the precise unavailable-backend configuration error.
 */
auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        auto session = mcr::Session::Create().value();
        session->SetProxies(mcr::options::Proxies{ { "http", "" }, { "no_proxy", "*" } }).value();
        session->SetTimeout(mcr::options::Timeout{ 5000 }).value();
        if (!(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_UNIX_SOCKETS)) {
            auto status = session->SetUnixSocket(mcr::options::UnixSocket{ "unavailable.sock" });
            require(!status && status.error().code == mcr::ErrorCode::NOT_BUILT_IN, "curl without Unix socket support must report that capability error");
            std::println("test_unix_socket: unavailable backend rejects Unix socket configuration");
        } else {
            UnixServer server;
            session->SetUrl(mcr::Url{ "http://unix.test/unix" }).value();
            session->SetUnixSocket(mcr::options::UnixSocket{ server.Path() }).value();
            auto response = session->Get().value();
            server.Check();
            require(!response.error && response.status_code == 200 && response.text == "unix-body", std::format("Unix HTTP transport failed: {}", response.error.message));
            std::println("test_unix_socket: verified HTTP over a real Unix domain socket");
        }
        session->SetUnixSocket(mcr::options::UnixSocket{ "" }).value();
        mcr::test::HttpServer tcp_server;
        session->SetUrl(tcp_server.Url("/hello")).value();
        auto restored{ session->Get().value() };
        require(!restored.error && restored.status_code == 200 && restored.text == "Hello session!", "clearing a Unix socket path must restore real TCP requests on every backend");
        tcp_server.Check();
    } catch (std::exception const& error) {
        std::println("test_unix_socket: {}", error.what());
        passed = false;
    }
    curl_global_cleanup();
    return passed ? 0 : 1;
}
