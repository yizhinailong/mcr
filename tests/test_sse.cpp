/**
 * @file test_sse.cpp
 * @brief Verify SSE fields, arbitrary stream chunks, parser reset, and callback control through mcr.
 */
import std;
import mcr;

static_assert(std::is_same_v<decltype(mcr::ServerSentEvent::id), std::optional<std::string>>);
static_assert(std::is_same_v<decltype(mcr::ServerSentEvent::retry), std::optional<std::size_t>>);
static_assert(std::is_same_v<decltype(mcr::ServerSentEventCallback::userdata), std::intptr_t>);

namespace {

    using Event = mcr::ServerSentEvent;
    using namespace std::string_view_literals;

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_sse: {}", message);
        }
        return condition;
    }

    auto make_event(
        std::string data,
        std::string type = "message",
        std::optional<std::string> id = {},
        std::optional<std::size_t> retry = {}
    ) -> Event {
        Event event;
        event.data = std::move(data);
        event.event = std::move(type);
        event.id = std::move(id);
        event.retry = retry;
        return event;
    }

    auto same_events(std::vector<Event> const& actual, std::vector<Event> const& expected) -> bool {
        return std::ranges::equal(actual, expected, [](Event const& lhs, Event const& rhs) {
            return lhs.id == rhs.id && lhs.event == rhs.event && lhs.data == rhs.data && lhs.retry == rhs.retry;
        });
    }

    struct Collector {
        mcr::ServerSentEventParser parser;
        std::vector<Event> events;

        auto Parse(std::string_view data) -> bool {
            return parser.Parse(data, [this](Event&& event) {
                events.push_back(std::move(event));
                return true;
            });
        }
    };

    auto check_fields() -> bool {
        Event const defaults;
        bool passed{ check(!defaults.id && defaults.event == "message" && defaults.data.empty() && !defaults.retry, "event defaults must match cpr") };

        struct FieldCase {
            std::string_view input;
            std::vector<Event> expected;
        };

        FieldCase const cases[]{
            { "data: Hello World\n\n", { make_event("Hello World") } },
            { "data: First line\ndata: Second line\n\n", { make_event("First line\nSecond line") } },
            { "id: 42\nevent: update\nretry: 3000\ndata: complete\n\n", { make_event("complete", "update", "42", 3000) } },
            { ": comment\nunknown: ignored\nevent: unused\nid: old\nretry: 10\n\n\ndata: next\n\n", { make_event("next") } },
            { "data\n\ndata:\n\ndata\ndata\n\n", { make_event(""), make_event(""), make_event("\n") } },
            { "data:\ndata: middle\ndata\n\n", { make_event("\nmiddle\n") } },
            { "event: custom\nevent\nid\ndata: payload\n\n", { make_event("payload", "message", "") } },
            { "id: old\nid: new\nevent: old\nevent: new\nretry: 10\nretry: 20\ndata: x\n\n", { make_event("x", "new", "new", 20) } },
            { "Data: ignored\n data: ignored\ndata:  leading:colon\ndata:\ttab\ndata:trailing \n\n", { make_event(" leading:colon\n\ttab\ntrailing ") } },
            { "id: good\nid: bad\0id\ndata: a\0b\n\n"sv, { make_event(std::string{ "a\0b", 3 }, "message", "good") } },
            { "id: bad\0id\ndata: x\n\n"sv, { make_event("x") } },
            { "id: 42\nretry: 10\nevent: update\ndata: first\n\ndata: second\n\n", { make_event("first", "update", "42", 10), make_event("second") } },
            { "data: x\n", {} },
            { "data: x", {} },
            { "data: \xEF\xBB\xBF\n\n", { make_event("\xEF\xBB\xBF") } },
        };
        for (auto const& entry : cases) {
            Collector collector;
            passed &= check(collector.Parse(entry.input) && same_events(collector.events, entry.expected), "field parsing must preserve values, defaults, and event boundaries");
            passed &= check(collector.Parse({}) && same_events(collector.events, entry.expected), "an empty chunk must not flush an incomplete event or duplicate completed events");
        }
        return passed;
    }

    auto check_chunk_boundaries() -> bool {
        std::string_view const stream{
            "\xEF\xBB\xBF" ": comment\r\n" "id: ignored\r\n\r\n" "id: 42\r\nretry: 3000\r\nretry: 12x\r\nevent: custom\r\n" "data: first\r\ndata:\r\ndata: \xE9\x9B\xAA\r\n\r\n" "data: next\n\n" "event:\rdata\r\r"
        };
        std::vector<Event> const expected{ make_event("first\n\n\xE9\x9B\xAA", "custom", "42", 3000), make_event("next"), make_event("") };
        bool passed{ true };
        for (std::size_t split{ 0 }; split <= stream.size(); ++split) {
            Collector collector;
            passed &= check(collector.Parse(stream.substr(0, split)) && collector.Parse({}) && collector.Parse(stream.substr(split)) && same_events(collector.events, expected), "every two-chunk split must preserve mixed line endings, BOM, UTF-8 bytes, and metadata");
        }
        Collector bytewise;
        for (std::size_t index{ 0 }; index < stream.size(); ++index) {
            passed &= check(bytewise.Parse(stream.substr(index, 1)), "single-byte chunks must continue parsing");
        }
        passed &= check(same_events(bytewise.events, expected), "single-byte chunks must produce the same complete events");

        Collector partial;
        std::string chunk{ "data: owned" };
        passed &= check(partial.Parse(chunk) && partial.events.empty(), "incomplete data must be buffered without dispatch");
        chunk.assign("changed");
        passed &= check(partial.Parse(" value\n") && partial.events.empty(), "a complete data line must still wait for a blank line");
        passed &= check(partial.Parse("\n") && same_events(partial.events, { make_event("owned value") }), "buffered input must outlive the source chunk");
        return passed;
    }

    auto check_retry() -> bool {
        bool passed{ true };
        for (std::size_t const value : { std::size_t{ 0 }, std::size_t{ 1 }, std::size_t{ 5000 }, (std::numeric_limits<std::size_t>::max)() }) {
            Collector collector;
            passed &= check(collector.Parse(std::format("retry: {}\ndata: x\n\n", value)) && same_events(collector.events, { make_event("x", "message", {}, value) }), "retry must preserve valid values through size_t max");
        }
        for (std::string_view const value : { ""sv, "-1"sv, "+1"sv, " 1"sv, "\t1"sv, "1 "sv, "1x"sv, "1.5"sv, "1e3"sv, "0x10"sv, "1\0"sv }) {
            Collector collector;
            passed &= check(collector.Parse(std::format("retry: {}\ndata: x\n\n", value)) && same_events(collector.events, { make_event("x") }), "retry must reject empty, signed, whitespace, nondecimal, and partially parsed values");
        }
        Collector overflow;
        passed &= check(overflow.Parse(std::format("retry: {}0\ndata: x\n\n", (std::numeric_limits<std::size_t>::max)())) && same_events(overflow.events, { make_event("x") }), "retry overflow must be ignored without throwing");
        Collector repeated;
        passed &= check(repeated.Parse("retry: 00042\nretry: bad\nretry\ndata: x\n\n") && same_events(repeated.events, { make_event("x", "message", {}, 42) }), "invalid retry fields must preserve the last valid value in the same event");
        return passed;
    }

    auto check_control_flow() -> bool {
        Collector collector;
        bool const continued{ collector.parser.Parse("data: first\r\n\r\ndata: second\r\n\r\ndata: third\r\n\r\n", [&](Event&& event) {
            collector.events.push_back(std::move(event));
            return collector.events.size() < 2;
        }) };
        bool passed{ check(!continued && same_events(collector.events, { make_event("first"), make_event("second") }), "false must stop dispatch immediately at the requested event") };
        passed &= check(collector.Parse({}) && same_events(collector.events, { make_event("first"), make_event("second"), make_event("third") }), "parsing may resume buffered input after abort, including a pending CRLF pair");

        Collector throwing;
        try {
            (void)throwing.parser.Parse("id: old\nevent: old\nretry: 42\ndata: first\n\ndata: second\n\n", [](Event&&) -> bool {
                throw std::runtime_error{ "consumer failed" };
            });
            passed &= check(false, "consumer exceptions must propagate");
        } catch (std::runtime_error const& error) {
            passed &= check(std::string_view{ error.what() } == "consumer failed", "callback exceptions must retain their diagnostics");
        }
        passed &= check(throwing.Parse({}) && same_events(throwing.events, { make_event("second") }), "throwing callbacks must not redeliver consumed events or leak their metadata");

        Collector empty_callback;
        passed &= check(empty_callback.parser.Parse(": comment\n\n", {}), "a block without data must not invoke an empty callback");
        try {
            (void)empty_callback.parser.Parse("data: first\n\ndata: second\n\n", {});
            passed &= check(false, "an empty parser callback must report bad_function_call on dispatch");
        } catch (std::bad_function_call const&) {
        }
        passed &= check(empty_callback.Parse({}) && same_events(empty_callback.events, { make_event("second") }), "a failed empty callback must leave remaining events available");

        for (std::string_view const prefix : { "id: old\nretry: 7\nevent: old\ndata: partial"sv, "\xEF"sv, "data: old\r"sv }) {
            Collector reset;
            passed &= check(reset.Parse(prefix) && reset.events.empty(), "reset fixture must contain only partial parser state");
            reset.parser.Reset();
            passed &= check(reset.Parse("\xEF\xBB\xBF" "data: fresh\r\r") && same_events(reset.events, { make_event("fresh") }), "Reset must discard partial fields, BOM, and CRLF state and start a new stream");
        }
        Collector aborted;
        passed &= check(!aborted.parser.Parse("data: first\n\ndata: discard\n\n", [](Event&&) { return false; }), "reset fixture must leave an event buffered after abort");
        aborted.parser.Reset();
        passed &= check(aborted.Parse("data: fresh\n\n") && same_events(aborted.events, { make_event("fresh") }), "Reset must discard complete buffered events after abort");
        return passed;
    }

    auto check_callback_adapter() -> bool {
        mcr::ServerSentEventCallback empty;
        bool passed{ check(empty.userdata == 0 && !empty.callback && empty(Event{}) && empty.HandleData("data: discarded\n\n"), "default callback must accept events and raw data") };
        std::vector<Event> events;
        std::vector<std::intptr_t> values;
        mcr::ServerSentEventCallback callback{
            [&](Event&& event, std::intptr_t userdata) {
                events.push_back(std::move(event));
                values.push_back(userdata);
                return events.size() < 2;
            },
            42
        };
        passed &= check(callback.HandleData("data: par") && events.empty(), "adapter must retain partial data between chunks");
        callback.userdata = -7;
        passed &= check(!callback.HandleData("tial\n\ndata: second\n\ndata: third\n\n") && same_events(events, { make_event("partial"), make_event("second") }) && values == std::vector<std::intptr_t>{ -7, -7 }, "adapter must forward current user data and callback abort results");
        callback.callback = [&](Event&& event, std::intptr_t userdata) {
            events.push_back(std::move(event));
            values.push_back(userdata);
            return true;
        };
        callback.userdata = 9;
        passed &= check(callback.HandleData({}) && same_events(events, { make_event("partial"), make_event("second"), make_event("third") }) && values.back() == 9, "replacing a public callback must preserve buffered stream state");
        passed &= check(std::as_const(callback)(make_event("direct")) && events.back().data == "direct" && values.back() == 9, "const direct invocation must forward an event and user data");
        callback.callback = {};
        passed &= check(callback.HandleData("data: ignored\n\n") && events.size() == 4, "clearing a callback must restore no-op acceptance");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_fields() };
    passed &= check_chunk_boundaries();
    passed &= check_retry();
    passed &= check_control_flow();
    passed &= check_callback_adapter();
    if (!passed) {
        return 1;
    }
    std::println("test_sse: ok");
    return 0;
}
