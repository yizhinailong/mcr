/**
 * @file test_cookies.cpp
 * @brief Verify owned cookie metadata, GMT expiration dates, collection operations, and request encoding.
 */
#include <curl/curl.h>

import std;
import mcr;
import mcr.curlholder;

static_assert(mcr::EXPIRES_STRING_SIZE == 100);
static_assert(std::is_same_v<decltype(std::declval<mcr::Cookie const&>().GetName()), std::string const&>);
static_assert(std::is_same_v<decltype(std::declval<mcr::Cookie const&>().GetValue()), std::string const&>);
static_assert(std::is_same_v<decltype(std::declval<mcr::Cookie const&>().GetExpires()), std::chrono::system_clock::time_point>);
static_assert(std::is_same_v<mcr::Cookies::iterator, std::vector<mcr::Cookie>::iterator>);
static_assert(std::is_same_v<mcr::Cookies::const_iterator, std::vector<mcr::Cookie>::const_iterator>);
static_assert(std::ranges::random_access_range<mcr::Cookies>);
static_assert(std::ranges::random_access_range<mcr::Cookies const>);
static_assert(std::is_convertible_v<bool, mcr::Cookies>);
static_assert(std::is_convertible_v<mcr::Cookie, mcr::Cookies>);

namespace {

    using namespace std::chrono;

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_cookies: {}", message);
        }
        return condition;
    }

    auto check_metadata() -> bool {
        mcr::Cookie const empty;
        bool              passed{ check(empty.GetName().empty() && empty.GetValue().empty() && empty.GetDomain().empty() && empty.GetPath().empty() && !empty.IsIncludingSubdomains() && !empty.IsHttpsOnly() && empty.GetExpires() == system_clock::from_time_t(0), "default cookie must have empty text, false flags, and an epoch expiration") };
        mcr::Cookie const named{ "SID", "value" };
        passed &= check(named.GetName() == "SID" && named.GetValue() == "value" && named.GetDomain().empty() && named.GetPath() == "/" && !named.IsIncludingSubdomains() && !named.IsHttpsOnly() && named.GetExpires() == system_clock::from_time_t(0), "name/value construction must use cpr's root path and metadata defaults");

        std::string                    name{ "SID" };
        std::string                    value{ "value" };
        std::string                    domain{ ".Example.COM" };
        std::string                    path{ "/account" };
        system_clock::time_point const expires{ sys_days{ 2015y / October / 21 } + 7h + 28min + 789ms };
        mcr::Cookie const              owned{ name, value, domain, true, path, true, expires };
        name = value = domain = path  = "changed";
        passed                       &= check(owned.GetName() == "SID" && owned.GetValue() == "value" && owned.GetDomain() == ".Example.COM" && owned.GetPath() == "/account" && owned.IsIncludingSubdomains() && owned.IsHttpsOnly() && owned.GetExpires() == expires, "cookie must own all strings and preserve flags and full timestamp precision without normalization");
        mcr::Cookie const empty_path{ "name", "value", "", false, "" };
        passed &= check(empty_path.GetPath().empty(), "an explicitly empty path must remain empty");
        std::string const binary{ "a\0b", 3 };
        mcr::Cookie const binary_cookie{ binary, binary, binary, false, binary };
        passed &= check(binary_cookie.GetName() == binary && binary_cookie.GetValue() == binary && binary_cookie.GetDomain() == binary && binary_cookie.GetPath() == binary, "all cookie strings must retain embedded null bytes");
        return passed;
    }

    auto check_expiration() -> bool {
        struct DateCase {
            system_clock::time_point expires;
            std::string_view         expected;
        };

        DateCase const cases[]{
            {                                  system_clock::from_time_t(0), "Thu, 01 Jan 1970 00:00:00 GMT" },
            {                 sys_days{ 2015y / October / 21 } + 7h + 28min, "Wed, 21 Oct 2015 07:28:00 GMT" },
            { sys_days{ 2024y / February / 29 } + 23h + 59min + 59s + 999ms, "Thu, 29 Feb 2024 23:59:59 GMT" },
            {                               sys_days{ 2040y / January / 1 }, "Sun, 01 Jan 2040 00:00:00 GMT" },
            {                          system_clock::from_time_t(0) - 500ms, "Wed, 31 Dec 1969 23:59:59 GMT" },
        };
        bool passed{ true };
        for (auto const& entry : cases) {
            mcr::Cookie const cookie{ "name", "value", "", false, "/", false, entry.expires };
            passed &= check(cookie.GetExpiresString() == entry.expected && cookie.GetExpires() == entry.expires, "expiration formatting must use English GMT dates, handle date boundaries, and leave stored precision intact");
        }
        return passed;
    }

    auto check_collection() -> bool {
        mcr::Cookies defaults;
        mcr::Cookies raw = false;
        bool         passed{ check(defaults.encode && defaults.empty() && !raw.encode && raw.empty() && defaults.begin() == defaults.end() && defaults.cbegin() == defaults.cend(), "empty collection constructors must preserve the selected encoding mode") };
        mcr::Cookie  input{ "single", "one" };
        mcr::Cookies single  = input;
        input                = mcr::Cookie{ "changed", "two" };
        passed              &= check(single.encode && std::ranges::distance(single) == 1 && single[0].GetName() == "single", "single-cookie conversion must own an independent copy");
        mcr::Cookies single_raw{ input, false };
        passed &= check(!single_raw.encode && single_raw[0].GetValue() == "two", "single-cookie constructor must honor an explicit encoding mode");

        mcr::Cookies cookies{
            {
             { "SID", "first", "example.test", false, "/a" },
             { "SID", "second", "example.test", false, "/b" },
             { "lang", "en-US" },
             },
            false
        };
        passed &= check(!cookies.encode && std::ranges::distance(cookies) == 3 && &cookies["SID"] == &cookies[0] && cookies[1].GetValue() == "second", "collection must retain order and duplicate names while lookup returns the first match");
        std::array<char, 5> const key{ 'x', 'l', 'a', 'n', 'g' };
        passed &= check(&cookies[std::string_view{ key.data() + 1, 4 }] == &cookies[2], "name lookup must respect string-view length");
        for (std::string_view const missing : { "missing", "sid" }) {
            try {
                (void)cookies[missing];
                passed &= check(false, "missing or differently cased names must throw");
            } catch (std::out_of_range const& error) {
                passed &= check(std::string_view{ error.what() } == std::format("Cookie: {} does not exist", missing) && std::ranges::distance(cookies) == 3, "failed lookup must preserve cpr's diagnostic without inserting a cookie");
            }
        }
        cookies["SID"]  = mcr::Cookie{ "SID", "updated" };
        passed         &= check(cookies[0].GetValue() == "updated" && cookies[1].GetValue() == "second", "named lookup must permit replacement of only the first matching cookie");
        mcr::Cookie appended{ "extra", "copy" };
        cookies.emplace_back(appended);
        cookies.push_back(appended);
        appended  = mcr::Cookie{ "changed", "changed" };
        passed   &= check(std::ranges::distance(cookies) == 5 && cookies[3].GetName() == "extra" && cookies[4].GetValue() == "copy", "both append methods must store independent copies in order");
        cookies.pop_back();
        mcr::Cookies const snapshot{ cookies };
        passed     &= check(snapshot.begin() == snapshot.cbegin() && snapshot.end() == snapshot.cend() && std::ranges::distance(snapshot) == 4, "const iteration must expose the remaining ordered cookies");
        cookies[0]  = mcr::Cookie{ "changed", "changed" };
        passed     &= check(snapshot.begin()->GetName() == "SID" && !snapshot.encode, "collection copies must preserve independent cookies and encoding mode");
        mcr::Cookies assigned;
        assigned = snapshot;
        mcr::Cookies moved{ std::move(assigned) };
        passed &= check(!moved.encode && std::ranges::distance(moved) == 4 && moved["SID"].GetValue() == "updated", "copy assignment and moving must preserve contents and encoding mode");
        for (auto& cookie : moved) {
            cookie = mcr::Cookie{ "same", "replacement" };
        }
        passed &= check(std::ranges::all_of(moved, [](auto const& cookie) { return cookie.GetName() == "same"; }), "mutable iterators must support replacing cookies");
        while (!moved.empty()) {
            moved.pop_back();
        }
        passed &= check(moved.begin() == moved.end(), "removing every cookie must produce an empty range");
        std::string const binary_name{ "a\0b", 3 };
        mcr::Cookies      binary{
            { binary_name,      "value" },
            {          "", "empty name" }
        };
        passed &= check(binary[std::string_view{ binary_name }].GetValue() == "value" && binary[std::string_view{}].GetValue() == "empty name", "lookup must support embedded nulls and empty names");
        return passed;
    }

    auto check_encoding() -> bool {
        auto         holder = mcr::curl::CurlHolder::Create().value();
        mcr::Cookies cookies{
            {  "SID", "value" },
            { "lang", "en-US" }
        };
        bool         passed{ check(cookies.GetEncoded(holder).value() == "SID=value; lang=en-US; ", "serialization must preserve insertion order and the trailing separator") };
        mcr::Cookies reserved{
            { "a b", "c+d;%&" }
        };
        passed          &= check(reserved.GetEncoded(holder).value() == "a%20b=c%2Bd%3B%25%26; ", "names and unquoted values must be percent-encoded by default");
        reserved.encode  = false;
        passed          &= check(reserved.GetEncoded(holder).value() == "a b=c+d;%&; ", "changing encode must select raw name/value serialization");
        mcr::Cookies quoted{
            { "quoted name", "\"hello world;=+%\"" },
            {       "empty",                "\"\"" },
            {      "single",                  "\"" },
            {     "leading",              "\"open" },
            {    "trailing",             "close\"" },
        };
        passed &= check(quoted.GetEncoded(holder).value() == "quoted%20name=\"hello world;=+%\"; empty=\"\"; single=\"; leading=%22open; trailing=close%22; ", "quoted values must bypass encoding exactly as in cpr while names and unmatched quotes are encoded");
        mcr::Cookies empty_values{
            {    "", "" },
            { "SID", "" }
        };
        passed &= check(empty_values.GetEncoded(holder).value() == "=; SID=; ", "empty names and values must still serialize as cookie pairs");
        mcr::Cookies metadata{
            { "SID", "first", ".example.test", true, "/a", true, sys_days{ 2040y / January / 1 } },
            { "SID", "second", "other.test", false, "/b" },
        };
        passed &= check(metadata.GetEncoded(holder).value() == "SID=first; SID=second; ", "request serialization must preserve duplicate pairs and omit response metadata");
        mcr::Cookies unicode{
            { "\xE9\x9B\xAA", "\xE4\xB8\x80" }
        };
        passed &= check(unicode.GetEncoded(holder).value() == "%E9%9B%AA=%E4%B8%80; ", "UTF-8 names and values must be encoded byte by byte");
        std::string const binary_name{ "n\0m", 3 };
        std::string const binary_value{ "v\0x", 3 };
        mcr::Cookies      binary{
            { binary_name, binary_value }
        };
        passed        &= check(binary.GetEncoded(holder).value() == "n%00m=v%00x; ", "binary names and values must be encoded without truncation");
        binary.encode  = false;
        passed        &= check(binary.GetEncoded(holder).value() == binary_name + '=' + binary_value + "; ", "raw serialization must retain embedded nulls");
        std::string const quoted_binary{ "\"a\0b\"", 5 };
        mcr::Cookies      binary_quoted{
            { "binary", quoted_binary }
        };
        passed &= check(binary_quoted.GetEncoded(holder).value() == "binary=" + quoted_binary + "; ", "quoted binary values must remain verbatim");

        mcr::curl::CurlHolder owner{ std::move(holder) };
        passed &= check(mcr::Cookies{}.GetEncoded(holder).value().empty() && reserved.GetEncoded(holder).value() == "a b=c+d;%&; ", "empty and raw collections must not require an active curl handle");
        {
            auto const failure  = cookies.GetEncoded(holder);
            passed             &= check(!failure && failure.error().code == mcr::ErrorCode::FAILED_INIT, "failure must return the expected error code");
        }
        return passed;
    }

} // namespace

int main() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::println("test_cookies: curl global initialization failed");
        return 1;
    }
    bool passed{ true };
    try {
        passed &= check_metadata();
        passed &= check_expiration();
        passed &= check_collection();
        passed &= check_encoding();
    } catch (std::exception const& error) {
        std::println("test_cookies: unexpected exception: {}", error.what());
        passed = false;
    }
    curl_global_cleanup();
    if (!passed) {
        return 1;
    }
    std::println("test_cookies: ok");
    return 0;
}
