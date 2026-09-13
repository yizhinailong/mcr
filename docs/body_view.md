# BodyView

Import `mcr` or `mcr.body` to use `BodyView`. The module re-exports
`mcr.buffer` and otherwise depends only on `import std;`. It adapts cpr's
`include/cpr/body_view.h` as a final class storing one private
`std::string_view m_body`.

```cpp
import std;
import mcr.body;

std::string source{ "x=5" };
mcr::BodyView body{ source };
std::string_view borrowed = body.Str();

std::array<unsigned char, 3> bytes{ 'a', 0, 'b' };
mcr::Buffer buffer{ bytes.begin(), bytes.end(), "body.bin" };
mcr::BodyView binary = buffer; // Three bytes, including the embedded null.
```

All reference constructors are retained and are non-explicit:

| Construction | Behavior |
| --- | --- |
| `BodyView{}` | Empty view with a null data pointer |
| `BodyView{std::string_view}` | Retains the view's exact pointer and length |
| `BodyView{char const*}` | Scans a valid, nonnull C string up to its first null |
| `BodyView{char const*, std::size_t}` | Retains the exact byte range, without scanning |
| `BodyView{Buffer const&}` | Retains `data` and `datalen`; ignores the filename |

Pointer/length input must describe a valid readable range. A null pointer is
allowed with a zero length, including an empty `Buffer`; a nonnull pointer
with zero length is preserved. These overloads do not require null
termination and retain embedded null bytes. The single-pointer C-string
overload requires a nonnull, terminated string, even for an empty body.
Invalid input is a caller error, following `std::string_view`'s preconditions.

`Str() const` returns a `std::string_view` by value. It replaces cpr's `str()`
using the project's public method naming convention. Modifying the returned
view's boundaries or rebinding a `BodyView` does not alter other descriptors.
There is no implicit conversion from `BodyView` back to `std::string_view`.
Direct construction from an existing `std::string` works through its view
conversion; implicit `std::string` to `BodyView` conversion would require two
user-defined conversions and is unavailable, matching cpr.

The source bytes are never copied or owned. Keep them alive at the same
address until all consumers finish, including an asynchronous request.
Existing-element changes are visible through every view of those bytes;
reallocation, shrinking past the viewed range, and source destruction can
invalidate a view. Constructing from a temporary owning string does not
extend its lifetime. A `Buffer` descriptor itself may be destroyed or have
its public fields changed after conversion: `BodyView` snapshots its pointer
and length, and only the underlying byte storage must remain valid.

Copy/move construction and assignment copy the borrowed descriptor without
transferring ownership. Assignment returns the destination; self-assignment
is supported. Destruction releases no byte storage. `BodyView` remains
trivially copyable, as required by cpr's `Session::SetBodyView` design.

Intentional differences from cpr are the module/namespace, `Str()` naming,
and explicit `constexpr`/`noexcept` on construction, assignment, and access.
The redundant pointer and size casts in Buffer conversion are unnecessary
because `mcr::Buffer` already exposes `char const*` and `std::size_t`.

Run `mcpp build` and `mcpp test` to check constant evaluation, non-owning
copies/moves, binary and empty ranges, and Buffer interoperability.
`Session::SetBodyView` borrows the bytes for transfers; the source must remain
valid until all requests using it complete. See [Session](session.md).
