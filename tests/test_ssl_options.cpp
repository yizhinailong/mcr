/**
 * @file test_ssl_options.cpp
 * @brief Verify TLS option ownership and local HTTPS verification with generated certificates.
 */
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <cerrno>

    #include <fcntl.h>
    #include <signal.h>
    #include <spawn.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif
#include <curl/curl.h>
import std;
import mcr;

namespace {
    using namespace std::chrono_literals;

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_ssl_options: {}", message);
        }
        return condition;
    }

    auto read_file(std::filesystem::path const& path) -> std::string {
        std::ifstream file{ path, std::ios::binary };
        if (!file) {
            throw std::runtime_error{ "TLS fixture file could not be opened." };
        }
        return { std::istreambuf_iterator<char>{ file }, {} };
    }

    auto check_options() -> bool {
        auto defaults{ mcr::options::Ssl() };
        bool passed{ check(defaults.verify_host && defaults.verify_peer && !defaults.verify_status && defaults.enable_alpn && defaults.session_id_cache, "default TLS options must retain certificate and hostname verification") };
        passed &= check(mcr::options::VerifySsl{}.verify && !bool(mcr::options::VerifySsl{ false }), "combined verification must default to enabled");
        auto options{ mcr::options::Ssl(mcr::options::ssl::DerCert{ "cert.der" }, mcr::options::ssl::DerKey{ "key.der", "password" }, mcr::options::ssl::CaInfo{ "ca.pem" }, mcr::options::ssl::CaPath{ "ca-directory" }, mcr::options::ssl::Crl{ "revoked.pem" }, mcr::options::ssl::TLSv1_2{}, mcr::options::ssl::MaxTLSv1_3{}, mcr::options::ssl::ALPN{ false }, mcr::options::ssl::VerifyHost{ false }, mcr::options::ssl::VerifyPeer{ false }, mcr::options::ssl::VerifyStatus{ true }, mcr::options::ssl::SessionIdCache{ false }, mcr::options::ssl::NoRevoke{ true }, mcr::options::ssl::Ciphers{ "cipher" }, mcr::options::ssl::TLS13_Ciphers{ "cipher13" }) };
        passed &= check(options.cert_file == "cert.der" && options.key_file == "key.der" && options.cert_type == "DER" && options.key_type == "DER" && options.key_pass == "password", "certificate/key formats, paths, and passwords must be owned");
        passed &= check(options.ssl_version == CURL_SSLVERSION_TLSv1_2 && options.max_version == CURL_SSLVERSION_MAX_TLSv1_3 && !options.enable_alpn && !options.verify_peer && !options.verify_host && options.verify_status && !options.session_id_cache && options.ssl_no_revoke, "TLS bounds and boolean preferences must be stored independently");
        std::string const binary{ "certificate\0bytes", 17 };
        options.SetOption(mcr::options::ssl::DerBlob{ binary });
        options.SetOption(mcr::options::ssl::KeyBlob{ binary, "new-password" });
        options.SetOption(mcr::options::ssl::CaInfoBlob{ "CA bytes" });
        passed &= check(options.cert_file.empty() && options.key_file.empty() && options.ca_info.empty() && std::string_view{ options.cert_blob } == binary && std::string_view{ options.key_blob } == binary && options.key_pass == "new-password", "blob options must replace file sources and preserve binary bytes");
        options.SetOption(mcr::options::ssl::PemCert{ "new.pem" });
        options.SetOption(mcr::options::ssl::PemKey{ "new-key.pem" });
        options.SetOption(mcr::options::ssl::CaBuffer{ "buffer" });
        passed &= check(options.cert_blob.empty() && options.key_blob.empty() && options.key_pass.empty() && options.cert_type == "PEM" && options.ca_info_blob.empty() && options.ca_buffer == "buffer", "replacement options must clear obsolete sources and passphrases");
        auto session_owner = mcr::Session::Create().value();
        auto& session = *session_owner;
        session.SetOption(defaults).value();
        session.SetOption(mcr::options::VerifySsl{ false }).value();
        session.SetVerifySsl({}).value();
        session.SetSslOptions(mcr::options::Ssl(mcr::options::ssl::TLSv1_2{}, mcr::options::ssl::MaxTLSv1_2{})).value();
        {
            auto const failure = session.SetSslOptions(mcr::options::Ssl(mcr::options::ssl::SslFastStart{ true }));
            passed &= check(!failure && failure.error().code == mcr::ErrorCode::NOT_BUILT_IN, "failure must return the expected error code");
        }
        return passed;
    }

    /**
     * @brief Identify the process when creating a unique fixture directory.
     */
    auto process_id() -> unsigned long {
#ifdef _WIN32
        return GetCurrentProcessId();
#else
        return static_cast<unsigned long>(getpid());
#endif
    }

    /**
     * @brief Own only this test's generated fixture directory.
     */
    struct TempDirectory {
        std::filesystem::path path{ std::filesystem::temp_directory_path() / std::filesystem::path{ u8"mcr-证书" } };

        TempDirectory() {
            path += std::format("-{}-{}", process_id(), std::chrono::steady_clock::now().time_since_epoch().count());
            if (!std::filesystem::create_directory(path)) {
                throw std::runtime_error{ "Could not create a unique TLS fixture directory." };
            }
        }

        ~TempDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    /**
     * @brief Own a hidden fixture process and its output log.
     */
    struct ChildProcess {
#ifdef _WIN32
        PROCESS_INFORMATION info{};
        HANDLE log{ INVALID_HANDLE_VALUE };
        std::filesystem::path directory;

        ~ChildProcess() {
            if (info.hProcess) {
                {
                    std::ofstream stop{ directory / "stop" };
                }
                if (WaitForSingleObject(info.hProcess, 5000) != WAIT_OBJECT_0) {
                    TerminateProcess(info.hProcess, 0);
                    WaitForSingleObject(info.hProcess, 5000);
                }
                CloseHandle(info.hProcess);
            }
            if (info.hThread) {
                CloseHandle(info.hThread);
            }
            if (log != INVALID_HANDLE_VALUE) {
                CloseHandle(log);
            }
        }

        auto Start(
            std::filesystem::path const& script,
            std::filesystem::path const& directory
        ) -> bool {
            this->directory = directory;
            SECURITY_ATTRIBUTES attributes{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
            log = CreateFileW((directory / "server.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (log == INVALID_HANDLE_VALUE) {
                throw std::runtime_error{ "Could not open TLS fixture log." };
            }
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            startup.wShowWindow = SW_HIDE;
            startup.hStdOutput = log;
            startup.hStdError = log;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            auto command{ std::format(L"pwsh.exe -NoLogo -NoProfile -NonInteractive -File \"{}\" -Directory \"{}\"", script.wstring(), directory.wstring()) };
            if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
                if (GetLastError() == ERROR_FILE_NOT_FOUND) {
                    return false;
                }
                throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), "Could not start TLS fixture" };
            }
            return true;
        }

        /**
         * @brief Poll child exit while allowing a bounded readiness wait.
         */
        auto WaitForExit(std::chrono::milliseconds delay) -> std::optional<int> {
            auto const state = WaitForSingleObject(info.hProcess, static_cast<DWORD>(delay.count()));
            if (state == WAIT_OBJECT_0) {
                DWORD exit_code{};
                if (!GetExitCodeProcess(info.hProcess, &exit_code)) {
                    throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), "Could not query TLS fixture exit code" };
                }
                return static_cast<int>(exit_code);
            }
            if (state == WAIT_FAILED) {
                throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), "Could not wait for TLS fixture" };
            }
            return {};
        }
#else
        pid_t process{};                 ///< Owned Python child, zero after it has been reaped.
        std::filesystem::path directory; ///< Parent-owned fixture directory.

        /**
         * @brief Request graceful shutdown, then reap the child even if it cannot finish normally.
         */
        ~ChildProcess() {
            if (process > 0) {
                std::ofstream{ directory / "stop" };
                auto const deadline = std::chrono::steady_clock::now() + 5s;
                try {
                    while (process > 0 && std::chrono::steady_clock::now() < deadline) {
                        (void)WaitForExit(20ms);
                    }
                } catch (...) {}
                if (process > 0) {
                    (void)kill(process, SIGKILL);
                    while (waitpid(process, nullptr, 0) < 0 && errno == EINTR) {}
                }
            }
        }

        /**
         * @brief Start Python without a shell and redirect fixture diagnostics to its log.
         */
        auto Start(
            std::filesystem::path const& script,
            std::filesystem::path const& fixture_directory
        ) -> bool {
            directory = fixture_directory;
            auto const script_text = script.string();
            auto const directory_text = directory.string();
            auto const log_text = (directory / "server.log").string();
            posix_spawn_file_actions_t actions;
            int result = posix_spawn_file_actions_init(&actions);
            if (result != 0) {
                throw std::system_error{ result, std::system_category(), "Could not initialize TLS process actions" };
            }
            result = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log_text.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (result == 0) {
                result = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
            }
            std::array<char*, 5> arguments{ const_cast<char*>("python3"), const_cast<char*>(script_text.c_str()), const_cast<char*>("--directory"), const_cast<char*>(directory_text.c_str()), nullptr };
            if (result == 0) {
                result = posix_spawnp(&process, "python3", &actions, nullptr, arguments.data(), environ);
            }
            (void)posix_spawn_file_actions_destroy(&actions);
            if (result != 0) {
                process = 0;
                throw std::system_error{ result, std::system_category(), "Could not start Python TLS fixture" };
            }
            return true;
        }

        /**
         * @brief Poll and reap a finished child, retaining exclusive ownership of its process ID.
         */
        auto WaitForExit(std::chrono::milliseconds delay) -> std::optional<int> {
            int status{};
            auto result = waitpid(process, &status, WNOHANG);
            if (result < 0) {
                if (errno == EINTR) {
                    return {};
                }
                throw std::system_error{ errno, std::system_category(), "Could not poll TLS fixture" };
            }
            if (result == process) {
                process = 0;
                return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
            std::this_thread::sleep_for(delay);
            return {};
        }
#endif
    };

    struct HttpsFixture {
        TempDirectory directory;
        ChildProcess child;
        unsigned short port{};
        unsigned short mutual_port{};
        bool available{ true }; ///< False only when the optional Windows PowerShell executable is absent.

        /**
         * @brief Wait for generated certificates and bound listeners on either supported fixture backend.
         * @throws std::runtime_error If the fixture exits early or fails to become ready within 60 seconds.
         * @throws std::system_error If waiting for the child process or querying its exit code fails.
         */
        HttpsFixture() {
#ifdef _WIN32
            constexpr auto SCRIPT = "https_server.ps1";
#else
            constexpr auto SCRIPT = "https_server.py";
#endif
            auto const script{ std::filesystem::absolute(std::filesystem::path{ __FILE__ }.parent_path() / "fixtures" / SCRIPT) };
            available = child.Start(script, directory.path);
            if (!available) {
                return;
            }
            auto const deadline{ std::chrono::steady_clock::now() + 60s };
            while (std::chrono::steady_clock::now() < deadline) {
                std::ifstream ready{ directory.path / "ready" };
                if (ready >> port >> mutual_port) {
                    return;
                }
                if (auto const exit_code = child.WaitForExit(20ms)) {
                    throw std::runtime_error{ std::format("TLS fixture exited before readiness (exit {}):\n{}", *exit_code, read_file(directory.path / "server.log")) };
                }
            }
            throw std::runtime_error{ "TLS fixture startup timed out after 60 seconds:\n" + read_file(directory.path / "server.log") };
        }

        auto Url(bool mutual = false, std::string_view hostname = "localhost") const -> mcr::Url {
            return mcr::Url{ std::format("https://{}:{}/hello", hostname, mutual ? mutual_port : port) };
        }

        auto Path(std::string_view name) const -> std::filesystem::path { return directory.path / name; }
    };

    auto check_https() -> bool {
        HttpsFixture fixture;
        if (!fixture.available) {
            std::println("test_ssl_options: SKIP HTTPS checks: pwsh is not installed.");
            return true;
        }
        auto const* version{ curl_version_info(CURLVERSION_NOW) };
        std::println("test_ssl_options: TLS backend {}", version->ssl_version ? version->ssl_version : "none");
        auto session_owner = mcr::Session::Create().value();
        auto& session = *session_owner;
        session.SetUrl(fixture.Url()).value();
        session.SetProxies({ { "https", "" }, { "no_proxy", "*" } })
            .value();
        session.SetTimeout(mcr::options::Timeout{ 3000ms }).value();
        bool passed{ check(bool(session.Get().value().error), "untrusted generated certificates must be rejected by default") };
        session.SetVerifySsl(false).value();
        auto unverified{ session.Get().value() };
        if (unverified.error) {
            std::println("test_ssl_options: handshake error {}: {}", mcr::to_string(unverified.error.code), unverified.error.message);
        }
        passed &= check(unverified.text == "TLS works", "explicit VerifySsl(false) must disable both checks");
        session.SetVerifySsl(true).value();
        passed &= check(bool(session.Get().value().error), "reenabling verification must reject the untrusted peer again");
        // Generated certificates have no online revocation service. Keep chain and hostname
        // verification enabled while explicitly disabling revocation checks for this fixture.
        auto local_tls = [](auto&&... options) {
            return mcr::options::Ssl(mcr::options::ssl::NoRevoke{ true }, std::forward<decltype(options)>(options)...);
        };
        auto trust{ local_tls(mcr::options::ssl::CaInfo{ fixture.Path("ca.pem") }, mcr::options::ssl::TLSv1_2{}, mcr::options::ssl::MaxTLSv1_2{}) };
        session.SetSslOptions(trust).value();
        auto trusted{ session.Get().value() };
        if (trusted.error) {
            std::println("test_ssl_options: trusted handshake error {}: {}", mcr::to_string(trusted.error.code), trusted.error.message);
        }
        passed &= check(!trusted.error && trusted.text == "TLS works" && !trusted.GetCertInfos().empty(), "CA files and TLS 1.2 bounds must permit a verified connection and capture certificates");
        session.SetSslOptions(local_tls(mcr::options::ssl::CaInfo{ fixture.Path("ca.pem") }, mcr::options::ssl::TLSv1_3{})).value();
        passed &= check(bool(session.Get().value().error), "a TLS 1.3 minimum must reject the TLS 1.2-only fixture");
        session.SetSslOptions(trust).value();
        passed &= check(session.Get().value().text == "TLS works", "restoring TLS 1.2 bounds must permit a fresh verified handshake");
        session.SetUrl(fixture.Url(false, "127.0.0.1")).value();
        passed &= check(bool(session.Get().value().error), "trusted certificates must still fail hostname mismatch");
        trust.SetOption(mcr::options::ssl::VerifyHost{ false });
        session.SetSslOptions(trust).value();
        passed &= check(session.Get().value().text == "TLS works", "hostname verification must be configurable independently of peer trust");
        session.SetUrl(fixture.Url()).value();
        session.SetSslOptions(local_tls(mcr::options::ssl::CaInfoBlob{ read_file(fixture.Path("ca.pem")) })).value();
        passed &= check(session.Get().value().text == "TLS works", "CA blobs must survive destruction of temporary option storage");
        session.SetSslOptions(mcr::options::Ssl()).value();
        passed &= check(bool(session.Get().value().error), "replacing TLS options with defaults must clear prior CA blobs");
        session.SetSslOptions(local_tls(mcr::options::ssl::CaBuffer{ read_file(fixture.Path("ca.pem")) }, mcr::options::ssl::PinnedPublicKey{ read_file(fixture.Path("pin.txt")) })).value();
        passed &= check(session.Get().value().text == "TLS works", "CaBuffer and a matching public-key pin must work together");
        session.SetSslOptions(local_tls(mcr::options::ssl::CaInfo{ fixture.Path("ca.pem") }, mcr::options::ssl::PinnedPublicKey{ "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=" })).value();
        passed &= check(session.Get().value().error.code == mcr::ErrorCode::SSL_PINNEDPUBKEYNOTMATCH, "mismatched public-key pins must reject the connection");
        trust = local_tls(mcr::options::ssl::CaInfo{ fixture.Path("ca.pem") });
        session.SetSslOptions(trust).value();
        passed &= check(session.Get().value().text == "TLS works", "replacement TLS options must remove old public-key pins");
        session.SetUrl(fixture.Url(true)).value();
        passed &= check(bool(session.Get().value().error), "mutual TLS must reject a request without a client certificate");
        bool const schannel{ version->ssl_version && std::string_view{ version->ssl_version }.starts_with("Schannel") };
        if (schannel) {
            auto const filename = fixture.Path("client.p12").u8string();
            trust.cert_file.assign(reinterpret_cast<char const*>(filename.data()), filename.size());
            trust.cert_type = "P12";
            trust.key_pass = "incorrect-password";
            session.SetSslOptions(trust).value();
            auto bad_password{ session.Get().value() };
            passed &= check(bad_password.error.code == mcr::ErrorCode::SSL_CERTPROBLEM, "an incorrect PKCS12 password must fail client credential import");
            trust.key_pass = "fixture-password";
        } else {
            trust.SetOption(mcr::options::ssl::PemCert{ fixture.Path("client.pem") });
            trust.SetOption(mcr::options::ssl::PemKey{ fixture.Path("client-key.pem") });
        }
        session.SetSslOptions(trust).value();
        auto mutual{ session.Get().value() };
        // Schannel can reject curl's nonpersistent PFX private keys before starting TLS.
        // Only this exact backend failure limits the success checks; all other failures fail the test.
        // Related upstream limitations: https://github.com/curl/curl/issues/17626
        auto unsupported_credentials = [schannel](mcr::Response const& response) {
            return schannel && response.error.code == mcr::ErrorCode::SSL_CONNECT_ERROR &&
                response.error.message.contains("AcquireCredentialsHandle failed: SEC_E_UNKNOWN_CREDENTIALS");
        };
        bool const limited{ unsupported_credentials(mutual) };
        if (limited) {
            std::println("test_ssl_options: SKIP mutual TLS success checks: Schannel rejects the generated P12 private key (SEC_E_UNKNOWN_CREDENTIALS).");
        } else {
            passed &= check(!mutual.error && mutual.text == "TLS works", "client certificate files and passwords must authenticate to a mutual TLS server");
        }
        if (schannel) {
            trust.cert_file.clear();
            trust.cert_blob = read_file(fixture.Path("client.p12"));
        } else {
            trust.SetOption(mcr::options::ssl::PemBlob{ read_file(fixture.Path("client.pem")) });
            trust.SetOption(mcr::options::ssl::KeyBlob{ read_file(fixture.Path("client-key.pem")) });
        }
        session.SetSslOptions(trust).value();
        trust = {};
        auto blob_mutual{ session.Get().value() };
        passed &= check(limited ? unsupported_credentials(blob_mutual) : (!blob_mutual.error && blob_mutual.text == "TLS works"), "copied certificate blobs must reach the same TLS result after source storage is destroyed");
        session.SetSslOptions(local_tls(mcr::options::ssl::CaInfo{ fixture.Path("ca.pem") })).value();
        auto cleared{ session.Get().value() };
        passed &= check(bool(cleared.error) && !unsupported_credentials(cleared), "replacing TLS credentials must clear old certificate blobs and passwords");
        passed &= check(trusted.status_code == 200 && !trusted.GetCertInfos().empty(), "earlier certificate snapshots must survive later failed handshakes");
        if (!passed) {
            std::println("test_ssl_options: fixture log: {}", read_file(fixture.Path("server.log")));
        } else if (!limited) {
            std::println("test_ssl_options: verified HTTPS and mutual TLS file/blob paths");
        }
        return passed;
    }
} // namespace

auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        passed &= check_options();
        passed &= check_https();
    } catch (std::exception const& error) {
        passed = check(false, error.what());
    }
    curl_global_cleanup();
    return passed ? 0 : 1;
}
