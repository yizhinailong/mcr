# Resolve

Import `mcr` or `mcr.transfer_options` to use `mcr::options::Resolve`.

```cpp
import mcr;

mcr::options::Resolve defaults{ "www.example.com", "127.0.0.1" }; // Ports 80 and 443.
mcr::options::Resolve custom{ "www.example.com", "127.0.0.1", { 8080 } };
mcr::options::Resolve empty_ports{ "www.example.com", "::1", {} }; // Also ports 80 and 443.
```

The public `host` and `addr` fields own `std::string` values. The public `ports`
field is a `std::set<std::uint16_t>`, so ports are unique and sorted. An omitted
or empty port set selects `{80, 443}`. A nonempty set is preserved exactly,
including port 0; default ports are not added to custom sets.

Hostnames and addresses are stored verbatim, with no validation, normalization,
or DNS lookup. Public fields remain mutable. The empty-set fallback applies
only when constructing from a hostname and address; clearing `ports` later,
copying, moving, or assigning a mapping does not restore default ports.

The option follows cpr's `include/cpr/resolve.h`. Intentional differences are
the C++23 module, namespace `mcr::options`, explicit `std::uint16_t`, and taking owned
constructor inputs by value and moving them into members instead of copying
from const references.

`Session::SetResolves` builds curl resolve entries from these fields and applies
them with `CURLOPT_RESOLVE`; `SetResolve` replaces the list with one mapping.
An empty list clears the overrides. Run `mcpp build` and `mcpp test` to verify defaults,
custom port sets, owned text, and field updates.
