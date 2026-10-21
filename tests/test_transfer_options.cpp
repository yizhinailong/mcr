/**
 * @file test_transfer_options.cpp
 * @brief Exercise network binding, transfer rate limits, ranges, and authentication selection locally.
 */
#include <curl/curl.h>

#include "fixtures/http_server.hpp"
import std;
import mcr;

namespace {
    using namespace std::chrono_literals;
    using mcr::test::HttpServer;

    /**
     * @brief Fail a real-request assertion with its relevant diagnostic.
     */
    auto require(bool condition, std::string_view message) -> void {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    /**
     * @brief Prepare an isolated loopback session without environment proxy routing.
     */
    auto session(HttpServer const& server, std::string_view path) -> std::shared_ptr<mcr::Session> {
        auto result = mcr::Session::Create().value();
        result->SetUrl(server.Url(path)).value();
        result->SetProxies({ { "http", "" }, { "no_proxy", "*" } }).value();
        result->SetTimeout(mcr::options::Timeout{ 5s }).value();
        return result;
    }

    /**
     * @brief Reserve a source port so the client must use the configured fallback range.
     */
    struct ReservedPort {
        mcr::test::Socket socket{ ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) }; ///< Bound blocker.
        std::uint16_t port{};                                                    ///< Assigned source port, unavailable while the blocker lives.

        /**
         * @brief Bind an exclusive loopback port without enabling address reuse.
         */
        ReservedPort() {
#ifdef _WIN32
            int exclusive{ 1 };
            require(setsockopt(socket.handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char const*>(&exclusive), sizeof(exclusive)) == 0, "source-port blocker must be exclusive");
#endif
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            require(bind(socket.handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "source-port blocker must bind");
            mcr::test::SocketLength length{ sizeof(address) };
            require(getsockname(socket.handle, reinterpret_cast<sockaddr*>(&address), &length) == 0, "source-port blocker must report its assigned port");
            port = ntohs(address.sin_port);
        }
    };

    /**
     * @brief Compare the actual client binding reported independently by curl and the server.
     */
    auto binding_and_ranges(HttpServer const& server) -> void {
        auto client = session(server, "/echo");
        client->SetInterface(mcr::options::Interface{ "mcr-nonexistent-interface" }).value();
        require(client->Get().value().error.code == mcr::ErrorCode::INTERFACE_FAILED, "an unavailable interface must fail a real connection");
        client->SetInterface(mcr::options::Interface{ "127.0.0.1" }).value();
        auto blocker = std::make_unique<ReservedPort>();
        while (blocker->port > 65527) {
            blocker = std::make_unique<ReservedPort>();
        }
        client->SetLocalPort(mcr::options::LocalPort{ blocker->port }).value();
        client->SetLocalPortRange(mcr::options::LocalPortRange{ 8 }).value();
        client->SetMultiRange(mcr::options::MultiRange{ mcr::options::Range{ 0, 1 }, mcr::options::Range{ 4, 5 } }).value();
        auto response = client->Get().value();
        require(!response.error && response.status_code == 200, std::format("local binding failed: {}", response.error.message));
        long port{};
        char* ip{};
        auto* handle = client->GetCurlHolder()->handle;
        require(curl_easy_getinfo(handle, CURLINFO_LOCAL_PORT, &port) == CURLE_OK && curl_easy_getinfo(handle, CURLINFO_LOCAL_IP, &ip) == CURLE_OK && ip, "curl must expose the actual local endpoint");
        require(port > blocker->port && port < blocker->port + 8 && std::string_view{ ip } == "127.0.0.1", "the blocked first port must force fallback within the requested range and interface");
        require(response.header.at("X-Client-Port") == std::to_string(port) && response.header.at("X-Client-IP") == ip, "the server must observe the same bound address and port");
        require(response.header.at("X-Request-Range") == "bytes=0-1, 4-5", "all configured byte ranges must reach the server");
    }

    /**
     * @brief Exercise low-speed cancellation and independent upload/download throttles.
     */
    auto rates(HttpServer const& server) -> void {
        auto client = session(server, "/stream");
        client->SetLowSpeed({ 1024 * 1024, 1s }).value();
        for (auto const invalid : { mcr::options::LowSpeed{ -1, 1s }, mcr::options::LowSpeed{ 0, -1s } }) {
            auto status = client->SetLowSpeed(invalid);
            require(!status && status.error().code == mcr::ErrorCode::BAD_FUNCTION_ARGUMENT, "negative low-speed inputs must be rejected before either curl option changes");
        }
        if constexpr (sizeof(std::chrono::seconds::rep) > sizeof(long)) {
            auto too_large = std::chrono::seconds{ (std::numeric_limits<long>::max)() };
            ++too_large;
            auto status = client->SetLowSpeed({ 0, too_large });
            require(!status && status.error().code == mcr::ErrorCode::BAD_FUNCTION_ARGUMENT, "low-speed durations must not narrow into curl's long");
        }
        auto slow = client->Get().value();
        require(slow.error.code == mcr::ErrorCode::OPERATION_TIMEDOUT && slow.error.message.contains("Operation too slow"), "the retained low-speed policy must cancel a slow real response");
        client->SetLowSpeed({ 0, 0s }).value();
        client->SetUrl(server.Url("/bulk")).value();
        require(client->Get().value().text.size() == 262144, "the unthrottled fixture must deliver its whole response");
        client->SetTimeout(mcr::options::Timeout{ 1200ms }).value();
        client->SetLimitRate({ 16384, 0 }).value();
        auto down = client->Get().value();
        require(down.error.code == mcr::ErrorCode::OPERATION_TIMEDOUT && down.downloaded_bytes < 131072, "download throttling must keep a real transfer below its full body before timeout");
        client->SetUrl(server.Url("/echo")).value();
        client->SetBody(mcr::Body{ std::string(262144, 'u') }).value();
        client->SetLimitRate({ 0, 8192 }).value();
        auto up = client->Post().value();
        require(up.error.code == mcr::ErrorCode::OPERATION_TIMEDOUT && up.uploaded_bytes > 0 && up.uploaded_bytes < 262144, "upload throttling must limit bytes actually sent to the server");
        client->SetLimitRate({ 0, 0 }).value();
        client->SetTimeout(mcr::options::Timeout{ 5s }).value();
        require(client->Post().value().text.size() == 262144, "clearing rate limits must restore a complete binary-safe upload");
    }

    /**
     * @brief Check auth masks via server challenges and selected Authorization schemes.
     * @note The fixture checks scheme negotiation, not Digest hashing or a complete NTLM/Kerberos login.
     */
    auto authentication(HttpServer const& server) -> void {
        using Mode = mcr::options::AuthMode;
        auto perform = [&](Mode mode, std::string_view path) {
            auto client = session(server, path);
            client->SetAuth({ "user", "password", mode }).value();
            return client->Get().value();
        };
        for (Mode mode : { Mode::BASIC, Mode::ANY }) {
            auto response = perform(mode, "/auth/basic");
            require(response.status_code == 200 && response.header.at("X-Request-Authorization") == "Basic dXNlcjpwYXNzd29yZA==", "BASIC and ANY must select Basic when it is the available scheme");
        }
        auto safe = perform(Mode::ANYSAFE, "/auth/basic");
        require(safe.status_code == 401 && safe.header.at("X-Request-Authorization").empty(), "ANYSAFE must exclude Basic on a real challenge");
        for (Mode mode : { Mode::DIGEST, Mode::ANY, Mode::ANYSAFE }) {
            auto response = perform(mode, "/auth/mixed");
            require(response.status_code == 200 && response.header.at("X-Request-Authorization").starts_with("Digest "), "Digest-capable masks must select Digest from mixed server challenges");
        }
        auto const features = curl_version_info(CURLVERSION_NOW)->features;
        for (auto const& [mode, feature, path, prefix] : {
                 std::tuple{ Mode::NTLM, CURL_VERSION_NTLM, "/auth/ntlm", "NTLM " },
                 std::tuple{ Mode::NEGOTIATE, CURL_VERSION_SPNEGO, "/auth/mixed", "Negotiate " } }) {
            auto client = session(server, path);
            auto configured = client->SetAuth({ "user", "password", mode });
            if (!(features & feature)) {
                require(!configured && configured.error().code == mcr::ErrorCode::NOT_BUILT_IN, "an unavailable auth mechanism must report its backend capability");
                continue;
            }
            configured.value();
            auto response = client->Get().value();
            if (mode == Mode::NTLM) {
                require(response.status_code == 200 && response.header.at("X-Request-Authorization").starts_with(prefix), "the NTLM mask must send its Type 1 negotiation to the server");
            } else {
                auto const authorization = response.header.find("X-Request-Authorization");
                require(response.status_code == 401 && authorization != response.header.end() && (authorization->second.empty() || authorization->second.starts_with(prefix)), std::format("NEGOTIATE must not fall back to Basic or Digest: HTTP {}, {}: {}", response.status_code, mcr::to_string(response.error.code), response.error.message));
            }
        }
    }
} // namespace

/**
 * @brief Run every option against controlled sockets and stop fixture workers on all failure paths.
 */
auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        HttpServer server;
        try {
            binding_and_ranges(server);
            rates(server);
            authentication(server);
        } catch (std::exception const& error) {
            std::println("test_transfer_options: {}", error.what());
            passed = false;
        }
        server.Check();
    } catch (std::exception const& error) {
        std::println("test_transfer_options fixture: {}", error.what());
        passed = false;
    }
    curl_global_cleanup();
    return passed ? 0 : 1;
}
