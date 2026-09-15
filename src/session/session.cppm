/**
 * @file session.cppm
 * @brief Reusable synchronous and asynchronous HTTP sessions built on curl easy handles.
 */
module;

#include <curl/curl.h>

// Windows COM headers define this macro, which would corrupt mcr.interface imports.
#ifdef interface
    #undef interface
#endif

export module mcr.session;

export import mcr.async;
export import mcr.auth;
export import mcr.body;
export import mcr.callback;
export import mcr.connection_pool;
export import mcr.fields;
export import mcr.http;
export import mcr.interface;
export import mcr.json;
export import mcr.multipart;
export import mcr.proxy;
export import mcr.response;
export import mcr.sse;
export import mcr.ssl_options;
export import mcr.transfer_options;

import mcr.util;
import mcr.curlmultiholder;
export import mcr.error;
import std;

export namespace mcr {

    using AsyncResponse = utils::AsyncWrapper<Result<Response>>;                                      ///< Asynchronous transfer result.
    using Content       = std::variant<std::monostate, Payload, Body, BodyView, Multipart, JsonBody>; ///< Persistent request content.

    class Session;
    class MultiPerform;

    /**
     * @brief Intercept a synchronous request, including requests executed by an async task.
     */
    class Interceptor {
    public:
        /**
         * @brief Methods supported by Proceed overloads.
         */
        enum class ProceedHttpMethod : std::uint8_t {
            GET_REQUEST,
            POST_REQUEST,
            PUT_REQUEST,
            DELETE_REQUEST,
            PATCH_REQUEST,
            HEAD_REQUEST,
            OPTIONS_REQUEST,
            DOWNLOAD_CALLBACK_REQUEST,
            DOWNLOAD_FILE_REQUEST
        };
        virtual ~Interceptor()                                       = default;
        /**
         * @brief Modify, forward, retry, or replace a request.
         * @param session Current session.
         * @return Response to pass to the preceding interceptor.
         */
        virtual auto Intercept(Session& session) -> Result<Response> = 0;

    protected:
        /**
         * @brief Continue with the current method and download destination.
         * @param session Current session.
         * @return Downstream response.
         */
        static auto Proceed(Session& session) -> Result<Response>;
        /**
         * @brief Continue using a different HTTP method.
         * @param session Current session.
         * @param method Method to execute.
         * @return Downstream response.
         */
        static auto Proceed(Session& session, ProceedHttpMethod method) -> Result<Response>;
        /**
         * @brief Continue with a file download.
         * @param session Current session.
         * @param method Must be DOWNLOAD_FILE_REQUEST.
         * @param file Borrowed stream.
         * @return Downstream response.
         */
        static auto Proceed(Session& session, ProceedHttpMethod method, std::ofstream& file) -> Result<Response>;
        /**
         * @brief Continue with a callback download.
         * @param session Current session.
         * @param method Must be DOWNLOAD_CALLBACK_REQUEST.
         * @param write Consumer to copy.
         * @return Downstream response.
         */
        static auto Proceed(Session& session, ProceedHttpMethod method, WriteCallback const& write) -> Result<Response>;
    };

    /**
     * @brief Intercept a complete MultiPerform batch.
     */
    class InterceptorMulti {
    public:
        using ProceedHttpMethod                                                      = Interceptor::ProceedHttpMethod; ///< Matching cpr method tags.
        virtual ~InterceptorMulti()                                                  = default;
        /**
         * @brief Modify, forward, retry, or replace a batch.
         * @param multi Current batch.
         * @return Responses in session order.
         */
        virtual auto Intercept(MultiPerform& multi) -> Result<std::vector<Response>> = 0;

    protected:
        /**
         * @brief Reprepare sessions and continue the remaining chain.
         * @param multi Current batch.
         * @return Downstream responses.
         */
        static auto Proceed(MultiPerform& multi) -> Result<std::vector<Response>>;
        /**
         * @brief Select a callback download destination.
         * @param multi Current batch.
         * @param index Session index.
         * @param write Consumer to copy.
         * @return Success or the first operation error.
         */
        static auto PrepareDownloadSession(MultiPerform& multi, std::size_t index, WriteCallback const& write) -> Result<void>;
        /**
         * @brief Select a file download destination.
         * @param multi Current batch.
         * @param index Session index.
         * @param file Borrowed stream.
         * @return Success or the first operation error.
         */
        static auto PrepareDownloadSession(MultiPerform& multi, std::size_t index, std::ofstream& file) -> Result<void>;
    };

    /**
     * @brief Reuse a curl connection cache, cookies, options, and content across requests.
     * @note A session must only be accessed by one caller at a time, including asynchronous work.
     * Async methods require std::shared_ptr ownership. Borrowed body buffers, files, callback captures,
     * and a configured ConnectionPool must outlive all transfers that use them.
     * Content persists until replaced or removed; HEAD and Download ignore it without removing it.
     * Curl option failures are returned as Error values. Transfer failures are reported in Response::error.
     * Callback exceptions are rethrown after curl returns, never through curl's C frames.
     */
    class Session : public std::enable_shared_from_this<Session> {
    private:
        friend Interceptor;
        friend MultiPerform;
        std::vector<std::shared_ptr<Interceptor>> m_interceptors;       ///< Interceptors in registration order.
        std::size_t                               m_next_interceptor{}; ///< Next interceptor in the current nested request.
        std::size_t                               m_request_depth{};    ///< Number of active interceptor/request frames.
        std::string                               m_method{ "GET" };    ///< Last prepared method, retained by Proceed.
        MultiPerform*                             m_multi_owner{};      ///< Batch that currently owns this session, if any.
        bool                                      m_multi_preparing{};  ///< Permit the owning batch to prepare its handle.
        bool                                      m_in_transfer{};      ///< Reject recursive transfers from curl callbacks.
        std::shared_ptr<curl::CurlHolder>         m_curl;               ///< Owned transfer resources.
        Url                                       m_url;                ///< Base URL before adding parameters.
        Parameters                                m_parameters;         ///< Persistent URL parameters.
        Header                                    m_header;             ///< Persistent request headers.
        options::Proxies                          m_proxies;            ///< Persistent proxy selection.
        options::ProxyAuthentication              m_proxy_auth;         ///< Persistent encoded proxy credentials.
        options::AcceptEncoding                   m_accept_encoding;    ///< Compression preference.
        Content                                   m_content;            ///< Owned or borrowed request content.
        ReadCallback                              m_read;               ///< Optional upload producer.
        HeaderCallback                            m_header_callback;    ///< Optional header observer.
        WriteCallback                             m_write;              ///< Optional response consumer.
        ProgressCallback                          m_progress;           ///< Optional progress observer.
        DebugCallback                             m_debug;              ///< Optional diagnostics observer.
        ServerSentEventCallback                   m_sse;                ///< Optional event consumer.
        ServerSentEventParser                     m_sse_parser;         ///< Parser reset before every transfer.
        std::shared_ptr<std::atomic_bool>         m_cancellation;       ///< Shared cancellation flag.
        std::string                               m_response_string;    ///< Current buffered response body.
        std::string                               m_header_string;      ///< Current raw response headers.
        std::size_t                               m_reserve_size{};     ///< Requested body buffer reservation.
        WriteCallback                             m_download_write;     ///< Consumer used only for a prepared download.
        std::ofstream*                            m_download_file{};    ///< Borrowed file for a prepared download.
        bool                                      m_downloading{};      ///< Selects the current body destination.
        std::exception_ptr                        m_callback_error;     ///< First exception caught inside a curl callback.

    public:
        /**
         * @brief Initialize cpr-compatible redirects, cookies, compression, and keepalive defaults.
         * @return Success or the first operation error.
         */
        [[nodiscard]] static auto Create() -> Result<std::shared_ptr<Session>> {
            auto holder = curl::CurlHolder::Create();
            if (!holder) {
                return std::unexpected{ std::move(holder.error()) };
            }
            // The private constructor keeps partially initialized sessions out of public code.
            auto result = std::shared_ptr<Session>{ new Session{ std::make_shared<curl::CurlHolder>(std::move(*holder)) } };
            if (auto status = result->initialize(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return result;
        }

        Session(Session const&)                    = delete;
        Session(Session&&)                         = delete;
        auto operator=(Session const&) -> Session& = delete;
        auto operator=(Session&&) -> Session&      = delete;

        /**
         * @brief Detach borrowed callback and body pointers before destroying session state.
         */
        ~Session() { curl_easy_reset(m_curl->handle); }

        /**
         * @brief Replace the base URL.
         * @param url URL to copy.
         * @return Success or the first operation error.
         */
        auto SetUrl(Url const& url) -> Result<void> {
            m_url = url;
            return {};
        }

        /**
         * @brief Copy URL parameters.
         * @param parameters Replacement parameters.
         * @return Success or the first operation error.
         */
        auto SetParameters(Parameters const& parameters) -> Result<void> {
            m_parameters = parameters;
            return {};
        }

        /**
         * @brief Move URL parameters.
         * @param parameters Replacement parameters.
         * @return Success or the first operation error.
         */
        auto SetParameters(Parameters&& parameters) -> Result<void> {
            m_parameters = std::move(parameters);
            return {};
        }

        /**
         * @brief Replace all request headers.
         * @param header Headers to copy.
         * @return Success or the first operation error.
         */
        auto SetHeader(Header const& header) -> Result<void> {
            m_header = header;
            return {};
        }

        /**
         * @brief Merge headers using case-insensitive replacement.
         * @param header Headers to add or replace.
         * @return Success or the first operation error.
         */
        auto UpdateHeader(Header const& header) -> Result<void> {
            for (auto const& [name, value] : header) {
                m_header[name] = value;
            }

            return {};
        }

        /**
         * @brief Access persistent request headers.
         * @return Mutable header map.
         */
        [[nodiscard]] auto GetHeader() -> Header& { return m_header; }

        /**
         * @brief Inspect persistent request headers.
         * @return Read-only header map.
         */
        [[nodiscard]] auto GetHeader() const -> Header const& { return m_header; }

        /**
         * @brief Set the total transfer timeout.
         * @param timeout Duration; zero disables the timeout.
         * @return Success or the first operation error.
         */
        auto SetTimeout(options::Timeout const& timeout) -> Result<void> {
            auto milliseconds = timeout.Milliseconds();
            if (!milliseconds) {
                return std::unexpected{ std::move(milliseconds.error()) };
            }
            if (auto status = setOption(CURLOPT_TIMEOUT_MS, *milliseconds); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Set the connection timeout.
         * @param timeout Connection establishment deadline.
         * @return Success or the first operation error.
         */
        auto SetConnectTimeout(options::ConnectTimeout const& timeout) -> Result<void> {
            auto milliseconds = timeout.Milliseconds();
            if (!milliseconds) {
                return std::unexpected{ std::move(milliseconds.error()) };
            }
            if (auto status = setOption(CURLOPT_CONNECTTIMEOUT_MS, *milliseconds); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Attach a borrowed connection pool.
         * @param pool Pool that must outlive this session's handle.
         * @return Success or the first operation error.
         */
        auto SetConnectionPool(ConnectionPool const& pool) -> Result<void> {
            if (auto status = pool.SetupHandler(m_curl->handle); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Configure HTTP credentials.
         * @param auth Owned credentials and authentication policy to copy into curl.
         * @return Success or the first operation error.
         */
        auto SetAuth(options::Authentication const& auth) -> Result<void> {
            long mode{};
            switch (auth.GetAuthMode()) {
                case options::AuthMode::BASIC    : mode = CURLAUTH_BASIC; break;
                case options::AuthMode::DIGEST   : mode = CURLAUTH_DIGEST; break;
                case options::AuthMode::NTLM     : mode = CURLAUTH_NTLM; break;
                case options::AuthMode::NEGOTIATE: mode = CURLAUTH_NEGOTIATE; break;
                case options::AuthMode::ANY      : mode = static_cast<long>(CURLAUTH_ANY); break;
                case options::AuthMode::ANYSAFE  : mode = static_cast<long>(CURLAUTH_ANYSAFE); break;
                default                          : return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::Session: unknown authentication mode." }
 };
            }
            if (auto status = setOption(CURLOPT_HTTPAUTH, mode); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_USERPWD, auth.GetAuthString()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Configure a bearer token.
         * @param token Token copied into curl.
         * @return Success or the first operation error.
         */
        auto SetBearer(options::Bearer const& token) -> Result<void> {
            if (auto status = setOption(CURLOPT_HTTPAUTH, static_cast<long>(CURLAUTH_BEARER)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_XOAUTH2_BEARER, token.GetToken()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Replace the User-Agent header.
         * @param ua User-agent text.
         * @return Success or the first operation error.
         */
        auto SetUserAgent(UserAgent const& ua) -> Result<void> {
            if (auto status = setOption(CURLOPT_USERAGENT, ua.CStr()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Copy form content for subsequent requests.
         * @param payload URL-encoded form fields.
         * @return Success or the first operation error.
         */
        auto SetPayload(Payload const& payload) -> Result<void> {
            m_content = payload;
            return {};
        }

        /**
         * @brief Move form content for subsequent requests.
         * @param payload URL-encoded form fields.
         * @return Success or the first operation error.
         */
        auto SetPayload(Payload&& payload) -> Result<void> {
            m_content = std::move(payload);
            return {};
        }

        /**
         * @brief Copy proxy mappings.
         * @param proxies Protocol and no_proxy mappings.
         * @return Success or the first operation error.
         */
        auto SetProxies(options::Proxies const& proxies) -> Result<void> {
            m_proxies = proxies;
            return {};
        }

        /**
         * @brief Move proxy mappings.
         * @param proxies Protocol and no_proxy mappings.
         * @return Success or the first operation error.
         */
        auto SetProxies(options::Proxies&& proxies) -> Result<void> {
            m_proxies = std::move(proxies);
            return {};
        }

        /**
         * @brief Copy protocol-specific proxy credentials.
         * @param auth Credentials to own.
         * @return Success or the first operation error.
         */
        auto SetProxyAuth(options::ProxyAuthentication const& auth) -> Result<void> {
            m_proxy_auth = auth;
            return {};
        }

        /**
         * @brief Move protocol-specific proxy credentials.
         * @param auth Credentials to own.
         * @return Success or the first operation error.
         */
        auto SetProxyAuth(options::ProxyAuthentication&& auth) -> Result<void> {
            m_proxy_auth = std::move(auth);
            return {};
        }

        /**
         * @brief Configure certificate and hostname verification together.
         * @param verify Verification preference.
         * @return Success or the first operation error.
         */
        auto SetVerifySsl(options::VerifySsl const& verify) -> Result<void> {
            if (auto status = setOption(CURLOPT_SSL_VERIFYPEER, verify.verify ? 1L : 0L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_SSL_VERIFYHOST, verify.verify ? 2L : 0L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Replace the TLS configuration, copying all in-memory certificate data into curl.
         * @param options Complete configuration; empty sources clear previous credentials or trust overrides.
         * @note Defaults for unavailable optional features are tolerated. A failed call may apply earlier options.
         * @return Success or the first operation error.
         */
        auto SetSslOptions(options::SslOptions const& options) -> Result<void>;

        /**
         * @brief Copy multipart descriptors.
         * @param multipart Parts; buffer bytes remain borrowed.
         * @return Success or the first operation error.
         */
        auto SetMultipart(Multipart const& multipart) -> Result<void> {
            m_content = multipart;
            return {};
        }

        /**
         * @brief Move multipart descriptors.
         * @param multipart Parts; buffer bytes remain borrowed.
         * @return Success or the first operation error.
         */
        auto SetMultipart(Multipart&& multipart) -> Result<void> {
            m_content = std::move(multipart);
            return {};
        }

        /**
         * @brief Configure redirect handling.
         * @param redirect Limits, credential forwarding, and POST preservation.
         * @return Success or the first operation error.
         */
        auto SetRedirect(options::Redirect const& redirect) -> Result<void> {
            if (auto status = setOption(CURLOPT_FOLLOWLOCATION, redirect.follow ? 1L : 0L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_MAXREDIRS, redirect.maximum); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_UNRESTRICTED_AUTH, redirect.cont_send_cred ? 1L : 0L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            long mask{};
            if (any(redirect.post_flags & options::PostRedirectFlags::POST_301)) {
                mask |= CURL_REDIR_POST_301;
            }
            if (any(redirect.post_flags & options::PostRedirectFlags::POST_302)) {
                mask |= CURL_REDIR_POST_302;
            }
            if (any(redirect.post_flags & options::PostRedirectFlags::POST_303)) {
                mask |= CURL_REDIR_POST_303;
            }
            if (auto status = setOption(CURLOPT_POSTREDIR, mask); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Clear the cookie engine and set explicit request cookies.
         * @param cookies Cookies to encode.
         * @return Success or the first operation error.
         */
        auto SetCookies(Cookies const& cookies) -> Result<void> {
            if (auto status = setOption(CURLOPT_COOKIELIST, "ALL"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            auto encoded = cookies.GetEncoded(*m_curl);
            if (!encoded) {
                return std::unexpected{ std::move(encoded.error()) };
            }
            if (auto status = setOption(CURLOPT_COOKIE, encoded->c_str()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Copy body bytes for subsequent requests.
         * @param body Bytes to own.
         * @return Success or the first operation error.
         */
        auto SetBody(Body const& body) -> Result<void> {
            m_content = body;
            return {};
        }

        /**
         * @brief Move body bytes for subsequent requests.
         * @param body Bytes to own.
         * @return Success or the first operation error.
         */
        auto SetBody(Body&& body) -> Result<void> {
            m_content = std::move(body);
            return {};
        }

        /**
         * @brief Borrow body bytes for subsequent requests.
         * @param body View whose bytes must outlive transfers.
         * @return Success or the first operation error.
         */
        auto SetBodyView(BodyView body) -> Result<void> {
            m_content = body;
            return {};
        }

        /**
         * @brief Copy serialized JSON for subsequent requests.
         * @param body JSON bytes to own; supplies a default Content-Type only when sent.
         * @return Success or the first operation error.
         */
        auto SetJsonBody(JsonBody const& body) -> Result<void> {
            m_content = body;
            return {};
        }

        /**
         * @brief Move serialized JSON for subsequent requests.
         * @param body JSON bytes to own; supplies a default Content-Type only when sent.
         * @return Success or the first operation error.
         */
        auto SetJsonBody(JsonBody&& body) -> Result<void> {
            m_content = std::move(body);
            return {};
        }

        /**
         * @brief Configure low-speed cancellation.
         * @param low_speed Minimum rate and observation duration.
         * @return Success or the first operation error.
         */
        auto SetLowSpeed(options::LowSpeed const& low_speed) -> Result<void> {
            if (auto status = setOption(CURLOPT_LOW_SPEED_LIMIT, static_cast<long>(low_speed.limit)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_LOW_SPEED_TIME, static_cast<long>(low_speed.time.count())); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Configure a Unix socket.
         * @param unix_socket Socket path copied into curl.
         * @return Success or the first operation error.
         */
        auto SetUnixSocket(options::UnixSocket const& unix_socket) -> Result<void> {
            if (auto status = setOption(CURLOPT_UNIX_SOCKET_PATH, unix_socket.GetUnixSocketString()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Set or clear the upload producer.
         * @param read Callback used when no Content is configured.
         * @return Success or the first operation error.
         */
        auto SetReadCallback(ReadCallback const& read) -> Result<void> {
            m_read = read;
            return {};
        }

        /**
         * @brief Set or clear a header observer; response headers are still collected.
         * @param header Observer to copy.
         * @return Success or the first operation error.
         */
        auto SetHeaderCallback(HeaderCallback const& header) -> Result<void> {
            m_header_callback = header;
            return {};
        }

        /**
         * @brief Set a body consumer and clear SSE consumption.
         * @param write Consumer; an empty callback restores buffering.
         * @return Success or the first operation error.
         */
        auto SetWriteCallback(WriteCallback const& write) -> Result<void> {
            m_write = write;
            m_sse   = {};

            return {};
        }

        /**
         * @brief Set or clear a progress observer.
         * @param progress Observer; false cancels the transfer.
         * @return Success or the first operation error.
         */
        auto SetProgressCallback(ProgressCallback const& progress) -> Result<void> {
            m_progress = progress;
            return {};
        }

        /**
         * @brief Set a diagnostic observer and enable verbose output when nonempty.
         * @param debug Observer to copy.
         * @return Success or the first operation error.
         */
        auto SetDebugCallback(DebugCallback const& debug) -> Result<void> {
            m_debug = debug;
            if (auto status = SetVerbose(options::Verbose{ bool(m_debug.callback) }); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Set an SSE consumer and clear raw body consumption.
         * @param sse Observer reset to a fresh stream each request.
         * @return Success or the first operation error.
         */
        auto SetServerSentEventCallback(ServerSentEventCallback const& sse) -> Result<void> {
            m_sse   = sse;
            m_write = {};

            return {};
        }

        /**
         * @brief Enable or disable curl diagnostics.
         * @param verbose Logging preference.
         * @return Success or the first operation error.
         */
        auto SetVerbose(options::Verbose const& verbose) -> Result<void> {
            if (auto status = setOption(CURLOPT_VERBOSE, verbose.verbose ? 1L : 0L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Bind an outgoing interface.
         * @param iface Empty text restores automatic selection.
         * @return Success or the first operation error.
         */
        auto SetInterface(options::Interface const& iface) -> Result<void> {
            if (auto status = setOption(CURLOPT_INTERFACE, iface.Str().empty() ? nullptr : iface.CStr()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Choose the first local port.
         * @param local_port Port number.
         * @return Success or the first operation error.
         */
        auto SetLocalPort(options::LocalPort const& local_port) -> Result<void> {
            if (auto status = setOption(CURLOPT_LOCALPORT, static_cast<long>(static_cast<std::uint16_t>(local_port))); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Choose the local port search range.
         * @param local_port_range Number of ports to try.
         * @return Success or the first operation error.
         */
        auto SetLocalPortRange(options::LocalPortRange const& local_port_range) -> Result<void> {
            if (auto status = setOption(CURLOPT_LOCALPORTRANGE, static_cast<long>(static_cast<std::uint16_t>(local_port_range))); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Set the preferred HTTP version.
         * @param version Protocol preference supported by the linked curl build.
         * @return Success or the first operation error.
         */
        auto SetHttpVersion(options::HttpVersion const& version) -> Result<void> {
            long value{};
            switch (version.code) {
                case options::HttpVersionCode::VERSION_NONE               : value = CURL_HTTP_VERSION_NONE; break;
                case options::HttpVersionCode::VERSION_1_0                : value = CURL_HTTP_VERSION_1_0; break;
                case options::HttpVersionCode::VERSION_1_1                : value = CURL_HTTP_VERSION_1_1; break;
                case options::HttpVersionCode::VERSION_2_0                : value = CURL_HTTP_VERSION_2_0; break;
                case options::HttpVersionCode::VERSION_2_0_TLS            : value = CURL_HTTP_VERSION_2TLS; break;
                case options::HttpVersionCode::VERSION_2_0_PRIOR_KNOWLEDGE: value = CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE; break;
                case options::HttpVersionCode::VERSION_3_0                : value = CURL_HTTP_VERSION_3; break;
                case options::HttpVersionCode::VERSION_3_0_ONLY           : value = CURL_HTTP_VERSION_3ONLY; break;
                default                                                   : return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::Session: unknown HTTP version." }
 };
            }
            if (auto status = setOption(CURLOPT_HTTP_VERSION, value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Request one byte range.
         * @param range Range serialized for curl.
         * @return Success or the first operation error.
         */
        auto SetRange(options::Range const& range) -> Result<void> {
            if (auto status = setOption(CURLOPT_RANGE, range.Str().c_str()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Replace hostname resolution overrides.
         * @param resolve One mapping.
         * @return Success or the first operation error.
         */
        auto SetResolve(options::Resolve const& resolve) -> Result<void> {
            if (auto status = SetResolves({ resolve }); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Replace all hostname resolution overrides.
         * @param resolves Mappings; empty clears the list.
         * @return Success or the first operation error.
         */
        auto SetResolves(std::vector<options::Resolve> const& resolves) -> Result<void> {
            CurlList list{ nullptr, &curl_slist_free_all };
            for (auto const& resolve : resolves) {
                for (auto port : resolve.ports) {
                    if (auto status = appendList(list, std::format("{}:{}:{}", resolve.host, port, resolve.addr)); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                }
            }
            if (auto status = setOption(CURLOPT_RESOLVE, list.get()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            curl_slist_free_all(std::exchange(m_curl->resolve_curl_list, list.release()));

            return {};
        }

        /**
         * @brief Request multiple byte ranges.
         * @param multi_range Ranges serialized for curl.
         * @return Success or the first operation error.
         */
        auto SetMultiRange(options::MultiRange const& multi_range) -> Result<void> {
            if (auto status = setOption(CURLOPT_RANGE, multi_range.Str().c_str()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Set response buffer reservation.
         * @param reserve_size Minimum capacity requested before each transfer.
         * @return Success or the first operation error.
         */
        auto SetReserveSize(options::ReserveSize const& reserve_size) -> Result<void> {
            if (auto status = ResponseStringReserve(reserve_size.size); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Copy compression preferences.
         * @param accept_encoding Encodings to advertise and decode.
         * @return Success or the first operation error.
         */
        auto SetAcceptEncoding(options::AcceptEncoding const& accept_encoding) -> Result<void> {
            m_accept_encoding = accept_encoding;
            return {};
        }

        /**
         * @brief Move compression preferences.
         * @param accept_encoding Encodings to advertise and decode.
         * @return Success or the first operation error.
         */
        auto SetAcceptEncoding(options::AcceptEncoding&& accept_encoding) -> Result<void> {
            m_accept_encoding = std::move(accept_encoding);
            return {};
        }

        /**
         * @brief Limit upload and download rates.
         * @param limit_rate Bytes per second; zero means unlimited.
         * @return Success or the first operation error.
         */
        auto SetLimitRate(options::LimitRate const& limit_rate) -> Result<void> {
            if (auto status = setOption(CURLOPT_MAX_RECV_SPEED_LARGE, static_cast<curl_off_t>(limit_rate.downrate)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_MAX_SEND_SPEED_LARGE, static_cast<curl_off_t>(limit_rate.uprate)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Inspect persistent request content.
         * @return Read-only content variant.
         */
        [[nodiscard]] auto GetContent() const -> Content const& { return m_content; }

        /**
         * @brief Remove stored content and detach body/MIME pointers; read callbacks remain configured.
         * @return Success or the first operation error.
         */
        auto RemoveContent() -> Result<void> {
            if (auto status = clearCurlContent(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            m_content = std::monostate{};

            return {};
        }

        /**
         * @brief Set a cancellation flag, independently of progress callback ordering.
         * @param param Shared flag; null disables cancellation.
         * @return Success or the first operation error.
         */
        auto SetCancellationParam(std::shared_ptr<std::atomic_bool> param) -> Result<void> {
            m_cancellation = std::move(param);
            return {};
        }

        /**
         * @brief Append an interceptor while idle.
         * @param interceptor Nonnull interceptor.
         * @return Success or the first operation error.
         */
        auto AddInterceptor(std::shared_ptr<Interceptor> const& interceptor) -> Result<void>;

        /**
         * @brief Reserve response capacity before each request.
         * @param size Zero restores ordinary dynamic allocation.
         * @return Success or the first operation error.
         */
        auto ResponseStringReserve(std::size_t size) -> Result<void> {
            m_reserve_size = size;
            return {};
        }

        /**
         * @brief Perform HEAD and obtain the server's advertised response length.
         * @return Length for a successful HTTP 200 response, or -1 if unknown or unsuccessful.
         */
        [[nodiscard]] auto GetDownloadFileLength() -> Result<CprOffT> {
            auto const response{ Head() };
            if (!response) {
                return std::unexpected{ response.error() };
            }
            CprOffT length{ -1 };
            if (!response->error && response->status_code == 200) {
                (void)curl_easy_getinfo(m_curl->handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
            }
            return length;
        }

        /**
         * @brief Access the easy handle for advanced configuration or prepared transfers.
         * @return Shared holder; options are reset when the session dies.
         */
        [[nodiscard]] auto GetCurlHolder() -> std::shared_ptr<curl::CurlHolder> { return m_curl; }

        /**
         * @brief Combine encoded parameters with the URL's existing query, before any fragment.
         * @return Full request URL.
         */
        [[nodiscard]] auto GetFullRequestUrl() -> Result<std::string> {
            auto       result{ m_url.Str() };
            auto const parameters{ m_parameters.GetContent(*m_curl) };
            if (!parameters) {
                return std::unexpected{ parameters.error() };
            }
            if (parameters->empty()) {
                return result;
            }
            auto const             fragment{ result.find('#') };
            auto const             end{ fragment == std::string::npos ? result.size() : fragment };
            std::string_view const base{ result.data(), end };
            std::string            separator;
            if (base.find('?') == std::string_view::npos) {
                separator = "?";
            } else if (!base.ends_with('?') && !base.ends_with('&')) {
                separator = "&";
            }
            result.insert(end, separator + *parameters);
            return result;
        }

        /**
         * @brief Obtain shared ownership for asynchronous work.
         * @return Shared session.
         */
        [[nodiscard]] auto GetSharedPtrFromThis() -> Result<std::shared_ptr<Session>> {
            auto shared{ weak_from_this().lock() };
            if (!shared) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::Session: asynchronous requests require std::shared_ptr ownership." }
                };
            }
            return shared;
        }

        /**
         * @brief Prepare a GET download into a temporary consumer.
         * @param write Consumer copied for this download only.
         * @return Success or the first operation error.
         */
        auto PrepareDownload(WriteCallback const& write) -> Result<void> {
            if (auto status = prepare("GET", true); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            m_download_write = write;

            return {};
        }

        /**
         * @brief Prepare a GET download into a borrowed binary stream.
         * @param file Stream that must outlive completion.
         * @return Success or the first operation error.
         */
        auto PrepareDownload(std::ofstream& file) -> Result<void> {
            if (auto status = prepare("GET", true); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            m_download_file = &file;

            return {};
        }

        /**
         * @brief Download into a callback without retaining it for later requests.
         * @param write Download consumer.
         * @return Transfer metadata with an empty body.
         */
        auto Download(WriteCallback const& write) -> Result<Response> {
            if (auto status = PrepareDownload(write); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return perform();
        }

        /**
         * @brief Download into a borrowed binary output stream.
         * @param file Output stream; the caller checks later flush/close errors.
         * @return Transfer metadata with an empty body.
         */
        auto Download(std::ofstream& file) -> Result<Response> {
            if (auto status = PrepareDownload(file); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return perform();
        }

        /**
         * @brief Download asynchronously into a callback.
         * @param write Copied consumer.
         * @return Future retaining this session.
         */
        auto DownloadAsync(WriteCallback const& write) -> Result<AsyncResponse> {
            return async([self = shared_from_this(), write] { return self->Download(write); });
        }

        /**
         * @brief Download asynchronously into a stream.
         * @param file Stream that must outlive completion.
         * @return Future retaining this session.
         */
        auto DownloadAsync(std::ofstream& file) -> Result<AsyncResponse> {
            return async([self = shared_from_this(), &file] { return self->Download(file); });
        }

        /**
         * @brief Capture a prepared transfer after curl_easy_perform or an external multi loop finishes.
         * @param curl_error Result returned by curl for this transfer.
         * @return Independent response snapshot.
         * @throws Any exception captured from user callbacks during the transfer.
         */
        auto Complete(CURLcode curl_error) -> Result<Response> {
            m_download_file  = nullptr;
            m_download_write = {};
            if (m_callback_error) {
                std::rethrow_exception(std::exchange(m_callback_error, {}));
            }
            CurlList owned_cookies{ nullptr, &curl_slist_free_all };
            // The variadic curl API requires an explicit pointer conversion.
            if (auto status = checkCurl(curl_easy_getinfo(m_curl->handle, CURLINFO_COOKIELIST, static_cast<curl_slist**>(std::out_ptr(owned_cookies)))); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            auto cookies{ utils::parse_cookies(owned_cookies.get()) };
            if (!cookies) {
                return std::unexpected{ std::move(cookies.error()) };
            }
            std::string error_message{ m_curl->error.data() };
            if (curl_error != CURLE_OK && error_message.empty()) {
                error_message = curl_easy_strerror(curl_error);
            }
            return Response::FromCurl(
                m_curl,
                std::move(m_response_string),
                std::move(m_header_string),
                std::move(*cookies),
                Error{ static_cast<std::int32_t>(curl_error), std::move(error_message) }
            );
        }

        /**
         * @brief Complete a prepared download.
         * @param curl_error Curl transfer result.
         * @return Download metadata.
         */
        auto CompleteDownload(CURLcode curl_error) -> Result<Response> { return Complete(curl_error); }

        /**
         * @brief Prepare DELETE without starting network I/O.
         * @return Success or the first operation error.
         */
        auto PrepareDelete() -> Result<void> {
            if (auto status = prepare("DELETE"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Execute DELETE with the stored options.
         * @return Completed response.
         */
        auto Delete() -> Result<Response> {
            if (auto status = PrepareDelete(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return perform();
        }

        /**
         * @brief Execute DELETE asynchronously.
         * @return Future retaining shared ownership of this session.
         */
        auto DeleteAsync() -> Result<AsyncResponse> {
            return async([self = shared_from_this()] { return self->Delete(); });
        }

        /**
         * @brief Pass a DELETE response to an asynchronous continuation.
         * @tparam Then Continuation type.
         * @param then Consumer of the response.
         * @return Future containing the consumer's result.
         */
        template <typename Then>
        auto DeleteCallback(Then then) {
            return async([self = shared_from_this(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Delete()); });
        }

        /**
         * @brief Prepare GET without starting network I/O.
         * @return Success or the first operation error.
         */
        auto PrepareGet() -> Result<void> {
            if (auto status = prepare("GET"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Execute GET with the stored options.
         * @return Completed response.
         */
        auto Get() -> Result<Response> {
            if (auto status = PrepareGet(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return perform();
        }

        /**
         * @brief Execute GET asynchronously.
         * @return Future retaining shared ownership of this session.
         */
        auto GetAsync() -> Result<AsyncResponse> {
            return async([self = shared_from_this()] { return self->Get(); });
        }

        /**
         * @brief Pass a GET response to an asynchronous continuation.
         * @tparam Then Continuation type.
         * @param then Consumer of the response.
         * @return Future containing the consumer's result.
         */
        template <typename Then>
        auto GetCallback(Then then) {
            return async([self = shared_from_this(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Get()); });
        }

        /**
         * @brief Prepare HEAD without starting network I/O.
         * @return Success or the first operation error.
         */
        auto PrepareHead() -> Result<void> {
            if (auto status = prepare("HEAD"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Execute HEAD with the stored options.
         * @return Completed response.
         */
        auto Head() -> Result<Response> {
            if (auto status = PrepareHead(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return perform();
        }

        /**
         * @brief Execute HEAD asynchronously.
         * @return Future retaining shared ownership of this session.
         */
        auto HeadAsync() -> Result<AsyncResponse> {
            return async([self = shared_from_this()] { return self->Head(); });
        }

        /**
         * @brief Pass a HEAD response to an asynchronous continuation.
         * @tparam Then Continuation type.
         * @param then Consumer of the response.
         * @return Future containing the consumer's result.
         */
        template <typename Then>
        auto HeadCallback(Then then) {
            return async([self = shared_from_this(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Head()); });
        }

        /**
         * @brief Prepare OPTIONS without starting network I/O.
         * @return Success or the first operation error.
         */
        auto PrepareOptions() -> Result<void> {
            if (auto status = prepare("OPTIONS"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Execute OPTIONS with the stored options.
         * @return Completed response.
         */
        auto Options() -> Result<Response> {
            if (auto status = PrepareOptions(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return perform();
        }

        /**
         * @brief Execute OPTIONS asynchronously.
         * @return Future retaining shared ownership of this session.
         */
        auto OptionsAsync() -> Result<AsyncResponse> {
            return async([self = shared_from_this()] { return self->Options(); });
        }

        /**
         * @brief Pass a OPTIONS response to an asynchronous continuation.
         * @tparam Then Continuation type.
         * @param then Consumer of the response.
         * @return Future containing the consumer's result.
         */
        template <typename Then>
        auto OptionsCallback(Then then) {
            return async([self = shared_from_this(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Options()); });
        }

        /**
         * @brief Prepare PATCH without starting network I/O.
         * @return Success or the first operation error.
         */
        auto PreparePatch() -> Result<void> {
            if (auto status = prepare("PATCH"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Execute PATCH with the stored options.
         * @return Completed response.
         */
        auto Patch() -> Result<Response> {
            if (auto status = PreparePatch(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return perform();
        }

        /**
         * @brief Execute PATCH asynchronously.
         * @return Future retaining shared ownership of this session.
         */
        auto PatchAsync() -> Result<AsyncResponse> {
            return async([self = shared_from_this()] { return self->Patch(); });
        }

        /**
         * @brief Pass a PATCH response to an asynchronous continuation.
         * @tparam Then Continuation type.
         * @param then Consumer of the response.
         * @return Future containing the consumer's result.
         */
        template <typename Then>
        auto PatchCallback(Then then) {
            return async([self = shared_from_this(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Patch()); });
        }

        /**
         * @brief Prepare POST without starting network I/O.
         * @return Success or the first operation error.
         */
        auto PreparePost() -> Result<void> {
            if (auto status = prepare("POST"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Execute POST with the stored options.
         * @return Completed response.
         */
        auto Post() -> Result<Response> {
            if (auto status = PreparePost(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return perform();
        }

        /**
         * @brief Execute POST asynchronously.
         * @return Future retaining shared ownership of this session.
         */
        auto PostAsync() -> Result<AsyncResponse> {
            return async([self = shared_from_this()] { return self->Post(); });
        }

        /**
         * @brief Pass a POST response to an asynchronous continuation.
         * @tparam Then Continuation type.
         * @param then Consumer of the response.
         * @return Future containing the consumer's result.
         */
        template <typename Then>
        auto PostCallback(Then then) {
            return async([self = shared_from_this(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Post()); });
        }

        /**
         * @brief Prepare PUT without starting network I/O.
         * @return Success or the first operation error.
         */
        auto PreparePut() -> Result<void> {
            if (auto status = prepare("PUT"); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Execute PUT with the stored options.
         * @return Completed response.
         */
        auto Put() -> Result<Response> {
            if (auto status = PreparePut(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return perform();
        }

        /**
         * @brief Execute PUT asynchronously.
         * @return Future retaining shared ownership of this session.
         */
        auto PutAsync() -> Result<AsyncResponse> {
            return async([self = shared_from_this()] { return self->Put(); });
        }

        /**
         * @brief Pass a PUT response to an asynchronous continuation.
         * @tparam Then Continuation type.
         * @param then Consumer of the response.
         * @return Future containing the consumer's result.
         */
        template <typename Then>
        auto PutCallback(Then then) {
            return async([self = shared_from_this(), then = std::move(then)]() mutable { return std::invoke(std::move(then), self->Put()); });
        }

        /**
         * @brief Forward a Url option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(Url const& value) -> Result<void> {
            if (auto status = SetUrl(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Parameters option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(Parameters const& value) -> Result<void> {
            if (auto status = SetParameters(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Move a Parameters option into this session.
         * @param value Option to transfer.
         * @return Success or the first operation error.
         */
        auto SetOption(Parameters&& value) -> Result<void> {
            if (auto status = SetParameters(std::move(value)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Header option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(Header const& value) -> Result<void> {
            if (auto status = SetHeader(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Timeout option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::Timeout const& value) -> Result<void> {
            if (auto status = SetTimeout(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a ConnectTimeout option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::ConnectTimeout const& value) -> Result<void> {
            if (auto status = SetConnectTimeout(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a ConnectionPool option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(ConnectionPool const& value) -> Result<void> {
            if (auto status = SetConnectionPool(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Authentication option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::Authentication const& value) -> Result<void> {
            if (auto status = SetAuth(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Bearer option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::Bearer const& value) -> Result<void> {
            if (auto status = SetBearer(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a UserAgent option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(UserAgent const& value) -> Result<void> {
            if (auto status = SetUserAgent(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Payload option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(Payload const& value) -> Result<void> {
            if (auto status = SetPayload(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Move a Payload option into this session.
         * @param value Option to transfer.
         * @return Success or the first operation error.
         */
        auto SetOption(Payload&& value) -> Result<void> {
            if (auto status = SetPayload(std::move(value)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Proxies option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::Proxies const& value) -> Result<void> {
            if (auto status = SetProxies(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Move a Proxies option into this session.
         * @param value Option to transfer.
         * @return Success or the first operation error.
         */
        auto SetOption(options::Proxies&& value) -> Result<void> {
            if (auto status = SetProxies(std::move(value)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Copy proxy authentication.
         * @param value Protocol credentials.
         * @return Success or the first operation error.
         */
        auto SetOption(options::ProxyAuthentication const& value) -> Result<void> {
            if (auto status = SetProxyAuth(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Move proxy authentication.
         * @param value Protocol credentials.
         * @return Success or the first operation error.
         */
        auto SetOption(options::ProxyAuthentication&& value) -> Result<void> {
            if (auto status = SetProxyAuth(std::move(value)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Apply combined TLS verification.
         * @param value Verification preference.
         * @return Success or the first operation error.
         */
        auto SetOption(options::VerifySsl const& value) -> Result<void> {
            if (auto status = SetVerifySsl(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Replace TLS configuration.
         * @param value Owned TLS options.
         * @return Success or the first operation error.
         */
        auto SetOption(options::SslOptions const& value) -> Result<void> {
            if (auto status = SetSslOptions(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Multipart option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(Multipart const& value) -> Result<void> {
            if (auto status = SetMultipart(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Move a Multipart option into this session.
         * @param value Option to transfer.
         * @return Success or the first operation error.
         */
        auto SetOption(Multipart&& value) -> Result<void> {
            if (auto status = SetMultipart(std::move(value)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Redirect option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::Redirect const& value) -> Result<void> {
            if (auto status = SetRedirect(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Cookies option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(Cookies const& value) -> Result<void> {
            if (auto status = SetCookies(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Body option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(Body const& value) -> Result<void> {
            if (auto status = SetBody(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Move a Body option into this session.
         * @param value Option to transfer.
         * @return Success or the first operation error.
         */
        auto SetOption(Body&& value) -> Result<void> {
            if (auto status = SetBody(std::move(value)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a BodyView option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(BodyView value) -> Result<void> {
            if (auto status = SetBodyView(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a copied JsonBody option to its setter.
         * @param value Serialized JSON option.
         * @return Success or the first operation error.
         */
        auto SetOption(JsonBody const& value) -> Result<void> {
            if (auto status = SetJsonBody(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a moved JsonBody option to its setter.
         * @param value Serialized JSON option.
         * @return Success or the first operation error.
         */
        auto SetOption(JsonBody&& value) -> Result<void> {
            if (auto status = SetJsonBody(std::move(value)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a ReadCallback option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(ReadCallback const& value) -> Result<void> {
            if (auto status = SetReadCallback(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a HeaderCallback option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(HeaderCallback const& value) -> Result<void> {
            if (auto status = SetHeaderCallback(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a WriteCallback option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(WriteCallback const& value) -> Result<void> {
            if (auto status = SetWriteCallback(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a ProgressCallback option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(ProgressCallback const& value) -> Result<void> {
            if (auto status = SetProgressCallback(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a DebugCallback option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(DebugCallback const& value) -> Result<void> {
            if (auto status = SetDebugCallback(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a ServerSentEventCallback option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(ServerSentEventCallback const& value) -> Result<void> {
            if (auto status = SetServerSentEventCallback(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a LowSpeed option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::LowSpeed const& value) -> Result<void> {
            if (auto status = SetLowSpeed(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Verbose option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::Verbose const& value) -> Result<void> {
            if (auto status = SetVerbose(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a UnixSocket option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::UnixSocket const& value) -> Result<void> {
            if (auto status = SetUnixSocket(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Interface option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::Interface const& value) -> Result<void> {
            if (auto status = SetInterface(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a LocalPort option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::LocalPort const& value) -> Result<void> {
            if (auto status = SetLocalPort(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a LocalPortRange option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::LocalPortRange const& value) -> Result<void> {
            if (auto status = SetLocalPortRange(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a HttpVersion option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::HttpVersion const& value) -> Result<void> {
            if (auto status = SetHttpVersion(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Range option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::Range const& value) -> Result<void> {
            if (auto status = SetRange(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a MultiRange option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::MultiRange const& value) -> Result<void> {
            if (auto status = SetMultiRange(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a ReserveSize option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::ReserveSize const& value) -> Result<void> {
            if (auto status = SetReserveSize(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a AcceptEncoding option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::AcceptEncoding const& value) -> Result<void> {
            if (auto status = SetAcceptEncoding(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Move a AcceptEncoding option into this session.
         * @param value Option to transfer.
         * @return Success or the first operation error.
         */
        auto SetOption(options::AcceptEncoding&& value) -> Result<void> {
            if (auto status = SetAcceptEncoding(std::move(value)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a LimitRate option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::LimitRate const& value) -> Result<void> {
            if (auto status = SetLimitRate(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a Resolve option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(options::Resolve const& value) -> Result<void> {
            if (auto status = SetResolve(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Forward a std::vector<Resolve> option to its setter.
         * @param value Option to apply.
         * @return Success or the first operation error.
         */
        auto SetOption(std::vector<options::Resolve> const& value) -> Result<void> {
            if (auto status = SetResolves(value); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

    private:
        /**
         * @brief Retain an initialized curl holder before applying defaults.
         */
        explicit Session(std::shared_ptr<curl::CurlHolder> holder) noexcept : m_curl{ std::move(holder) } {}

        /**
         * @brief Apply session defaults, returning the first curl configuration failure.
         * @return Success or the first operation error.
         */
        auto initialize() -> Result<void> {
            auto const* version{ curl_version_info(CURLVERSION_NOW) };
            if (auto status = SetUserAgent(UserAgent{ std::string{ "curl/" } + version->version }); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = SetRedirect(options::Redirect{}); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_COOKIEFILE, ""); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_NOSIGNAL, 1L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_TCP_KEEPALIVE, 1L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_CERTINFO, 1L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        using CurlList = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;
        using CurlMime = std::unique_ptr<curl_mime, decltype(&curl_mime_free)>;

        /**
         * @brief Translate curl setup failures into explicit results.
         * @param code Setup result.
         * @return Success or the first operation error.
         */
        static auto checkCurl(CURLcode code) -> Result<void> {
            if (code != CURLE_OK) {
                return std::unexpected{
                    Error{ static_cast<std::int32_t>(code), std::string{ "mcr::Session: " } + curl_easy_strerror(code) }
                };
            }

            return {};
        }

        /**
         * @brief Apply an option with its exact curl argument type.
         * @tparam T Argument type.
         * @param option Curl option.
         * @param value Option value.
         * @return Success or the first operation error.
         */
        template <typename T>
        auto setOption(CURLoption option, T value) -> Result<void> {
            if (auto status = checkCurl(curl_easy_setopt(m_curl->handle, option, value)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Append without losing ownership on allocation failure.
         * @param list Owned list.
         * @param value Entry text.
         * @return Success or the first operation error.
         */
        static auto appendList(CurlList& list, std::string const& value) -> Result<void> {
            auto* next{ curl_slist_append(list.get(), value.c_str()) };
            if (!next) {
                throw std::bad_alloc{};
            }
            (void)list.release();
            list.reset(next);

            return {};
        }

        /**
         * @brief Detach the previous content before freeing MIME data or replacing borrowed bytes.
         * @return Success or the first operation error.
         */
        auto clearCurlContent() -> Result<void> {
            if (auto status = setOption(CURLOPT_MIMEPOST, static_cast<curl_mime*>(nullptr)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_POSTFIELDS, static_cast<char const*>(nullptr)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(-1)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            curl_mime_free(std::exchange(m_curl->multipart, nullptr));

            return {};
        }

        /**
         * @brief Rebuild headers, preserving explicit values ahead of inferred defaults.
         * @param chunked Whether an unknown-sized upload needs chunking.
         * @param json_body Whether this transfer sends a JSON body.
         * @return Success or the first operation error.
         */
        auto prepareHeader(bool chunked, bool json_body) -> Result<void> {
            CurlList list{ nullptr, &curl_slist_free_all };
            for (auto const& [name, value] : m_header) {
                if (auto status = appendList(list, name + (value.empty() ? ";" : ": " + value)); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
            if (json_body && !m_header.contains("Content-Type")) {
                if (auto status = appendList(list, "Content-Type: application/json"); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
            if (chunked && !m_header.contains("Transfer-Encoding")) {
                if (auto status = appendList(list, "Transfer-Encoding: chunked"); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
            if (!m_header.contains("Expect")) {
                if (auto status = appendList(list, "Expect:"); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
            if (auto status = setOption(CURLOPT_HTTPHEADER, list.get()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            curl_slist_free_all(std::exchange(m_curl->chunk, list.release()));

            return {};
        }

        /**
         * @brief Select proxy options afresh so prior protocols and no_proxy settings cannot leak.
         * @return Success or the first operation error.
         */
        auto prepareProxy() -> Result<void> {
            auto protocol{ m_url.Str().substr(0, m_url.Str().find(':')) };
            std::ranges::transform(protocol, protocol.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (auto status = setOption(CURLOPT_PROXY, m_proxies.Has(protocol) ? m_proxies[protocol].c_str() : nullptr); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (m_proxies.Has(protocol) && m_proxy_auth.Has(protocol)) {
                // CURLOPT_PROXYUSERNAME/PASSWORD expect raw bytes, unlike credentials inside a proxy URL.
                auto const username{ utils::url_decode(m_proxy_auth.GetUsernameUnderlying(protocol)) };
                auto const password{ utils::url_decode(m_proxy_auth.GetPasswordUnderlying(protocol)) };
                if (!username) {
                    return std::unexpected{ username.error() };
                }
                if (auto status = setOption(CURLOPT_PROXYUSERNAME, username->c_str()); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
                if (!password) {
                    return std::unexpected{ password.error() };
                }
                if (auto status = setOption(CURLOPT_PROXYPASSWORD, password->c_str()); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            } else {
                if (auto status = setOption(CURLOPT_PROXYUSERNAME, static_cast<char const*>(nullptr)); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
                if (auto status = setOption(CURLOPT_PROXYPASSWORD, static_cast<char const*>(nullptr)); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
            char const* no_proxy{ nullptr };
            if (m_proxies.Has("no_proxy")) {
                no_proxy = m_proxies["no_proxy"].c_str();
            } else if (m_proxies.Has("NO_PROXY")) {
                no_proxy = m_proxies["NO_PROXY"].c_str();
            }
            if (auto status = setOption(CURLOPT_NOPROXY, no_proxy); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Copy MIME fields into an owned curl MIME tree.
         * @param multipart Persistent descriptors.
         * @return Success or the first operation error.
         */
        auto prepareMultipart(Multipart const& multipart) -> Result<void> {
            CurlMime mime{ curl_mime_init(m_curl->handle), &curl_mime_free };
            if (!mime) {
                throw std::bad_alloc{};
            }
            for (auto const& part : multipart.parts) {
                auto add_part = [&]() -> Result<curl_mimepart*> {
                    auto* item{ curl_mime_addpart(mime.get()) };
                    if (!item) {
                        throw std::bad_alloc{};
                    }
                    if (auto status = checkCurl(curl_mime_name(item, part.name.c_str())); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                    if (!part.content_type.empty()) {
                        if (auto status = checkCurl(curl_mime_type(item, part.content_type.c_str())); !status) {
                            return std::unexpected{ std::move(status.error()) };
                        }
                    }
                    return item;
                };
                if (part.is_file) {
                    for (auto const& file : part.files) {
                        auto item_result = add_part();
                        if (!item_result) {
                            return std::unexpected{ std::move(item_result.error()) };
                        }
                        auto* item = *item_result;
                        if (auto status = checkCurl(curl_mime_filedata(item, file.filepath.c_str())); !status) {
                            return std::unexpected{ std::move(status.error()) };
                        }
                        auto const filename{ file.HasOverridenFilename() ? file.overriden_filename : std::filesystem::path{ file.filepath }.filename().string() };
                        if (auto status = checkCurl(curl_mime_filename(item, filename.c_str())); !status) {
                            return std::unexpected{ std::move(status.error()) };
                        }
                    }
                } else {
                    auto item_result = add_part();
                    if (!item_result) {
                        return std::unexpected{ std::move(item_result.error()) };
                    }
                    auto* item = *item_result;
                    if (part.is_buffer) {
                        if (auto status = checkCurl(curl_mime_data(item, part.datalen == 0 ? "" : part.data, part.datalen)); !status) {
                            return std::unexpected{ std::move(status.error()) };
                        }
                        if (auto status = checkCurl(curl_mime_filename(item, part.value.c_str())); !status) {
                            return std::unexpected{ std::move(status.error()) };
                        }
                    } else {
                        if (auto status = checkCurl(curl_mime_data(item, part.value.data(), part.value.size())); !status) {
                            return std::unexpected{ std::move(status.error()) };
                        }
                    }
                }
            }
            if (auto status = setOption(CURLOPT_MIMEPOST, mime.get()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            m_curl->multipart = mime.release();

            return {};
        }

        /**
         * @brief Configure a binary body with an explicit byte length.
         * @param body Body bytes.
         * @param copy Whether curl must own a copy.
         * @return Success or the first operation error.
         */
        auto prepareBytes(std::string_view body, bool copy) -> Result<void> {
            if (auto status = setOption(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size())); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(copy ? CURLOPT_COPYPOSTFIELDS : CURLOPT_POSTFIELDS, body.empty() ? "" : body.data()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Reset method state and prepare a complete transfer.
         * @param method HTTP method.
         * @param download Whether to bypass stored content and body consumers.
         * @return Success or the first operation error.
         */
        auto prepare(std::string_view method, bool download = false) -> Result<void> {
            if (m_in_transfer || (m_multi_owner && !m_multi_preparing)) {
                return std::unexpected{
                    Error{ ErrorCode::RECURSIVE_API_CALL, "mcr::Session: handle is in use by a transfer or MultiPerform." }
                };
            }
            m_method = method;
            if (auto status = clearCurlContent(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_UPLOAD, 0L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_NOBODY, 0L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_HTTPGET, 1L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_CUSTOMREQUEST, static_cast<char const*>(nullptr)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            m_downloading    = download;
            m_download_file  = nullptr;
            m_download_write = {};
            m_callback_error = {};
            m_curl->error.fill('\0');
            m_response_string.clear();
            m_header_string.clear();
            if (!download) {
                m_response_string.reserve(m_reserve_size);
            }
            m_sse_parser.Reset();
            bool const has_content{ !std::holds_alternative<std::monostate>(m_content) };
            bool const read_upload{ !download && method != "HEAD" && !has_content && bool(m_read.callback) };
            if (!download && method != "HEAD") {
                if (auto const* payload{ std::get_if<Payload>(&m_content) }) {
                    auto encoded = payload->GetContent(*m_curl);
                    if (!encoded) {
                        return std::unexpected{ std::move(encoded.error()) };
                    }
                    if (auto status = prepareBytes(*encoded, true); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                } else if (auto const* body{ std::get_if<Body>(&m_content) }) {
                    if (auto status = prepareBytes(body->Str(), true); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                } else if (auto const* json{ std::get_if<JsonBody>(&m_content) }) {
                    if (auto status = prepareBytes(json->Str(), true); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                } else if (auto const* view{ std::get_if<BodyView>(&m_content) }) {
                    if (auto status = prepareBytes(view->Str(), false); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                } else if (auto const* multipart{ std::get_if<Multipart>(&m_content) }) {
                    if (auto status = prepareMultipart(*multipart); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                } else if (read_upload) {
                    if (auto status = setOption(CURLOPT_POST, 1L); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                    if (auto status = setOption(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(m_read.size)); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                } else if (method == "POST" || method == "PUT" || method == "PATCH") {
                    if (auto status = prepareBytes({}, false); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                }
            }
            if (method == "HEAD") {
                if (auto status = setOption(CURLOPT_NOBODY, 1L); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            } else if (method != "POST" && (method != "GET" || (!download && (has_content || read_upload)))) {
                if (auto status = setOption(CURLOPT_CUSTOMREQUEST, std::string{ method }.c_str()); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
            if (method == "PUT") {
                if (auto status = setOption(CURLOPT_RANGE, static_cast<char const*>(nullptr)); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
            if (auto status = prepareHeader(read_upload && m_read.size == -1, !download && method != "HEAD" && std::holds_alternative<JsonBody>(m_content)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            auto url = GetFullRequestUrl();
            if (!url) {
                return std::unexpected{ std::move(url.error()) };
            }
            if (auto status = setOption(CURLOPT_URL, url->c_str()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = prepareProxy(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            auto const encodings{ m_accept_encoding.GetString() };
            auto       disabled = m_accept_encoding.Disabled();
            if (!disabled) {
                return std::unexpected{ std::move(disabled.error()) };
            }
            if (auto status = setOption(CURLOPT_ACCEPT_ENCODING, *disabled ? nullptr : encodings.c_str()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_WRITEFUNCTION, &writeCallback); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_WRITEDATA, static_cast<void*>(this)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_HEADERFUNCTION, &headerCallback); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_HEADERDATA, static_cast<void*>(this)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_READFUNCTION, &readCallback); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_READDATA, static_cast<void*>(this)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_XFERINFOFUNCTION, &progressCallback); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_XFERINFODATA, static_cast<void*>(this)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_NOPROGRESS, m_cancellation || m_progress.callback || m_debug.callback ? 0L : 1L); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_DEBUGFUNCTION, &debugCallback); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setOption(CURLOPT_DEBUGDATA, static_cast<void*>(this)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        }

        /**
         * @brief Execute the prepared easy handle and snapshot its result.
         * @return Completed response.
         */
        auto perform() -> Result<Response>;
        /**
         * @brief Reprepare the current method and continue downstream interceptors.
         * @return Downstream response.
         */
        auto proceed() -> Result<Response>;

        /**
         * @brief Receive body bytes without allowing C++ exceptions through curl.
         */
        static auto writeCallback(char* data, std::size_t size, std::size_t count, void* context) noexcept -> std::size_t {
            auto& self{ *static_cast<Session*>(context) };
            if (self.m_callback_error) {
                return 0;
            }
            auto const length{ size * count };
            try {
                std::string_view const bytes{ data, length };
                if (self.m_downloading) {
                    if (self.m_download_file) {
                        return utils::write_file_function(data, size, count, self.m_download_file);
                    }
                    return self.m_download_write(bytes) ? length : 0;
                }
                if (self.m_write.callback) {
                    return self.m_write(bytes) ? length : 0;
                }
                if (self.m_sse.callback) {
                    return self.m_sse_parser.Parse(bytes, [&](ServerSentEvent&& event) { return self.m_sse(std::move(event)); }) ? length : 0;
                }
                self.m_response_string.append(bytes);
                return length;
            } catch (...) {
                self.m_callback_error = std::current_exception();
                return 0;
            }
        }

        /**
         * @brief Collect headers and notify the observer while containing exceptions.
         */
        static auto headerCallback(char* data, std::size_t size, std::size_t count, void* context) noexcept -> std::size_t {
            auto& self{ *static_cast<Session*>(context) };
            if (self.m_callback_error) {
                return 0;
            }
            auto const length{ size * count };
            try {
                std::string_view const bytes{ data, length };
                self.m_header_string.append(bytes);
                return self.m_header_callback(bytes) ? length : 0;
            } catch (...) {
                self.m_callback_error = std::current_exception();
                return 0;
            }
        }

        /**
         * @brief Fill upload bytes while validating the producer's reported size.
         */
        static auto readCallback(char* data, std::size_t size, std::size_t count, void* context) noexcept -> std::size_t {
            auto& self{ *static_cast<Session*>(context) };
            if (self.m_callback_error) {
                return CURL_READFUNC_ABORT;
            }
            try {
                if (!self.m_read.callback) {
                    return 0;
                }
                auto length{ size * count };
                if (!self.m_read(data, length) || length > size * count) {
                    return CURL_READFUNC_ABORT;
                }
                return length;
            } catch (...) {
                self.m_callback_error = std::current_exception();
                return CURL_READFUNC_ABORT;
            }
        }

        /**
         * @brief Combine explicit cancellation and progress observer decisions.
         */
        static auto progressCallback(void* context, curl_off_t total_down, curl_off_t now_down, curl_off_t total_up, curl_off_t now_up) noexcept -> int {
            auto& self{ *static_cast<Session*>(context) };
            if (self.m_callback_error || (self.m_cancellation && self.m_cancellation->load())) {
                return 1;
            }
            try {
                return self.m_progress(total_down, now_down, total_up, now_up) ? 0 : 1;
            } catch (...) {
                self.m_callback_error = std::current_exception();
                return 1;
            }
        }

        /**
         * @brief Deliver curl diagnostics while containing observer exceptions.
         */
        static auto debugCallback(CURL*, curl_infotype type, char* data, std::size_t size, void* context) noexcept -> int {
            auto& self{ *static_cast<Session*>(context) };
            if (!self.m_callback_error) {
                try {
                    self.m_debug(static_cast<DebugCallback::InfoType>(type), { data, size });
                } catch (...) {
                    self.m_callback_error = std::current_exception();
                }
            }
            return 0;
        }
    };

    /**
     * @brief Execute sessions concurrently on one curl multi handle and return registration-order responses.
     * @note Sessions belong to one batch until removed or the batch is destroyed. Access a batch from one
     * caller at a time. Batch interceptors run instead of individual Session interceptors, as in cpr.
     * Membership changes should use AddSession/RemoveSession; edits through GetSessions are validated
     * before the next batch operation. A moved-from batch can be reused.
     */
    class MultiPerform {
    public:
        /**
         * @brief Per-session HTTP method, or UNDEFINED until a batch method is selected.
         */
        enum class HttpMethod : std::uint8_t {
            UNDEFINED,
            GET_REQUEST,
            POST_REQUEST,
            PUT_REQUEST,
            DELETE_REQUEST,
            PATCH_REQUEST,
            HEAD_REQUEST,
            OPTIONS_REQUEST,
            DOWNLOAD_REQUEST
        };
        using Sessions = std::vector<std::pair<std::shared_ptr<Session>, HttpMethod>>; ///< Ordered registrations.

    private:
        friend InterceptorMulti;
        using DownloadTarget = std::variant<WriteCallback, std::reference_wrapper<std::ofstream>>;
        Sessions                                       m_sessions;           ///< Sessions and their current methods.
        std::vector<std::weak_ptr<Session>>            m_claimed;            ///< Claims used to release ownership after mutable list edits.
        std::unique_ptr<curl::CurlMultiHolder>         m_multi;              ///< Owned multi handle, allocated lazily after moving out.
        std::unordered_map<Session*, DownloadTarget>   m_downloads;          ///< Destinations for the current batch request.
        std::vector<std::shared_ptr<InterceptorMulti>> m_interceptors;       ///< Batch interceptor chain.
        std::size_t                                    m_next_interceptor{}; ///< Next interceptor for nested retries.
        std::size_t                                    m_request_depth{};    ///< Active request frames.
        bool                                           m_transferring{};     ///< True only while easy handles are attached to the multi handle.

    public:
        /**
         * @brief Create an empty batch.
         */
        MultiPerform();
        MultiPerform(MultiPerform const&)                    = delete;
        auto operator=(MultiPerform const&) -> MultiPerform& = delete;
        /**
         * @brief Move an idle batch and transfer session claims.
         * @param other Idle source batch.
         * @pre Neither batch is executing.
         */
        MultiPerform(MultiPerform&& other) noexcept;
        /**
         * @brief Replace an idle batch and transfer session claims.
         * @param other Idle source batch.
         * @return This batch.
         * @pre Neither batch is executing.
         */
        auto operator=(MultiPerform&& other) noexcept -> MultiPerform&;
        /**
         * @brief Release all session claims.
         * @pre No batch request is executing.
         */
        ~MultiPerform();
        /**
         * @brief Register a session in this batch.
         * @param session Nonnull, unowned session.
         * @param method Initial method.
         * @return Success or the first operation error.
         */
        auto               AddSession(std::shared_ptr<Session> const& session, HttpMethod method = HttpMethod::UNDEFINED) -> Result<void>;
        /**
         * @brief Remove an existing registration and release its claim.
         * @param session Registered session.
         * @return Success or the first operation error.
         */
        auto               RemoveSession(std::shared_ptr<Session> const& session) -> Result<void>;
        /**
         * @brief Access registrations while no network transfer is active.
         * @return Ordered mutable registrations, including methods.
         */
        [[nodiscard]] auto GetSessions() -> Result<std::reference_wrapper<Sessions>>;

        /**
         * @brief Inspect registrations.
         * @return Ordered registrations.
         */
        [[nodiscard]] auto GetSessions() const noexcept -> Sessions const& { return m_sessions; }

        /**
         * @brief Append a batch interceptor while idle.
         * @param interceptor Nonnull interceptor.
         * @return Success or the first operation error.
         */
        auto AddInterceptor(std::shared_ptr<InterceptorMulti> const& interceptor) -> Result<void>;
        /**
         * @brief Execute each session's selected HTTP method.
         * @return Responses in registration order, including transport failures.
         */
        auto Perform() -> Result<std::vector<Response>>;
        /**
         * @brief Execute GET for all registered sessions.
         * @return Responses in registration order.
         */
        auto Get() -> Result<std::vector<Response>>;
        /**
         * @brief Execute DELETE for all registered sessions.
         * @return Responses in registration order.
         */
        auto Delete() -> Result<std::vector<Response>>;
        /**
         * @brief Execute PUT for all registered sessions.
         * @return Responses in registration order.
         */
        auto Put() -> Result<std::vector<Response>>;
        /**
         * @brief Execute HEAD for all registered sessions.
         * @return Responses in registration order.
         */
        auto Head() -> Result<std::vector<Response>>;
        /**
         * @brief Execute OPTIONS for all registered sessions.
         * @return Responses in registration order.
         */
        auto Options() -> Result<std::vector<Response>>;
        /**
         * @brief Execute PATCH for all registered sessions.
         * @return Responses in registration order.
         */
        auto Patch() -> Result<std::vector<Response>>;
        /**
         * @brief Execute POST for all registered sessions.
         * @return Responses in registration order.
         */
        auto Post() -> Result<std::vector<Response>>;

        /**
         * @brief Download once per registered session.
         * @tparam Args Destination types.
         * @param args Callbacks or borrowed streams in session order.
         * @return Download responses.
         */
        template <typename... Args>
        auto Download(Args&&... args) -> Result<std::vector<Response>> {
            if (auto status = checkDownloadCount(sizeof...(args)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = setHttpMethod(HttpMethod::DOWNLOAD_REQUEST); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return PerformDownload(std::forward<Args>(args)...);
        }

        /**
         * @brief Download sessions already marked DOWNLOAD_REQUEST.
         * @tparam Args Destination types.
         * @param args One destination per session.
         * @return Download responses.
         */
        template <typename... Args>
        auto PerformDownload(Args&&... args) -> Result<std::vector<Response>> {
            if (auto status = checkDownloadCount(sizeof...(args)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (auto status = validateDownloads(); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            m_downloads.clear();
            try {
                std::size_t  index{};
                Result<void> targets;
                ((targets ? targets = setDownloadTarget(index++, std::forward<Args>(args)) : targets), ...);
                if (!targets) {
                    m_downloads.clear();
                    return std::unexpected{ std::move(targets.error()) };
                }
                return Perform();
            } catch (...) {
                m_downloads.clear();
                throw;
            }
        }

    private:
        /**
         * @brief Reject mutation or recursion during an attached curl transfer.
         * @return Success or the first operation error.
         */
        auto checkIdleTransfer() const -> Result<void>;
        /**
         * @brief Validate mutable registrations and refresh exclusive session claims.
         * @return Success or the first operation error.
         */
        auto synchronizeSessions() -> Result<void>;
        /**
         * @brief Release claims without dereferencing sessions removed through mutable access.
         */
        auto releaseSessions() noexcept -> void;
        /**
         * @brief Bind existing claims to this batch after a move or synchronization.
         */
        auto rebindSessions() noexcept -> void;
        /**
         * @brief Validate one destination per session.
         * @param count Number supplied by the caller.
         * @return Success or the first operation error.
         */
        auto checkDownloadCount(std::size_t count) -> Result<void>;
        /**
         * @brief Require each registration to select DOWNLOAD_REQUEST.
         * @return Success or the first operation error.
         */
        auto validateDownloads() const -> Result<void>;
        /**
         * @brief Copy a download consumer.
         * @param index Registration index.
         * @param write Download callback.
         * @return Success or the first operation error.
         */
        auto setDownloadTarget(std::size_t index, WriteCallback const& write) -> Result<void>;
        /**
         * @brief Borrow a download stream.
         * @param index Registration index.
         * @param file Output stream.
         * @return Success or the first operation error.
         */
        auto setDownloadTarget(std::size_t index, std::ofstream& file) -> Result<void>;

        /**
         * @brief Unwrap a borrowed stream.
         * @param index Registration index.
         * @param file Output stream reference.
         * @return Success or the first operation error.
         */
        auto setDownloadTarget(std::size_t index, std::reference_wrapper<std::ofstream> file) -> Result<void> {
            if (auto status = setDownloadTarget(index, file.get()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            return {};
        }

        /**
         * @brief Select one method for all registrations.
         * @param method Requested HTTP method.
         * @return Success or the first operation error.
         */
        auto setHttpMethod(HttpMethod method) -> Result<void>;
        /**
         * @brief Validate the whole batch, then prepare each easy handle.
         * @return Success or the first operation error.
         */
        auto prepareSessions() -> Result<void>;
        /**
         * @brief Enter the remaining interceptor chain or perform transfers.
         * @return Ordered responses.
         */
        auto makeRequest() -> Result<std::vector<Response>>;
        /**
         * @brief Attach, drive and detach prepared handles.
         * @return Ordered transfer snapshots.
         */
        auto runPrepared() -> Result<std::vector<Response>>;
        /**
         * @brief Reprepare and continue a batch from an interceptor.
         * @return Downstream responses.
         */
        auto proceed() -> Result<std::vector<Response>>;
    };

} // namespace mcr

namespace mcr::detail {
    /**
     * @brief Restore session or batch state on normal and exceptional exits.
     * @tparam Fn Cleanup callable.
     */
    template <typename Fn>
    struct ScopeExit {
        Fn cleanup; ///< Cleanup action invoked on scope exit.

        ~ScopeExit() { cleanup(); }
    };

    /**
     * @brief Translate a curl multi failure into an explicit result.
     * @param result Curl multi operation result.
     * @return Success or the first operation error.
     */
    auto check_multi(CURLMcode result) -> Result<void> {
        if (result != CURLM_OK) {
            return std::unexpected{
                Error{ ErrorCode::FAILED_INIT, std::string{ "mcr::MultiPerform: " } + curl_multi_strerror(result) }
            };
        }

        return {};
    }

    /**
     * @brief Check whether a batch method tag is recognized.
     * @param method Method tag to inspect.
     * @return Whether the tag belongs to the supported method range.
     */
    auto valid_method(MultiPerform::HttpMethod method) -> bool {
        return method >= MultiPerform::HttpMethod::UNDEFINED && method <= MultiPerform::HttpMethod::DOWNLOAD_REQUEST;
    }
} // namespace mcr::detail

namespace mcr {
    auto Session::SetSslOptions(options::SslOptions const& options) -> Result<void> {
        // Some backends reject even the default value for unsupported optional settings.
        auto optional_option = [this](CURLoption option, auto value, bool requested) -> Result<void> {
            auto const result{ curl_easy_setopt(m_curl->handle, option, value) };
            if (!requested && (result == CURLE_NOT_BUILT_IN || result == CURLE_UNKNOWN_OPTION)) {
                return {};
            }
            if (auto status = checkCurl(result); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        };
        auto string_option = [&](CURLoption option, std::string_view value) -> Result<void> {
            if (auto status = optional_option(option, value.empty() ? nullptr : value.data(), !value.empty()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        };
        auto blob_option = [&](CURLoption option, std::string_view value) -> Result<void> {
            curl_blob blob{ const_cast<char*>(value.data()), value.size(), CURL_BLOB_COPY };
            if (auto status = optional_option(option, value.empty() ? nullptr : &blob, !value.empty()); !status) {
                return std::unexpected{ std::move(status.error()) };
            }

            return {};
        };

        if (auto status = string_option(CURLOPT_SSLCERT, options.cert_file); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = blob_option(CURLOPT_SSLCERT_BLOB, options.cert_file.empty() ? std::string_view{ options.cert_blob } : std::string_view{}); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = setOption(CURLOPT_SSLCERTTYPE, options.cert_type.empty() ? "PEM" : options.cert_type.c_str()); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = string_option(CURLOPT_SSLKEY, options.key_file); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = blob_option(CURLOPT_SSLKEY_BLOB, options.key_file.empty() ? std::string_view{ options.key_blob } : std::string_view{}); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = setOption(CURLOPT_SSLKEYTYPE, options.key_type.empty() ? "PEM" : options.key_type.c_str()); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = string_option(CURLOPT_KEYPASSWD, options.key_pass); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = string_option(CURLOPT_PINNEDPUBLICKEY, options.pinned_public_key); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = setOption(CURLOPT_SSL_ENABLE_ALPN, options.enable_alpn ? 1L : 0L); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = setOption(CURLOPT_SSL_VERIFYPEER, options.verify_peer ? 1L : 0L); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = setOption(CURLOPT_SSL_VERIFYHOST, options.verify_host ? 2L : 0L); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = optional_option(CURLOPT_SSL_VERIFYSTATUS, options.verify_status ? 1L : 0L, options.verify_status); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = setOption(CURLOPT_SSLVERSION, options.ssl_version | options.max_version); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        long flags{ options.ssl_no_revoke ? CURLSSLOPT_NO_REVOKE : 0L };
#ifdef _WIN32
        flags |= CURLSSLOPT_NATIVE_CA;
#endif
        if (auto status = setOption(CURLOPT_SSL_OPTIONS, flags); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (options.ssl_fast_start) {
            return std::unexpected{
                Error{ ErrorCode::NOT_BUILT_IN, "mcr::Session: TLS false start was removed in curl 8.15." }
            };
        }

        char* default_ca{ nullptr };
        (void)curl_easy_getinfo(m_curl->handle, CURLINFO_CAINFO, &default_ca);
        if (auto status = setOption(CURLOPT_CAINFO, options.ca_info.empty() ? default_ca : options.ca_info.c_str()); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = blob_option(CURLOPT_CAINFO_BLOB, options.ca_buffer.empty() ? options.ca_info_blob : options.ca_buffer); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        char* default_path{ nullptr };
        (void)curl_easy_getinfo(m_curl->handle, CURLINFO_CAPATH, &default_path);
        if (auto status = optional_option(CURLOPT_CAPATH, options.ca_path.empty() ? default_path : options.ca_path.c_str(), !options.ca_path.empty()); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = string_option(CURLOPT_CRLFILE, options.crl_file); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = string_option(CURLOPT_SSL_CIPHER_LIST, options.ciphers); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = string_option(CURLOPT_TLS13_CIPHERS, options.tls13_ciphers); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (auto status = setOption(CURLOPT_SSL_SESSIONID_CACHE, options.session_id_cache ? 1L : 0L); !status) {
            return std::unexpected{ std::move(status.error()) };
        }

        return {};
    }

    auto Session::AddInterceptor(std::shared_ptr<Interceptor> const& interceptor) -> Result<void> {
        if (m_request_depth || m_in_transfer) {
            return std::unexpected{
                Error{ ErrorCode::RECURSIVE_API_CALL, "mcr::Session: cannot modify an active interceptor chain." }
            };
        }
        if (!interceptor) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::Session: interceptor must not be null." }
            };
        }
        m_interceptors.push_back(interceptor);

        return {};
    }

    auto Session::perform() -> Result<Response> {
        if (m_in_transfer || m_multi_owner) {
            return std::unexpected{
                Error{ ErrorCode::RECURSIVE_API_CALL, "mcr::Session: handle is already in use." }
            };
        }
        detail::ScopeExit restore{ [this, next = m_next_interceptor, method = m_method, downloading = m_downloading, write = m_download_write, file = m_download_file]() mutable {
            m_next_interceptor = next;
            m_method           = std::move(method);
            m_downloading      = downloading;
            --m_request_depth;
            m_download_write = m_request_depth ? std::move(write) : WriteCallback{};
            m_download_file  = m_request_depth ? file : nullptr;
        } };
        ++m_request_depth;
        if (m_next_interceptor < m_interceptors.size()) {
            auto const interceptor{ m_interceptors[m_next_interceptor++] };
            return interceptor->Intercept(*this);
        }
        m_in_transfer = true;
        detail::ScopeExit finish{ [this] { m_in_transfer = false; } };
        return Complete(curl_easy_perform(m_curl->handle));
    }

    auto Session::proceed() -> Result<Response> {
        auto       method{ m_method };
        auto       write{ m_download_write };
        auto*      file{ m_download_file };
        bool const downloading{ m_downloading };
        if (auto status = prepare(method, downloading); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        m_download_write = std::move(write);
        m_download_file  = file;
        return perform();
    }

    auto Interceptor::Proceed(Session& session) -> Result<Response> {
        return session.proceed();
    }

    auto Interceptor::Proceed(Session& session, ProceedHttpMethod method) -> Result<Response> {
        switch (method) {
            case ProceedHttpMethod::GET_REQUEST    : return session.Get();
            case ProceedHttpMethod::POST_REQUEST   : return session.Post();
            case ProceedHttpMethod::PUT_REQUEST    : return session.Put();
            case ProceedHttpMethod::DELETE_REQUEST : return session.Delete();
            case ProceedHttpMethod::PATCH_REQUEST  : return session.Patch();
            case ProceedHttpMethod::HEAD_REQUEST   : return session.Head();
            case ProceedHttpMethod::OPTIONS_REQUEST: return session.Options();
            default                                : return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::Interceptor: this method requires a download destination." }
 };
        }
    }

    auto Interceptor::Proceed(Session& session, ProceedHttpMethod method, std::ofstream& file) -> Result<Response> {
        if (method != ProceedHttpMethod::DOWNLOAD_FILE_REQUEST) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::Interceptor: a stream requires DOWNLOAD_FILE_REQUEST." }
            };
        }
        return session.Download(file);
    }

    auto Interceptor::Proceed(Session& session, ProceedHttpMethod method, WriteCallback const& write) -> Result<Response> {
        if (method != ProceedHttpMethod::DOWNLOAD_CALLBACK_REQUEST) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::Interceptor: a callback requires DOWNLOAD_CALLBACK_REQUEST." }
            };
        }
        return session.Download(write);
    }

    MultiPerform::MultiPerform() = default;

    MultiPerform::MultiPerform(MultiPerform&& other) noexcept {
        *this = std::move(other);
    }

    auto MultiPerform::operator=(MultiPerform&& other) noexcept -> MultiPerform& {
        if (this != &other) {
            releaseSessions();
            m_sessions         = std::move(other.m_sessions);
            m_claimed          = std::move(other.m_claimed);
            m_multi            = std::move(other.m_multi);
            m_downloads        = std::move(other.m_downloads);
            m_interceptors     = std::move(other.m_interceptors);
            m_next_interceptor = 0;
            m_request_depth    = 0;
            m_transferring     = false;
            rebindSessions();
        }
        return *this;
    }

    MultiPerform::~MultiPerform() {
        releaseSessions();
    }

    auto MultiPerform::checkIdleTransfer() const -> Result<void> {
        if (m_transferring) {
            return std::unexpected{
                Error{ ErrorCode::RECURSIVE_API_CALL, "mcr::MultiPerform: cannot modify or reenter a running transfer." }
            };
        }

        return {};
    }

    auto MultiPerform::releaseSessions() noexcept -> void {
        for (auto const& weak : m_claimed) {
            if (auto session{ weak.lock() }; session && session->m_multi_owner == this) {
                session->m_multi_owner = nullptr;
            }
        }
        m_claimed.clear();
    }

    auto MultiPerform::rebindSessions() noexcept -> void {
        for (auto const& weak : m_claimed) {
            if (auto session{ weak.lock() }) {
                session->m_multi_owner = this;
            }
        }
    }

    auto MultiPerform::synchronizeSessions() -> Result<void> {
        if (auto status = checkIdleTransfer(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        std::unordered_set<Session*>        seen;
        std::vector<std::weak_ptr<Session>> claims;
        claims.reserve(m_sessions.size());
        for (auto const& [session, method] : m_sessions) {
            if (!session || !detail::valid_method(method) || !seen.insert(session.get()).second) {
                return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: null or duplicate session, or invalid HTTP method." }
                };
            }
            if (session->m_in_transfer || session->m_request_depth || (session->m_multi_owner && session->m_multi_owner != this)) {
                return std::unexpected{
                    Error{ ErrorCode::RECURSIVE_API_CALL, "mcr::MultiPerform: session is already in use." }
                };
            }
            claims.push_back(session);
        }
        releaseSessions();
        m_claimed = std::move(claims);
        rebindSessions();
        std::erase_if(m_downloads, [&](auto const& entry) { return !seen.contains(entry.first); });

        return {};
    }

    auto MultiPerform::AddSession(std::shared_ptr<Session> const& session, HttpMethod method) -> Result<void> {
        if (auto status = synchronizeSessions(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (!session || !detail::valid_method(method)) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: invalid session or HTTP method." }
            };
        }
        if (session->m_multi_owner || session->m_in_transfer || session->m_request_depth) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: session already belongs to a request or batch." }
            };
        }
        for (auto const& [existing, existing_method] : m_sessions) {
            if (existing_method != HttpMethod::UNDEFINED && method != HttpMethod::UNDEFINED &&
                (existing_method == HttpMethod::DOWNLOAD_REQUEST) != (method == HttpMethod::DOWNLOAD_REQUEST)) {
                return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: cannot mix download and ordinary registrations." }
                };
            }
        }
        m_claimed.reserve(m_claimed.size() + 1);
        m_sessions.emplace_back(session, method);
        m_claimed.emplace_back(session);
        session->m_multi_owner = this;

        return {};
    }

    auto MultiPerform::RemoveSession(std::shared_ptr<Session> const& session) -> Result<void> {
        if (auto status = synchronizeSessions(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        auto const found{ std::ranges::find_if(m_sessions, [&](auto const& entry) { return entry.first == session; }) };
        if (found == m_sessions.end()) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: session is not registered." }
            };
        }
        session->m_multi_owner = nullptr;
        m_downloads.erase(session.get());
        std::erase_if(m_claimed, [&](auto const& weak) { return weak.lock() == session; });
        m_sessions.erase(found);

        return {};
    }

    auto MultiPerform::GetSessions() -> Result<std::reference_wrapper<Sessions>> {
        if (auto status = checkIdleTransfer(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return std::ref(m_sessions);
    }

    auto MultiPerform::AddInterceptor(std::shared_ptr<InterceptorMulti> const& interceptor) -> Result<void> {
        if (auto status = checkIdleTransfer(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (m_request_depth) {
            return std::unexpected{
                Error{ ErrorCode::RECURSIVE_API_CALL, "mcr::MultiPerform: cannot modify an active interceptor chain." }
            };
        }
        if (!interceptor) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: interceptor must not be null." }
            };
        }
        m_interceptors.push_back(interceptor);

        return {};
    }

    auto MultiPerform::checkDownloadCount(std::size_t count) -> Result<void> {
        if (auto status = synchronizeSessions(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        if (count != m_sessions.size()) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: provide one download destination per session." }
            };
        }

        return {};
    }

    auto MultiPerform::validateDownloads() const -> Result<void> {
        for (auto const& [session, method] : m_sessions) {
            if (method != HttpMethod::DOWNLOAD_REQUEST) {
                return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: PerformDownload requires download registrations." }
                };
            }
        }

        return {};
    }

    auto MultiPerform::setDownloadTarget(std::size_t index, WriteCallback const& write) -> Result<void> {
        if (auto status = checkIdleTransfer(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        auto const& [session, method]{ m_sessions.at(index) };
        if (method != HttpMethod::DOWNLOAD_REQUEST) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: destination requires a download method." }
            };
        }
        m_downloads.insert_or_assign(session.get(), write);

        return {};
    }

    auto MultiPerform::setDownloadTarget(std::size_t index, std::ofstream& file) -> Result<void> {
        if (auto status = checkIdleTransfer(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        auto const& [session, method]{ m_sessions.at(index) };
        if (method != HttpMethod::DOWNLOAD_REQUEST) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: destination requires a download method." }
            };
        }
        m_downloads.insert_or_assign(session.get(), std::ref(file));

        return {};
    }

    auto MultiPerform::setHttpMethod(HttpMethod method) -> Result<void> {
        if (auto status = synchronizeSessions(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        for (auto& [session, selected] : m_sessions) {
            selected = method;
        }

        return {};
    }

    auto MultiPerform::prepareSessions() -> Result<void> {
        if (auto status = synchronizeSessions(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        // Validate the complete batch before preparing any handle.
        bool downloads{ false }, ordinary{ false };
        for (auto const& [session, method] : m_sessions) {
            if (method == HttpMethod::UNDEFINED) {
                return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: select an HTTP method before Perform." }
                };
            }
            if (method == HttpMethod::DOWNLOAD_REQUEST) {
                downloads = true;
                if (!m_downloads.contains(session.get())) {
                    return std::unexpected{
                        Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: missing download destination." }
                    };
                }
            } else {
                ordinary = true;
            }
        }
        if (downloads && ordinary) {
            return std::unexpected{
                Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: cannot mix download and ordinary requests." }
            };
        }
        for (auto const& [session, method] : m_sessions) {
            session->m_multi_preparing = true;
            detail::ScopeExit reset{ [&] { session->m_multi_preparing = false; } };
            switch (method) {
                case HttpMethod::GET_REQUEST:
                    if (auto status = session->PrepareGet(); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                    break;
                case HttpMethod::POST_REQUEST:
                    if (auto status = session->PreparePost(); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                    break;
                case HttpMethod::PUT_REQUEST:
                    if (auto status = session->PreparePut(); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                    break;
                case HttpMethod::DELETE_REQUEST:
                    if (auto status = session->PrepareDelete(); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                    break;
                case HttpMethod::PATCH_REQUEST:
                    if (auto status = session->PreparePatch(); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                    break;
                case HttpMethod::HEAD_REQUEST:
                    if (auto status = session->PrepareHead(); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                    break;
                case HttpMethod::OPTIONS_REQUEST:
                    if (auto status = session->PrepareOptions(); !status) {
                        return std::unexpected{ std::move(status.error()) };
                    }
                    break;
                case HttpMethod::DOWNLOAD_REQUEST: {
                    auto prepared = std::visit([&](auto& target) -> Result<void> {
                        if constexpr (std::same_as<std::decay_t<decltype(target)>, WriteCallback>) {
                            return session->PrepareDownload(target);
                        } else {
                            return session->PrepareDownload(target.get());
                        }

                        return {};
                    },
                                               m_downloads.at(session.get()));
                    if (!prepared) {
                        return std::unexpected{ std::move(prepared.error()) };
                    }
                    break;
                }
                default: return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "mcr::MultiPerform: invalid HTTP method." }
 };
            }
        }

        return {};
    }

    auto MultiPerform::Perform() -> Result<std::vector<Response>> {
        detail::ScopeExit clear{ [this] {
            if (!m_request_depth) {
                m_downloads.clear();
                for (auto const& weak : m_claimed) {
                    if (auto session{ weak.lock() }) {
                        session->m_download_write = {};
                        session->m_download_file  = nullptr;
                    }
                }
            }
        } };
        if (auto status = prepareSessions(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return makeRequest();
    }

    auto MultiPerform::makeRequest() -> Result<std::vector<Response>> {
        if (auto status = checkIdleTransfer(); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        detail::ScopeExit restore{ [this, next = m_next_interceptor] { m_next_interceptor = next; --m_request_depth; } };
        ++m_request_depth;
        if (m_next_interceptor < m_interceptors.size()) {
            auto const interceptor{ m_interceptors[m_next_interceptor++] };
            return interceptor->Intercept(*this);
        }
        return runPrepared();
    }

    auto MultiPerform::proceed() -> Result<std::vector<Response>> {
        return Perform();
    }

    auto MultiPerform::runPrepared() -> Result<std::vector<Response>> {
        if (!m_multi) {
            auto holder = curl::CurlMultiHolder::Create();
            if (!holder) {
                return std::unexpected{ std::move(holder.error()) };
            }
            m_multi = std::make_unique<curl::CurlMultiHolder>(std::move(*holder));
        }
        std::vector<Session*> attached;
        attached.reserve(m_sessions.size());
        std::unordered_map<CURL*, std::size_t> positions;
        for (auto const& [index, entry] : m_sessions | std::views::enumerate) {
            positions.emplace(entry.first->m_curl->handle, static_cast<std::size_t>(index));
        }
        std::vector<std::optional<Response>> completed(m_sessions.size());
        m_transferring = true;
        detail::ScopeExit detach{ [&] {
            for (auto* session : attached) {
                (void)curl_multi_remove_handle(m_multi->handle, session->m_curl->handle);
                session->m_in_transfer = false;
            }
            int queued{};
            while (curl_multi_info_read(m_multi->handle, &queued)) {}
            m_transferring = false;
        } };
        for (auto const& [session, method] : m_sessions) {
            if (auto status = detail::check_multi(curl_multi_add_handle(m_multi->handle, session->m_curl->handle)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            attached.push_back(session.get());
            session->m_in_transfer = true;
        }
        int running{};
        do {
            if (auto status = detail::check_multi(curl_multi_perform(m_multi->handle, &running)); !status) {
                return std::unexpected{ std::move(status.error()) };
            }
            if (running) {
                if (auto status = detail::check_multi(curl_multi_poll(m_multi->handle, nullptr, 0, 100, nullptr)); !status) {
                    return std::unexpected{ std::move(status.error()) };
                }
            }
        } while (running);
        int queued{};
        while (auto* message{ curl_multi_info_read(m_multi->handle, &queued) }) {
            if (message->msg != CURLMSG_DONE) {
                continue;
            }
            auto const position{ positions.at(message->easy_handle) };
            auto       response = m_sessions[position].first->Complete(message->data.result);
            if (!response) {
                return std::unexpected{ std::move(response.error()) };
            }
            completed[position] = std::move(*response);
        }
        std::vector<Response> responses;
        responses.reserve(completed.size());
        for (auto& response : completed) {
            if (!response) {
                return std::unexpected{
                    Error{ ErrorCode::FAILED_INIT, "mcr::MultiPerform: curl did not report every transfer's completion." }
                };
            }
            responses.push_back(std::move(*response));
        }
        return responses;
    }

    auto MultiPerform::Get() -> Result<std::vector<Response>> {
        if (auto status = setHttpMethod(HttpMethod::GET_REQUEST); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return Perform();
    }

    auto MultiPerform::Delete() -> Result<std::vector<Response>> {
        if (auto status = setHttpMethod(HttpMethod::DELETE_REQUEST); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return Perform();
    }

    auto MultiPerform::Put() -> Result<std::vector<Response>> {
        if (auto status = setHttpMethod(HttpMethod::PUT_REQUEST); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return Perform();
    }

    auto MultiPerform::Head() -> Result<std::vector<Response>> {
        if (auto status = setHttpMethod(HttpMethod::HEAD_REQUEST); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return Perform();
    }

    auto MultiPerform::Options() -> Result<std::vector<Response>> {
        if (auto status = setHttpMethod(HttpMethod::OPTIONS_REQUEST); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return Perform();
    }

    auto MultiPerform::Patch() -> Result<std::vector<Response>> {
        if (auto status = setHttpMethod(HttpMethod::PATCH_REQUEST); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return Perform();
    }

    auto MultiPerform::Post() -> Result<std::vector<Response>> {
        if (auto status = setHttpMethod(HttpMethod::POST_REQUEST); !status) {
            return std::unexpected{ std::move(status.error()) };
        }
        return Perform();
    }

    auto InterceptorMulti::Proceed(MultiPerform& multi) -> Result<std::vector<Response>> {
        return multi.proceed();
    }

    auto InterceptorMulti::PrepareDownloadSession(MultiPerform& multi, std::size_t index, WriteCallback const& write) -> Result<void> {
        if (auto status = multi.setDownloadTarget(index, write); !status) {
            return std::unexpected{ std::move(status.error()) };
        }

        return {};
    }

    auto InterceptorMulti::PrepareDownloadSession(MultiPerform& multi, std::size_t index, std::ofstream& file) -> Result<void> {
        if (auto status = multi.setDownloadTarget(index, file); !status) {
            return std::unexpected{ std::move(status.error()) };
        }

        return {};
    }
} // namespace mcr
