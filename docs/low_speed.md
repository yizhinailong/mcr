# LowSpeed

Import `mcr` or `mcr.transfer_options` to use `mcr::options::LowSpeed`.

```cpp
import std;
import mcr.transfer_options;

using namespace std::chrono_literals;
mcr::options::LowSpeed option{ 1000, 1s };
option.limit = 2048;
option.time = 2min; // Stored as 120 seconds.
```

The option stores a minimum transfer rate in the public `std::int32_t limit`
field and an observation duration in the public `std::chrono::seconds time`
field. The constructor requires both values, in that order. Rates are measured
in bytes per second. Values are stored verbatim, including zero, negative
values, and the full ranges of both field types. Copying, moving, and assigning
options preserve independent values; both fields remain mutable.

The API follows cpr's `include/cpr/low_speed.h` with C++23 modules, namespace
`mcr::options`, and Doxygen documentation. Its deprecated integer-time constructor is
intentionally omitted: use `LowSpeed{1000, 1s}` instead of `LowSpeed{1000, 1}`.
Integral durations such as minutes and hours may convert implicitly to seconds
when representable. Milliseconds and floating-point durations require an
explicit conversion; the option does not silently truncate them.

In cpr, `Session::SetLowSpeed` applies `limit` to `CURLOPT_LOW_SPEED_LIMIT` and
the duration's second count to `CURLOPT_LOW_SPEED_TIME`. Its session and error
tests check normal requests and slow responses against a local HTTP fixture.
This project currently provides the option value; Session integration and
transfer timeout enforcement are not yet implemented.

Run `mcpp build` and `mcpp test`. `test_low_speed` checks duration units, signed
boundaries, field updates, value independence, and compile-time rejection of
the deprecated integer-time signature and implicit lossy duration conversions.
