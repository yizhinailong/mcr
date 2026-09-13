# Payload

Import `mcr` or `mcr.fields` to use `Payload` and `Pair`.
Import `mcr.curlholder` explicitly when naming `mcr::curl::CurlHolder`. The fields module combines cpr's
`include/cpr/payload.h` and its otherwise empty `cpr/payload.cpp` in one
module, without a separate implementation unit.

```cpp
import std;
import mcr.fields;
import mcr.curlholder;

mcr::Payload form{ { "name", "hello world" }, { "flag", "" } };
form.Add(mcr::Pair{ "name", "x+y" });
auto raw = form.GetContent(); // name=hello world&flag=&name=x+y

std::vector<mcr::Pair> pairs{ { "first", "one" }, { "last", "two" } };
mcr::Payload from_range{ pairs.cbegin(), pairs.cend() };
mcr::Payload empty{};

// After curl_global_init(), and before curl_global_cleanup():
mcr::curl::CurlHolder holder;
auto encoded = form.GetContent(holder);
// name=hello%20world&flag=&name=x%2By
```

`mcr::Payload` is a distinct public subclass of `mcr::curl::CurlContainer<mcr::Pair>`. The
non-explicit `std::initializer_list<Pair> const&` constructor copies the
supplied entries, preserving order, duplicate keys, and empty strings.
There is no default constructor, matching cpr: `Payload payload;` is invalid
and `std::is_default_constructible_v<Payload>` is false. `Payload{}` works
through the empty initializer-list constructor.

The range constructor takes two copyable iterators of the same type and
calls `Add(*iterator)` for each entry in a single pass. It accepts pointers,
const and mutable container iterators, noncontiguous iterators, and
single-pass input iterators yielding pairs. It does not require random
access or precompute a distance. Traversal order determines stored order;
reverse iterators produce reversed entries. Equal iterators, including two
null pointers, create an empty payload without dereferencing them.
The supplied range must be valid, with its end reachable from its beginning.
As in cpr, separate sentinel types are not accepted, and unsuitable iterator
types fail when the constructor body is instantiated.

Each range entry is copied through `Add(Pair const&)`, even with move
iterators. Source elements can be modified or destroyed after construction.
Both constructors enable encoding. Copying a payload produces independent
storage; implicit moves are `noexcept`. Copy/move operations retain the
encoding flag as well as the ordered pairs.

`Add()`, both `GetContent()` overloads, public `encode`, and protected
`m_container_list` are inherited. With encoding enabled,
`GetContent(holder)` percent-encodes values and emits keys verbatim. Each
pair always contains `=`, including empty values; pairs are joined by `&`.
`GetContent()` always returns raw bytes without using curl. See
[CurlContainer](curl_container.md) for binary lengths, escaping, and holder
error behavior. No request or content-type header is created by this module.

Apart from the module and namespace, the wrapper follows the reference's
constructors directly and inherits the documented base-module differences.
Run `mcpp build` and `mcpp test`. `tests/test_curl_container.cpp` exercises the
actual `Payload` and `Parameters` wrappers, including payload list/range
construction, source ownership, single-pass and move iterators, empty
ranges, raw/encoded output, appends, copy/move operations, and holder errors.
