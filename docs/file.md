# File and Files

Import `mcr` or `mcr.file` to use `File` and `Files`. Their implementation
combines cpr's `include/cpr/file.h` and `cpr/file.cpp` in one module using
`import std;`. Filesystem operations use `std::filesystem` directly; there is
no filesystem compatibility module, namespace alias, Boost dependency, or
experimental filesystem fallback.

```cpp
import std;
import mcr.file;

std::filesystem::path path{ std::filesystem::path{ "uploads" } / "report.txt" };
mcr::File file{ path.string(), "download.txt" };
bool overridden = file.HasOverridenFilename(); // true
mcr::Files single = file;
mcr::Files paths{ "first.txt", "second.txt" };
mcr::Files named{ mcr::File{ "first.txt", "first-upload.txt" }, file };
named.push_back(mcr::File{ "third.txt" });
```

`File` retains cpr's public `std::string filepath` and
`std::string overriden_filename` fields, including the spelling of the latter.
Construction is explicit and requires a path; the optional filename defaults
to empty. The path is taken by value and moved into storage. The filename is
copied from a `std::string_view`, so bounded, non-null-terminated views are
accepted. Both fields remain mutable, and all supplied bytes are retained.

`HasOverridenFilename() const noexcept` replaces cpr's
`hasOverridenFilename()` using the project's method naming convention. It
checks only whether `overriden_filename` is nonempty. Whitespace counts as
an override; no trimming, basename extraction, or filename validation occurs.
Clearing the public field removes the override.

These are descriptors: constructing or copying a `File` does not open, read,
create, check, or normalize a file. Relative paths, empty paths, and paths to
files that do not exist are stored unchanged. Use `std::filesystem::path`
explicitly when path operations are needed, then pass its `.string()` result
to this string-based interface.

`Files` owns a private vector named `m_files` and supports an empty default
constructor, implicit construction from one `File`, and initializer lists of
either `File` descriptors or path strings. A path list creates files without
overrides. Order, duplicate entries, empty paths, and supplied overrides are
preserved. `Files{}` is empty; explicitly typed empty initializer lists of
either element type are also supported.

The standard container names `begin`, `end`, `cbegin`, `cend`, `emplace_back`,
`push_back`, and `pop_back` are retained. Mutable iterators expose both fields;
const iteration is read-only. Both append methods accept `File const&` and
append a copy; `emplace_back` is not a variadic constructor-forwarding API.
They return `void`, as in cpr. `pop_back` requires a nonempty collection.
Iterators and references follow vector invalidation rules.

Copy construction and assignment produce independent descriptors; moves
transfer vector storage without throwing. Copy and move assignments return
the destination, and self-assignment/self-move leave it unchanged.

Intentional differences from cpr are the module and namespace, the method and
private-member naming changes, `std::string_view` for the optional filename,
and `noexcept` on iterator accessors. There is no filesystem compatibility
layer. The descriptor and collection behavior otherwise follows the reference.

In Session's multipart implementation, `filepath` goes to `curl_mime_filedata`, while
the transmitted filename is the override or the result of
`std::filesystem::path(filepath).filename().string()`. The
[`multipart module`](multipart.md) provides the corresponding descriptors,
which Session serializes when preparing the request.
Run `mcpp build` and `mcpp test` to
verify descriptor ownership, override detection, both list constructors,
iteration, appends, removals, copying/moving, and standard filesystem path
interoperation without accessing real files.
