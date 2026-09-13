/**
 * @file test_resolve.cpp
 * @brief Verify resolve defaults, custom ports, text ownership, and public field updates.
 */
import std;
import mcr.transfer_options;

static_assert(!std::is_default_constructible_v<mcr::options::Resolve>);
static_assert(std::is_same_v<decltype(mcr::options::Resolve::host), std::string>);
static_assert(std::is_same_v<decltype(mcr::options::Resolve::addr), std::string>);
static_assert(std::is_same_v<decltype(mcr::options::Resolve::ports), std::set<std::uint16_t>>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_resolve: {}", message);
        }
        return condition;
    }

    auto check_ports() -> bool {
        std::set<std::uint16_t> const defaults{ 80U, 443U };
        mcr::options::Resolve const   omitted{ "www.example.com", "127.0.0.1" };
        mcr::options::Resolve const   empty{ "www.example.com", "127.0.0.1", {} };
        bool                          passed{ check(omitted.ports == defaults && empty.ports == defaults, "omitted and empty port sets must both select 80 and 443") };
        passed &= check(omitted.host == "www.example.com" && omitted.addr == "127.0.0.1", "construction must preserve hostname and address");

        mcr::options::Resolve const custom{
            "www.example.com",
            "127.0.0.1",
            { 8443U, 8080U, 8443U }
        };
        passed &= check(custom.ports == std::set<std::uint16_t>{ 8080U, 8443U }, "custom ports must be sorted and unique without adding defaults");
        mcr::options::Resolve const boundaries{
            "www.example.com",
            "::1",
            { 0U, 65535U }
        };
        passed &= check(boundaries.ports == std::set<std::uint16_t>{ 0U, 65535U }, "both uint16 port boundaries must be preserved without validation");
        mcr::options::Resolve const single{ "www.example.com", "127.0.0.1", { 443U } };
        passed &= check(single.ports == std::set<std::uint16_t>{ 443U }, "a single supplied port must not acquire another default port");
        return passed;
    }

    auto check_ownership_and_updates() -> bool {
        std::string             host{ "Example.COM" };
        std::string             addr{ "[2001:db8::1]" };
        std::set<std::uint16_t> ports{ 8080U };
        mcr::options::Resolve   mapping{ host, addr, ports };
        host = "changed.example";
        addr = "127.0.0.2";
        ports.clear();
        bool passed{ check(mapping.host == "Example.COM" && mapping.addr == "[2001:db8::1]" && mapping.ports == std::set<std::uint16_t>{ 8080U }, "mapping must own its inputs without normalizing hostname or IPv6 text") };

        mcr::options::Resolve copied{ mapping };
        mapping.host = "updated.example";
        mapping.addr = "127.0.0.3";
        mapping.ports.clear();
        passed &= check(copied.host == "Example.COM" && copied.addr == "[2001:db8::1]" && copied.ports == std::set<std::uint16_t>{ 8080U }, "copying must preserve independent field values");
        copied  = mapping;
        passed &= check(copied.host == "updated.example" && copied.addr == "127.0.0.3" && copied.ports.empty(), "assignment must copy public updates without restoring default ports");
        mcr::options::Resolve moved{ std::move(copied) };
        passed &= check(moved.host == "updated.example" && moved.addr == "127.0.0.3" && moved.ports.empty(), "moving a mapping must preserve an explicitly cleared port set");

        std::string const  binary_host{ "a\0b", 3 };
        std::string const  binary_addr{ "x\0y", 3 };
        mcr::options::Resolve const binary{ binary_host, binary_addr };
        passed &= check(binary.host == binary_host && binary.addr == binary_addr, "string inputs must retain embedded null bytes");
        mcr::options::Resolve const empty_text{ "", "" };
        passed &= check(empty_text.host.empty() && empty_text.addr.empty(), "empty hostname and address must remain accepted, as in cpr");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_ports() };
    passed &= check_ownership_and_updates();
    if (!passed) {
        return 1;
    }
    std::println("test_resolve: ok");
    return 0;
}
