/**
 * @file test_proxies.cpp
 * @brief Verify proxy ownership, exact protocol lookup, and insertion of empty proxy addresses.
 */
import std;
import mcr.proxy;

using ProxyMap  = std::map<std::string, std::string>;
using ProxyList = std::initializer_list<ProxyMap::value_type>;

static_assert(std::is_default_constructible_v<mcr::options::Proxies>);
static_assert(std::is_constructible_v<mcr::options::Proxies, ProxyMap const&>);
static_assert(!std::is_convertible_v<ProxyMap, mcr::options::Proxies>);
static_assert(std::is_convertible_v<ProxyList, mcr::options::Proxies>);
static_assert(std::is_same_v<decltype(std::declval<mcr::options::Proxies&>()["http"]), std::string const&>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_proxies: {}", message);
        }
        return condition;
    }

    auto check_construction() -> bool {
        mcr::options::Proxies const defaults;
        mcr::options::Proxies const empty_list(ProxyList{});
        mcr::options::Proxies const empty_map(ProxyMap{});
        bool               passed{ check(!defaults.Has("http") && !defaults.Has("") && !empty_list.Has("https") && !empty_map.Has("http"), "default and empty inputs must contain no mappings") };

        std::string  protocol{ "http" };
        std::string  address{ "http://proxy.test:8080" };
        mcr::options::Proxies from_list = {
            { protocol,                    address },
            {  "https", "socks5://proxy.test:1080" }
        };
        protocol  = "changed";
        address   = "changed";
        passed   &= check(from_list.Has("http") && from_list["http"] == "http://proxy.test:8080" && from_list["https"] == "socks5://proxy.test:1080" && !from_list.Has("changed"), "initializer-list construction must copy both protocol names and proxy addresses");

        ProxyMap source{
            {     "http", "proxy.test:3128" },
            { "no_proxy",                "" }
        };
        mcr::options::Proxies from_map{ source };
        source["http"] = "changed";
        source.erase("no_proxy");
        source["https"]  = "added";
        passed          &= check(from_map["http"] == "proxy.test:3128" && from_map.Has("no_proxy") && from_map["no_proxy"].empty() && !from_map.Has("https"), "explicit map construction must retain an independent copy, including empty proxy values");
        (void)from_map["ftp"];
        passed &= check(!source.contains("ftp"), "subscript insertion must not modify the source map");

        mcr::options::Proxies duplicate{
            { "http",  "first" },
            { "http", "second" }
        };
        passed &= check(duplicate.Has("http") && (duplicate["http"] == "first" || duplicate["http"] == "second"), "duplicate keys must retain one supplied mapping according to std::map initialization rules");
        return passed;
    }

    auto check_lookup() -> bool {
        mcr::options::Proxies proxies{
            {              "http",                   "lower" },
            {              "HTTP",                   "upper" },
            {             "https",                        "" },
            {          "no_proxy",                        "" },
            {          "NO_PROXY", "localhost,.example.test" },
            { " custom protocol ",   " unvalidated address " },
        };
        bool passed{ check(std::as_const(proxies).Has("http") && proxies["http"] == "lower" && proxies["HTTP"] == "upper" && !proxies.Has("Http"), "protocol matching must be exact and case-sensitive") };
        passed &= check(proxies.Has("https") && proxies["https"].empty() && proxies.Has("no_proxy") && proxies["no_proxy"].empty() && proxies["NO_PROXY"] == "localhost,.example.test", "empty addresses and both no_proxy spellings must remain distinct stored entries");
        passed &= check(proxies.Has(" custom protocol ") && !proxies.Has("custom protocol") && proxies[" custom protocol "] == " unvalidated address ", "proxy storage must not trim or validate names or addresses");
        auto const& existing{ proxies["http"] };
        auto const* existing_address{ &existing };
        passed &= check(!proxies.Has("missing") && !proxies.Has("missing"), "Has must not insert absent keys");
        auto const& inserted{ proxies["missing"] };
        passed &= check(inserted.empty() && proxies.Has("missing") && &proxies["missing"] == &inserted && &proxies["http"] == existing_address && existing == "lower", "subscript must insert a stable empty value and preserve existing references");

        std::array<char, 6> const bounded{ 'x', 'h', 't', 't', 'p', 'x' };
        std::string_view const    view{ bounded.data() + 1, 4 };
        passed &= check(proxies.Has(view) && &proxies[view] == existing_address && !proxies.Has(std::string_view{ bounded.data(), bounded.size() }), "lookup must respect nonterminated string-view boundaries");
        std::string new_key{ "xnewx" };
        (void)proxies[std::string_view{ new_key }.substr(1, 3)];
        new_key.assign("changed");
        passed &= check(proxies.Has("new") && !proxies.Has("xnewx") && !proxies.Has("changed"), "inserted keys must copy only the selected view and own their storage");
        passed &= check(!proxies.Has(std::string_view{}) && proxies[std::string_view{}].empty() && proxies.Has(""), "an empty key must support lookup and insertion");

        std::string const binary_key{ "h\0ttp", 5 };
        std::string const binary_address{ "proxy\0address", 13 };
        mcr::options::Proxies binary{
            { binary_key, binary_address }
        };
        passed &= check(binary.Has(binary_key) && !binary.Has("h") && binary[binary_key] == binary_address, "keys and addresses must retain embedded null bytes");
        std::string const missing_binary{ "new\0key", 7 };
        passed &= check(binary[missing_binary].empty() && binary.Has(missing_binary) && !binary.Has("new"), "inserting a binary key must not truncate it");
        return passed;
    }

    auto check_value_semantics() -> bool {
        mcr::options::Proxies original{
            { "http", "proxy.test:8080" }
        };
        mcr::options::Proxies copied{ original };
        original = mcr::options::Proxies{
            { "https", "proxy.test:8443" }
        };
        (void)copied["no_proxy"];
        mcr::options::Proxies moved{ std::move(copied) };
        mcr::options::Proxies assigned;
        assigned = moved;
        mcr::options::Proxies move_assigned;
        move_assigned = std::move(moved);
        (void)assigned["ftp"];
        return check(original.Has("https") && !original.Has("http") && !original.Has("no_proxy") && assigned["http"] == "proxy.test:8080" && assigned.Has("ftp") && move_assigned["http"] == "proxy.test:8080" && move_assigned.Has("no_proxy") && !move_assigned.Has("ftp"), "copying, moving, assignment, and insertion must preserve independent mappings");
    }

} // namespace

int main() {
    bool passed{ check_construction() };
    passed &= check_lookup();
    passed &= check_value_semantics();
    if (!passed) {
        return 1;
    }
    std::println("test_proxies: ok");
    return 0;
}
