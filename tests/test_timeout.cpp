/**
 * @file test_timeout.cpp
 * @brief Verify request and connection timeout construction and inherited long-range checks.
 */
import std;
import mcr.transfer_options;

static_assert(!std::is_default_constructible_v<mcr::options::Timeout>);
static_assert(std::is_convertible_v<std::int32_t, mcr::options::Timeout>);
static_assert(std::is_convertible_v<std::chrono::seconds, mcr::options::Timeout>);
static_assert(std::is_same_v<decltype(std::declval<mcr::options::Timeout const&>().Milliseconds()), long>);
static_assert(std::is_base_of_v<mcr::options::Timeout, mcr::options::ConnectTimeout>);
static_assert(!std::is_same_v<mcr::options::Timeout, mcr::options::ConnectTimeout>);
static_assert(std::is_convertible_v<mcr::options::ConnectTimeout*, mcr::options::Timeout*>);
static_assert(!std::is_default_constructible_v<mcr::options::ConnectTimeout>);
static_assert(std::is_convertible_v<std::int32_t, mcr::options::ConnectTimeout>);
static_assert(std::is_convertible_v<std::chrono::milliseconds, mcr::options::ConnectTimeout>);
static_assert(std::is_constructible_v<mcr::options::ConnectTimeout, std::chrono::seconds>);
static_assert(!std::is_convertible_v<std::chrono::seconds, mcr::options::ConnectTimeout>);
static_assert(!std::is_constructible_v<mcr::options::ConnectTimeout, std::chrono::microseconds>);
static_assert(!std::is_constructible_v<mcr::options::ConnectTimeout, std::chrono::duration<double>>);
static_assert(!std::is_constructible_v<mcr::options::ConnectTimeout, mcr::options::Timeout>);
static_assert(std::is_same_v<decltype(mcr::options::ConnectTimeout::ms), std::chrono::milliseconds>);
static_assert(std::is_same_v<decltype(std::declval<mcr::options::ConnectTimeout const&>().Milliseconds()), long>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_timeout: {}", message);
        }
        return condition;
    }

    auto check_construction() -> bool {
        using namespace std::chrono_literals;

        bool passed{ true };
        for (std::int32_t const count : { 0, 1, -1, 1500, (std::numeric_limits<std::int32_t>::min)(), (std::numeric_limits<std::int32_t>::max)() }) {
            mcr::options::Timeout const timeout{ count };
            passed &= check(
                timeout.ms.count() == count && timeout.Milliseconds() == count,
                "integer construction must preserve the full int32 millisecond range"
            );
        }

        passed &= check(mcr::options::Timeout{ 1250ms }.Milliseconds() == 1250, "milliseconds must preserve their count");
        passed &= check(mcr::options::Timeout{ 2s }.Milliseconds() == 2000, "seconds must convert to milliseconds");
        passed &= check(mcr::options::Timeout{ 3min }.Milliseconds() == 180000, "minutes must convert to milliseconds");
        passed &= check(mcr::options::Timeout{ 1h }.Milliseconds() == 3600000, "hours must convert to milliseconds");
        passed &= check(
            mcr::options::Timeout{ 1999us }.Milliseconds() == 1 && mcr::options::Timeout{ -1999us }.Milliseconds() == -1,
            "sub-millisecond remainders must truncate toward zero"
        );
        passed &= check(
            mcr::options::Timeout{ 999999ns }.Milliseconds() == 0 && mcr::options::Timeout{ -999999ns }.Milliseconds() == 0,
            "durations shorter than one millisecond must truncate to zero"
        );
        passed &= check(
            mcr::options::Timeout{ std::chrono::duration<double>{ 1.2345 } }.Milliseconds() == 1234 &&
                mcr::options::Timeout{ std::chrono::duration<double>{ -1.2345 } }.Milliseconds() == -1234,
            "finite floating-point durations must truncate fractional milliseconds toward zero"
        );
        passed &= check(
            mcr::options::Timeout{ std::chrono::duration<int, std::ratio<1, 3>>{ 2 } }.Milliseconds() == 666,
            "custom duration periods must convert to whole milliseconds"
        );

        mcr::options::Timeout       timeout = 1500;
        mcr::options::Timeout const copied{ timeout };
        timeout.ms  = 2s;
        passed     &= check(
            copied.Milliseconds() == 1500 && timeout.Milliseconds() == 2000,
            "copies must be independent and Milliseconds must observe public ms updates"
        );
        timeout  = 3s;
        passed  &= check(timeout.Milliseconds() == 3000, "chrono durations must implicitly convert for assignment");
        return passed;
    }

    auto check_connection_construction() -> bool {
        using namespace std::chrono_literals;

        bool passed{ true };
        for (std::int32_t const count : { 0, 1, -1, 1500, (std::numeric_limits<std::int32_t>::min)(), (std::numeric_limits<std::int32_t>::max)() }) {
            mcr::options::ConnectTimeout const integer  = count;
            mcr::options::ConnectTimeout const chrono   = std::chrono::milliseconds{ count };
            passed                            &= check(integer.ms == chrono.ms && integer.Milliseconds() == count, "both connection timeout constructors must preserve the full int32 millisecond range");
        }
        passed &= check(mcr::options::ConnectTimeout{ 2s }.Milliseconds() == 2000 && mcr::options::ConnectTimeout{ 1min }.Milliseconds() == 60000, "whole-millisecond chrono conversions must work through the milliseconds constructor");
        passed &= check(mcr::options::ConnectTimeout{ std::chrono::duration_cast<std::chrono::milliseconds>(1999us) }.Milliseconds() == 1, "sub-millisecond durations require an explicit cast for connection timeouts");

        mcr::options::ConnectTimeout original{ 1500ms };
        mcr::options::ConnectTimeout copied{ original };
        mcr::options::Timeout&       base{ original };
        base.ms   = 2s;
        passed   &= check(original.Milliseconds() == 2000 && copied.Milliseconds() == 1500, "the derived option must use inherited storage and retain independent copies");
        copied    = 42;
        original  = 500ms;
        passed   &= check(copied.Milliseconds() == 42 && original.Milliseconds() == 500, "implicit integer and millisecond construction must support assignment");
        return passed;
    }

    template <typename TimeoutType>
    auto check_range() -> bool {
        using MillisecondsRep = std::chrono::milliseconds::rep;

        constexpr auto LONG_MIN_MS{ static_cast<MillisecondsRep>((std::numeric_limits<long>::min)()) };
        constexpr auto LONG_MAX_MS{ static_cast<MillisecondsRep>((std::numeric_limits<long>::max)()) };

        bool passed{ true };
        passed &= check(
            TimeoutType{ std::chrono::milliseconds{ LONG_MIN_MS } }.Milliseconds() == (std::numeric_limits<long>::min)() &&
                TimeoutType{ std::chrono::milliseconds{ LONG_MAX_MS } }.Milliseconds() == (std::numeric_limits<long>::max)(),
            "both long boundaries must convert without throwing or losing precision"
        );

        if constexpr (std::numeric_limits<MillisecondsRep>::digits > std::numeric_limits<long>::digits) {
            TimeoutType timeout{ std::chrono::milliseconds{ LONG_MAX_MS } };
            ++timeout.ms;
            try {
                (void)timeout.Milliseconds();
                passed &= check(false, "a count above long max must throw overflow_error");
            } catch (std::overflow_error const& error) {
                passed &= check(
                    std::string_view{ error.what() } == std::format("mcr::options::Timeout: timeout value overflow: {} ms.", timeout.ms.count()),
                    "overflow diagnostics must include the original millisecond count"
                );
            }

            timeout.ms = std::chrono::milliseconds{ LONG_MIN_MS };
            --timeout.ms;
            try {
                (void)timeout.Milliseconds();
                passed &= check(false, "a count below long min must throw underflow_error");
            } catch (std::underflow_error const& error) {
                passed &= check(
                    std::string_view{ error.what() } == std::format("mcr::options::Timeout: timeout value underflow: {} ms.", timeout.ms.count()),
                    "underflow diagnostics must include the original millisecond count"
                );
            }

            timeout.ms  = std::chrono::milliseconds{ 42 };
            passed     &= check(timeout.Milliseconds() == 42, "a timeout must remain usable after a failed conversion");
        }
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_construction() };
    passed &= check_connection_construction();
    passed &= check_range<mcr::options::Timeout>();
    passed &= check_range<mcr::options::ConnectTimeout>();
    if (!passed) {
        return 1;
    }
    std::println("test_timeout: ok");
    return 0;
}
