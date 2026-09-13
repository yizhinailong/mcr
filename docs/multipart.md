# Part and Multipart

Import `mcr` or `mcr.multipart` to use `Part` and `Multipart`. The module
re-exports `mcr.buffer` and `mcr.file`, using only the standard library for
its implementation. It combines cpr's `include/cpr/multipart.h` and
`cpr/multipart.cpp` in one module.

```cpp
import std;
import mcr.multipart;

std::array<unsigned char, 3> bytes{ 'a', 0, 'b' };
mcr::Buffer buffer{ bytes.begin(), bytes.end(), "upload.bin" };
mcr::Multipart form{
    { "text", "hello" },
    { "number", 5, "application/number" },
    { "file", mcr::File{ "report.txt", "download.txt" } },
    { "files", mcr::Files{ "first.bin", "second.bin" } },
    { "buffer", buffer, "application/octet-stream" }
};
form.parts.emplace_back("text", "another value");
```

`Part` has five constructors taking a field name, one of the inputs below,
and an optional content type defaulting to empty. Names, text values, and
content types are copied from `std::string_view` into owned strings.
Empty strings, bounded views, and embedded null bytes are retained without
encoding, normalization, or validation.

| Input | Stored value | `is_file` | `is_buffer` |
| --- | --- | --- | --- |
| `std::string_view` | Copied text | false | false |
| `std::int32_t` | Signed decimal text via `std::to_string` | false | false |
| `Files const&` | Empty; file descriptors are copied into `files` | true | false |
| `Files&&` | Empty; file descriptors are moved into `files` | true | false |
| `Buffer const&` | `buffer.filename.string()` | false | true |

The public fields retain cpr's names and types: `std::string name`, `value`,
and `content_type`; `Buffer::data_t data`; `std::size_t datalen`; `bool is_file`
and `is_buffer`; and `Files files`. Text and file fields have null `data` and
zero `datalen`. Text and buffer fields have an empty `files` collection.
There is no default constructor. All fields remain mutable; callers changing
the mode flags must keep the associated fields consistent.

A `File` implicitly converts to a one-element `Files`, so a single file can
be passed directly. File order, duplicate descriptors, and filename overrides
are preserved without opening or checking any path. Empty file collections
still select file mode. The descriptor constructor performs no I/O.

Buffer construction copies only metadata: `data` and `datalen` snapshot the
descriptor's borrowed range, and `value` owns its full filename converted
with standard `std::filesystem::path::string()`. Directory components are
not removed, and `value` remains a string as in cpr. Empty buffers retain
buffer mode with null/zero data. The `Buffer` object may be destroyed or its
fields changed afterward, but its backing bytes must stay alive and at the
same address until all consumers finish. Copying or moving a `Part` or
`Multipart` does not extend that lifetime.

`Multipart` owns a public `std::vector<Part> parts`. Its non-explicit
initializer-list constructor copies entries in order and allows nested list
syntax and `Multipart{}`. It has no default constructor, so
`Multipart multipart;` is invalid. Both vector constructors are explicit:
`std::vector<Part> const&` copies, while `std::vector<Part>&&` moves storage
without throwing. A const vector rvalue binds to the const-reference
constructor and is copied. Duplicate field names and empty vectors are
retained without interpretation.

Implicit copy/move construction and assignment are available for both
types; moves are `noexcept`. Copies own independent strings and file
descriptors while retaining the same borrowed buffer addresses and lengths.
The public `parts` vector supports normal mutation and its usual reference
and iterator invalidation rules.

Intentional differences from cpr are the module/namespace, read-only text
parameters using `std::string_view`, and taking the integer by value. The
vector rvalue constructor now takes a nonconst rvalue and moves it; cpr's
`const std::vector<Part>&&` overload copied every part. Const inputs still
copy. Filesystem conversion uses the standard library directly.

`Session::SetMultipart` retains these descriptors and builds a curl MIME tree
when preparing a request. Text and buffer parts both use explicit byte lengths,
preserving embedded nulls; files are supplied through `curl_mime_filedata`.
Borrowed buffers and referenced files must remain available for the transfers.
See [Session](session.md) for request integration and local HTTP tests.
Run `mcpp build` and `mcpp test` to verify all field constructors, numeric
limits, file copying/moving, buffer lifetimes, mixed collections, and vector
copy/move behavior without filesystem or network I/O.
