/**
 * @file session_interceptor.cpp
 * @brief Exception-safe interceptor chaining and request retries.
 */
module;
#include <curl/curl.h>
module mcr.session;
import std;

namespace {
    /**
     * @brief Restore request state on normal and exceptional exits.
     */
    template <typename Fn>
    struct ScopeExit {
        Fn cleanup;

        ~ScopeExit() { cleanup(); }
    };
} // namespace

namespace mcr {
    auto Session::AddInterceptor(std::shared_ptr<Interceptor> const& interceptor) -> void {
        if (m_request_depth || m_in_transfer) {
            throw std::logic_error{ "mcr::Session: cannot modify an active interceptor chain." };
        }
        if (!interceptor) {
            throw std::invalid_argument{ "mcr::Session: interceptor must not be null." };
        }
        m_interceptors.push_back(interceptor);
    }

    auto Session::perform() -> Response {
        if (m_in_transfer || m_multi_owner) {
            throw std::logic_error{ "mcr::Session: handle is already in use." };
        }
        ScopeExit restore{ [this, next = m_next_interceptor, method = m_method, downloading = m_downloading, write = m_download_write, file = m_download_file]() mutable {
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
        ScopeExit finish{ [this] { m_in_transfer = false; } };
        return Complete(curl_easy_perform(m_curl->handle));
    }

    auto Session::proceed() -> Response {
        auto       method{ m_method };
        auto       write{ m_download_write };
        auto*      file{ m_download_file };
        bool const downloading{ m_downloading };
        prepare(method, downloading);
        m_download_write = std::move(write);
        m_download_file  = file;
        return perform();
    }

    auto Interceptor::Proceed(Session& session) -> Response {
        return session.proceed();
    }

    auto Interceptor::Proceed(Session& session, ProceedHttpMethod method) -> Response {
        switch (method) {
            case ProceedHttpMethod::GET_REQUEST    : return session.Get();
            case ProceedHttpMethod::POST_REQUEST   : return session.Post();
            case ProceedHttpMethod::PUT_REQUEST    : return session.Put();
            case ProceedHttpMethod::DELETE_REQUEST : return session.Delete();
            case ProceedHttpMethod::PATCH_REQUEST  : return session.Patch();
            case ProceedHttpMethod::HEAD_REQUEST   : return session.Head();
            case ProceedHttpMethod::OPTIONS_REQUEST: return session.Options();
            default                                : throw std::invalid_argument{ "mcr::Interceptor: this method requires a download destination." };
        }
    }

    auto Interceptor::Proceed(Session& session, ProceedHttpMethod method, std::ofstream& file) -> Response {
        if (method != ProceedHttpMethod::DOWNLOAD_FILE_REQUEST) {
            throw std::invalid_argument{ "mcr::Interceptor: a stream requires DOWNLOAD_FILE_REQUEST." };
        }
        return session.Download(file);
    }

    auto Interceptor::Proceed(Session& session, ProceedHttpMethod method, WriteCallback const& write) -> Response {
        if (method != ProceedHttpMethod::DOWNLOAD_CALLBACK_REQUEST) {
            throw std::invalid_argument{ "mcr::Interceptor: a callback requires DOWNLOAD_CALLBACK_REQUEST." };
        }
        return session.Download(write);
    }
} // namespace mcr
