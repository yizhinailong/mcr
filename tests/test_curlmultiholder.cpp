/**
 * @file test_curlmultiholder.cpp
 * @brief Verify multi-handle ownership, moves, easy-handle interoperability, and allocation failure.
 */
#include <curl/curl.h>

import std;
import mcr;

static_assert(!std::is_copy_constructible_v<mcr::curl::CurlMultiHolder>);
static_assert(!std::is_copy_assignable_v<mcr::curl::CurlMultiHolder>);
static_assert(std::is_nothrow_move_constructible_v<mcr::curl::CurlMultiHolder>);
static_assert(std::is_nothrow_move_assignable_v<mcr::curl::CurlMultiHolder>);
static_assert(std::is_nothrow_destructible_v<mcr::curl::CurlMultiHolder>);
static_assert(std::is_same_v<decltype(mcr::curl::CurlMultiHolder::handle), CURLM*>);

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
            std::println("test_curlmultiholder: {}", message);
        }
        return condition;
    }

    auto check_empty_and_moves() -> bool {
        auto const before{ g_live_allocations.load() };
        bool       passed{ true };
        {
            mcr::curl::CurlMultiHolder source;
            auto*                original{ source.handle };
            int                  running{ -1 };
            passed &= check(original && curl_multi_perform(original, &running) == CURLM_OK && running == 0, "a new multi handle must support an empty perform");
            int queued{ -1 };
            passed &= check(curl_multi_info_read(original, &queued) == nullptr && queued == 0, "a new multi handle must have no completion messages");

            mcr::curl::CurlMultiHolder moved{ std::move(source) };
            passed &= check(!source.handle && moved.handle == original, "move construction must transfer the original handle and clear the source");
            auto const           with_one{ g_live_allocations.load() };
            mcr::curl::CurlMultiHolder destination;
            passed &= check(g_live_allocations.load() > with_one, "a second multi handle must own separate allocations");
            passed &= check(&(destination = std::move(moved)) == &destination && !moved.handle && destination.handle == original, "move assignment must transfer ownership and return the destination");
            passed &= check(g_live_allocations.load() == with_one, "move assignment must immediately release the replaced multi handle");

            auto* self{ &destination };
            destination  = std::move(*self);
            passed      &= check(destination.handle == original && curl_multi_setopt(destination.handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, 2L) == CURLM_OK, "self-move must preserve a usable multi handle");
            mcr::curl::CurlMultiHolder empty{ std::move(source) };
            passed &= check(!empty.handle, "move construction from an empty holder must remain empty");
            source  = std::move(destination);
            passed &= check(!destination.handle && source.handle == original, "a moved-from holder must accept ownership again");
            source  = std::move(empty);
            passed &= check(!source.handle && !empty.handle && g_live_allocations.load() == before, "assignment from an empty holder must release the destination's resources");
            source  = std::move(destination);
            passed &= check(!source.handle && !destination.handle, "assignment between empty holders must be safe");
        }
        {
            auto holder = [] {
                mcr::curl::CurlMultiHolder source;
                return mcr::curl::CurlMultiHolder{ std::move(source) };
            }();
            int running{ -1 };
            passed &= check(curl_multi_perform(holder.handle, &running) == CURLM_OK && running == 0, "destroying a moved-from source must not invalidate the new owner");
        }
        return check(g_live_allocations.load() == before, "destruction must release all multi-handle allocations") && passed;
    }

    auto check_easy_handles() -> bool {
        mcr::curl::CurlHolder first;
        mcr::curl::CurlHolder second;
        bool            passed{ true };
        {
            mcr::curl::CurlMultiHolder source;
            mcr::curl::CurlMultiHolder destination;
            // Malformed URLs fail before any network connection is attempted.
            passed &= check(curl_easy_setopt(first.handle, CURLOPT_URL, "http://[") == CURLE_OK && curl_easy_setopt(second.handle, CURLOPT_URL, "http://[") == CURLE_OK, "easy-handle fixtures must accept the malformed URLs");
            passed &= check(curl_multi_add_handle(source.handle, first.handle) == CURLM_OK, "the first easy handle must attach successfully");
            passed &= check(curl_multi_add_handle(source.handle, second.handle) == CURLM_OK, "the second easy handle must attach successfully");
            mcr::curl::CurlMultiHolder moved{ std::move(source) };
            destination  = std::move(moved);
            passed      &= check(curl_multi_add_handle(destination.handle, first.handle) == CURLM_ADDED_ALREADY, "attached easy handles must remain attached after both move operations");
            int running{ -1 };
            passed &= check(curl_multi_perform(destination.handle, &running) == CURLM_OK && running == 0, "both malformed transfers must finish without network access");
            std::array<bool, 2> seen{};
            int                 completed{ 0 };
            int                 queued{ 0 };
            while (auto* message = curl_multi_info_read(destination.handle, &queued)) {
                ++completed;
                passed &= check(message->msg == CURLMSG_DONE && message->data.result == CURLE_URL_MALFORMAT, "completion messages must report the malformed URL errors");
                if (message->easy_handle == first.handle) {
                    seen[0] = true;
                } else if (message->easy_handle == second.handle) {
                    seen[1] = true;
                }
            }
            passed &= check(completed == 2 && seen[0] && seen[1] && queued == 0, "the moved multi handle must return exactly one completion for each easy handle");
            passed &= check(curl_multi_remove_handle(destination.handle, first.handle) == CURLM_OK, "the first easy handle must detach successfully");
            passed &= check(curl_multi_remove_handle(destination.handle, second.handle) == CURLM_OK, "the second easy handle must detach successfully");
        }
        passed &= check(first.UrlEncode("still alive") == "still%20alive" && second.UrlDecode("still%20alive") == "still alive", "multi cleanup must leave detached easy handles usable by their owners");
        {
            mcr::curl::CurlMultiHolder another;
            passed &= check(curl_multi_add_handle(another.handle, first.handle) == CURLM_OK, "a detached easy handle must be reusable with another multi handle");
            passed &= check(curl_multi_remove_handle(another.handle, first.handle) == CURLM_OK, "a reused easy handle must detach before cleanup");
        }
        return passed;
    }

    auto check_initialization_failure() -> bool {
        auto const before{ g_live_allocations.load() };
        bool       passed{ true };
        {
            FailAllocations fail;
            try {
                mcr::curl::CurlMultiHolder invalid;
                passed &= check(false, "initialization failure must throw instead of exposing a null handle");
            } catch (std::runtime_error const& error) {
                passed &= check(std::string_view{ error.what() }.contains("curl_multi_init"), "initialization failure must identify the failed curl operation");
            }
        }
        passed &= check(g_live_allocations.load() == before, "failed initialization must not leak allocations");
        mcr::curl::CurlMultiHolder recovered;
        return check(recovered.handle != nullptr, "construction must recover after allocation failures stop") && passed;
    }

} // namespace

int main() {
    if (curl_global_init_mem(CURL_GLOBAL_DEFAULT, tracked_malloc, tracked_free, tracked_realloc, tracked_strdup, tracked_calloc) != CURLE_OK) {
        std::println("test_curlmultiholder: curl global initialization failed");
        return 1;
    }
    bool passed{ true };
    try {
        passed &= check_empty_and_moves();
        passed &= check_easy_handles();
        passed &= check_initialization_failure();
    } catch (std::exception const& error) {
        std::println("test_curlmultiholder: unexpected exception: {}", error.what());
        passed = false;
    }
    curl_global_cleanup();
    passed &= check(g_live_allocations.load() == 0, "all curl allocations must be released after holders and global state are destroyed");
    if (!passed) {
        return 1;
    }
    std::println("test_curlmultiholder: ok");
    return 0;
}
