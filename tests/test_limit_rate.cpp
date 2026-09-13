/**
 * @file test_limit_rate.cpp
 * @brief Verify download/upload rate ordering, signed boundaries, and independent option values.
 */
import std;
import mcr.transfer_options;

static_assert(!std::is_default_constructible_v<mcr::options::LimitRate>);
static_assert(!std::is_constructible_v<mcr::options::LimitRate, int>);
static_assert(std::is_same_v<decltype(mcr::options::LimitRate::downrate), std::int64_t>);
static_assert(std::is_same_v<decltype(mcr::options::LimitRate::uprate), std::int64_t>);

int main() {
    using Rate = decltype(mcr::options::LimitRate::downrate);
    static_assert(std::signed_integral<Rate>);
    static_assert(std::is_same_v<Rate, decltype(mcr::options::LimitRate::uprate)>);

    std::pair<Rate, Rate> const cases[]{
        {                                  0,                                  0 },
        {                               1024,                               1024 },
        {                               1024,                               2048 },
        {                                  0,                               4096 },
        {                               8192,                                  0 },
        {                                 -1,                                 -2 },
        {                    Rate{ 1 } << 32,                    Rate{ 1 } << 33 },
        { (std::numeric_limits<Rate>::min)(), (std::numeric_limits<Rate>::max)() },
        { (std::numeric_limits<Rate>::max)(), (std::numeric_limits<Rate>::min)() },
    };
    for (auto const& [downrate, uprate] : cases) {
        mcr::options::LimitRate const option = { downrate, uprate };
        if (option.downrate != downrate || option.uprate != uprate) {
            std::println("test_limit_rate: construction must preserve download {} and upload {} without swapping or normalization", downrate, uprate);
            return 1;
        }
    }

    mcr::options::LimitRate original{ 1024, 2048 };
    mcr::options::LimitRate copied{ original };
    original.downrate = 0;
    original.uprate   = -1;
    mcr::options::LimitRate moved{ std::move(copied) };
    mcr::options::LimitRate assigned{ 0, 0 };
    assigned = moved;
    mcr::options::LimitRate move_assigned{ 0, 0 };
    move_assigned     = std::move(moved);
    assigned.downrate = 4096;
    assigned.uprate   = 8192;
    if (original.downrate != 0 || original.uprate != -1 || move_assigned.downrate != 1024 || move_assigned.uprate != 2048 || assigned.downrate != 4096 || assigned.uprate != 8192) {
        std::println("test_limit_rate: public updates, copying, and moving must preserve independent download/upload values");
        return 1;
    }

    std::println("test_limit_rate: ok");
    return 0;
}
