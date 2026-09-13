/**
 * @file test_util.cpp
 * @brief Verify HTTP metadata parsing, binary curl adapters, cancellation, and URL conversion.
 */
#include <curl/curl.h>

import std;
import mcr;

namespace {

    namespace utils = mcr::utils;

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_util: {}", message);
        }
        return condition;
    }

    template <typename Exception, typename Function>
    auto throws(Function&& function) -> bool {
        try {
            function();
        } catch (Exception const&) {
            return true;
        }
        return false;
    }

    auto check_headers() -> bool {
        std::string status;
        std::string reason;
        auto        header{ utils::parse_header(
            "HTTP/1.1 200 OK\r\nServer: nginx\r\nDate: Sun, 05 Mar 2017 00:34:54 GMT\r\n" "Content-Type: application/json\r\nContent-Length: 351\r\nConnection: keep-alive\r\n" "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Credentials: true\r\n\r\n",
            &status,
            &reason
        ) };
        bool        passed{ check(header.size() == 7 && header.at("server") == "nginx" && header.at("date") == "Sun, 05 Mar 2017 00:34:54 GMT" && header.at("content-type") == "application/json" && header.at("content-length") == "351" && header.at("connection") == "keep-alive" && header.at("access-control-allow-origin") == "*" && header.at("access-control-allow-credentials") == "true" && status == "HTTP/1.1 200 OK" && reason == "OK", "parse cpr's basic response and expose case-insensitive headers") };
        for (std::string_view const ending : { "\n", " \n", "\r\n", " \r\n", "\t \r\n" }) {
            header  = utils::parse_header(std::string{ "HTTP/1.1 200 OK\nAuth:" } + std::string{ ending } + "Next: \t value \t\r\n");
            passed &= check(header.size() == 2 && header.at("Auth").empty() && header.at("Next") == "value", "empty values and surrounding header whitespace must be handled for LF and CRLF");
        }
        header = utils::parse_header(
            "Preamble: ignored\nHTTP/1.1 100 Continue\r\nInterim: discarded\r\n\r\n" "HTTP/1.1 302 Found\r\nLocation: /final\r\n\r\nHTTP/2 204 \t\r\n" "X-Value: first\r\nx-value: second: value\r\nMalformed line\r\n\r\nTrailer: final",
            &status,
            &reason
        );
        passed &= check(header.size() == 2 && header.at("X-Value") == "second: value" && header.at("Trailer") == "final" && header.find("X-Value")->first == "X-Value" && status == "HTTP/2 204" && reason.empty(), "only final headers survive redirects and interim responses; absent reasons clear stale output");

        struct StatusCase {
            std::string_view input;
            std::string_view status;
            std::string_view reason;
        };

        StatusCase const cases[]{
            {              "HTTP/1.1 407 Proxy Authentication Required\n",         "HTTP/1.1 407 Proxy Authentication Required",    "Proxy Authentication Required" },
            {                                          "HTTP/1.1 200\r\n",                                       "HTTP/1.1 200",                                 "" },
            {                                             "HTTP/3 200 \n",                                         "HTTP/3 200",                                 "" },
            { "HTTP/1.1\t 503\t  Service: temporarily unavailable \t\r\n", "HTTP/1.1\t 503\t  Service: temporarily unavailable", "Service: temporarily unavailable" },
            {                                                   "HTTP/\n",                                              "HTTP/",                                 "" },
            {                                              "HTTP/ \t\r\n",                                              "HTTP/",                                 "" },
        };
        for (auto const& entry : cases) {
            for (int outputs{ 0 }; outputs != 4; ++outputs) {
                status = reason  = "old";
                header            = utils::parse_header(entry.input, outputs & 1 ? &status : nullptr, outputs & 2 ? &reason : nullptr);
                passed          &= check(header.empty() && status == (outputs & 1 ? entry.status : "old") && reason == (outputs & 2 ? entry.reason : "old"), "status extraction must honor independent optional outputs and never turn a reason colon into a header");
            }
        }
        status = reason  = "unchanged";
        passed           &= check(utils::parse_header({}, &status, &reason).empty() && status == "unchanged" && reason == "unchanged", "empty input must preserve status outputs");
        header            = utils::parse_header("Only: value", &status, &reason);
        passed          &= check(header.at("Only") == "value" && status == "unchanged" && reason == "unchanged", "header-only input must not invent a status");
        std::string const binary{ "xX-Binary: a\0b\r\ny", 17 };
        header  = utils::parse_header(std::string_view{ binary }.substr(1, 15));
        passed &= check(header.size() == 1 && header.at("X-Binary") == std::string{ "a\0b", 3 }, "header parsing must honor bounded views and embedded null bytes");
        std::string aliased{ "HTTP/1.1 200 A longer reason phrase\r\nOwned: preserved\r\n" };
        header    = utils::parse_header(aliased, &aliased, &reason);
        passed  &= check(aliased == "HTTP/1.1 200 A longer reason phrase" && reason == "A longer reason phrase" && header.at("Owned") == "preserved", "status output may reuse the string backing the input view");
        aliased  = "HTTP/1.1 404 Missing\r\nOwned: preserved\r\n";
        header    = utils::parse_header(aliased, &status, &aliased);
        passed  &= check(aliased == "Missing" && status == "HTTP/1.1 404 Missing" && header.at("Owned") == "preserved", "reason output may reuse the string backing the input view");
        return passed;
    }

    auto check_text_helpers() -> bool {
        bool              passed{ check(utils::split({}, ',').empty() && utils::split(",", ',') == std::vector<std::string>{ "" } && utils::split(",a,,b,", ',') == std::vector<std::string>{ "", "a", "", "b" } && utils::split("a,,", ',') == std::vector<std::string>{ "a", "" } && utils::split("abc", ',') == std::vector<std::string>{ "abc" }, "split must preserve cpr's leading/interior empties and omit the final empty field") };
        std::string const binary{ "a\0b\0", 4 };
        passed &= check(utils::split(binary, '\0') == std::vector<std::string>{ "a", "b" } && utils::split(binary, ',') == std::vector<std::string>{ binary }, "split must accept null bytes as data and delimiters");
        for (unsigned int mask{ 0 }; mask < 16; ++mask) {
            std::string value{ "true" };
            for (std::size_t index{ 0 }; index < value.size(); ++index) {
                if (mask & (1U << index)) {
                    value[index] -= 'a' - 'A';
                }
            }
            passed &= check(utils::is_true(value), "every ASCII case combination of true must be accepted");
        }
        for (std::string_view const value : { "", "FALSE", "1", "tru", "true!", " true", "true ", "\xFF" "rue" }) {
            passed &= check(!utils::is_true(value), "truth parsing must reject other tokens, whitespace, and high-bit bytes");
        }
        passed &= check(!utils::is_true(std::string_view{ "true\0", 5 }), "truth parsing must not stop at embedded null bytes");
        passed &= check(utils::s_timestamp_to_t("0") == 0 && utils::s_timestamp_to_t("1656908640") == 1656908640 && utils::s_timestamp_to_t(" \t+42suffix") == 42 && utils::s_timestamp_to_t(std::string_view{ "x123x" }.substr(1, 3)) == 123, "timestamps use seconds and cpr's decimal prefix and whitespace rules");
        auto const maximum{ (std::numeric_limits<std::time_t>::max)() };
        auto const minimum{ (std::numeric_limits<std::time_t>::min)() };
        passed &= check(utils::s_timestamp_to_t(std::to_string(maximum)) == maximum && utils::s_timestamp_to_t(std::to_string(minimum)) == minimum, "timestamp parsing must cover the entire platform time_t range");
        if constexpr (std::is_signed_v<std::time_t>) {
            passed &= check(utils::s_timestamp_to_t("-1") == -1 && throws<std::out_of_range>([&] { (void)utils::s_timestamp_to_t(std::to_string(minimum) + "0"); }), "signed timestamps must retain negative values and reject underflow");
        }
        passed &= check(throws<std::out_of_range>([&] { (void)utils::s_timestamp_to_t(std::to_string(maximum) + "0"); }), "timestamp overflow must throw");
        for (std::string_view const value : { "", " ", "invalid", "+", "--1" }) {
            passed &= check(throws<std::invalid_argument>([&] { (void)utils::s_timestamp_to_t(value); }), "timestamps without a decimal prefix must throw");
        }
        return passed;
    }

    auto check_cookies() -> bool {
        auto         empty{ utils::parse_cookies(nullptr) };
        bool         passed{ check(empty.empty() && empty.encode, "a null curl list must yield an empty collection with encoding enabled") };
        mcr::Cookies cookies;
        {
            mcr::curl::CurlHolder owner;
            for (char const* record : {
                     "127.0.0.1\tFALSE\t/\tFALSE\t1656908640\tstatus\ton",
                     ".example.test\tTrUe\t/account\tTRUE\t0\tstatus\tdebug",
                     "#HttpOnly_.example.test\tTRUE\t/\tFALSE\t0\tempty\t",
                     "example.test\tFALSE\t\tFALSE\t0",
                     "example.test\tFALSE\t/\tFALSE\t0\textra\tvalue\tignored",
                 }) {
                auto* appended{ curl_slist_append(owner.chunk, record) };
                if (!appended) {
                    throw std::bad_alloc{};
                }
                owner.chunk = appended;
            }
            cookies  = utils::parse_cookies(owner.chunk);
            passed  &= check(std::string_view{ owner.chunk->data }.starts_with("127.0.0.1\tFALSE"), "parsing must leave the borrowed curl list intact");
        }
        passed &= check(cookies.encode && std::ranges::distance(cookies) == 5, "cookies must own their strings after the raw list is freed");
        passed &= check(cookies[0].GetName() == "status" && cookies[0].GetValue() == "on" && cookies[0].GetDomain() == "127.0.0.1" && !cookies[0].IsIncludingSubdomains() && cookies[0].GetPath() == "/" && !cookies[0].IsHttpsOnly() && cookies[0].GetExpires() == std::chrono::system_clock::from_time_t(1656908640), "all seven curl cookie fields must map correctly");
        passed &= check(cookies[1].GetName() == "status" && cookies[1].GetValue() == "debug" && cookies[1].GetDomain() == ".example.test" && cookies[1].IsIncludingSubdomains() && cookies[1].GetPath() == "/account" && cookies[1].IsHttpsOnly() && cookies[1].GetExpires() == std::chrono::system_clock::from_time_t(0), "duplicate names, true flags, paths, and session expiration must be retained in order");
        passed &= check(cookies[2].GetDomain() == "#HttpOnly_.example.test" && cookies[2].GetName() == "empty" && cookies[2].GetValue().empty() && cookies[3].GetName().empty() && cookies[3].GetValue().empty() && cookies[3].GetPath().empty() && cookies[4].GetValue() == "value", "HttpOnly text, missing fields, empty values, and extra columns must follow cpr");
        for (std::string record : { "example.test", "example.test\tFALSE\t/\tFALSE\tbad\tname\tvalue" }) {
            curl_slist raw{ record.data(), nullptr };
            passed &= check(throws<std::invalid_argument>([&] { (void)utils::parse_cookies(&raw); }), "missing and invalid expirations must throw");
        }
        std::string oversized{ "example.test\tFALSE\t/\tFALSE\t999999999999999999999999\tname\tvalue" };
        curl_slist  raw{ oversized.data(), nullptr };
        passed &= check(throws<std::out_of_range>([&] { (void)utils::parse_cookies(&raw); }), "overflowing cookie timestamps must throw");
        return passed;
    }

    auto check_callbacks() -> bool {
        std::array<char, 8> buffer{};
        bool                passed{ true };
        mcr::ReadCallback   read{ [&](char* output, std::size_t& count, std::intptr_t userdata) {
                                   passed &= check(output == buffer.data() && count == 8 && userdata == 42, "read adapter must forward buffer, full capacity, and user data");
                                   std::ranges::copy(std::string_view{ "a\0b", 3 }, output);
                                   count = 3;
                                   return true;
                               },
                                42 };
        passed         &= check(utils::read_user_function(buffer.data(), 2, 4, &read) == 3 && std::string_view{ buffer.data(), 3 } == std::string_view{ "a\0b", 3 }, "read adapter must return a short binary read");
        read.callback  = [](char*, std::size_t& count, std::intptr_t) {
            count = 0;
            return true;
        };
        passed         &= check(utils::read_user_function(buffer.data(), 1, 8, &read) == 0, "successful zero-byte reads must indicate EOF");
        read.callback  = [](char*, std::size_t& count, std::intptr_t) {
            count = 0;
            return false;
        };
        passed         &= check(utils::read_user_function(buffer.data(), 1, 8, &read) == CURL_READFUNC_ABORT, "a rejected read must abort even if the producer sets count to zero");
        read.callback  = {};
        passed         &= check(utils::read_user_function(buffer.data(), 2, 4, &read) == 8, "empty read callbacks must preserve cpr's unchanged-count behavior");

        std::string bytes{ "a\0b\r\nx", 6 };
        bool        accept{ true };
        int         calls{ 0 };
        auto        consume = [&](std::string_view data, std::intptr_t userdata) {
            ++calls;
            passed &= check(data.data() == bytes.data() && data == bytes && userdata == -7, "header and write adapters must borrow the exact binary chunk");
            return accept;
        };
        mcr::HeaderCallback header{ consume, -7 };
        mcr::WriteCallback  write{ consume, -7 };
        passed           &= check(utils::header_user_function(bytes.data(), 2, 3, &header) == 6 && utils::write_user_function(bytes.data(), 3, 2, &write) == 6, "accepted header and body chunks must return their byte counts");
        accept           = false;
        passed           &= check(utils::header_user_function(bytes.data(), 1, 6, &header) == 0 && utils::write_user_function(bytes.data(), 1, 6, &write) == 0 && calls == 4, "rejected header and body chunks must stop the transfer");
        header.callback  = {};
        write.callback   = {};
        passed           &= check(utils::header_user_function(bytes.data(), 0, 6, &header) == 0 && utils::write_user_function(bytes.data(), 1, 6, &write) == 6, "empty consumers must accept chunks, including empty input");
        std::string accumulated{ "prefix:" };
        passed         &= check(utils::write_function(bytes.data(), 2, 3, &accumulated) == 6 && utils::write_function(bytes.data(), 0, 6, &accumulated) == 0 && accumulated == "prefix:" + bytes, "string writes must append binary bytes and permit empty chunks");

        using Counter  = mcr::CprPfArgT;
        Counter const         large{ (std::numeric_limits<Counter>::max)() };
        mcr::ProgressCallback progress{ [&](Counter total, Counter now, Counter upload_total, Counter upload_now, std::intptr_t userdata) {
                                           passed &= check(total == large && now == 2 && upload_total == 3 && upload_now == 1 && userdata == 42, "progress counters must retain full width and order");
                                           return accept;
                                       },
                                        42 };
        passed  &= check(utils::progress_user_function(&progress, large, 2, 3, 1) == 1, "false progress must abort instead of requesting curl's default progress meter");
        accept  = true;
        passed  &= check(utils::progress_user_function(&progress, large, 2, 3, 1) == 0, "true progress must continue");
        auto                      state{ std::make_shared<std::atomic_bool>(false) };
        mcr::CancellationCallback cancellation{ std::shared_ptr{ state } };
        passed &= check(utils::progress_user_function(&cancellation, 0, 0, 0, 0) == 0, "the progress template must support cancellation callbacks");
        state->store(true);
        passed             &= check(utils::progress_user_function(&cancellation, 0, 0, 0, 0) == 1, "shared cancellation must translate to curl abort");
        auto const custom  = [](Counter, Counter, Counter, Counter) {
            return false;
        };
        passed &= check(utils::progress_user_function(&custom, 0, 0, 0, 0) == 1, "the progress template must also accept a custom callable");

        curl_infotype      expected_type{};
        int                debug_calls{ 0 };
        mcr::DebugCallback debug{ [&](mcr::DebugCallback::InfoType type, std::string_view data, std::intptr_t userdata) {
                                     ++debug_calls;
                                     passed &= check(static_cast<int>(type) == expected_type && data == bytes && data.data() == bytes.data() && userdata == 9, "debug adapter must retain category and binary bytes without copying");
                                 },
                                  9 };
        for (auto type : { CURLINFO_TEXT, CURLINFO_HEADER_IN, CURLINFO_HEADER_OUT, CURLINFO_DATA_IN, CURLINFO_DATA_OUT, CURLINFO_SSL_DATA_IN, CURLINFO_SSL_DATA_OUT }) {
            expected_type  = type;
            passed         &= check(utils::debug_user_function(nullptr, type, bytes.data(), bytes.size(), &debug) == 0, "debug adapter must always return zero");
        }
        passed         &= check(debug_calls == 7, "all debug categories must be forwarded");
        write.callback  = [](std::string_view, std::intptr_t) -> bool {
            throw std::runtime_error{ "consumer failed" };
        };
        passed &= check(throws<std::runtime_error>([&] { (void)utils::write_user_function(bytes.data(), 1, bytes.size(), &write); }), "direct adapter calls must preserve cpr's exception propagation");
        return passed;
    }

    auto check_sse() -> bool {
        std::vector<std::string>     events;
        bool                         accept{ true };
        bool                         passed{ true };
        mcr::ServerSentEventCallback sse{ [&](mcr::ServerSentEvent&& event, std::intptr_t userdata) {
                                             passed &= check(userdata == 42, "SSE adapter must retain user data");
                                             events.push_back(std::move(event.data));
                                             return accept;
                                         },
                                          42 };
        std::string first{ "data: hel" };
        std::string second{ "lo\r\n\r\n" };
        passed  &= check(utils::write_sse_function(first.data(), 1, first.size(), &sse) == first.size() && events.empty(), "incomplete SSE chunks must be buffered and accepted");
        passed  &= check(utils::write_sse_function(second.data(), 2, 3, &sse) == 6 && events == std::vector<std::string>{ "hello" }, "SSE state must span curl callback invocations");
        accept  = false;
        std::string rejected{ "data: stop\n\ndata: later\n\n" };
        passed &= check(utils::write_sse_function(rejected.data(), 1, rejected.size(), &sse) == 0 && events == std::vector<std::string>{ "hello", "stop" }, "SSE cancellation must reject the whole chunk and suppress later events");
        return passed;
    }

    struct TemporaryFile {
        std::filesystem::path directory{ std::filesystem::temp_directory_path() / std::format("mcr_util_{}", std::chrono::steady_clock::now().time_since_epoch().count()) };
        std::filesystem::path path{ directory / "body.bin" };

        TemporaryFile() {
            if (!std::filesystem::create_directory(directory)) {
                throw std::runtime_error{ "temporary directory already exists" };
            }
        }

        ~TemporaryFile() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            std::filesystem::remove(directory, ignored);
        }
    };

    auto check_file() -> bool {
        TemporaryFile temporary;
        std::string   bytes{ "a\0b\r\nx", 6 };
        std::ofstream file{ temporary.path, std::ios::binary };
        bool          passed{ check(file.is_open() && utils::write_file_function(bytes.data(), 2, 3, &file) == 6 && utils::write_file_function(bytes.data(), 1, 0, &file) == 0 && utils::write_file_function(bytes.data(), 1, 6, &file) == 6, "file adapter must write complete binary chunks and accept empty chunks") };
        file.close();
        passed &= check(!file.fail(), "closing the output file must succeed");
        std::ifstream     input{ temporary.path, std::ios::binary };
        std::string const actual{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        passed &= check(actual == bytes + bytes, "file contents must match repeated binary chunks exactly");
        passed &= check(utils::write_file_function(bytes.data(), 1, bytes.size(), &file) == 0, "a closed output stream must cause curl to abort instead of silently consuming bytes");
        std::ofstream throwing;
        throwing.exceptions(std::ios::badbit | std::ios::failbit);
        passed &= check(throws<std::ios_base::failure>([&] { (void)utils::write_file_function(bytes.data(), 1, bytes.size(), &throwing); }), "direct writes must honor the stream's exception mask");
        return passed;
    }

    auto check_url() -> bool {
        bool                   passed{ check(utils::url_encode("Hello World!") == "Hello%20World%21" && utils::url_decode("Hello%20World%21") == "Hello World!", "URL convenience helpers must match cpr's ASCII examples") };
        std::string_view const unicode{ "\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89" };
        passed &= check(utils::url_encode(unicode) == "%E4%B8%80%E4%BA%8C%E4%B8%89" && utils::url_decode("%E4%B8%80%E4%BA%8C%E4%B8%89") == unicode, "URL helpers must encode UTF-8 bytes");
        std::string const binary{ "a\0b", 3 };
        passed &= check(utils::url_encode(binary) == "a%00b" && utils::url_decode("a%00b") == std::string_view{ binary }, "URL helpers must preserve binary data through percent escapes");
        passed &= check(utils::url_encode({}).empty() && utils::url_decode({}).empty() && utils::url_encode(std::string_view{ "x a x" }.substr(1, 3)) == "%20a%20" && utils::url_decode(std::string_view{ "x%41x" }.substr(1, 3)) == "A", "URL helpers must respect empty and bounded views");
        passed &= check(utils::url_decode("a+b%2Bc%zz%") == "a+b+c%zz%", "decoding must retain plus signs and malformed escapes as curl does");
        return passed;
    }

} // namespace

int main() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::println("test_util: curl global initialization failed");
        return 1;
    }
    bool passed{ true };
    try {
        passed &= check_headers();
        passed &= check_text_helpers();
        passed &= check_cookies();
        passed &= check_callbacks();
        passed &= check_sse();
        passed &= check_file();
        passed &= check_url();
    } catch (std::exception const& error) {
        std::println("test_util: unexpected exception: {}", error.what());
        passed = false;
    }
    curl_global_cleanup();
    if (!passed) {
        return 1;
    }
    std::println("test_util: ok");
    return 0;
}
