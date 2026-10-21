/**
 * @file http_server.hpp
 * @brief Shared loopback HTTP fixture for Session and free API tests.
 */
#pragma once
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
#else
    #include <arpa/inet.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

#include <curl/curl.h>
import std;
import mcr;

namespace mcr::test {
    using namespace std::chrono_literals;

#ifdef _WIN32
    /**
     * @brief Balance this fixture's Winsock reference independently of libcurl initialization.
     */
    struct SocketRuntime {
        /**
         * @brief Initialize the socket API before creating any fixture sockets.
         */
        SocketRuntime() {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
                throw std::runtime_error{ "fixture WSAStartup failed" };
            }
        }

        /**
         * @brief Release the fixture's reference after all sockets and workers are gone.
         */
        ~SocketRuntime() { WSACleanup(); }
    };

    using NativeSocket = SOCKET;
    using SocketLength = int;
    constexpr NativeSocket INVALID_NATIVE_SOCKET{ INVALID_SOCKET };
#else
    using NativeSocket = int;
    using SocketLength = socklen_t;
    constexpr NativeSocket INVALID_NATIVE_SOCKET{ -1 };
#endif

    /**
     * @brief Close a socket on all test exit paths.
     */
    struct Socket {
        NativeSocket handle;

        explicit Socket(NativeSocket value) : handle{ value } {
            if (handle == INVALID_NATIVE_SOCKET) {
                throw std::runtime_error{ "fixture socket creation failed" };
            }
        }

        Socket(Socket const&) = delete;
        auto operator=(Socket const&) -> Socket& = delete;

        ~Socket() {
#ifdef _WIN32
            closesocket(handle);
#else
            close(handle);
#endif
        }
    };

    /**
     * @brief Bound all socket waits so fixture teardown cannot hang.
     */
    inline auto readable(NativeSocket socket) -> bool {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(socket, &set);
        timeval timeout{ 0, 50000 };
#ifdef _WIN32
        auto const result{ select(0, &set, nullptr, nullptr, &timeout) };
#else
        auto const result{ select(socket + 1, &set, nullptr, nullptr, &timeout) };
#endif
        if (result < 0) {
            throw std::runtime_error{ "fixture select failed" };
        }
        return result > 0;
    }

    /**
     * @brief Serve persistent connections and both fixed-length and chunked uploads.
     */
    class HttpServer {
    private:
#ifdef _WIN32
        SocketRuntime m_socket_runtime; ///< Initialized before sockets and destroyed after them.
#endif
        Socket m_listener{ socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) };
        std::string m_url;
        std::atomic_int m_connections{};
        std::mutex m_barrier_mutex;              ///< Protects reusable barrier generations.
        std::condition_variable m_barrier_ready; ///< Wakes the current pair together.
        unsigned m_barrier_arrivals{};           ///< Arrivals in the current generation.
        std::size_t m_barrier_generation{};      ///< Incremented after each complete pair.
        std::mutex m_failure_mutex;
        std::exception_ptr m_failure;
        std::vector<std::jthread> m_clients;
        std::jthread m_worker;

    public:
        HttpServer() {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (bind(m_listener.handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(m_listener.handle, 16) != 0) {
                throw std::runtime_error{ "fixture bind/listen failed" };
            }
            SocketLength length{ sizeof(address) };
            if (getsockname(m_listener.handle, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
                throw std::runtime_error{ "fixture getsockname failed" };
            }
            m_url = std::format("http://127.0.0.1:{}", ntohs(address.sin_port));
            m_worker = std::jthread{ [this](std::stop_token stop) {
                try {
                    while (!stop.stop_requested()) {
                        if (!readable(m_listener.handle)) {
                            continue;
                        }
                        auto client{ std::make_shared<Socket>(accept(m_listener.handle, nullptr, nullptr)) };
                        ++m_connections;
                        m_clients.emplace_back([this, client](std::stop_token client_stop) {
                            try {
                                serve(client->handle, client_stop);
                            } catch (...) {
                                recordFailure();
                            }
                        });
                    }
                } catch (...) {
                    recordFailure();
                }
            } };
        }

        ~HttpServer() { stop(); }

        auto Url(std::string_view path = "/hello") const -> mcr::Url { return mcr::Url{ m_url + std::string{ path } }; }

        auto Connections() const -> int { return m_connections.load(); }

        auto Check() -> void {
            stop();
            if (m_failure) {
                std::rethrow_exception(m_failure);
            }
        }

    private:
        /**
         * @brief Pair concurrent requests without letting arrivals from an older group satisfy a new one.
         */
        auto awaitPair(std::stop_token stop) -> bool {
            std::unique_lock lock{ m_barrier_mutex };
            auto const generation = m_barrier_generation;
            if (++m_barrier_arrivals == 2) {
                m_barrier_arrivals = 0;
                ++m_barrier_generation;
                m_barrier_ready.notify_all();
                return true;
            }
            bool const ready = m_barrier_ready.wait_for(lock, 2s, [&] { return m_barrier_generation != generation; });
            if (!ready) {
                --m_barrier_arrivals;
            }
            return ready && !stop.stop_requested();
        }

        auto stop() -> void {
            if (m_worker.joinable()) {
                m_worker.request_stop();
                m_worker.join();
            }
            for (auto& client : m_clients) {
                client.request_stop();
            }
            m_clients.clear();
        }

        auto recordFailure() -> void {
            std::lock_guard lock{ m_failure_mutex };
            if (!m_failure) {
                m_failure = std::current_exception();
            }
        }

        static auto sendAll(NativeSocket socket, std::string_view bytes) -> bool {
            while (!bytes.empty()) {
#ifdef MSG_NOSIGNAL
                constexpr int flags{ MSG_NOSIGNAL };
#else
                constexpr int flags{ 0 };
#endif
                auto const sent{ send(socket, bytes.data(), static_cast<int>(bytes.size()), flags) };
                if (sent <= 0) {
                    return false;
                } // Cancellation and timeout intentionally close sockets.
                bytes.remove_prefix(static_cast<std::size_t>(sent));
            }
            return true;
        }

        static auto receive(NativeSocket socket, std::string& pending, std::stop_token stop) -> bool {
            while (!stop.stop_requested()) {
                if (!readable(socket)) {
                    continue;
                }
                std::array<char, 4096> buffer{};
                auto const count{ recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0) };
                if (count <= 0) {
                    return false;
                }
                pending.append(buffer.data(), static_cast<std::size_t>(count));
                return true;
            }
            return false;
        }

        static auto line(NativeSocket socket, std::string& pending, std::string& output, std::stop_token stop) -> bool {
            while (pending.find("\r\n") == std::string::npos) {
                if (!receive(socket, pending, stop)) {
                    return false;
                }
            }
            auto const end{ pending.find("\r\n") };
            output = pending.substr(0, end);
            pending.erase(0, end + 2);
            return true;
        }

        auto serve(NativeSocket socket, std::stop_token stop) -> void {
            std::string pending;
            while (!stop.stop_requested()) {
                std::string request_line;
                if (!line(socket, pending, request_line, stop)) {
                    return;
                }
                std::istringstream request{ request_line };
                std::string method, target;
                request >> method >> target;
                std::string raw_headers, field;
                while (true) {
                    if (!line(socket, pending, field, stop)) {
                        return;
                    }
                    if (field.empty()) {
                        break;
                    }
                    raw_headers += field + "\r\n";
                }
                auto headers{ mcr::utils::parse_header(raw_headers) };
                if (headers["Expect"] == "100-continue" && !sendAll(socket, "HTTP/1.1 100 Continue\r\n\r\n")) {
                    return;
                }
                std::string body;
                if (headers["Transfer-Encoding"] == "chunked") {
                    while (true) {
                        if (!line(socket, pending, field, stop)) {
                            return;
                        }
                        auto const length{ std::stoull(field, nullptr, 16) };
                        if (length == 0) {
                            do {
                                if (!line(socket, pending, field, stop)) {
                                    return;
                                }
                            } while (!field.empty());
                            break;
                        }
                        while (pending.size() < length + 2) {
                            if (!receive(socket, pending, stop)) {
                                return;
                            }
                        }
                        body.append(pending.data(), static_cast<std::size_t>(length));
                        pending.erase(0, static_cast<std::size_t>(length) + 2);
                    }
                } else {
                    auto const length{ headers["Content-Length"].empty() ? 0ULL : std::stoull(headers["Content-Length"]) };
                    while (pending.size() < length) {
                        if (!receive(socket, pending, stop)) {
                            return;
                        }
                    }
                    body = pending.substr(0, static_cast<std::size_t>(length));
                    pending.erase(0, static_cast<std::size_t>(length));
                }
                std::string status{ "200 OK" }, output{ "Hello session!" }, content_type{ "text/plain" }, extra;
                if (target == "/binary") {
                    output.resize(256);
                    for (std::size_t index{}; index < output.size(); ++index) {
                        output[index] = static_cast<char>(index);
                    }
                } else if (target == "/stream") {
                    if (!sendAll(socket, "HTTP/1.1 200 OK\r\nContent-Length: 307200\r\n\r\n")) {
                        return;
                    }
                    std::string const chunk(1024, 'x');
                    for (int index{}; index < 300 && !stop.stop_requested(); ++index) {
                        if (!sendAll(socket, chunk)) {
                            return;
                        }
                        std::this_thread::sleep_for(5ms);
                    }
                    return;
                } else if (target == "/barrier") {
                    if (!awaitPair(stop)) {
                        status = "503 Requests Were Not Concurrent";
                    }
                } else if (target.starts_with("/auth/")) {
                    auto const& authorization = headers["Authorization"];
                    bool const basic = authorization == "Basic dXNlcjpwYXNzd29yZA==";
                    bool const digest = authorization.starts_with("Digest ") && authorization.contains("username=\"user\"") && authorization.contains("nonce=\"mcr-nonce\"") && authorization.contains("response=\"");
                    bool const ntlm = authorization.starts_with("NTLM TlRMTVNTUAAB");
                    bool const accepted = (target == "/auth/basic" && basic) || (target == "/auth/digest" && digest) || (target == "/auth/mixed" && (basic || digest)) || (target == "/auth/ntlm" && ntlm);
                    if (!accepted) {
                        status = "401 Unauthorized";
                        if (target == "/auth/basic" || target == "/auth/mixed") {
                            extra += "WWW-Authenticate: Basic realm=\"mcr\"\r\n";
                        }
                        if (target == "/auth/digest" || target == "/auth/mixed") {
                            extra += "WWW-Authenticate: Digest realm=\"mcr\", nonce=\"mcr-nonce\", algorithm=MD5, qop=\"auth\"\r\n";
                        }
                        if (target == "/auth/ntlm") {
                            extra += "WWW-Authenticate: NTLM\r\n";
                        }
                    }
                } else if (target.ends_with("/proxy-auth")) {
                    if (headers["Proxy-Authorization"] != "Basic dSRlcjpwQHNz") {
                        status = "407 Proxy Authentication Required";
                        extra = "Proxy-Authenticate: Basic realm=\"local-test\"\r\n";
                    }
                } else if (target.starts_with("/echo")) {
                    output = body;
                } else if (target.starts_with("/redirect/")) {
                    status = target.substr(std::string_view{ "/redirect/" }.size()) + " Redirect";
                    auto const location = headers["X-Redirect-To"].empty() ? std::string{ "/echo" } : headers["X-Redirect-To"];
                    extra = "Location: " + location + "\r\n";
                } else if (target == "/redirect" || target == "/loop") {
                    status = "302 Found";
                    output = "redirect body";
                    extra = std::string{ "X-Intermediate: yes\r\nLocation: " } + (target == "/loop" ? "/loop" : "/hello") + "\r\n";
                } else if (target == "/cookie") {
                    extra = "Set-Cookie: stored=yes; Path=/; HttpOnly\r\n";
                } else if (target == "/sse") {
                    output = "id: 7\ndata: first\n\ndata: second\n\ndata: unfinished";
                } else if (target == "/error") {
                    status = "404 Not Found";
                    output = "missing";
                } else if (target == "/slow") {
                    std::this_thread::sleep_for(150ms);
                } else if (target == "/delay") {
                    // Keep a transfer active without socket traffic; fixture shutdown still ends the wait promptly.
                    for (int index{}; index < 200 && !stop.stop_requested(); ++index) {
                        std::this_thread::sleep_for(50ms);
                    }
                } else if (target == "/bulk") {
                    output.assign(262144, 'b');
                } else if (target == "/partial") {
                    (void)sendAll(socket, "HTTP/1.1 200 OK\r\nContent-Length: 99\r\n\r\nshort");
                    return;
                } else if (target == "/range" && headers["Range"] == "bytes=2-5") {
                    status = "206 Partial Content";
                    output = "2345";
                }
                for (std::string const name : { "Cookie", "User-Agent", "Authorization", "Proxy-Authorization", "X-Custom", "X-Empty", "Accept-Encoding", "Content-Type", "Transfer-Encoding", "Range", "Expect" }) {
                    extra += "X-Request-" + name + ": " + headers[name] + "\r\n";
                }
                extra += std::format("X-Method: {}\r\nX-Target: {}\r\n", method, target);
                sockaddr_in peer{};
                SocketLength peer_length{ sizeof(peer) };
                std::array<char, INET_ADDRSTRLEN> peer_ip{};
                if (getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &peer_length) != 0 || !inet_ntop(AF_INET, &peer.sin_addr, peer_ip.data(), static_cast<SocketLength>(peer_ip.size()))) {
                    throw std::runtime_error{ "fixture getpeername/inet_ntop failed" };
                }
                extra += std::format("X-Client-IP: {}\r\nX-Client-Port: {}\r\n", peer_ip.data(), ntohs(peer.sin_port));
                auto response{ std::format("HTTP/1.1 {}\r\nContent-Length: {}\r\nContent-Type: {}\r\nConnection: keep-alive\r\n{}\r\n", status, output.size(), content_type, extra) };
                if (method != "HEAD") {
                    response += output;
                }
                if (!sendAll(socket, response)) {
                    return;
                }
            }
        }
    };

} // namespace mcr::test
