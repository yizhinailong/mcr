# Local port options

Import `mcr` or `mcr.transfer_options` to use `mcr::options::LocalPort` and
`mcr::options::LocalPortRange`.

```cpp
import std;
import mcr;

mcr::options::LocalPort port = std::uint16_t{ 50000 };
mcr::options::LocalPortRange range = std::uint16_t{ 100 };
std::uint16_t port_number = port;
std::uint16_t range_value = range;
```

Both options follow cpr's `include/cpr/local_port.h` and
`include/cpr/local_port_range.h`: they store a private `std::uint16_t`, allow
implicit construction from that type, and provide a const implicit conversion
back to it. Values from 0 through 65535 are preserved without validation,
normalization, socket binding, or port probing. Neither class has a default
constructor. Ordinary copying, moving, and assignment are supported; assigning
a `uint16_t` implicitly constructs a replacement option.

Intentional differences are the C++23 modules, namespace `mcr::options`, private member
names `m_local_port` / `m_local_port_range`, and `[[nodiscard]]` on conversion
operators. No runtime behavior is changed.

In cpr, `Session::SetLocalPort` and `Session::SetLocalPortRange` convert these
values to `long` for `CURLOPT_LOCALPORT` and `CURLOPT_LOCALPORTRANGE`.
Its `test/session_tests.cpp` checks source port selection and occupied ports
against a local HTTP fixture. Session integration is not yet implemented in
this project; these modules provide the option values.

Run `mcpp build` and `mcpp test`. `test_local_port` covers both options' implicit
conversions, zero and maximum values, and independent copy/move assignments.
