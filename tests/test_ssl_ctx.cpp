/**
 * @file test_ssl_ctx.cpp
 * @brief Check unsupported backends or real OpenSSL certificate-store updates.
 */
#include <curl/curl.h>
#ifdef MCR_SSL_CTX_OPENSSL
    #include <openssl/bio.h>
    #include <openssl/pem.h>
    #include <openssl/ssl.h>
    #include <openssl/x509_vfy.h>
    #include <openssl/x509v3.h>
#endif
import std;
import mcr;

namespace {
    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_ssl_ctx: {}", message);
        }
        return condition;
    }

#ifdef MCR_SSL_CTX_OPENSSL
    auto require(bool condition) -> void {
        if (!condition) {
            throw std::runtime_error{ "Could not create the OpenSSL certificate fixture." };
        }
    }

    auto certificate(long serial) -> std::string {
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{ EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", 2048), &EVP_PKEY_free };
        std::unique_ptr<X509, decltype(&X509_free)>         cert{ X509_new(), &X509_free };
        require(key && cert);
        require(X509_set_version(cert.get(), 2) == 1 && ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial) == 1);
        require(X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60) && X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600));
        require(X509_set_pubkey(cert.get(), key.get()) == 1);
        auto*      name{ X509_get_subject_name(cert.get()) };
        auto const common_name{ std::format("mcr-test-{}", serial) };
        require(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<unsigned char const*>(common_name.c_str()), -1, -1, 0) == 1);
        require(X509_set_issuer_name(cert.get(), name) == 1);
        std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> ca{ X509V3_EXT_conf_nid(nullptr, nullptr, NID_basic_constraints, "critical,CA:TRUE"), &X509_EXTENSION_free };
        require(ca && X509_add_ext(cert.get(), ca.get(), -1) == 1);
        require(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0);
        std::unique_ptr<BIO, decltype(&BIO_free)> bio{ BIO_new(BIO_s_mem()), &BIO_free };
        require(bio && PEM_write_bio_X509(bio.get(), cert.get()) == 1);
        char*      bytes{};
        auto const length{ BIO_get_mem_data(bio.get(), &bytes) };
        require(length > 0);
        return { bytes, static_cast<std::size_t>(length) };
    }

    auto check_openssl() -> bool {
        static_assert(mcr::curl::SSL_CTX_OPENSSL_ENABLED);
        std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context{ SSL_CTX_new(TLS_method()), &SSL_CTX_free };
        require(bool(context));
        auto load = [&](std::string bytes) {
            return mcr::curl::sslctx_function_load_ca_cert_from_buffer(nullptr, context.get(), bytes.data());
        };
        auto* store{ SSL_CTX_get_cert_store(context.get()) };
        auto  count = [&] {
            return sk_X509_OBJECT_num(X509_STORE_get0_objects(store));
        };
        bool       passed{ check(load("") == CURLE_ABORTED_BY_CALLBACK && load("not a certificate") == CURLE_ABORTED_BY_CALLBACK, "empty and invalid bundles must fail") };
        auto const first{ certificate(1) };
        auto const second{ certificate(2) };
        passed &= check(load(first + "-----BEGIN CERTIFICATE-----\n!!!\n-----END CERTIFICATE-----\n") == CURLE_ABORTED_BY_CALLBACK && count() == 0, "a malformed trailing certificate must fail before updating trust");
        passed &= check(load(first + second) == CURLE_OK && count() == 2, "every valid certificate in a bundle must be added");
        passed &= check(load(first + second) == CURLE_OK && count() == 2, "reloading a CA bundle must tolerate duplicate certificates");
        std::unique_ptr<BIO, decltype(&BIO_free)>                       bio{ BIO_new_mem_buf(first.data(), static_cast<int>(first.size())), &BIO_free };
        std::unique_ptr<X509, decltype(&X509_free)>                     cert{ PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), &X509_free };
        std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> verification{ X509_STORE_CTX_new(), &X509_STORE_CTX_free };
        require(cert && verification && X509_STORE_CTX_init(verification.get(), store, cert.get(), nullptr) == 1);
        passed &= check(X509_verify_cert(verification.get()) == 1, "loaded certificates must participate in OpenSSL trust verification");
        return passed;
    }
#endif
} // namespace

auto main() -> int {
    static_assert(noexcept(mcr::curl::sslctx_function_load_ca_cert_from_buffer(nullptr, nullptr, nullptr)));
    bool passed{ check(mcr::curl::sslctx_function_load_ca_cert_from_buffer(nullptr, nullptr, nullptr) == CURLE_ABORTED_BY_CALLBACK, "null callback arguments must fail without throwing") };
    try {
#ifdef MCR_SSL_CTX_OPENSSL
        passed &= check_openssl();
#else
        static_assert(!mcr::curl::SSL_CTX_OPENSSL_ENABLED);
        char context{}, buffer{};
        passed &= check(mcr::curl::sslctx_function_load_ca_cert_from_buffer(nullptr, &context, &buffer) == CURLE_NOT_BUILT_IN, "non-OpenSSL builds must report unsupported context loading");
#endif
    } catch (std::exception const& error) {
        passed = check(false, error.what());
    }
    return passed ? 0 : 1;
}
