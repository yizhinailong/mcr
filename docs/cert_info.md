# CertInfo

Import `mcr` or `mcr.cert_info` to use `mcr::CertInfo`.

```cpp
import std;
import mcr.cert_info;

mcr::CertInfo certificate{
    "Subject:CN = test-server",
    "Issuer:C = GB, O = Example, CN = Sub CA",
};
certificate.emplace_back("Version:2");
for (auto const& entry : certificate) {
    std::println("{}", entry);
}
```

The type follows cpr's `include/cpr/cert_info.h` and `cpr/cert_info.cpp`, storing
an owned `std::vector<std::string>` internally. Default construction creates
an empty collection. Initializer-list construction preserves entry order,
duplicates, empty strings, embedded nulls, and multiline text without parsing
or certificate validation.

The public interface provides mutable `operator[]`, mutable and const
`begin()` / `end()`, and read-only `cbegin()` / `cend()`. The iterator aliases
are the corresponding vector iterator types. `emplace_back` and `push_back`
both accept one `std::string const&`, copy it, and return `void`, matching cpr.
`pop_back` removes the last entry. Indexes must be in range and popping requires
a nonempty collection; reference and iterator invalidation follows vector.

Copy construction owns independent entries, and move construction transfers
them. As in the reference, copy and move assignment are unavailable. A
`std::vector<CertInfo>` can still collect a certificate chain through copy or
move insertion and grow using move construction.

Intentional differences from cpr are C++23 modules, namespace `mcr`, private
member name `m_cert_info`, taking the subscript index by value, and explicit
`noexcept` on move construction and iterator access. Standard container method
names are retained, consistently with `Cookies`.

`Response` snapshots curl's certificate information when a transfer completes;
`GetCertInfos()` returns an independent copy, empty when no certificates are
available. Local TLS coverage is described in [Session](session.md). Run
`mcpp build` and `mcpp test` to verify entry ownership, mutation, iteration,
appending/removal, and certificate-chain storage without requiring TLS access.
