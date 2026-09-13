/**
 * @file test_curl_container.cpp
 * @brief Verify Parameters, Payload, and their common query/form formatting and ownership.
 */
#include <curl/curl.h>

import std;
import mcr;

using Parameters = mcr::Parameters;
using Pairs      = mcr::Payload;

static_assert(std::derived_from<Parameters, mcr::curl::CurlContainer<mcr::Parameter>>);
static_assert(!std::is_same_v<Parameters, mcr::curl::CurlContainer<mcr::Parameter>>);
static_assert(std::is_convertible_v<std::initializer_list<mcr::Parameter>, Parameters>);
static_assert(!std::is_convertible_v<mcr::Parameter, Parameters>);
static_assert(std::is_nothrow_move_constructible_v<Parameters>);
static_assert(std::is_nothrow_move_assignable_v<Parameters>);
static_assert(std::derived_from<Pairs, mcr::curl::CurlContainer<mcr::Pair>>);
static_assert(!std::is_same_v<Pairs, mcr::curl::CurlContainer<mcr::Pair>>);
static_assert(std::is_convertible_v<std::initializer_list<mcr::Pair>, Pairs>);
static_assert(!std::is_convertible_v<mcr::Pair, Pairs>);
static_assert(!std::is_default_constructible_v<Pairs>);
static_assert(std::is_nothrow_move_constructible_v<Pairs>);
static_assert(std::is_nothrow_move_assignable_v<Pairs>);
static_assert(!std::is_same_v<mcr::Parameter, mcr::Pair>);
static_assert(!std::is_default_constructible_v<mcr::Parameter>);
static_assert(!std::is_default_constructible_v<mcr::Pair>);
static_assert(std::is_same_v<decltype(mcr::Parameter::key), std::string>);
static_assert(std::is_same_v<decltype(mcr::Pair::value), std::string>);
static_assert(std::is_same_v<decltype(std::declval<Parameters const&>().GetContent()), std::string>);
static_assert(std::is_same_v<decltype(std::declval<Pairs const&>().GetContent(std::declval<mcr::curl::CurlHolder const&>())), std::string>);

namespace {

    template <typename T>
    concept ContainerElement = requires { typename mcr::curl::CurlContainer<T>; };

    static_assert(ContainerElement<mcr::Parameter> && ContainerElement<mcr::Pair>);
    static_assert(!ContainerElement<int> && !ContainerElement<std::pair<std::string, std::string>>);

    struct DerivedParameters : Parameters {
        using Parameters::Parameters;

        auto First() -> mcr::Parameter& { return m_container_list.front(); }
    };

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_curl_container: {}", message);
        }
        return condition;
    }

    auto check_without_curl() -> bool {
        Parameters       empty_parameters;
        Parameters const empty_list = std::initializer_list<mcr::Parameter>{};
        Pairs            empty_pairs{};
        bool             passed{ check(empty_parameters.encode && empty_list.encode && empty_pairs.encode && empty_parameters.GetContent().empty() && empty_list.GetContent().empty() && empty_pairs.GetContent().empty(), "empty parameter and payload collections must enable encoding and produce empty raw content") };
        Parameters       parameters{
            { "key one", "hello world" },
            {    "flag",            "" },
            { "key one",         "x+y" }
        };
        Pairs pairs{
            { "key one", "hello world" },
            {    "flag",            "" },
            { "key one",         "x+y" }
        };
        passed            &= check(parameters.GetContent() == "key one=hello world&flag&key one=x+y", "holderless parameters must ignore the encoding flag and omit equals signs for empty values");
        passed            &= check(pairs.GetContent() == "key one=hello world&flag=&key one=x+y", "holderless pairs must ignore the encoding flag and retain every equals sign");
        parameters.encode  = false;
        pairs.encode       = false;
        passed            &= check(parameters.GetContent() == "key one=hello world&flag&key one=x+y" && pairs.GetContent() == "key one=hello world&flag=&key one=x+y", "holderless content must be independent of the encoding flag");
        return passed;
    }

    auto check_payload_ranges() -> bool {
        std::vector<mcr::Pair> source{
            { "outside", "prefix" },
            {     "key",  "first" },
            {     "key", "second" },
            {   "empty",       "" },
            { "outside", "suffix" }
        };
        Pairs const subrange{ source.cbegin() + 1, source.cend() - 1 };
        source[1].value = "changed";
        bool passed{ check(subrange.encode && subrange.GetContent() == "key=first&key=second&empty=", "iterator construction must copy the exact subrange with duplicates and empty values in order") };

        std::list<mcr::Pair> const linked{
            { "first", "one" },
            {  "last", "two" }
        };
        Pairs const from_list{ linked.begin(), linked.end() };
        Pairs const reversed{ linked.rbegin(), linked.rend() };
        passed &= check(from_list.GetContent() == "first=one&last=two" && reversed.GetContent() == "last=two&first=one", "payload ranges must support noncontiguous iterators and follow their traversal order");

        std::istringstream words{ "first second third" };
        auto               input{
            std::ranges::subrange{ std::istream_iterator<std::string>{ words }, std::istream_iterator<std::string>{} }
            |
            std::views::transform([](std::string const& word) { return mcr::Pair{ "word", word }; }
            )
        };
        static_assert(std::input_iterator<decltype(input.begin())> && !std::forward_iterator<decltype(input.begin())>);
        Pairs const from_input{ input.begin(), input.end() };
        passed &= check(from_input.GetContent() == "word=first&word=second&word=third", "single-pass input iterators yielding temporary pairs must be consumed exactly once");

        std::vector<mcr::Pair> move_source{
            { std::string(64, 'k'), std::string(64, 'v') }
        };
        Pairs const from_move_iterators{ std::make_move_iterator(move_source.begin()), std::make_move_iterator(move_source.end()) };
        passed                      &= check(move_source.front().key == std::string(64, 'k') && move_source.front().value == std::string(64, 'v') && from_move_iterators.GetContent() == std::string(64, 'k') + '=' + std::string(64, 'v'), "range construction must retain cpr's copy semantics even for move iterators");

        Pairs const      empty_list  = std::initializer_list<mcr::Pair>{};
        Pairs const      empty_range{ source.end(), source.end() };
        mcr::Pair const* null_pair{ nullptr };
        Pairs const      empty_pointers{ null_pair, null_pair };
        passed &= check(empty_list.encode && empty_range.encode && empty_pointers.encode && empty_list.GetContent().empty() && empty_range.GetContent().empty() && empty_pointers.GetContent().empty(), "empty lists and equal iterator ranges must produce empty payloads without dereferencing iterators");
        return passed;
    }

    template <typename Container, typename Element>
    auto check_ownership(mcr::curl::CurlHolder const& holder) -> bool {
        std::string key{ "first" };
        std::string value{ "one" };
        Element     element{ key, value };
        key   = "changed";
        value = "changed";
        bool      passed{ check(element.key == "first" && element.value == "one", "element construction must own independent key and value strings") };
        Container original{ element };
        element.key   = "second";
        element.value = "two";
        original.Add(element);
        original.Add({
            { "first", "three" },
            {  "last",  "four" }
        });
        original.Add({});
        element.value = "changed again";
        std::string const expected{ "first=one&second=two&first=three&last=four" };
        passed          &= check(original.GetContent() == expected && original.GetContent(holder) == expected, "construction and Add must copy in order, preserving duplicates and ignoring empty lists");
        original.encode  = false;
        Container copied{ original };
        original.Add(Element{ "extra", "five" });
        original.encode = true;
        Container moved{ std::move(copied) };
        Container assigned{};
        assigned = moved;
        Container move_assigned{};
        move_assigned  = std::move(assigned);
        passed        &= check(!move_assigned.encode && move_assigned.GetContent() == expected && original.GetContent() == expected + "&extra=five", "copies and moves must preserve ordered content and the encoding flag without sharing state");
        return passed;
    }

    auto check_encoding(mcr::curl::CurlHolder const& holder) -> bool {
        Parameters parameters{
            {       "a b&=", "x+y/%" },
            { "empty value",      "" },
            {            "",   "v v" }
        };
        Pairs pairs{
            {       "a b&=", "x+y/%" },
            { "empty value",      "" },
            {            "",   "v v" }
        };
        std::string const raw_parameters{ "a b&==x+y/%&empty value&=v v" };
        std::string const raw_pairs{ "a b&==x+y/%&empty value=&=v v" };
        std::string const encoded_parameters{ "a%20b%26%3D=x%2By%2F%25&empty%20value&=v%20v" };
        std::string const encoded_pairs{ "a b&==x%2By%2F%25&empty value=&=v%20v" };
        bool              passed{ check(parameters.GetContent(holder) == encoded_parameters && pairs.GetContent(holder) == encoded_pairs, "encoding must escape parameter keys and values, but only pair values") };
        passed            &= check(parameters.GetContent() == raw_parameters && pairs.GetContent() == raw_pairs, "encoded serialization must not mutate stored bytes");
        parameters.encode  = false;
        pairs.encode       = false;
        passed            &= check(parameters.GetContent(holder) == raw_parameters && pairs.GetContent(holder) == raw_pairs, "disabling encoding must emit all bytes verbatim even with a holder");
        parameters.encode  = true;
        pairs.encode       = true;
        passed            &= check(parameters.GetContent(holder) == encoded_parameters && pairs.GetContent(holder) == encoded_pairs, "reenabling encoding must escape the original data without double encoding");

        std::string const utf8{ "\xE4\xB8\xAD" };
        passed &= check(Parameters{
                            { utf8, utf8 }
        }
                                    .GetContent(holder) == "%E4%B8%AD=%E4%B8%AD" &&
                            Pairs{ { utf8, utf8 } }.GetContent(holder) == utf8 + "=%E4%B8%AD",
                        "UTF-8 must be encoded byte by byte with pair keys left raw");
        std::string const binary_key{ "a\0b", 3 };
        std::string const binary_value{ "x\0 y", 4 };
        Parameters        binary_parameters{
            { binary_key, binary_value }
        };
        Pairs binary_pairs{
            { binary_key, binary_value }
        };
        passed &= check(binary_parameters.GetContent() == binary_key + "=" + binary_value && binary_pairs.GetContent() == binary_key + "=" + binary_value, "raw serialization must preserve embedded nulls in both fields");
        passed &= check(binary_parameters.GetContent(holder) == "a%00b=x%00%20y" && binary_pairs.GetContent(holder) == binary_key + "=x%00%20y", "encoded serialization must retain the full binary input lengths");
        return passed;
    }

    auto check_empty_entries(mcr::curl::CurlHolder const& holder) -> bool {
        Parameters parameters{
            {      "",  "" },
            {      "",  "" },
            { "first",  "" },
            {      "",  "" },
            {  "last", "x" },
            {      "",  "" }
        };
        Pairs pairs{
            {      "",  "" },
            {      "",  "" },
            { "first",  "" },
            {      "",  "" },
            {  "last", "x" },
            {      "",  "" }
        };
        bool passed{ check(parameters.GetContent() == "first&&last=x&" && parameters.GetContent(holder) == "first&&last=x&", "parameter separators must follow cpr's emitted-content rule, including disappearing leading empties") };
        passed &= check(pairs.GetContent() == "=&=&first=&=&last=x&=" && pairs.GetContent(holder) == "=&=&first=&=&last=x&=", "empty pairs must always contribute an equals sign and retain their positions");
        passed &= check(Parameters{
                            { "", "" },
                            { "", "" }
        }
                            .GetContent(holder)
                            .empty(),
                        "entirely empty parameters must produce no text");
        DerivedParameters derived{
            { "key", "value" }
        };
        derived.First().value  = "updated value";
        passed                &= check(derived.GetContent(holder) == "key=updated%20value", "derived containers must be able to update the protected ordered storage");
        return passed;
    }

    template <typename Container>
    auto check_holder_lifetime() -> bool {
        mcr::curl::CurlHolder source;
        mcr::curl::CurlHolder owner{ std::move(source) };
        Container       empty{};
        bool            passed{ check(empty.GetContent(source).empty(), "empty containers must not consult the supplied holder") };
        Container       values{
            { "key", "a b" }
        };
        values.encode  = false;
        passed        &= check(values.GetContent(source) == "key=a b", "disabled encoding must not consult a moved-from holder");
        values.encode  = true;
        try {
            (void)values.GetContent(source);
            passed &= check(false, "encoding with a moved-from holder must propagate its logic_error");
        } catch (std::logic_error const&) {
        }
        passed &= check(values.GetContent(owner) == "key=a%20b" && values.GetContent() == "key=a b", "failed encoding must leave the container usable and unchanged");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_without_curl() };
    passed &= check_payload_ranges();
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::println("test_curl_container: curl global initialization failed");
        return 1;
    }
    try {
        mcr::curl::CurlHolder holder;
        passed &= check_ownership<Parameters, mcr::Parameter>(holder);
        passed &= check_ownership<Pairs, mcr::Pair>(holder);
        passed &= check_encoding(holder);
        passed &= check_empty_entries(holder);
        passed &= check_holder_lifetime<Parameters>();
        passed &= check_holder_lifetime<Pairs>();
    } catch (std::exception const& error) {
        std::println("test_curl_container: unexpected exception: {}", error.what());
        passed = false;
    }
    curl_global_cleanup();
    if (!passed) {
        return 1;
    }
    std::println("test_curl_container: ok");
    return 0;
}
