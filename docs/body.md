# Body

Import `mcr` or `mcr.body` to use `Body`. The module re-exports `mcr.buffer`,
`mcr.file`, and `mcr.types`. `Body` derives from `StringHolder<Body>` and owns
its request bytes, adapting cpr's `include/cpr/body.h`.

```cpp
import std;
import mcr.body;

mcr::Body text{ "x=", "5&y=13" };
std::array<unsigned char, 3> bytes{ 'a', 0, 'b' };
mcr::Buffer buffer{ bytes.begin(), bytes.end(), "ignored.bin" };
mcr::Body binary = buffer; // Copies all three bytes into owned storage.
mcr::Body from_file{ mcr::File{ "request.bin" } }; // Reads immediately.
```

The default constructor creates an empty body. All reference constructors
are retained and non-explicit:

| Input | Behavior |
| --- | --- |
| `std::string` by value | Moves the supplied string into storage |
| `std::string_view` | Copies the view's exact bytes |
| `char const*` | Copies a valid, nonnull C string up to its first null |
| `char const*`, `std::size_t` | Copies the specified number of readable bytes |
| `std::initializer_list<std::string>` | Concatenates fragments without separators |
| `Buffer const&` | Copies `data` and `datalen` bytes, ignoring the filename |
| `File const&` | Opens `filepath` in binary mode and reads to EOF |

Length-based input preserves embedded nulls and requires no terminator. A
null pointer with a zero length is accepted, including an empty `Buffer`.
Input must otherwise describe a valid readable range. After construction,
source strings and buffers can be modified or destroyed without affecting
the body. No encoding, trimming, content-type inference, or automatic
conversion from `BodyView` is added.

File construction reads synchronously using `std::ifstream`. It uses the
string `File::filepath` exactly as supplied; `overriden_filename` has no
effect. Empty files produce an empty body. Binary mode preserves all bytes,
including CR/LF and control characters on Windows. The implementation uses
fixed-size read blocks and appends their contents to the owned string until
normal EOF, without a filesystem compatibility layer or preliminary size
query. The entire file contents still occupy memory; this is not a streamed
request-body API. Concurrent file changes are not an atomic snapshot.

If opening fails, construction throws `std::invalid_argument` with cpr's
message, `Can't open the file for HTTP request body!`. A read failure before
normal EOF throws `std::runtime_error` with
`Can't read the file for HTTP request body!`. Allocation and string-capacity
errors propagate as `std::bad_alloc` or `std::length_error`. RAII closes the
stream on every exit; a successfully constructed body remains valid after
the source file is replaced or removed.

`Str()`, `CStr()`, `Data()`, comparisons, concatenation returning `Body`,
void-returning `+=`, explicit conversion to `std::string`, and stream output
are inherited from `StringHolder`. Copying produces independent bytes; moves
are `noexcept`. The destructor overrides the base's virtual destructor, and
the class remains extensible. Protected storage is named `m_str`, following
the existing base module.

Intentional differences from cpr are the module/namespace and inherited
project naming, plus checked reads to EOF. The reference seeks to determine
the length, resizes a string, and reads once without checking seek/read
failures. This implementation avoids converting a failed size query to an
unsigned allocation size and throws on read errors instead of exposing a
partial or padded body. Buffer conversion needs no redundant casts because
its fields already have the required pointer and size types.

Run `mcpp build` and `mcpp test` to validate constructor ownership, Buffer
subranges and lifetimes, inherited operations, empty and binary file reads,
multiple read blocks, and file failures. Tests create and remove their own
temporary file. `Session::SetBody` owns the bytes and sends them using an explicit
length; [Session tests](session.md) also cover local HTTP transfers and body reuse.
