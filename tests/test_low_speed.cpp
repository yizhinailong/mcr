/**
 * @file test_low_speed.cpp
 * @brief Verify chrono-only low-speed construction, duration units, boundaries, and public updates.
 */
import std;
import mcr.transfer_options;

static_assert(std::is_same_v<decltype(mcr::options::LowSpeed::limit), std::int32_t>);
static_assert(std::is_same_v<decltype(mcr::options::LowSpeed::time), std::chrono::seconds>);
static_assert(!std::is_default_constructible_v<mcr::options::LowSpeed>);
static_assert(!std::is_constructible_v<mcr::options::LowSpeed, std::int32_t>);
static_assert(!std::is_constructible_v<mcr::options::LowSpeed, std::int32_t, std::int32_t>);
static_assert(!std::is_constructible_v<mcr::options::LowSpeed, std::int32_t, std::chrono::milliseconds>);
static_assert(!std::is_constructible_v<mcr::options::LowSpeed, std::int32_t, std::chrono::duration<double>>);
static_assert(std::is_constructible_v<mcr::options::LowSpeed, std::int32_t, std::chrono::seconds>);
static_assert(std::is_constructible_v<mcr::options::LowSpeed, std::int32_t, std::chrono::minutes>);

int main() {
    using namespace std::chrono_literals;
    using Seconds = std::chrono::seconds;

    std::pair<std::int32_t, Seconds> const cases[]{
        {                                          0,               0s },
        {                                          1,               1s },
        {                                       1000,               1s },
        {                                       1024,              30s },
        {                                          0,              10s },
        {                                       1000,               0s },
        {                                         -1,              -2s },
        { (std::numeric_limits<std::int32_t>::min)(), (Seconds::max)() },
        { (std::numeric_limits<std::int32_t>::max)(), (Seconds::min)() },
    };
    for (auto const& [limit, time] : cases) {
        mcr::options::LowSpeed const option = { limit, time };
        if (option.limit != limit || option.time != time) {
            std::println("test_low_speed: construction must preserve limit {} and duration {} seconds without narrowing or normalization", limit, time.count());
            return 1;
        }
    }

    mcr::options::LowSpeed const minutes{ 1000, 2min };
    mcr::options::LowSpeed const hours{ 2000, 1h };
    mcr::options::LowSpeed const truncated{ 3000, std::chrono::duration_cast<Seconds>(1500ms) };
    if (minutes.limit != 1000 || minutes.time != 120s || hours.limit != 2000 || hours.time != 3600s || truncated.time != 1s) {
        std::println("test_low_speed: chrono conversions must use seconds and require explicit subsecond truncation");
        return 1;
    }

    mcr::options::LowSpeed original{ 1000, 1s };
    mcr::options::LowSpeed copied{ original };
    original.limit = 0;
    original.time  = -1s;
    mcr::options::LowSpeed moved{ std::move(copied) };
    mcr::options::LowSpeed assigned{ 0, 0s };
    assigned = moved;
    mcr::options::LowSpeed move_assigned{ 0, 0s };
    move_assigned  = std::move(moved);
    assigned.limit = 2048;
    assigned.time  = 3min;
    if (original.limit != 0 || original.time != -1s || move_assigned.limit != 1000 || move_assigned.time != 1s || assigned.limit != 2048 || assigned.time != 180s) {
        std::println("test_low_speed: copying, moving, and public field updates must retain independent threshold/duration pairs");
        return 1;
    }

    std::println("test_low_speed: ok");
    return 0;
}
