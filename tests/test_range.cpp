/**
 * @file test_range.cpp
 * @brief Verify optional range endpoints, full-width formatting, and ordered multi-range ownership.
 */
import std;
import mcr.http;

static_assert(std::is_same_v<decltype(mcr::options::Range::resume_from), std::int64_t>);
static_assert(std::is_same_v<decltype(mcr::options::Range::finish_at), std::int64_t>);
static_assert(std::is_default_constructible_v<mcr::options::Range>);
static_assert(!std::is_convertible_v<std::int64_t, mcr::options::Range>);
static_assert(!std::is_convertible_v<std::optional<std::int64_t>, mcr::options::Range>);
static_assert(std::is_convertible_v<std::initializer_list<mcr::options::Range>, mcr::options::MultiRange>);
static_assert(std::is_same_v<decltype(std::declval<mcr::options::Range const&>().Str()), std::string>);
static_assert(std::is_same_v<decltype(std::declval<mcr::options::MultiRange const&>().Str()), std::string>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_range: {}", message);
        }
        return condition;
    }

    auto check_single_range() -> bool {
        mcr::options::Range const defaults;
        bool                      passed{ check(defaults.resume_from == 0 && defaults.finish_at == -1 && defaults.Str() == "0-" && mcr::options::Range{ 7 }.Str() == "7-", "default and one-endpoint construction must retain cpr's zero-start and open-finish defaults") };

        struct RangeCase {
            std::optional<std::int64_t> from;
            std::optional<std::int64_t> to;
            std::string_view            expected;
        };

        constexpr auto  MINIMUM{ (std::numeric_limits<std::int64_t>::min)() };
        constexpr auto  MAXIMUM{ (std::numeric_limits<std::int64_t>::max)() };
        RangeCase const cases[]{
            {            std::nullopt,            std::nullopt,                                      "0-" },
            {                       1,            std::nullopt,                                      "1-" },
            {            std::nullopt,                       5,                                     "0-5" },
            {                       2,                       3,                                     "2-3" },
            {                       0,                       0,                                     "0-0" },
            {                      -1,                     500,                                    "-500" },
            {                      -2,                       0,                                      "-0" },
            {                      10,                      -2,                                     "10-" },
            {                      -1,                      -1,                                       "-" },
            {                 MINIMUM,                 MINIMUM,                                       "-" },
            {                      10,                       2,                                    "10-2" },
            { std::int64_t{ 1 } << 32, std::int64_t{ 1 } << 33,                   "4294967296-8589934592" },
            {                 MAXIMUM,                 MAXIMUM, "9223372036854775807-9223372036854775807" },
            {                 MAXIMUM,                 MINIMUM,                    "9223372036854775807-" },
            {                 MINIMUM,                 MAXIMUM,                    "-9223372036854775807" },
        };
        for (auto const& entry : cases) {
            mcr::options::Range const range{ entry.from, entry.to };
            passed &= check(range.Str() == entry.expected, std::format("expected range '{}'", entry.expected));
        }
        std::optional<std::int64_t> start{ 123 };
        std::optional<std::int64_t> finish{ 456 };
        mcr::options::Range         range{ start, finish };
        start = 0;
        finish.reset();
        passed            &= check(range.Str() == "123-456", "construction must copy optional endpoint values");
        range.resume_from  = MINIMUM;
        range.finish_at    = -2;
        passed            &= check(range.resume_from == MINIMUM && range.finish_at == -2 && range.Str() == "-", "negative endpoints must remain stored verbatim while formatting omits their digits");
        mcr::options::Range copied{ range };
        range.resume_from = 5;
        range.finish_at   = 8;
        mcr::options::Range moved{ std::move(copied) };
        mcr::options::Range assigned;
        assigned = moved;
        mcr::options::Range move_assigned;
        move_assigned  = std::move(moved);
        passed        &= check(range.Str() == "5-8" && assigned.Str() == "-" && move_assigned.Str() == "-", "copying, moving, and assignment must retain independent endpoints");
        auto text{ range.Str() };
        text.clear();
        passed &= check(range.Str() == "5-8", "Str must return an independent string and observe public endpoint changes");
        return passed;
    }

    auto check_multi_range() -> bool {
        mcr::options::MultiRange const empty{};
        mcr::options::MultiRange const explicit_empty(std::initializer_list<mcr::options::Range>{});
        mcr::options::MultiRange const single{ mcr::options::Range{} };
        bool                  passed{ check(empty.Str().empty() && explicit_empty.Str().empty() && single.Str() == "0-", "empty lists and single ranges must format without separators") };
        mcr::options::MultiRange const two{
            mcr::options::Range{ std::nullopt, 3 },
            mcr::options::Range{            5, 6 }
        };
        mcr::options::MultiRange const three{
            mcr::options::Range{ std::nullopt, 2 },
            mcr::options::Range{            4, 5 },
            mcr::options::Range{            7, 8 }
        };
        passed &= check(two.Str() == "0-3, 5-6" && three.Str() == "0-2, 4-5, 7-8", "cpr's multipart download examples must use exactly comma-space separators");
        mcr::options::MultiRange const mixed{
            mcr::options::Range{ 10, 2 },
            mcr::options::Range{ 1, 8 },
            mcr::options::Range{ 1, 8 },
            mcr::options::Range{ -1, 500 },
            mcr::options::Range{ -2, -3 },
            mcr::options::Range{ 9 }
        };
        passed &= check(mixed.Str() == "10-2, 1-8, 1-8, -500, -, 9-", "multi-ranges must retain order, overlap, duplicates, negative endpoints, and reversed ranges without normalization");

        mcr::options::Range      source{ 1, 2 };
        mcr::options::MultiRange owned = {
            source,
            mcr::options::Range{ 4, 5 }
        };
        source.resume_from = 100;
        source.finish_at   = 200;
        mcr::options::MultiRange copied{ owned };
        owned = mcr::options::MultiRange{ source };
        mcr::options::MultiRange moved{ std::move(copied) };
        mcr::options::MultiRange assigned{};
        assigned = moved;
        mcr::options::MultiRange move_assigned{};
        move_assigned  = std::move(moved);
        passed        &= check(owned.Str() == "100-200" && assigned.Str() == "1-2, 4-5" && move_assigned.Str() == "1-2, 4-5", "multi-ranges must own snapshots and support independent copy/move assignments");
        auto text{ assigned.Str() };
        text.clear();
        passed &= check(assigned.Str() == "1-2, 4-5", "multi-range strings must be independent of stored ranges");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_single_range() };
    passed &= check_multi_range();
    if (!passed) {
        return 1;
    }
    std::println("test_range: ok");
    return 0;
}
