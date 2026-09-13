/**
 * @file test_local_port.cpp
 * @brief Verify local port options, implicit conversions, and uint16_t boundaries through their module.
 */
import std;
import mcr.transfer_options;

static_assert(!std::is_same_v<mcr::options::LocalPort, mcr::options::LocalPortRange>);

namespace {

    template <typename Option>
    auto check_option(std::string_view name) -> bool {
        static_assert(!std::is_default_constructible_v<Option>);
        static_assert(std::is_convertible_v<std::uint16_t, Option>);
        static_assert(std::is_convertible_v<Option const&, std::uint16_t>);

        for (std::uint16_t const value : { std::uint16_t{ 0 }, std::uint16_t{ 1 }, std::uint16_t{ 1024 }, std::uint16_t{ 32768 }, (std::numeric_limits<std::uint16_t>::max)() }) {
            Option const        option     = value;
            std::uint16_t const converted  = option;
            long const          curl_value = option;
            if (converted != value || curl_value != value) {
                std::println("test_local_port: {} must preserve {} through implicit construction and conversion", name, value);
                return false;
            }

            Option copied{ option };
            Option moved{ std::move(copied) };
            Option assigned{ 0 };
            assigned = moved;
            Option move_assigned{ 0 };
            move_assigned = std::move(moved);
            if (assigned != value || move_assigned != value) {
                std::println("test_local_port: {} copy and move operations must preserve {}", name, value);
                return false;
            }
            assigned = std::uint16_t{ 42 };
            if (assigned != 42 || option != value || move_assigned != value) {
                std::println("test_local_port: {} numeric assignment must use implicit construction and leave other values unchanged", name);
                return false;
            }
        }
        return true;
    }

} // namespace

int main() {
    bool passed{ check_option<mcr::options::LocalPort>("LocalPort") };
    passed &= check_option<mcr::options::LocalPortRange>("LocalPortRange");
    if (!passed) {
        return 1;
    }
    std::println("test_local_port: ok");
    return 0;
}
