# Utilities

Import `mcr.util` to use the free functions in `mcr::utils`.
See [utility namespaces](utils.md) for the shared namespace and migration details.
The implementation follows cpr's `include/cpr/util.h`, `cpr/util.cpp`, and
`test/util_tests.cpp`.

```cpp
import std;
import mcr.util;

std::string status;
std::string reason;
auto headers = mcr::utils::parse_header(
    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n",
    &status, &reason);
std::println("{}: {}", reason, headers.at("content-type"));
```

The API uses C++23 modules, namespace `mcr::utils`, snake_case free functions,
and `std::string_view` for borrowed input. Name mappings are:

| cpr | mcr::utils |
| --- | --- |
| `parseHeader` | `parse_header` |
| `parseCookies` | `parse_cookies` |
| `readUserFunction` | `read_user_function` |
| `headerUserFunction` | `header_user_function` |
| `writeFunction` | `write_function` |
| `writeFileFunction` | `write_file_function` |
| `writeUserFunction` | `write_user_function` |
| `writeSSEFunction` | `write_sse_function` |
| `progressUserFunction` | `progress_user_function` |
| `debugUserFunction` | `debug_user_function` |
| `split` | `split` |
| `urlEncode`, `urlDecode` | `url_encode`, `url_decode` |
| `isTrue` | `is_true` |
| `sTimestampToT` | `s_timestamp_to_t` |

`parse_header` accepts LF and CRLF, trims values, and keeps the last value for
each case-insensitive field name. Each `HTTP/` status line resets the header
map, so only the final response survives redirects and interim responses.
Fields after the final blank line are retained as trailers. Optional status
outputs remain unchanged if there is no status line. Unlike cpr, a new status
without a reason clears an earlier reason, spaces and tabs between status
tokens are handled consistently, and colons in a reason never create header
fields. Malformed lines without a colon are ignored; this is not an HTTP
syntax validator.

`parse_cookies` borrows a `curl_slist const*`; null gives an empty collection.
It copies Netscape-format records in order, preserves duplicate names and
domain text (including `#HttpOnly_`), and retains cpr's default `encode = true`.
Missing columns are padded and columns after the seventh are ignored.
Missing or invalid expiration numbers throw `std::invalid_argument`; values
outside `time_t` throw `std::out_of_range`. Expirations must also fit
`std::chrono::system_clock::time_point`. The caller still owns the raw list.

`split` preserves leading and interior empty fields, omits the empty field
after a final delimiter, and returns no fields for empty input, matching cpr's
`std::getline` loop. Embedded null bytes are supported, including as delimiters.
`is_true` accepts only ASCII case variations of `true`, without trimming;
its comparison is locale independent and handles high-bit bytes safely.
`s_timestamp_to_t` accepts a decimal prefix with leading whitespace and a sign,
matching cpr's standard string-to-integer conversions, and checks the platform's
full `time_t` range. Unix cookie timestamps are in **seconds**, correcting the
reference header's milliseconds comment.

The callback adapters borrow their buffers and callback objects. Read callbacks
may reduce the byte count; a successful zero-byte read means EOF, while false
returns `CURL_READFUNC_ABORT`. Header, body, and SSE consumers return the full
chunk size on true and zero on false. SSE parser state persists across chunks.
String and file writes preserve binary bytes; open files in binary mode.
Unlike cpr, `write_file_function` returns zero when the stream reports failure.
Callers must still check errors discovered during flushing or closing.
Progress callbacks return zero to continue or one to abort, including when
instantiated for `CancellationCallback`. The current curl dependency supplies
`CURL_PROGRESSFUNC_CONTINUE`, so no legacy version branch is needed.
Debug callbacks receive borrowed views and always return zero.

As in cpr, exceptions propagate from direct adapter invocations. Consumers
registered with curl must not throw across its C boundary; this module does not
yet provide a session-level exception capture mechanism. Nonnull callback and
destination pointers must remain valid throughout invocation, and buffer sizes
must fit the corresponding size types.

`url_encode` and `url_decode` return `SecureString` through a temporary
`CurlHolder`. They inherit its length checks, binary-safe decoding, and empty
result on curl conversion failure. Plus signs stay literal on decoding.
For repeated conversions, reuse a holder and call `UrlEncode` / `UrlDecode`.
Curl global initialization and cleanup remain the caller's responsibility.

Run `mcpp build` and `mcpp test`. The utility tests cover cpr's parsing and URL
examples, response transitions, cookie ownership, timestamp limits, callback
byte counts and cancellation, SSE chunking, and binary file writes without
requiring a network service.
