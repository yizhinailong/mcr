/**
 * @file test_ssl_options.cpp
 * @brief Verify TLS option ownership and local HTTPS verification with generated certificates.
 */
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
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
        mcr::Session session;
        session.SetOption(defaults);
        session.SetOption(mcr::options::VerifySsl{ false });
        session.SetVerifySsl({});
        session.SetSslOptions(mcr::options::Ssl(mcr::options::ssl::TLSv1_2{}, mcr::options::ssl::MaxTLSv1_2{}));
        try {
            session.SetSslOptions(mcr::options::Ssl(mcr::options::ssl::SslFastStart{ true }));
            passed &= check(false, "removed TLS false-start support must not be silently accepted");
        } catch (std::runtime_error const&) {}
        return passed;
    }

#ifdef _WIN32
    /**
     * @brief Own only this test's generated fixture directory.
     */
    struct TempDirectory {
        std::filesystem::path path{ std::filesystem::temp_directory_path() / std::format("mcr-tls-{}-{}", GetCurrentProcessId(), std::chrono::steady_clock::now().time_since_epoch().count()) };

        TempDirectory() { std::filesystem::create_directory(path); }

        ~TempDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    /**
     * @brief Own a hidden fixture process and its output log.
     */
    struct ChildProcess {
        PROCESS_INFORMATION   info{};
        HANDLE                log{ INVALID_HANDLE_VALUE };
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

        auto Start(std::filesystem::path const& script, std::filesystem::path const& directory) -> void {
            this->directory = directory;
            SECURITY_ATTRIBUTES attributes{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
            log = CreateFileW((directory / "server.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (log == INVALID_HANDLE_VALUE) {
                throw std::runtime_error{ "Could not open TLS fixture log." };
            }
            STARTUPINFOW startup{};
            startup.cb          = sizeof(startup);
            startup.dwFlags     = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            startup.wShowWindow = SW_HIDE;
            startup.hStdOutput  = log;
            startup.hStdError   = log;
            startup.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);
            auto command{ std::format(L"pwsh.exe -NoLogo -NoProfile -NonInteractive -File \"{}\" -Directory \"{}\"", script.wstring(), directory.wstring()) };
            if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
                throw std::runtime_error{ "TLS fixture requires PowerShell 7.5 or later (pwsh) on PATH." };
            }
        }
    };

    struct HttpsFixture {
        TempDirectory  directory;
        ChildProcess   child;
        unsigned short port{};
        unsigned short mutual_port{};

        HttpsFixture() {
            auto const script{ std::filesystem::absolute(std::filesystem::path{ __FILE__ }.parent_path() / "fixtures" / "https_server.ps1") };
            child.Start(script, directory.path);
            auto const deadline{ std::chrono::steady_clock::now() + 15s };
            while (std::chrono::steady_clock::now() < deadline) {
                std::ifstream ready{ directory.path / "ready" };
                if (ready >> port >> mutual_port) {
                    return;
                }
                if (WaitForSingleObject(child.info.hProcess, 20) == WAIT_OBJECT_0) {
                    break;
                }
            }
            throw std::runtime_error{ "TLS fixture did not start: " + read_file(directory.path / "server.log") };
        }

        auto Url(bool mutual = false, std::string_view hostname = "localhost") const -> mcr::Url {
            return mcr::Url{ std::format("https://{}:{}/hello", hostname, mutual ? mutual_port : port) };
        }

        auto Path(std::string_view name) const -> std::filesystem::path { return directory.path / name; }
    };

    auto check_https() -> bool {
        HttpsFixture fixture;
        auto const*  version{ curl_version_info(CURLVERSION_NOW) };
        std::println("test_ssl_options: TLS backend {}", version->ssl_version ? version->ssl_version : "none");
        mcr::Session session;
        session.SetUrl(fixture.Url());
        session.SetProxies({
            {    "https",  "" },
            { "no_proxy", "*" }
        });
        session.SetTimeout(mcr::options::Timeout{ 3000ms });
        bool passed{ check(bool(session.Get().error), "untrusted generated certificates must be rejected by default") };
        session.SetVerifySsl(false);
        auto unverified{ session.Get() };
        if (unverified.error) {
            std::println("test_ssl_options: handshake error {}: {}", mcr::to_string(unverified.error.code), unverified.error.message);
        }
        passed &= check(unverified.text == "TLS works", "explicit VerifySsl(false) must disable both checks");
        session.SetVerifySsl(true);
        passed         &= check(bool(session.Get().error), "reenabling verification must reject the untrusted peer again");
        // Generated certificates have no online revocation service. Keep chain and hostname
        // verification enabled while explicitly disabling revocation checks for this fixture.
        auto local_tls  = [](auto&&... options) {
            return mcr::options::Ssl(mcr::options::ssl::NoRevoke{ true }, std::forward<decltype(options)>(options)...);
        };
        auto trust{ local_tls(mcr::options::ssl::CaInfo{ fixture.Path("ca.pem") }, mcr::options::ssl::TLSv1_2{}, mcr::options::ssl::MaxTLSv1_2{}) };
        session.SetSslOptions(trust);
        auto trusted{ session.Get() };
        if (trusted.error) {
            std::println("test_ssl_options: trusted handshake error {}: {}", mcr::to_string(trusted.error.code), trusted.error.message);
        }
        passed &= check(!trusted.error && trusted.text == "TLS works" && !trusted.GetCertInfos().empty(), "CA files and TLS 1.2 bounds must permit a verified connection and capture certificates");
        session.SetUrl(fixture.Url(false, "127.0.0.1"));
        passed &= check(bool(session.Get().error), "trusted certificates must still fail hostname mismatch");
        trust.SetOption(mcr::options::ssl::VerifyHost{ false });
        session.SetSslOptions(trust);
        passed &= check(session.Get().text == "TLS works", "hostname verification must be configurable independently of peer trust");
        session.SetUrl(fixture.Url());
        session.SetSslOptions(local_tls(mcr::options::ssl::CaInfoBlob{ read_file(fixture.Path("ca.pem")) }));
        passed &= check(session.Get().text == "TLS works", "CA blobs must survive destruction of temporary option storage");
        session.SetSslOptions(mcr::options::Ssl());
        passed &= check(bool(session.Get().error), "replacing TLS options with defaults must clear prior CA blobs");
        session.SetSslOptions(local_tls(mcr::options::ssl::CaBuffer{ read_file(fixture.Path("ca.pem")) }, mcr::options::ssl::PinnedPublicKey{ read_file(fixture.Path("pin.txt")) }));
        passed &= check(session.Get().text == "TLS works", "CaBuffer and a matching public-key pin must work together");
        session.SetSslOptions(local_tls(mcr::options::ssl::CaInfo{ fixture.Path("ca.pem") }, mcr::options::ssl::PinnedPublicKey{ "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=" }));
        passed &= check(session.Get().error.code == mcr::ErrorCode::SSL_PINNEDPUBKEYNOTMATCH, "mismatched public-key pins must reject the connection");
        trust   = local_tls(mcr::options::ssl::CaInfo{ fixture.Path("ca.pem") });
        session.SetSslOptions(trust);
        passed &= check(session.Get().text == "TLS works", "replacement TLS options must remove old public-key pins");
        session.SetUrl(fixture.Url(true));
        passed &= check(bool(session.Get().error), "mutual TLS must reject a request without a client certificate");
        bool const schannel{ version->ssl_version && std::string_view{ version->ssl_version }.starts_with("Schannel") };
        if (schannel) {
            trust.cert_file = fixture.Path("client.p12").string();
            trust.cert_type = "P12";
            trust.key_pass  = "incorrect-password";
            session.SetSslOptions(trust);
            auto bad_password{ session.Get() };
            passed         &= check(bad_password.error.code == mcr::ErrorCode::SSL_CERTPROBLEM, "an incorrect PKCS12 password must fail client credential import");
            trust.key_pass  = "fixture-password";
        } else {
            trust.SetOption(mcr::options::ssl::PemCert{ fixture.Path("client.pem") });
            trust.SetOption(mcr::options::ssl::PemKey{ fixture.Path("client-key.pem") });
        }
        session.SetSslOptions(trust);
        auto mutual{ session.Get() };
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
        session.SetSslOptions(trust);
        trust = {};
        auto blob_mutual{ session.Get() };
        passed &= check(limited ? unsupported_credentials(blob_mutual) : (!blob_mutual.error && blob_mutual.text == "TLS works"), "copied certificate blobs must reach the same TLS result after source storage is destroyed");
        session.SetSslOptions(local_tls(mcr::options::ssl::CaInfo{ fixture.Path("ca.pem") }));
        auto cleared{ session.Get() };
        passed &= check(bool(cleared.error) && !unsupported_credentials(cleared), "replacing TLS credentials must clear old certificate blobs and passwords");
        passed &= check(trusted.status_code == 200 && !trusted.GetCertInfos().empty(), "earlier certificate snapshots must survive later failed handshakes");
        if (!passed) {
            std::println("test_ssl_options: fixture log: {}", read_file(fixture.Path("server.log")));
        }
        return passed;
    }
#endif
} // namespace

auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    bool passed{ true };
    try {
        passed &= check_options();
#ifdef _WIN32
        passed &= check_https();
#else
        std::println("test_ssl_options: SKIP HTTPS checks: generated fixture requires Windows with PowerShell 7.5 or later.");
#endif
    } catch (std::exception const& error) {
        passed = check(false, error.what());
    }
    curl_global_cleanup();
    return passed ? 0 : 1;
}
