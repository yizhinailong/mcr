/**
 * @file multiperform.cpp
 * @brief Concurrent curl transfers with ordered results and scoped handle attachment.
 */
module;
#include <curl/curl.h>
module mcr.session;
import std;

namespace {
    template <typename Fn>
    struct ScopeExit {
        Fn cleanup;

        ~ScopeExit() { cleanup(); }
    };

    auto check_multi(CURLMcode result) -> void {
        if (result != CURLM_OK) {
            throw std::runtime_error{ std::string{ "mcr::MultiPerform: " } + curl_multi_strerror(result) };
        }
    }

    auto valid_method(mcr::MultiPerform::HttpMethod method) -> bool {
        return method >= mcr::MultiPerform::HttpMethod::UNDEFINED && method <= mcr::MultiPerform::HttpMethod::DOWNLOAD_REQUEST;
    }
} // namespace

namespace mcr {
    MultiPerform::MultiPerform() : m_multi{ std::make_unique<curl::CurlMultiHolder>() } {}

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

    auto MultiPerform::checkIdleTransfer() const -> void {
        if (m_transferring) {
            throw std::logic_error{ "mcr::MultiPerform: cannot modify or reenter a running transfer." };
        }
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

    auto MultiPerform::synchronizeSessions() -> void {
        checkIdleTransfer();
        std::unordered_set<Session*>        seen;
        std::vector<std::weak_ptr<Session>> claims;
        claims.reserve(m_sessions.size());
        for (auto const& [session, method] : m_sessions) {
            if (!session || !valid_method(method) || !seen.insert(session.get()).second) {
                throw std::invalid_argument{ "mcr::MultiPerform: null or duplicate session, or invalid HTTP method." };
            }
            if (session->m_in_transfer || session->m_request_depth || (session->m_multi_owner && session->m_multi_owner != this)) {
                throw std::logic_error{ "mcr::MultiPerform: session is already in use." };
            }
            claims.push_back(session);
        }
        releaseSessions();
        m_claimed = std::move(claims);
        rebindSessions();
        std::erase_if(m_downloads, [&](auto const& entry) { return !seen.contains(entry.first); });
    }

    auto MultiPerform::AddSession(std::shared_ptr<Session> const& session, HttpMethod method) -> void {
        synchronizeSessions();
        if (!session || !valid_method(method)) {
            throw std::invalid_argument{ "mcr::MultiPerform: invalid session or HTTP method." };
        }
        if (session->m_multi_owner || session->m_in_transfer || session->m_request_depth) {
            throw std::invalid_argument{ "mcr::MultiPerform: session already belongs to a request or batch." };
        }
        for (auto const& [existing, existing_method] : m_sessions) {
            if (existing_method != HttpMethod::UNDEFINED && method != HttpMethod::UNDEFINED &&
                (existing_method == HttpMethod::DOWNLOAD_REQUEST) != (method == HttpMethod::DOWNLOAD_REQUEST)) {
                throw std::invalid_argument{ "mcr::MultiPerform: cannot mix download and ordinary registrations." };
            }
        }
        m_claimed.reserve(m_claimed.size() + 1);
        m_sessions.emplace_back(session, method);
        m_claimed.emplace_back(session);
        session->m_multi_owner = this;
    }

    auto MultiPerform::RemoveSession(std::shared_ptr<Session> const& session) -> void {
        synchronizeSessions();
        auto const found{ std::ranges::find_if(m_sessions, [&](auto const& entry) { return entry.first == session; }) };
        if (found == m_sessions.end()) {
            throw std::invalid_argument{ "mcr::MultiPerform: session is not registered." };
        }
        session->m_multi_owner = nullptr;
        m_downloads.erase(session.get());
        std::erase_if(m_claimed, [&](auto const& weak) { return weak.lock() == session; });
        m_sessions.erase(found);
    }

    auto MultiPerform::GetSessions() -> Sessions& {
        checkIdleTransfer();
        return m_sessions;
    }

    auto MultiPerform::AddInterceptor(std::shared_ptr<InterceptorMulti> const& interceptor) -> void {
        checkIdleTransfer();
        if (m_request_depth) {
            throw std::logic_error{ "mcr::MultiPerform: cannot modify an active interceptor chain." };
        }
        if (!interceptor) {
            throw std::invalid_argument{ "mcr::MultiPerform: interceptor must not be null." };
        }
        m_interceptors.push_back(interceptor);
    }

    auto MultiPerform::checkDownloadCount(std::size_t count) -> void {
        synchronizeSessions();
        if (count != m_sessions.size()) {
            throw std::invalid_argument{ "mcr::MultiPerform: provide one download destination per session." };
        }
    }

    auto MultiPerform::validateDownloads() const -> void {
        for (auto const& [session, method] : m_sessions) {
            if (method != HttpMethod::DOWNLOAD_REQUEST) {
                throw std::invalid_argument{ "mcr::MultiPerform: PerformDownload requires download registrations." };
            }
        }
    }

    auto MultiPerform::setDownloadTarget(std::size_t index, WriteCallback const& write) -> void {
        checkIdleTransfer();
        auto const& [session, method]{ m_sessions.at(index) };
        if (method != HttpMethod::DOWNLOAD_REQUEST) {
            throw std::invalid_argument{ "mcr::MultiPerform: destination requires a download method." };
        }
        m_downloads.insert_or_assign(session.get(), write);
    }

    auto MultiPerform::setDownloadTarget(std::size_t index, std::ofstream& file) -> void {
        checkIdleTransfer();
        auto const& [session, method]{ m_sessions.at(index) };
        if (method != HttpMethod::DOWNLOAD_REQUEST) {
            throw std::invalid_argument{ "mcr::MultiPerform: destination requires a download method." };
        }
        m_downloads.insert_or_assign(session.get(), std::ref(file));
    }

    auto MultiPerform::setHttpMethod(HttpMethod method) -> void {
        synchronizeSessions();
        for (auto& [session, selected] : m_sessions) {
            selected = method;
        }
    }

    auto MultiPerform::prepareSessions() -> void {
        synchronizeSessions();
        // Validate the complete batch before preparing any handle.
        bool downloads{ false }, ordinary{ false };
        for (auto const& [session, method] : m_sessions) {
            if (method == HttpMethod::UNDEFINED) {
                throw std::invalid_argument{ "mcr::MultiPerform: select an HTTP method before Perform." };
            }
            if (method == HttpMethod::DOWNLOAD_REQUEST) {
                downloads = true;
                if (!m_downloads.contains(session.get())) {
                    throw std::invalid_argument{ "mcr::MultiPerform: missing download destination." };
                }
            } else {
                ordinary = true;
            }
        }
        if (downloads && ordinary) {
            throw std::invalid_argument{ "mcr::MultiPerform: cannot mix download and ordinary requests." };
        }
        for (auto const& [session, method] : m_sessions) {
            session->m_multi_preparing = true;
            ScopeExit reset{ [&] { session->m_multi_preparing = false; } };
            switch (method) {
                case HttpMethod::GET_REQUEST    : session->PrepareGet(); break;
                case HttpMethod::POST_REQUEST   : session->PreparePost(); break;
                case HttpMethod::PUT_REQUEST    : session->PreparePut(); break;
                case HttpMethod::DELETE_REQUEST : session->PrepareDelete(); break;
                case HttpMethod::PATCH_REQUEST  : session->PreparePatch(); break;
                case HttpMethod::HEAD_REQUEST   : session->PrepareHead(); break;
                case HttpMethod::OPTIONS_REQUEST: session->PrepareOptions(); break;
                case HttpMethod::DOWNLOAD_REQUEST:
                    std::visit([&](auto& target) {
                        if constexpr (std::same_as<std::decay_t<decltype(target)>, WriteCallback>) {
                            session->PrepareDownload(target);
                        } else {
                            session->PrepareDownload(target.get());
                        }
                    },
                               m_downloads.at(session.get()));
                    break;
                default: throw std::invalid_argument{ "mcr::MultiPerform: invalid HTTP method." };
            }
        }
    }

    auto MultiPerform::Perform() -> std::vector<Response> {
        ScopeExit clear{ [this] {
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
        prepareSessions();
        return makeRequest();
    }

    auto MultiPerform::makeRequest() -> std::vector<Response> {
        checkIdleTransfer();
        ScopeExit restore{ [this, next = m_next_interceptor] { m_next_interceptor = next; --m_request_depth; } };
        ++m_request_depth;
        if (m_next_interceptor < m_interceptors.size()) {
            auto const interceptor{ m_interceptors[m_next_interceptor++] };
            return interceptor->Intercept(*this);
        }
        return runPrepared();
    }

    auto MultiPerform::proceed() -> std::vector<Response> {
        return Perform();
    }

    auto MultiPerform::runPrepared() -> std::vector<Response> {
        if (!m_multi) {
            m_multi = std::make_unique<curl::CurlMultiHolder>();
        }
        std::vector<Session*> attached;
        attached.reserve(m_sessions.size());
        std::unordered_map<CURL*, std::size_t> positions;
        for (std::size_t index{}; index < m_sessions.size(); ++index) {
            positions.emplace(m_sessions[index].first->m_curl->handle, index);
        }
        std::vector<std::optional<Response>> completed(m_sessions.size());
        m_transferring = true;
        ScopeExit detach{ [&] {
            for (auto* session : attached) {
                (void)curl_multi_remove_handle(m_multi->handle, session->m_curl->handle);
                session->m_in_transfer = false;
            }
            int queued{};
            while (curl_multi_info_read(m_multi->handle, &queued)) {}
            m_transferring = false;
        } };
        for (auto const& [session, method] : m_sessions) {
            check_multi(curl_multi_add_handle(m_multi->handle, session->m_curl->handle));
            attached.push_back(session.get());
            session->m_in_transfer = true;
        }
        int running{};
        do {
            check_multi(curl_multi_perform(m_multi->handle, &running));
            if (running) {
                check_multi(curl_multi_poll(m_multi->handle, nullptr, 0, 100, nullptr));
            }
        } while (running);
        int queued{};
        while (auto* message{ curl_multi_info_read(m_multi->handle, &queued) }) {
            if (message->msg != CURLMSG_DONE) {
                continue;
            }
            auto const position{ positions.at(message->easy_handle) };
            completed[position] = m_sessions[position].first->Complete(message->data.result);
        }
        std::vector<Response> responses;
        responses.reserve(completed.size());
        for (auto& response : completed) {
            if (!response) {
                throw std::runtime_error{ "mcr::MultiPerform: curl did not report every transfer's completion." };
            }
            responses.push_back(std::move(*response));
        }
        return responses;
    }

    auto MultiPerform::Get() -> std::vector<Response> {
        setHttpMethod(HttpMethod::GET_REQUEST);
        return Perform();
    }

    auto MultiPerform::Delete() -> std::vector<Response> {
        setHttpMethod(HttpMethod::DELETE_REQUEST);
        return Perform();
    }

    auto MultiPerform::Put() -> std::vector<Response> {
        setHttpMethod(HttpMethod::PUT_REQUEST);
        return Perform();
    }

    auto MultiPerform::Head() -> std::vector<Response> {
        setHttpMethod(HttpMethod::HEAD_REQUEST);
        return Perform();
    }

    auto MultiPerform::Options() -> std::vector<Response> {
        setHttpMethod(HttpMethod::OPTIONS_REQUEST);
        return Perform();
    }

    auto MultiPerform::Patch() -> std::vector<Response> {
        setHttpMethod(HttpMethod::PATCH_REQUEST);
        return Perform();
    }

    auto MultiPerform::Post() -> std::vector<Response> {
        setHttpMethod(HttpMethod::POST_REQUEST);
        return Perform();
    }

    auto InterceptorMulti::Proceed(MultiPerform& multi) -> std::vector<Response> {
        return multi.proceed();
    }

    auto InterceptorMulti::PrepareDownloadSession(MultiPerform& multi, std::size_t index, WriteCallback const& write) -> void {
        multi.setDownloadTarget(index, write);
    }

    auto InterceptorMulti::PrepareDownloadSession(MultiPerform& multi, std::size_t index, std::ofstream& file) -> void {
        multi.setDownloadTarget(index, file);
    }
} // namespace mcr
