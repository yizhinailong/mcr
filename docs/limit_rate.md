# LimitRate

Import `mcr` or `mcr.transfer_options` to use `mcr::options::LimitRate`.

```cpp
import mcr.transfer_options;

mcr::options::LimitRate limited{ 1024, 2048 }; // Download and upload bytes per second.
mcr::options::LimitRate unlimited{ 0, 0 };
limited.uprate = 4096;
```

The option follows cpr's `include/cpr/limit_rate.h`. Both constructor arguments
and the public `downrate` / `uprate` fields use `std::int64_t`, matching the
current reference and retaining values above the 32-bit range. The download
argument comes first. Both arguments are required; there is no default or
single-argument constructor.

Zero represents an unlimited rate when applied to curl. The option stores all
signed 64-bit values verbatim, including negative values, without clamping or
validation. Public fields can be updated independently. Copying, moving, and
assignment preserve the two values without sharing state.

Intentional differences from cpr are the C++23 module, namespace `mcr::options`,
constructor parameter names, and Doxygen documentation. Public field names and
runtime behavior are unchanged.

In cpr, `Session::SetLimitRate` sends `downrate` to
`CURLOPT_MAX_RECV_SPEED_LARGE` and `uprate` to `CURLOPT_MAX_SEND_SPEED_LARGE`.
Its `test/get_tests.cpp` checks a local request using `LimitRate(1024, 1024)`.
Session integration is not yet implemented in this project; this module stores
the option values and does not itself throttle transfers.

Run `mcpp build` and `mcpp test`. `test_limit_rate` checks argument order, zero
and negative values, rates above 32 bits, both 64-bit boundaries, and independent
field updates after copying and moving.
