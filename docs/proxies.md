# Proxies

Import `mcr` or `mcr.proxy` to use `mcr::options::Proxies`.

```cpp
import std;
import mcr.proxy;

mcr::options::Proxies proxies{
    { "http", "http://proxy.test:8080" },
    { "https", "socks5://proxy.test:1080" },
    { "no_proxy", "" },
};
if (proxies.Has("http")) {
    std::println("{}", proxies["http"]);
}
```

The type follows cpr's `include/cpr/proxies.h` and `cpr/proxies.cpp`. It owns
an internal map rather than inheriting from `std::map`. Default construction
creates no mappings. Initializer-list construction copies protocol/address
pairs; the explicit `std::map<std::string, std::string> const&` constructor
copies an existing map. Copies and assignments own independent mappings.
Duplicate initializer-list keys follow `std::map`'s initialization rules,
which do not specify which equivalent entry is retained.

`Has(protocol)` performs an exact, case-sensitive lookup without inserting a
key. An existing empty value still returns true. `operator[]` is non-const
and inserts an empty string when the key is missing, returning a
`std::string const&` as in cpr. Returned references remain valid across other
insertions. Values cannot be changed through those references; replacing the
option replaces its configuration.

Protocol names and proxy addresses are stored verbatim, without normalization,
URL validation, or network access. Empty names and values and embedded null
bytes are preserved. `no_proxy` and `NO_PROXY` are independent keys. In cpr,
Session interprets those keys as proxy exclusions, preferring `no_proxy` when
both exist; an empty exclusion value overrides the environment's exclusion
list. This option does not itself read or modify the environment.

Intentional API differences are C++23 modules, namespace `mcr::options`, the private
member name `m_hosts`, `Has()` replacing `has()`, and `std::string_view` query
parameters. A transparent `std::less<>` map comparator permits bounded views
without allocating a key during lookups. Missing-key subscript copies the view
into owned storage before inserting, preserving cpr's insertion behavior.

The reference's `test/proxy_tests.cpp` and `test/proxy_auth_tests.cpp` exercise
proxy options through requests and sessions. Session integration is not yet
implemented in this project. `mcpp build` and `mcpp test` validate the option's
construction, ownership, exact lookup, empty-value insertion, binary text, and
copy/move behavior without an external proxy service.
