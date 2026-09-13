/**
 * @file ssl_options.cppm
 * @brief Owned TLS configuration and cpr-compatible typed SSL options.
 */
module;
#include <curl/curl.h>
export module mcr.ssl_options;
import mcr.secure_string;
import std;

export namespace mcr::options {
    /**
     * @brief Enable or disable certificate and hostname verification together.
     */
    struct VerifySsl {
        bool verify{ true }; ///< Verification is enabled by default.
        VerifySsl() = default;

        /**
         * @brief Store the verification preference.
         * @param value Whether to verify.
         */
        VerifySsl(bool value) : verify{ value } {}

        /**
         * @brief Inspect the stored preference.
         * @return Whether to verify.
         */
        explicit operator bool() const noexcept { return verify; }
    };

    namespace ssl {
        /**
         * @brief Own a client certificate path; derived options select its encoding.
         */
        class CertFile {
        public:
            std::filesystem::path const filename; ///< Owned certificate path.

            /**
             * @brief Own a certificate path.
             * @param value Path to the certificate.
             */
            CertFile(std::filesystem::path value) : filename{ std::move(value) } {}

            virtual ~CertFile() = default;

            /**
             * @brief Select PEM encoding.
             * @return Curl certificate type.
             */
            [[nodiscard]] virtual auto GetCertType() const -> char const* { return "PEM"; }
        };

        using PemCert = CertFile;

        /**
         * @brief Select a DER client certificate.
         */
        class DerCert : public CertFile {
        public:
            using CertFile::CertFile;

            [[nodiscard]] auto GetCertType() const -> char const* override { return "DER"; }
        };

        /**
         * @brief Own in-memory client certificate bytes.
         */
        class CertBlob {
        public:
            std::string blob; ///< Owned certificate bytes, including binary data.

            /**
             * @brief Own certificate bytes.
             * @param value Certificate contents.
             */
            CertBlob(std::string value) : blob{ std::move(value) } {}

            virtual ~CertBlob() = default;

            /**
             * @brief Select PEM encoding.
             * @return Curl certificate type.
             */
            [[nodiscard]] virtual auto GetCertType() const -> char const* { return "PEM"; }
        };

        using PemBlob = CertBlob;

        /**
         * @brief Select a DER certificate from memory.
         */
        class DerBlob : public CertBlob {
        public:
            using CertBlob::CertBlob;

            [[nodiscard]] auto GetCertType() const -> char const* override { return "DER"; }
        };

        /**
         * @brief Own a private-key path and optional passphrase.
         */
        class KeyFile {
        public:
            std::filesystem::path filename; ///< Owned key path.
            utils::SecureString   password; ///< Owned passphrase in secure storage.

            /**
             * @brief Own a key path and passphrase.
             * @param value Key path.
             * @param passphrase Optional key password.
             */
            KeyFile(std::filesystem::path value, std::string_view passphrase = {}) : filename{ std::move(value) }, password{ passphrase } {}

            virtual ~KeyFile() = default;

            /**
             * @brief Select PEM encoding.
             * @return Curl key type.
             */
            [[nodiscard]] virtual auto GetKeyType() const -> char const* { return "PEM"; }
        };

        using PemKey = KeyFile;

        /**
         * @brief Select a DER private key.
         */
        class DerKey : public KeyFile {
        public:
            using KeyFile::KeyFile;

            [[nodiscard]] auto GetKeyType() const -> char const* override { return "DER"; }
        };

        /**
         * @brief Own private-key bytes and optional passphrase.
         */
        class KeyBlob {
        public:
            std::string        blob;     ///< Owned private-key bytes.
            utils::SecureString password; ///< Owned passphrase in secure storage.

            /**
             * @brief Own key bytes and a passphrase.
             * @param value Key contents.
             * @param passphrase Optional key password.
             */
            KeyBlob(std::string value, std::string_view passphrase = {}) : blob{ std::move(value) }, password{ passphrase } {}

            virtual ~KeyBlob() = default;

            /**
             * @brief Select PEM encoding.
             * @return Curl key type.
             */
            [[nodiscard]] virtual auto GetKeyType() const -> char const* { return "PEM"; }
        };

        /**
         * @brief Pinned public key filename or sha256 hash list.
         */
        struct PinnedPublicKey {
            std::string pinned_public_key; ///< Owned option value.

            /**
             * @brief Own the option value.
             * @param value Pinned public key filename or sha256 hash list.
             */
            PinnedPublicKey(std::string value) : pinned_public_key{ std::move(value) } {}
        };

        /**
         * @brief CA bundle filename.
         */
        struct CaInfo {
            std::filesystem::path filename; ///< Owned option value.

            /**
             * @brief Own the option value.
             * @param value CA bundle filename.
             */
            CaInfo(std::filesystem::path value) : filename{ std::move(value) } {}
        };

        /**
         * @brief CA bundle bytes.
         */
        struct CaInfoBlob {
            std::string blob; ///< Owned option value.

            /**
             * @brief Own the option value.
             * @param value CA bundle bytes.
             */
            CaInfoBlob(std::string value) : blob{ std::move(value) } {}
        };

        /**
         * @brief CA certificate directory.
         */
        struct CaPath {
            std::filesystem::path filename; ///< Owned option value.

            /**
             * @brief Own the option value.
             * @param value CA certificate directory.
             */
            CaPath(std::filesystem::path value) : filename{ std::move(value) } {}
        };

        /**
         * @brief CA bundle bytes applied through curl's copied CAINFO_BLOB.
         */
        struct CaBuffer {
            std::string buffer; ///< Owned option value.

            /**
             * @brief Own the option value.
             * @param value CA bundle bytes applied through curl's copied CAINFO_BLOB.
             */
            CaBuffer(std::string value) : buffer{ std::move(value) } {}
        };

        /**
         * @brief Certificate revocation list filename.
         */
        struct Crl {
            std::filesystem::path filename; ///< Owned option value.

            /**
             * @brief Own the option value.
             * @param value Certificate revocation list filename.
             */
            Crl(std::filesystem::path value) : filename{ std::move(value) } {}
        };

        /**
         * @brief TLS cipher selection.
         */
        struct Ciphers {
            std::string ciphers; ///< Owned option value.

            /**
             * @brief Own the option value.
             * @param value TLS cipher selection.
             */
            Ciphers(std::string value) : ciphers{ std::move(value) } {}
        };

        /**
         * @brief TLS 1.3 cipher selection.
         */
        struct TLS13_Ciphers {
            std::string ciphers; ///< Owned option value.

            /**
             * @brief Own the option value.
             * @param value TLS 1.3 cipher selection.
             */
            TLS13_Ciphers(std::string value) : ciphers{ std::move(value) } {}
        };

        /**
         * @brief Configure ALPN.
         */
        struct ALPN {
            bool enabled{ true }; ///< Stored preference.
            ALPN() = default;

            /**
             * @brief Store the preference.
             * @param value Whether to enable it.
             */
            ALPN(bool value) : enabled{ value } {}

            /**
             * @brief Inspect the preference.
             * @return Whether enabled.
             */
            explicit operator bool() const noexcept { return enabled; }
        };

        /**
         * @brief Configure VerifyHost.
         */
        struct VerifyHost {
            bool enabled{ true }; ///< Stored preference.
            VerifyHost() = default;

            /**
             * @brief Store the preference.
             * @param value Whether to enable it.
             */
            VerifyHost(bool value) : enabled{ value } {}

            /**
             * @brief Inspect the preference.
             * @return Whether enabled.
             */
            explicit operator bool() const noexcept { return enabled; }
        };

        /**
         * @brief Configure VerifyPeer.
         */
        struct VerifyPeer {
            bool enabled{ true }; ///< Stored preference.
            VerifyPeer() = default;

            /**
             * @brief Store the preference.
             * @param value Whether to enable it.
             */
            VerifyPeer(bool value) : enabled{ value } {}

            /**
             * @brief Inspect the preference.
             * @return Whether enabled.
             */
            explicit operator bool() const noexcept { return enabled; }
        };

        /**
         * @brief Configure VerifyStatus.
         */
        struct VerifyStatus {
            bool enabled{ false }; ///< Stored preference.
            VerifyStatus() = default;

            /**
             * @brief Store the preference.
             * @param value Whether to enable it.
             */
            VerifyStatus(bool value) : enabled{ value } {}

            /**
             * @brief Inspect the preference.
             * @return Whether enabled.
             */
            explicit operator bool() const noexcept { return enabled; }
        };

        /**
         * @brief Configure SessionIdCache.
         */
        struct SessionIdCache {
            bool enabled{ true }; ///< Stored preference.
            SessionIdCache() = default;

            /**
             * @brief Store the preference.
             * @param value Whether to enable it.
             */
            SessionIdCache(bool value) : enabled{ value } {}

            /**
             * @brief Inspect the preference.
             * @return Whether enabled.
             */
            explicit operator bool() const noexcept { return enabled; }
        };

        /**
         * @brief Configure SslFastStart.
         * @note The current curl dependency does not support false start; Session rejects enabled values.
         */
        struct SslFastStart {
            bool enabled{ false }; ///< Stored preference.
            SslFastStart() = default;

            /**
             * @brief Store the preference.
             * @param value Whether to enable it.
             */
            SslFastStart(bool value) : enabled{ value } {}

            /**
             * @brief Inspect the preference.
             * @return Whether enabled.
             */
            explicit operator bool() const noexcept { return enabled; }
        };

        /**
         * @brief Configure NoRevoke.
         */
        struct NoRevoke {
            bool enabled{ false }; ///< Stored preference.
            NoRevoke() = default;

            /**
             * @brief Store the preference.
             * @param value Whether to enable it.
             */
            NoRevoke(bool value) : enabled{ value } {}

            /**
             * @brief Inspect the preference.
             * @return Whether enabled.
             */
            explicit operator bool() const noexcept { return enabled; }
        };

        /**
         * @brief Select the TLSv1 protocol bound.
         */
        struct TLSv1 {};

        /**
         * @brief Select the TLSv1_0 protocol bound.
         */
        struct TLSv1_0 {};

        /**
         * @brief Select the TLSv1_1 protocol bound.
         */
        struct TLSv1_1 {};

        /**
         * @brief Select the TLSv1_2 protocol bound.
         */
        struct TLSv1_2 {};

        /**
         * @brief Select the TLSv1_3 protocol bound.
         */
        struct TLSv1_3 {};

        /**
         * @brief Select the MaxTLSVersion protocol bound.
         */
        struct MaxTLSVersion {};

        /**
         * @brief Select the MaxTLSv1_0 protocol bound.
         */
        struct MaxTLSv1_0 {};

        /**
         * @brief Select the MaxTLSv1_1 protocol bound.
         */
        struct MaxTLSv1_1 {};

        /**
         * @brief Select the MaxTLSv1_2 protocol bound.
         */
        struct MaxTLSv1_2 {};

        /**
         * @brief Select the MaxTLSv1_3 protocol bound.
         */
        struct MaxTLSv1_3 {};
    } // namespace ssl

    /**
     * @brief Own a complete TLS configuration with verification enabled by default.
     * @note File/blob setters select the last supplied source. Curl backend support is checked by Session.
     * Obsolete SSLv2, SSLv3 and NPN options are not exposed by the project's curl 8.21 dependency.
     */
    struct SslOptions {
        std::string        cert_file;                                  ///< Client certificate path.
        utils::SecureString cert_blob;                                  ///< In-memory client certificate.
        std::string        cert_type;                                  ///< Client certificate encoding.
        std::string        key_file;                                   ///< Private-key path.
        utils::SecureString key_blob;                                   ///< In-memory private key.
        std::string        key_type;                                   ///< Private-key encoding.
        utils::SecureString key_pass;                                   ///< Private-key password.
        std::string        pinned_public_key;                          ///< Public-key pin specification.
        bool               enable_alpn{ true };                        ///< Advertise ALPN.
        bool               verify_host{ true };                        ///< Verify the requested hostname.
        bool               verify_peer{ true };                        ///< Verify the certificate chain.
        bool               verify_status{ false };                     ///< Require stapled OCSP status.
        long               ssl_version{ CURL_SSLVERSION_DEFAULT };     ///< Minimum TLS version.
        long               max_version{ CURL_SSLVERSION_MAX_DEFAULT }; ///< Maximum TLS version.
        bool               ssl_no_revoke{ false };                     ///< Disable revocation checks when supported.
        bool               ssl_fast_start{ false };                    ///< Unsupported preference; Session rejects true.
        std::string        ca_info;                                    ///< CA bundle path; empty restores curl's default.
        std::string        ca_info_blob;                               ///< CA bundle contents.
        std::string        ca_path;                                    ///< CA directory; empty restores curl's default.
        std::string        ca_buffer;                                  ///< Alternative in-memory CA bundle.
        std::string        crl_file;                                   ///< CRL path.
        std::string        ciphers;                                    ///< Cipher selection for TLS up to 1.2.
        std::string        tls13_ciphers;                              ///< TLS 1.3 cipher selection.
        bool               session_id_cache{ true };                   ///< Cache TLS sessions.

        /**
         * @brief Select a certificate file and clear blob storage.
         * @param opt Certificate option.
         */
        auto SetOption(ssl::CertFile const& opt) -> void {
            cert_file = opt.filename.string();
            cert_blob.clear();
            cert_type = opt.GetCertType();
        }

        /**
         * @brief Select a certificate blob and clear the file path.
         * @param opt Certificate option.
         */
        auto SetOption(ssl::CertBlob const& opt) -> void {
            cert_blob = opt.blob;
            cert_file.clear();
            cert_type = opt.GetCertType();
        }

        /**
         * @brief Select a key file and password, clearing blob storage.
         * @param opt Key option.
         */
        auto SetOption(ssl::KeyFile const& opt) -> void {
            key_file = opt.filename.string();
            key_blob.clear();
            key_type = opt.GetKeyType();
            key_pass = opt.password;
        }

        /**
         * @brief Select a key blob and password, clearing the file path.
         * @param opt Key option.
         */
        auto SetOption(ssl::KeyBlob const& opt) -> void {
            key_blob = opt.blob;
            key_file.clear();
            key_type = opt.GetKeyType();
            key_pass = opt.password;
        }

        /**
         * @brief Apply PinnedPublicKey.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::PinnedPublicKey const& opt) -> void { pinned_public_key = opt.pinned_public_key; }

        /**
         * @brief Apply ALPN.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::ALPN const& opt) -> void { enable_alpn = opt.enabled; }

        /**
         * @brief Apply VerifyHost.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::VerifyHost const& opt) -> void { verify_host = opt.enabled; }

        /**
         * @brief Apply VerifyPeer.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::VerifyPeer const& opt) -> void { verify_peer = opt.enabled; }

        /**
         * @brief Apply VerifyStatus.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::VerifyStatus const& opt) -> void { verify_status = opt.enabled; }

        /**
         * @brief Apply SessionIdCache.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::SessionIdCache const& opt) -> void { session_id_cache = opt.enabled; }

        /**
         * @brief Apply SslFastStart.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::SslFastStart const& opt) -> void { ssl_fast_start = opt.enabled; }

        /**
         * @brief Apply NoRevoke.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::NoRevoke const& opt) -> void { ssl_no_revoke = opt.enabled; }

        /**
         * @brief Apply CaInfo.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::CaInfo const& opt) -> void {
            ca_info = opt.filename.string();
            ca_info_blob.clear();
            ca_buffer.clear();
        }

        /**
         * @brief Apply CaInfoBlob.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::CaInfoBlob const& opt) -> void {
            ca_info_blob = opt.blob;
            ca_info.clear();
            ca_buffer.clear();
        }

        /**
         * @brief Apply CaPath.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::CaPath const& opt) -> void { ca_path = opt.filename.string(); }

        /**
         * @brief Apply CaBuffer.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::CaBuffer const& opt) -> void {
            ca_buffer = opt.buffer;
            ca_info.clear();
            ca_info_blob.clear();
        }

        /**
         * @brief Apply Crl.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::Crl const& opt) -> void { crl_file = opt.filename.string(); }

        /**
         * @brief Apply Ciphers.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::Ciphers const& opt) -> void { ciphers = opt.ciphers; }

        /**
         * @brief Apply TLS13_Ciphers.
         * @param opt Value to copy.
         */
        auto SetOption(ssl::TLS13_Ciphers const& opt) -> void { tls13_ciphers = opt.ciphers; }

        /**
         * @brief Apply TLSv1.
         * @param opt Protocol bound tag.
         */
        auto SetOption(ssl::TLSv1 const& /*opt*/) -> void { ssl_version = CURL_SSLVERSION_TLSv1; }

        /**
         * @brief Apply TLSv1_0.
         * @param opt Protocol bound tag.
         */
        auto SetOption(ssl::TLSv1_0 const& /*opt*/) -> void { ssl_version = CURL_SSLVERSION_TLSv1_0; }

        /**
         * @brief Apply TLSv1_1.
         * @param opt Protocol bound tag.
         */
        auto SetOption(ssl::TLSv1_1 const& /*opt*/) -> void { ssl_version = CURL_SSLVERSION_TLSv1_1; }

        /**
         * @brief Apply TLSv1_2.
         * @param opt Protocol bound tag.
         */
        auto SetOption(ssl::TLSv1_2 const& /*opt*/) -> void { ssl_version = CURL_SSLVERSION_TLSv1_2; }

        /**
         * @brief Apply TLSv1_3.
         * @param opt Protocol bound tag.
         */
        auto SetOption(ssl::TLSv1_3 const& /*opt*/) -> void { ssl_version = CURL_SSLVERSION_TLSv1_3; }

        /**
         * @brief Apply MaxTLSVersion.
         * @param opt Protocol bound tag.
         */
        auto SetOption(ssl::MaxTLSVersion const& /*opt*/) -> void { max_version = CURL_SSLVERSION_MAX_DEFAULT; }

        /**
         * @brief Apply MaxTLSv1_0.
         * @param opt Protocol bound tag.
         */
        auto SetOption(ssl::MaxTLSv1_0 const& /*opt*/) -> void { max_version = CURL_SSLVERSION_MAX_TLSv1_0; }

        /**
         * @brief Apply MaxTLSv1_1.
         * @param opt Protocol bound tag.
         */
        auto SetOption(ssl::MaxTLSv1_1 const& /*opt*/) -> void { max_version = CURL_SSLVERSION_MAX_TLSv1_1; }

        /**
         * @brief Apply MaxTLSv1_2.
         * @param opt Protocol bound tag.
         */
        auto SetOption(ssl::MaxTLSv1_2 const& /*opt*/) -> void { max_version = CURL_SSLVERSION_MAX_TLSv1_2; }

        /**
         * @brief Apply MaxTLSv1_3.
         * @param opt Protocol bound tag.
         */
        auto SetOption(ssl::MaxTLSv1_3 const& /*opt*/) -> void { max_version = CURL_SSLVERSION_MAX_TLSv1_3; }
    };

    /**
     * @brief Compose TLS options in argument order.
     * @tparam Ts Option types.
     * @param options Options to apply.
     * @return Owned configuration.
     */
    template <typename... Ts>
    [[nodiscard]] auto Ssl(Ts&&... options) -> SslOptions {
        SslOptions result;
        (result.SetOption(std::forward<Ts>(options)), ...);
        return result;
    }
} // namespace mcr::options
