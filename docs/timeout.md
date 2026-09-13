# Request and connection timeouts

Import `mcr` or `mcr.transfer_options` to use `mcr::options::Timeout` and
`mcr::options::ConnectTimeout`.

```cpp
import std;
import mcr;

using namespace std::chrono_literals;

mcr::options::Timeout const from_integer{ 1500 };
mcr::options::Timeout timeout{ 2s };
std::println("{} ms", timeout.Milliseconds()); // 2000 ms
timeout.ms = 500ms;
```

`Timeout` accepts an integer millisecond count (`std::int32_t`) or any
`std::chrono::duration<Rep, Period>`. Both constructors allow implicit
conversion. There is no default constructor; use `Timeout{0}` to represent
no overall request timeout when passed to curl. The public `ms` member stores
`std::chrono::milliseconds` and can be updated after construction.

Chrono construction uses `std::chrono::duration_cast`, truncating fractional
milliseconds toward zero. For example, `1999us` becomes `1ms` and `-1999us`
becomes `-1ms`. As in cpr, callers must supply durations that convert within
the milliseconds representation without overflowing the conversion arithmetic;
floating-point durations must also be finite.

`Milliseconds()` returns the `long` required by curl's `CURLOPT_TIMEOUT_MS`.
It throws `std::overflow_error` above `LONG_MAX` and `std::underflow_error`
below `LONG_MIN`, including the stored count in the diagnostic. Range checks
occur when reading the value, so they also apply to changes made through `ms`.
Zero and representable negative values are preserved without validation.

The interface and behavior follow cpr's `include/cpr/timeout.h` and
`cpr/timeout.cpp`. Intentional differences are the C++23 module exports,
namespace `mcr::options`, diagnostic prefix `mcr::options::Timeout`, and passing the integer
constructor argument by value instead of by const reference.

`ConnectTimeout` publicly derives from `Timeout`, following cpr's
`include/cpr/connect_timeout.h`. It adds no state and inherits both `ms` and
`Milliseconds()`, including the base class's range checks and diagnostics.
Its distinct type allows a session to distinguish connection timeout options
from overall request timeout options.

```cpp
mcr::options::ConnectTimeout connect{ 1500 };
mcr::options::ConnectTimeout from_milliseconds = 500ms;
mcr::options::ConnectTimeout from_seconds{ 2s };
connect.ms = 750ms;
```

The two constructors accept `std::int32_t` and
`std::chrono::milliseconds const&`, and neither is explicit. Unlike `Timeout`,
the derived type does not expose a generic duration constructor. Direct
construction from seconds or minutes works through chrono's lossless
conversion to milliseconds. Copy-initialization from seconds would require
two user-defined conversions and is not supported. Sub-millisecond and
floating-point durations require an explicit `duration_cast` to milliseconds.
There is no default constructor. The integer is passed by value, matching
the existing base class rather than cpr's const-reference parameter.

In cpr, `Session::SetConnectTimeout` applies the inherited millisecond value
to `CURLOPT_CONNECTTIMEOUT_MS`. It covers DNS resolution and protocol
handshakes until a connection is established. Zero selects curl's built-in
300-second connection timeout; an overall request timeout can still impose a
shorter limit. These modules only represent options and do not apply them to
requests yet.

Run `mcpp build` and `mcpp test` to validate conversions, truncation, public
member updates, connection constructor constraints, and platform-dependent
range checks for both timeout types.
