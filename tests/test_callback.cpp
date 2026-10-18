/**
 * @file test_callback.cpp
 * @brief Verify transfer callback forwarding, abort results, and cancellation precedence through mcr.
 */
#include <curl/curl.h>

import std;
import mcr;

using InfoType = mcr::DebugCallback::InfoType;
using Counter = mcr::CprPfArgT;

static_assert(std::is_same_v<decltype(mcr::ReadCallback::size), mcr::CprOffT>);
static_assert(std::is_same_v<std::underlying_type_t<InfoType>, std::uint8_t>);
static_assert(std::to_underlying(InfoType::TEXT) == CURLINFO_TEXT);
static_assert(std::to_underlying(InfoType::HEADER_IN) == CURLINFO_HEADER_IN);
static_assert(std::to_underlying(InfoType::HEADER_OUT) == CURLINFO_HEADER_OUT);
static_assert(std::to_underlying(InfoType::DATA_IN) == CURLINFO_DATA_IN);
static_assert(std::to_underlying(InfoType::DATA_OUT) == CURLINFO_DATA_OUT);
static_assert(std::to_underlying(InfoType::SSL_DATA_IN) == CURLINFO_SSL_DATA_IN);
static_assert(std::to_underlying(InfoType::SSL_DATA_OUT) == CURLINFO_SSL_DATA_OUT);
static_assert(!std::is_same_v<mcr::HeaderCallback, mcr::WriteCallback>);
static_assert(!std::is_convertible_v<std::shared_ptr<std::atomic_bool>, mcr::CancellationCallback>);
static_assert(!std::is_constructible_v<mcr::CancellationCallback, std::shared_ptr<std::atomic_bool>&>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_callback: {}", message);
        }
        return condition;
    }

    template <typename Function>
    auto propagates_exception(Function&& invoke) -> bool {
        try {
            std::forward<Function>(invoke)();
        } catch (std::runtime_error const& error) {
            return std::string_view{ error.what() } == "consumer failed";
        }
        return false;
    }

    auto check_read() -> bool {
        mcr::ReadCallback const empty;
        std::array<char, 2> buffer{ 'x', 'y' };
        std::size_t size{ buffer.size() };
        bool passed{ check(empty.userdata == 0 && empty.size == 0 && !empty.callback && empty(buffer.data(), size) && size == buffer.size() && buffer == std::array{ 'x', 'y' }, "default read callback must accept without changing the buffer or byte count") };

        std::string const payload{ "a\0b", 3 };
        std::size_t offset{ 0 };
        mcr::ReadCallback reader{
            3,
            [&](char* destination, std::size_t& available, std::intptr_t userdata) {
                passed &= check(destination == buffer.data() && &available == &size && userdata == -7, "read callback must forward the original buffer, count reference, and current user data");
                available = (std::min)(available, payload.size() - offset);
                std::copy_n(payload.data() + offset, available, destination);
                offset += available;
                return true;
            },
            42
        };
        passed &= check(reader.size == 3 && reader.userdata == 42, "sized read constructor must preserve the declared size and user data");
        reader.userdata = -7;
        std::string uploaded;
        for (std::size_t const expected : { std::size_t{ 2 }, std::size_t{ 1 }, std::size_t{ 0 } }) {
            size = buffer.size();
            passed &= check(std::as_const(reader)(buffer.data(), size) && size == expected, "producer byte counts must support partial reads and end-of-input");
            uploaded.append(buffer.data(), size);
        }
        passed &= check(uploaded == payload, "partial reads must reconstruct binary upload data");
        mcr::ReadCallback unknown{ [](char*, std::size_t&, std::intptr_t) { return false; } };
        passed &= check(unknown.size == -1 && unknown.userdata == 0 && !unknown(buffer.data(), size), "callback-only construction must select unknown size and forward abort");
        for (mcr::CprOffT const total : { mcr::CprOffT{ -1 }, mcr::CprOffT{ 0 }, (std::numeric_limits<mcr::CprOffT>::max)() }) {
            mcr::ReadCallback const sized{ total, {} };
            size = buffer.size();
            passed &= check(sized.size == total && sized(buffer.data(), size) && size == buffer.size(), "declared upload sizes must retain the full offset range independently of the buffer size");
        }
        reader.callback = [](char*, std::size_t& count, std::intptr_t) {
            count = 0;
            return false;
        };
        passed &= check(!reader(buffer.data(), size) && size == 0, "replacing a read producer must forward its abort and count update");
        reader.callback = [](char*, std::size_t&, std::intptr_t) -> bool {
            throw std::runtime_error{ "consumer failed" };
        };
        passed &= check(propagates_exception([&] { (void)reader(buffer.data(), size); }), "read exceptions must propagate");
        reader.callback = {};
        passed &= check(reader(buffer.data(), size) && size == 0, "clearing a read producer must restore default acceptance");
        return passed;
    }

    template <typename Callback>
    auto check_text() -> bool {
        Callback const empty;
        bool passed{ check(empty.userdata == 0 && !empty.callback && empty({}), "default header/body callbacks must accept empty input") };
        std::array<char, 5> const storage{ 'x', 'a', '\0', 'b', 'y' };
        std::string_view const payload{ storage.data() + 1, 3 };
        std::string_view expected{ payload };
        std::intptr_t expected_userdata{ 42 };
        bool keep_going{ true };
        int calls{ 0 };
        Callback consumer{
            [&](std::string_view data, std::intptr_t userdata) {
                ++calls;
                passed &= check(data.data() == expected.data() && data.size() == expected.size() && data == expected && userdata == expected_userdata, "header/body callbacks must preserve the borrowed view, binary contents, and user data");
                return keep_going;
            },
            expected_userdata
        };
        passed &= check(std::as_const(consumer)(payload) && calls == 1, "header/body callbacks must forward true");
        consumer.userdata = expected_userdata = -7;
        keep_going = false;
        passed &= check(!std::as_const(consumer)(payload) && calls == 2, "header/body callbacks must forward false and public user data updates");
        expected = {};
        passed &= check(!consumer({}) && calls == 3, "installed header/body callbacks must receive empty chunks");
        consumer.callback = [](std::string_view, std::intptr_t) -> bool {
            throw std::runtime_error{ "consumer failed" };
        };
        passed &= check(propagates_exception([&] { (void)consumer(payload); }), "header/body callback exceptions must propagate");
        consumer.callback = {};
        passed &= check(consumer(payload) && calls == 3, "clearing a header/body callback must restore default acceptance");
        return passed;
    }

    auto check_progress() -> bool {
        mcr::ProgressCallback const empty;
        bool passed{ check(empty.userdata == 0 && !empty.callback && empty(0, 0, 0, 0), "default progress callback must continue") };
        std::array<Counter, 4> expected{ (std::numeric_limits<Counter>::max)(), (std::numeric_limits<Counter>::min)(), 17, 3 };
        std::intptr_t expected_userdata{ 42 };
        bool keep_going{ true };
        mcr::ProgressCallback observer{
            [&](Counter download_total, Counter download_now, Counter upload_total, Counter upload_now, std::intptr_t userdata) {
                passed &= check(std::array{ download_total, download_now, upload_total, upload_now } == expected && userdata == expected_userdata, "progress callbacks must preserve counter order, full range, and user data");
                return keep_going;
            },
            expected_userdata
        };
        passed &= check(std::as_const(observer)(expected[0], expected[1], expected[2], expected[3]), "progress observer must forward true");
        observer.userdata = expected_userdata = -7;
        expected = { 0, 0, 0, 0 };
        keep_going = false;
        passed &= check(!observer(0, 0, 0, 0), "progress observer must forward false, zero counters, and user data changes");
        observer.callback = [](Counter, Counter, Counter, Counter, std::intptr_t) -> bool {
            throw std::runtime_error{ "consumer failed" };
        };
        passed &= check(propagates_exception([&] { (void)observer(0, 0, 0, 0); }), "progress exceptions must propagate");
        observer.callback = {};
        passed &= check(observer(0, 0, 0, 0), "clearing progress observer must restore default acceptance");
        return passed;
    }

    auto check_debug() -> bool {
        mcr::DebugCallback const empty;
        empty(InfoType::TEXT, {});
        bool passed{ check(empty.userdata == 0 && !empty.callback, "default debug callback must do nothing") };
        std::string const binary{ "a\0b", 3 };
        std::string_view expected_data{ binary };
        InfoType expected_type{ InfoType::TEXT };
        std::intptr_t expected_userdata{ 42 };
        int calls{ 0 };
        mcr::DebugCallback consumer{
            [&](InfoType type, std::string_view data, std::intptr_t userdata) {
                ++calls;
                passed &= check(type == expected_type && data.data() == expected_data.data() && data == expected_data && userdata == expected_userdata, "debug callback must forward category, borrowed binary bytes, and current user data");
            },
            expected_userdata
        };
        for (InfoType const type : { InfoType::TEXT, InfoType::HEADER_IN, InfoType::HEADER_OUT, InfoType::DATA_IN, InfoType::DATA_OUT, InfoType::SSL_DATA_IN, InfoType::SSL_DATA_OUT, static_cast<InfoType>(255) }) {
            expected_type = type;
            std::as_const(consumer)(type, binary);
        }
        passed &= check(calls == 8, "debug callback must forward every category without filtering");
        consumer.userdata = expected_userdata = -7;
        expected_data = {};
        consumer(expected_type, {});
        passed &= check(calls == 9, "debug callback must forward empty diagnostics");
        consumer.callback = [](InfoType, std::string_view, std::intptr_t) {
            throw std::runtime_error{ "consumer failed" };
        };
        passed &= check(propagates_exception([&] { consumer(InfoType::TEXT, binary); }), "debug exceptions must propagate");
        consumer.callback = {};
        consumer(InfoType::TEXT, binary);
        return passed;
    }

    auto check_cancellation() -> bool {
        mcr::CancellationCallback unbound;
        mcr::CancellationCallback const null_state{ std::shared_ptr<std::atomic_bool>{} };
        bool passed{ check(unbound(0, 0, 0, 0) && null_state(0, 0, 0, 0), "default and null cancellation states must permit progress safely") };
        int calls{ 0 };
        mcr::ProgressCallback observer{
            [&](Counter download_total, Counter download_now, Counter upload_total, Counter upload_now, std::intptr_t userdata) {
                ++calls;
                passed &= check(std::array{ download_total, download_now, upload_total, upload_now } == std::array<Counter, 4>{ 10, 2, 20, 3 } && userdata == 42, "cancellation adapter must forward all counters through the bound observer");
                return true;
            },
            42
        };
        unbound.SetProgressCallback(observer);
        passed &= check(unbound(10, 2, 20, 3) && calls == 1, "a progress observer must work without a cancellation state");

        auto state{ std::make_shared<std::atomic_bool>(false) };
        auto transferred_state{ state };
        mcr::CancellationCallback cancellation{ std::move(transferred_state), observer };
        mcr::CancellationCallback copied{ cancellation };
        passed &= check(!transferred_state && std::as_const(cancellation)(10, 2, 20, 3) && calls == 2, "construction must take shared ownership and borrow the progress observer");
        std::jthread cancel_thread{ [state] { state->store(true); } };
        cancel_thread.join();
        passed &= check(!cancellation(10, 2, 20, 3) && !copied(10, 2, 20, 3) && calls == 2, "shared cancellation must stop copies and suppress user callback invocation");
        state->store(false);
        observer.userdata = -7;
        observer.callback = [&](Counter, Counter, Counter, Counter, std::intptr_t userdata) {
            ++calls;
            passed &= check(userdata == -7, "borrowed observer updates must be visible without rebinding");
            return false;
        };
        passed &= check(!cancellation(0, 0, 0, 0) && !copied(0, 0, 0, 0) && calls == 4, "the observer's false result must abort even when the flag is clear");
        mcr::ProgressCallback replacement;
        cancellation.SetProgressCallback(replacement);
        passed &= check(cancellation(0, 0, 0, 0) && !copied(0, 0, 0, 0) && calls == 5, "rebinding must replace only that adapter's observer reference");

        replacement.callback = [](Counter, Counter, Counter, Counter, std::intptr_t) -> bool {
            throw std::runtime_error{ "consumer failed" };
        };
        state->store(true);
        passed &= check(!cancellation(0, 0, 0, 0), "cancellation must suppress even a throwing observer");
        state->store(false);
        passed &= check(propagates_exception([&] { (void)cancellation(0, 0, 0, 0); }), "observer exceptions must propagate when not cancelled");

        std::weak_ptr<std::atomic_bool> weak_state{ state };
        state.reset();
        auto retained_state{ weak_state.lock() };
        if (!check(static_cast<bool>(retained_state), "cancellation adapters must retain shared state after the caller releases it")) {
            return false;
        }
        retained_state->store(true);
        passed &= check(!cancellation(0, 0, 0, 0), "retained shared state must remain observable");

        auto self_state{ std::make_shared<std::atomic_bool>(false) };
        int self_calls{ 0 };
        mcr::ProgressCallback self_observer{ [&](Counter, Counter, Counter, Counter, std::intptr_t) {
            ++self_calls;
            self_state->store(true);
            return true;
        } };
        mcr::CancellationCallback const self_cancelling{ std::shared_ptr{ self_state }, self_observer };
        passed &= check(self_cancelling(0, 0, 0, 0) && !self_cancelling(0, 0, 0, 0) && self_calls == 1, "the cancellation flag must be checked before the observer, matching cpr");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_read() };
    passed &= check_text<mcr::HeaderCallback>();
    passed &= check_text<mcr::WriteCallback>();
    passed &= check_progress();
    passed &= check_debug();
    passed &= check_cancellation();
    if (!passed) {
        return 1;
    }
    std::println("test_callback: ok");
    return 0;
}
