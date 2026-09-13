/**
 * @file test_connection_pool.cpp
 * @brief Verify shared cache lifetime, HTTP connection reuse, and initialization failures.
 */
#ifdef _WIN32
    #include <winsock2.h>
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
import mcr.curlholder;

static_assert(std::is_nothrow_copy_constructible_v<mcr::ConnectionPool>);
static_assert(!std::is_copy_assignable_v<mcr::ConnectionPool>);
static_assert(!std::is_move_assignable_v<mcr::ConnectionPool>);
static_assert(std::is_nothrow_destructible_v<mcr::ConnectionPool>);
static_assert(std::is_same_v<decltype(std::declval<mcr::ConnectionPool const&>().SetupHandler(nullptr)), void>);

namespace {

#ifdef _WIN32
    using NativeSocket = SOCKET;
    using SocketLength = int;
    constexpr NativeSocket INVALID_NATIVE_SOCKET{ INVALID_SOCKET };
#else
    using NativeSocket = int;
    using SocketLength = socklen_t;
    constexpr NativeSocket INVALID_NATIVE_SOCKET{ -1 };
#endif

    /**
     * @brief Close a fixture socket on every exit path.
     */
    struct Socket {
        NativeSocket handle;

        explicit Socket(NativeSocket value) : handle{ value } {
            if (handle == INVALID_NATIVE_SOCKET) {
                throw std::runtime_error{ "fixture socket creation failed" };
            }
        }

        Socket(Socket const&)                    = delete;
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
     * @brief Serve sequential HTTP/1.1 requests over persistent loopback connections.
     */
    class HttpServer {
    private:
        Socket             m_listener{ socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) };
        std::string        m_url;
        std::atomic_int    m_connections{ 0 };
        std::exception_ptr m_failure;
        std::jthread       m_worker; ///< Stops and joins before any socket or worker state is destroyed.

    public:
        HttpServer() {
            sockaddr_in address{};
            address.sin_family      = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port        = 0;
            if (bind(m_listener.handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(m_listener.handle, 8) != 0) {
                throw std::runtime_error{ "fixture bind/listen failed" };
            }
            SocketLength length{ sizeof(address) };
            if (getsockname(m_listener.handle, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
                throw std::runtime_error{ "fixture getsockname failed" };
            }
            m_url    = std::format("http://127.0.0.1:{}/pool", ntohs(address.sin_port));
            m_worker = std::jthread{ [this](std::stop_token stop) {
                try {
                    while (!stop.stop_requested()) {
                        if (!readable(m_listener.handle)) {
                            continue;
                        }
                        Socket client{ accept(m_listener.handle, nullptr, nullptr) };
                        ++m_connections;
                        serve(client.handle, stop);
                    }
                } catch (...) {
                    m_failure = std::current_exception();
                }
            } };
        }

        auto Url() const -> std::string const& { return m_url; }

        auto Connections() const -> int { return m_connections.load(); }

        /**
         * @brief Join the server and surface worker failures after all pools have been released.
         */
        auto Stop() -> void {
            m_worker.request_stop();
            m_worker.join();
            if (m_failure) {
                std::rethrow_exception(m_failure);
            }
        }

    private:
        static auto readable(NativeSocket socket) -> bool {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(socket, &read_set);
            timeval timeout{ 0, 100000 };
#ifdef _WIN32
            auto const result{ select(0, &read_set, nullptr, nullptr, &timeout) };
#else
            auto const result{ select(socket + 1, &read_set, nullptr, nullptr, &timeout) };
#endif
            if (result < 0) {
                throw std::runtime_error{ "fixture select failed" };
            }
            return result > 0;
        }

        static auto serve(NativeSocket socket, std::stop_token stop) -> void {
            std::string            request;
            std::array<char, 2048> buffer{};
            while (!stop.stop_requested()) {
                if (!readable(socket)) {
                    continue;
                }
                auto const received{ recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0) };
                if (received == 0) {
                    return;
                }
                if (received < 0) {
                    throw std::runtime_error{ "fixture recv failed" };
                }
                request.append(buffer.data(), static_cast<std::size_t>(received));
                auto const end{ request.find("\r\n\r\n") };
                if (end == std::string::npos) {
                    continue;
                }
                request.erase(0, end + 4);
                std::string_view response{ "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: keep-alive\r\n\r\nHello pool!" };
                while (!response.empty()) {
                    auto const sent{ send(socket, response.data(), static_cast<int>(response.size()), 0) };
                    if (sent <= 0) {
                        throw std::runtime_error{ "fixture send failed" };
                    }
                    response.remove_prefix(static_cast<std::size_t>(sent));
                }
            }
        }
    };

    std::atomic<std::ptrdiff_t> g_live_allocations{ 0 };
    std::atomic<std::ptrdiff_t> g_allocation_budget{ -1 };
    std::atomic<std::size_t>    g_allocation_attempts{ 0 };

    auto fail_allocation() noexcept -> bool {
        ++g_allocation_attempts;
        if (g_allocation_budget.load() < 0) {
            return false;
        }
        if (g_allocation_budget.load() == 0) {
            return true;
        }
        --g_allocation_budget;
        return false;
    }

    auto tracked_malloc(std::size_t size) noexcept -> void* {
        if (fail_allocation()) {
            return nullptr;
        }
        auto* result{ std::malloc(size) };
        if (result) {
            ++g_live_allocations;
        }
        return result;
    }

    auto tracked_free(void* pointer) noexcept -> void {
        if (pointer) {
            --g_live_allocations;
        }
        std::free(pointer);
    }

    auto tracked_realloc(void* pointer, std::size_t size) noexcept -> void* {
        if (size == 0) {
            tracked_free(pointer);
            return nullptr;
        }
        if (!pointer) {
            return tracked_malloc(size);
        }
        return fail_allocation() ? nullptr : std::realloc(pointer, size);
    }

    auto tracked_strdup(char const* input) noexcept -> char* {
        auto const length{ std::strlen(input) + 1 };
        auto*      result{ static_cast<char*>(tracked_malloc(length)) };
        if (result) {
            std::memcpy(result, input, length);
        }
        return result;
    }

    auto tracked_calloc(std::size_t count, std::size_t size) noexcept -> void* {
        if (fail_allocation()) {
            return nullptr;
        }
        auto* result{ std::calloc(count, size) };
        if (result) {
            ++g_live_allocations;
        }
        return result;
    }

    struct FailAfterAllocations {
        explicit FailAfterAllocations(std::size_t budget) { g_allocation_budget.store(static_cast<std::ptrdiff_t>(budget)); }

        ~FailAfterAllocations() { g_allocation_budget.store(-1); }
    };

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_connection_pool: {}", message);
        }
        return condition;
    }

    auto check_failures() -> bool {
        auto const before{ g_live_allocations.load() };
        g_allocation_attempts.store(0);
        { mcr::ConnectionPool pool; }
        auto const allocations{ g_allocation_attempts.load() };
        bool       passed{ check(allocations > 0 && g_live_allocations.load() == before, "a pool must allocate and then release its curl state") };
        bool       saw_init_failure{ false };
        bool       saw_option_failure{ false };
        for (std::size_t budget{ 0 }; budget < allocations; ++budget) {
            {
                FailAfterAllocations fail{ budget };
                try {
                    mcr::ConnectionPool pool;
                } catch (std::runtime_error const& error) {
                    std::string_view message{ error.what() };
                    saw_init_failure   |= message.contains("curl_share_init");
                    saw_option_failure |= message.contains("CURLSHOPT_SHARE");
                    passed             &= check(message.contains("mcr::ConnectionPool"), "curl allocation errors must identify the pool operation");
                }
            }
            passed &= check(g_live_allocations.load() == before, "failure at each allocation point must release partially initialized curl state");
        }
        passed &= check(saw_init_failure && saw_option_failure, "failure injection must exercise both share initialization and cache configuration");
        mcr::ConnectionPool recovered;
        try {
            recovered.SetupHandler(nullptr);
            passed &= check(false, "a null easy handle must be rejected");
        } catch (std::invalid_argument const&) {
        }
        mcr::curl::CurlHolder holder;
        recovered.SetupHandler(holder.handle);
        return passed;
    }

    auto check_attached_lifetime() -> bool {
        std::optional<mcr::ConnectionPool> original{ std::in_place };
        mcr::ConnectionPool const          survivor{ *original };
        mcr::curl::CurlHolder              holder;
        original->SetupHandler(holder.handle);
        original.reset();
        survivor.SetupHandler(holder.handle);
        bool passed{ check(curl_easy_setopt(holder.handle, CURLOPT_URL, "http://[") == CURLE_OK && curl_easy_perform(holder.handle) == CURLE_URL_MALFORMAT, "a retained copy must keep callbacks valid after the original pool is destroyed") };
        {
            mcr::ConnectionPool replacement;
            replacement.SetupHandler(holder.handle);
            passed &= check(curl_easy_setopt(holder.handle, CURLOPT_SHARE, static_cast<CURLSH*>(nullptr)) == CURLE_OK, "an idle handle must support replacement and explicit detachment before pool destruction");
        }
        survivor.SetupHandler(holder.handle);
        return passed;
    }

    auto request(std::string const& url, mcr::ConnectionPool const* pool, long expected_connections) -> bool {
        mcr::curl::CurlHolder holder;
        if (pool) {
            pool->SetupHandler(holder.handle);
        }
        std::string         body;
        curl_write_callback writer{ +[](char* bytes, std::size_t size, std::size_t count, void* userdata) noexcept -> std::size_t {
            auto const length{ size * count };
            try {
                static_cast<std::string*>(userdata)->append(bytes, length);
                return length;
            } catch (...) {
                return 0;
            }
        } };
        if (!check(curl_easy_setopt(holder.handle, CURLOPT_URL, url.c_str()) == CURLE_OK && curl_easy_setopt(holder.handle, CURLOPT_PROXY, "") == CURLE_OK && curl_easy_setopt(holder.handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1) == CURLE_OK && curl_easy_setopt(holder.handle, CURLOPT_TIMEOUT_MS, 3000L) == CURLE_OK && curl_easy_setopt(holder.handle, CURLOPT_NOSIGNAL, 1L) == CURLE_OK && curl_easy_setopt(holder.handle, CURLOPT_WRITEFUNCTION, writer) == CURLE_OK && curl_easy_setopt(holder.handle, CURLOPT_WRITEDATA, static_cast<void*>(&body)) == CURLE_OK, "request fixture options must be configured")) {
            return false;
        }
        auto const result{ curl_easy_perform(holder.handle) };
        if (!check(result == CURLE_OK, curl_easy_strerror(result))) {
            return false;
        }
        long status{ 0 };
        long connections{ -1 };
        return check(curl_easy_getinfo(holder.handle, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK && status == 200 && body == "Hello pool!", "the local fixture must return a complete HTTP response") &&
               check(curl_easy_getinfo(holder.handle, CURLINFO_NUM_CONNECTS, &connections) == CURLE_OK && connections == expected_connections, "each transfer must report the expected new-connection count");
    }

    auto check_connection_reuse() -> bool {
        HttpServer server;
        bool       passed{ true };
        for (int i{ 0 }; i < 3; ++i) {
            passed &= request(server.Url(), nullptr, 1);
        }
        passed &= check(server.Connections() == 3, "three independent easy handles must open three connections");
        {
            auto survivor = [&] {
                mcr::ConnectionPool original;
                passed &= request(server.Url(), &original, 1);
                return mcr::ConnectionPool{ original };
            }();
            passed &= request(server.Url(), &survivor, 0);
            mcr::ConnectionPool const from_rvalue{ std::move(survivor) };
            passed &= request(server.Url(), &survivor, 0);
            passed &= request(server.Url(), &from_rvalue, 0);
            passed &= check(server.Connections() == 4, "four pooled requests across copies must reuse one TCP connection");
        }
        server.Stop();
        return passed;
    }

} // namespace

int main() {
    if (curl_global_init_mem(CURL_GLOBAL_DEFAULT, tracked_malloc, tracked_free, tracked_realloc, tracked_strdup, tracked_calloc) != CURLE_OK) {
        std::println("test_connection_pool: curl global initialization failed");
        return 1;
    }
    bool passed{ true };
    try {
        passed &= check_failures();
        passed &= check_attached_lifetime();
        passed &= check_connection_reuse();
    } catch (std::exception const& error) {
        std::println("test_connection_pool: unexpected exception: {}", error.what());
        passed = false;
    }
    curl_global_cleanup();
    passed &= check(g_live_allocations.load() == 0, "the final pool copy must release all curl resources after easy handles and global state are cleaned up");
    if (!passed) {
        return 1;
    }
    std::println("test_connection_pool: ok");
    return 0;
}
