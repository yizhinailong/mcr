/**
 * @file test_curlholder.cpp
 * @brief Verify curl ownership, move safety, error-buffer lifetime, and binary URL conversion.
 */
#include <curl/curl.h>

import std;
import mcr;

static_assert(!std::is_copy_constructible_v<mcr::curl::CurlHolder>);
static_assert(!std::is_copy_assignable_v<mcr::curl::CurlHolder>);
static_assert(std::is_nothrow_move_constructible_v<mcr::curl::CurlHolder>);
static_assert(std::is_nothrow_move_assignable_v<mcr::curl::CurlHolder>);
static_assert(std::is_nothrow_destructible_v<mcr::curl::CurlHolder>);
static_assert(std::is_same_v<decltype(mcr::curl::CurlHolder::error), std::array<char, CURL_ERROR_SIZE>>);
static_assert(std::is_same_v<decltype(std::declval<mcr::curl::CurlHolder const&>().UrlEncode({})), mcr::utils::SecureString>);
static_assert(std::is_same_v<decltype(std::declval<mcr::curl::CurlHolder const&>().UrlDecode({})), mcr::utils::SecureString>);

namespace {

    std::atomic<std::ptrdiff_t> g_live_allocations{ 0 };
    std::atomic_bool            g_fail_allocations{ false };

    auto tracked_malloc(std::size_t size) noexcept -> void* {
        if (g_fail_allocations.load()) {
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
        if (g_fail_allocations.load()) {
            return nullptr;
        }
        return std::realloc(pointer, size);
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
        if (g_fail_allocations.load()) {
            return nullptr;
        }
        auto* result{ std::calloc(count, size) };
        if (result) {
            ++g_live_allocations;
        }
        return result;
    }

    struct FailAllocations {
        FailAllocations() { g_fail_allocations.store(true); }

        ~FailAllocations() { g_fail_allocations.store(false); }
    };

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_curlholder: {}", message);
        }
        return condition;
    }

    auto view(mcr::utils::SecureString const& value) -> std::string_view {
        return { value.data(), value.size() };
    }

    auto has_no_resources(mcr::curl::CurlHolder const& holder) -> bool {
        return !holder.handle && !holder.chunk && !holder.resolve_curl_list && !holder.multipart;
    }

    auto attach_resources(mcr::curl::CurlHolder& holder, int& mime_frees) -> bool {
        holder.chunk             = curl_slist_append(nullptr, "X-Test: owned");
        holder.resolve_curl_list = curl_slist_append(nullptr, "example.test:80:127.0.0.1");
        holder.multipart         = curl_mime_init(holder.handle);
        if (!check(holder.chunk && holder.resolve_curl_list && holder.multipart, "fixture must allocate header, resolve, and MIME resources")) {
            return false;
        }
        auto* part{ curl_mime_addpart(holder.multipart) };
        if (!check(part != nullptr, "fixture must allocate a MIME part")) {
            return false;
        }
        return check(
            curl_mime_data_cb(
                part,
                0,
                [](char*, std::size_t, std::size_t, void*) -> std::size_t { return 0; },
                nullptr,
                [](void* counter) { ++*static_cast<int*>(counter); },
                &mime_frees
            ) == CURLE_OK &&
                curl_easy_setopt(holder.handle, CURLOPT_HTTPHEADER, holder.chunk) == CURLE_OK &&
                curl_easy_setopt(holder.handle, CURLOPT_RESOLVE, holder.resolve_curl_list) == CURLE_OK &&
                curl_easy_setopt(holder.handle, CURLOPT_MIMEPOST, holder.multipart) == CURLE_OK,
            "fixture resources must be attached to the easy handle"
        );
    }

    auto check_error_buffer(mcr::curl::CurlHolder& holder) -> bool {
        // The invalid URL fails before any network connection is attempted.
        return check(
            curl_easy_setopt(holder.handle, CURLOPT_URL, "http://[") == CURLE_OK &&
                curl_easy_perform(holder.handle) == CURLE_URL_MALFORMAT && holder.error.front() != '\0',
            "curl failures must write diagnostics to the current holder's error buffer"
        );
    }

    auto check_url_conversion() -> bool {
        mcr::curl::CurlHolder holder;
        bool            passed{ check(holder.handle && !holder.chunk && !holder.resolve_curl_list && !holder.multipart && std::ranges::all_of(holder.error, [](char value) { return value == '\0'; }), "construction must create a handle and initialize empty resources and error storage") };
        passed &= check(holder.UrlEncode("Hello World!") == "Hello%20World%21" && holder.UrlDecode("Hello%20World%21") == "Hello World!", "URL helpers must preserve cpr's ASCII behavior");
        passed &= check(holder.UrlEncode("AZaz09-._~") == "AZaz09-._~", "unreserved URL characters must remain unescaped");
        passed &= check(holder.UrlEncode("/?&=+#%") == "%2F%3F%26%3D%2B%23%25", "reserved component characters must be escaped");
        passed &= check(holder.UrlEncode("\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89") == "%E4%B8%80%E4%BA%8C%E4%B8%89", "UTF-8 input must be encoded byte by byte");
        passed &= check(holder.UrlDecode("a+b%2B%20c") == "a+b+ c" && holder.UrlDecode("%GG%2%") == "%GG%2%", "decoding must preserve plus signs and malformed percent escapes as curl does");
        std::array<char, 4> const plain{ 'a', ' ', 'b', 'x' };
        std::array<char, 4> const escaped{ '%', '4', '1', 'x' };
        passed &= check(holder.UrlEncode({ plain.data(), 3 }) == "a%20b" && holder.UrlDecode({ escaped.data(), 3 }) == "A", "URL helpers must respect view lengths without requiring null termination");
        passed &= check(holder.UrlEncode({}).empty() && holder.UrlDecode({}).empty() && holder.UrlEncode({ plain.data(), 0 }).empty() && holder.UrlDecode({ escaped.data(), 0 }).empty(), "empty views must not trigger curl's strlen fallback");
        std::string const binary{ "\0a\0b\0", 5 };
        passed &= check(holder.UrlEncode(binary) == "%00a%00b%00" && view(holder.UrlDecode("%00a%00b%00")) == binary && view(holder.UrlDecode(binary)) == binary, "embedded nulls must survive encoding and decoding at every position");
        std::string bytes;
        for (unsigned value{ 0 }; value < 256; ++value) {
            bytes.push_back(static_cast<char>(value));
        }
        auto const encoded{ holder.UrlEncode(bytes) };
        passed &= check(view(holder.UrlDecode(view(encoded))) == bytes, "all 256 byte values must round-trip through percent encoding");
        passed &= check_error_buffer(holder);
        return passed;
    }

    auto check_moves() -> bool {
        int  source_frees{ 0 };
        int  replaced_frees{ 0 };
        bool passed{ true };
        {
            mcr::curl::CurlHolder source;
            if (!attach_resources(source, source_frees)) {
                return false;
            }
            auto* handle{ source.handle };
            auto* chunk{ source.chunk };
            auto* resolve{ source.resolve_curl_list };
            auto* multipart{ source.multipart };
            source.error.front() = 'x';
            auto const      old_error{ source.error };
            mcr::curl::CurlHolder moved{ std::move(source) };
            passed &= check(has_no_resources(source) && moved.handle == handle && moved.chunk == chunk && moved.resolve_curl_list == resolve && moved.multipart == multipart && moved.error == old_error && source_frees == 0, "move construction must transfer every resource and preserve error text without freeing the source resources");
            passed &= check_error_buffer(moved);
            passed &= check(source.error == old_error, "moved handles must not write to the source object's error buffer");
            auto const      moved_error{ moved.error };
            mcr::curl::CurlHolder destination;
            if (!attach_resources(destination, replaced_frees)) {
                return false;
            }
            passed &= check(&(destination = std::move(moved)) == &destination && has_no_resources(moved) && destination.handle == handle && destination.chunk == chunk && destination.resolve_curl_list == resolve && destination.multipart == multipart && destination.error == moved_error && replaced_frees == 1 && source_frees == 0, "move assignment must release replaced resources and transfer all source ownership");
            passed &= check_error_buffer(destination);
            passed &= check(moved.error == moved_error, "move assignment must rebind error storage without modifying the former holder");
            auto* self{ &destination };
            destination  = std::move(*self);
            passed      &= check(destination.handle == handle && destination.chunk == chunk && destination.resolve_curl_list == resolve && destination.multipart == multipart && source_frees == 0 && destination.UrlEncode("still usable") == "still%20usable", "self-move must preserve resources and usability");
            passed      &= check(std::string_view{ destination.chunk->data } == "X-Test: owned" && std::string_view{ destination.resolve_curl_list->data } == "example.test:80:127.0.0.1", "owned lists must remain valid after moving");

            for (bool const encode : { true, false }) {
                try {
                    if (encode) {
                        (void)source.UrlEncode("input");
                    } else {
                        (void)source.UrlDecode("input");
                    }
                    passed &= check(false, "URL conversion on a holder without a handle must throw");
                } catch (std::logic_error const&) {
                }
            }
            mcr::curl::CurlHolder empty{ std::move(source) };
            passed &= check(has_no_resources(empty), "moving an empty holder must remain safe");
            source  = std::move(destination);
            passed &= check(has_no_resources(destination) && source.UrlDecode("reused%20holder") == "reused holder", "a moved-from holder must be reusable by move assignment");
        }
        passed &= check(source_frees == 1 && replaced_frees == 1, "each MIME resource must be freed exactly once across moves and destruction");

        int discarded_frees{ 0 };
        {
            mcr::curl::CurlHolder source;
            mcr::curl::CurlHolder owner{ std::move(source) };
            if (!attach_resources(owner, discarded_frees)) {
                return false;
            }
            owner   = std::move(source);
            passed &= check(has_no_resources(owner) && discarded_frees == 1, "assignment from an empty holder must release the destination's prior resources");
        }
        return passed;
    }

    auto check_failures() -> bool {
        bool passed{ true };
        {
            FailAllocations fail;
            try {
                mcr::curl::CurlHolder invalid;
                passed &= check(false, "easy-handle initialization failure must throw instead of leaving a null handle");
            } catch (std::runtime_error const& error) {
                passed &= check(std::string_view{ error.what() }.contains("curl_easy_init"), "initialization failure must identify the failed operation");
            }
        }
        mcr::curl::CurlHolder holder;
        auto const      before{ g_live_allocations.load() };
        {
            FailAllocations fail;
            passed &= check(holder.UrlEncode("allocation required").empty() && holder.UrlDecode("allocation%20required").empty(), "curl conversion allocation failures must preserve cpr's empty-result behavior");
        }
        passed &= check(g_live_allocations.load() == before && holder.UrlEncode("works again") == "works%20again", "allocation failures must not leak or prevent later conversion");
        return passed;
    }

    auto check_concurrent_construction() -> bool {
        std::vector<std::future<bool>> workers;
        for (int worker{ 0 }; worker < 8; ++worker) {
            workers.push_back(std::async(std::launch::async, [] {
                for (int iteration{ 0 }; iteration < 8; ++iteration) {
                    mcr::curl::CurlHolder holder;
                    if (!holder.handle || holder.UrlEncode("thread safe") != "thread%20safe") {
                        return false;
                    }
                }
                return true;
            }));
        }
        bool passed{ true };
        for (auto& worker : workers) {
            passed &= check(worker.get(), "independent holders must support concurrent construction and destruction");
        }
        return passed;
    }

} // namespace

int main() {
    if (curl_global_init_mem(CURL_GLOBAL_DEFAULT, tracked_malloc, tracked_free, tracked_realloc, tracked_strdup, tracked_calloc) != CURLE_OK) {
        std::println("test_curlholder: curl global initialization failed");
        return 1;
    }
    bool passed{ true };
    try {
        passed &= check_url_conversion();
        passed &= check_moves();
        passed &= check_failures();
        passed &= check_concurrent_construction();
    } catch (std::exception const& error) {
        std::println("test_curlholder: unexpected exception: {}", error.what());
        passed = false;
    }
    curl_global_cleanup();
    passed &= check(g_live_allocations.load() == 0, "all curl allocations must be released after holders and global state are destroyed");
    if (!passed) {
        return 1;
    }
    std::println("test_curlholder: ok");
    return 0;
}
