# Parameters

Import `mcr` or `mcr.fields` to use `Parameters` and `Parameter`.
The `mcr` entry module also exports `mcr::curl::CurlHolder`; when importing only `mcr.fields`,
add `mcr.curlholder` to name that backend type. The fields module combines cpr's
`include/cpr/parameters.h` and its otherwise empty `cpr/parameters.cpp` in
one module; no separate implementation unit is needed.

```cpp
import mcr.fields;
import mcr.curlholder;

mcr::Parameters query{ { "q", "hello world" }, { "flag", "" } };
query.Add(mcr::Parameter{ "q", "x+y" });
auto raw = query.GetContent(); // q=hello world&flag&q=x+y

// After curl_global_init(), and before curl_global_cleanup():
mcr::curl::CurlHolder holder;
auto encoded = query.GetContent(holder);
// q=hello%20world&flag&q=x%2By
```

`mcr::Parameters` is a distinct public subclass of `mcr::curl::CurlContainer<mcr::Parameter>`.
Its default constructor creates an empty collection with `encode == true`.
The non-explicit `std::initializer_list<Parameter> const&` constructor copies
the supplied entries, preserving order, duplicate keys, and empty strings.
Copying produces independent storage; implicit moves are `noexcept`. Both
copy/move operations retain the encoding flag.

`Add(Parameter const&)`, `Add(initializer_list)`, both `GetContent()`
overloads, the public `encode` flag, and protected `m_container_list` are
inherited. `GetContent(holder)` percent-encodes keys and nonempty values when
encoding is enabled. `GetContent()` always returns raw content without using
curl. An empty value emits the key without `=`. The result has no leading
`?`, and no URL or network request is created by this module.

Serialization, empty-entry separators, binary lengths, and holder error
behavior are described in [CurlContainer](curl_container.md). The wrapper
adds no validation or formatting policy of its own. Apart from the module
and namespace, its interface follows the reference directly and inherits
the already documented base-module differences.

Run `mcpp build` and `mcpp test`. `tests/test_curl_container.cpp` exercises
`Parameters` through the entry module, covering default/list construction,
appends, ownership and copy/move behavior, raw and encoded output, duplicate
and empty entries, binary/UTF-8 input, and holder lifetime errors.
