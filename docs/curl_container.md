# CurlContainer

Import `mcr` or `mcr.fields` to use `mcr::Parameter` and `mcr::Pair`.
Import `mcr` or `mcr.curl_container` to also use `mcr::curl::CurlContainer<T>`.
The backend module lives in `src/curl/curl_container.cppm`
and also re-exports `mcr.curlholder`. The request record types retain namespace
`mcr`; the container uses namespace `mcr::curl` for its encoding adapter.
It follows cpr's `include/cpr/curl_container.h` and `cpr/curl_container.cpp`.

`Parameter` and `Pair` are distinct types with two public, owned `std::string`
members: `key` and `value`. Their constructors take both strings by value and
move them into storage. Empty strings and embedded null bytes are retained.
Neither element type has a default constructor.

```cpp
import mcr.curl_container;

mcr::curl::CurlContainer<mcr::Parameter> query{ { "q", "hello world" }, { "flag", "" } };
query.Add(mcr::Parameter{ "q", "another value" });
auto raw = query.GetContent(); // q=hello world&flag&q=another value

// After curl_global_init(), and before curl_global_cleanup():
mcr::curl::CurlHolder holder;
auto encoded = query.GetContent(holder);
// q=hello%20world&flag&q=another%20value
```

`CurlContainer<T>` defaults to an empty vector with public `encode = true`.
An initializer list copies elements in order. `Add(element)` and
`Add(initializer_list)` append copies, retaining duplicate keys. Modifying an
input element afterward does not change the stored copy. Containers support
copy/move construction and assignment; copying retains independent elements
and the encoding flag. The protected vector is named `m_container_list`.

The two element types have deliberately different serialization policies:

| Element | `GetContent(holder)` with `encode = true` | Empty value |
| --- | --- | --- |
| `Parameter` | Percent-encode both key and value | Emit just the key |
| `Pair` | Keep the key verbatim; percent-encode the value | Emit `key=` |

`GetContent(holder)` with `encode = false` emits both fields verbatim.
`GetContent()` always emits raw fields regardless of `encode`; it needs neither
a holder nor curl initialization. Pair keys remain unescaped even with encoding
enabled, matching cpr. For example, a key `a b` and value `x+y` produce
`a%20b=x%2By` for a parameter and `a b=x%2By` for a pair.

Each result is an owned string without a leading `?`. Serialization preserves
order, duplicate keys, and binary string lengths, and does not change stored
elements. Curl encodes spaces as `%20` and literal plus signs as `%2B`.
Raw output is not normalized or checked for delimiter characters.

Separators follow cpr's existing behavior: append `&` before an element only
when output is already nonempty. Leading parameters with both fields empty
therefore disappear. Empty parameters after emitted text can introduce empty
segments or a trailing `&`: `{ { "", "" }, { "a", "" }, { "", "" } }`
becomes `a&`. A pair with both fields empty always produces `=`.

Only encoded, nonempty containers consult the supplied holder. An empty
container, or a call with `encode = false`, can use a moved-from holder.
Otherwise `CurlHolder::UrlEncode` supplies its normal exceptions and its
empty-result behavior on curl allocation failure.

Intentional differences from cpr are the C++23 module and namespace, the
protected `m_container_list` name, returning `std::string` without top-level
`const`, and copying const inputs directly instead of applying ineffective
`std::move` calls to them. The template explicitly requires `Parameter` or
`Pair`; unsupported types fail at compile time rather than cpr's missing
template definitions at link time. Encoding uses the existing `UrlEncode`
method and appends its result with an explicit length.

This module supplies the base for [`Parameters`](parameters.md) and
[`Payload`](payload.md). Run `mcpp build` and `mcpp test`. Tests exercise both
public wrappers, adapt cpr's
`test/structures_tests.cpp` and cover both formatting policies, encoding
switches, empty entries, duplicates, appends, binary and UTF-8 strings,
independent ownership, payload iterator ranges, derived access, and holder lifetime behavior.
