# Buffer

Import `mcr` or `mcr.buffer` to use `Buffer`. It adapts cpr's
`include/cpr/buffer.h` using only `import std;` and `std::filesystem::path`.
There is no filesystem compatibility layer or additional dependency.

```cpp
import std;
import mcr.buffer;

std::vector<unsigned char> bytes{ 'h', 'i', 0, 0xff };
mcr::Buffer buffer{ bytes.begin(), bytes.end(), "upload.bin" };
// buffer.data points into bytes; buffer.datalen is 4, including the null byte.

std::filesystem::path filename{ "another.bin" };
mcr::Buffer named{ bytes.cbegin(), bytes.cend(), std::move(filename) };
```

The public fields retain cpr's names and types:

| Member | Type | Meaning |
| --- | --- | --- |
| `data_t` | `char const*` | Alias for the borrowed byte pointer |
| `data` | `data_t` | Read-only access to source memory; the pointer itself is mutable |
| `datalen` | `std::size_t` | Byte count; publicly mutable |
| `filename` | `std::filesystem::path const` | Owned, immutable upload filename |

Construction takes two iterators of the same type and a
`std::filesystem::path&&`. A filename literal converts to a temporary path;
pass an existing path with `std::move`, or explicitly copy it into a temporary
to retain the original. An empty filename is allowed. Paths are stored without
normalization, basename extraction, opening a file, or checking its existence.

Constructor `static_assert` checks require a C++20 contiguous iterator and
an element size of one byte. Pointers, string/vector/array iterators, and span
iterators over `char`, signed/unsigned char, and `std::byte` are supported.
Multibyte elements such as `std::uint32_t`, list/deque iterators,
`std::vector<bool>` proxies, and reverse iterators are rejected. The byte
check concerns size, not a whitelist of element types. `std::to_address`
obtains the real element address without invoking an overloaded `operator&`.
Assertions are checked when the constructor body is instantiated; merely
using `std::is_constructible` does not test these assertions.

The range must belong to the same live contiguous sequence. A pair of null
pointers also describes an empty range. Equal iterators produce `data ==
nullptr` and `datalen == 0` without dereferencing or subtracting them.
For a nonempty range, `end - begin` determines the length. A negative distance
throws `std::invalid_argument`. Unrelated, dangling, or otherwise invalid
iterators remain caller errors and cannot be validated here.

`Buffer` does not copy or own the bytes. Keep the source storage alive and
avoid invalidating its address until all consumers, including asynchronous
uploads, finish reading it. Changes to existing source elements are visible
through the buffer. Embedded null bytes are retained; a string terminator is
included only if it lies inside the supplied range.

There is no default constructor. Implicit copy and move constructors share
the byte pointer and length. Because `filename` is const, it is copied even
during a move, which can allocate and throw. Copy and move assignment are
unavailable because of that const member, matching cpr.

Intentional differences from cpr are the module/namespace, direct standard
filesystem use, and stronger constructor validation. The reference only
checks the legacy random-access iterator category, which can admit
noncontiguous memory, and dereferences `begin` even when the range is empty.
The unused public `is_random_access_iterator` helper is replaced by the
constructor's static assertions. Empty and reversed ranges have the defined
behavior above. Valid contiguous byte ranges retain the reference behavior.

Run `mcpp build` and `mcpp test` to verify borrowing, binary subranges,
supported storage types, empty and reversed ranges, filename ownership,
and copy/move behavior. [`Part`](multipart.md) can borrow these buffers;
Session serializes them into MIME parts using their explicit byte lengths.
Keep the underlying storage alive until all transfers using it complete.
