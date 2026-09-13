# Authentication

Import `mcr` or `mcr.auth` to use `AuthMode` and `Authentication`.
They follow cpr's `include/cpr/auth.h` and `cpr/auth.cpp`.

```cpp
import mcr.auth;

mcr::options::Authentication auth{ "user", "password", mcr::options::AuthMode::BASIC };
auto mode = auth.GetAuthMode();
auto credentials = auth.GetAuthString(); // Borrowed pointer to "user:password".
```

`AuthMode` is a scoped `std::uint8_t` enum with cpr's ordinal values:

| Mode | Ordinal | Corresponding curl policy |
| --- | --- | --- |
| `BASIC` | 0 | `CURLAUTH_BASIC` |
| `DIGEST` | 1 | `CURLAUTH_DIGEST` |
| `NTLM` | 2 | `CURLAUTH_NTLM` |
| `NEGOTIATE` | 3 | `CURLAUTH_NEGOTIATE` |
| `ANY` | 4 | `CURLAUTH_ANY` |
| `ANYSAFE` | 5 | `CURLAUTH_ANYSAFE` |

These ordinals are not curl's authentication bitmask values. `ANY` lets curl
choose among supported authentication methods; `ANYSAFE` excludes Basic from
that choice. Actual support depends on the linked curl build and server.

`Authentication` requires a username view, password view, and mode; there is
no default constructor or default mode. It copies exactly the view lengths
into owned `utils::SecureString` storage, inserting one colon. Both empty views
produce `":"`; an empty username produces `":password"`; an empty password
produces `"username:"`. Views need not be null-terminated. No URL encoding,
Base64 encoding, escaping, trimming, or mode validation occurs.

`GetAuthString() const noexcept` returns a borrowed `char const*` into that
storage. `GetAuthMode() const noexcept` returns the stored enum. Input strings
may be changed or destroyed after construction. Copies own independent
credential strings and retain the mode; moves transfer the string using its
normal move semantics. Assignment, moving, and destruction can invalidate
previously borrowed credential pointers. A moved-from object remains valid
to query or assign to, with unspecified credential contents.

Colons and embedded nulls are preserved in storage, as in cpr. When the pointer
is supplied to `CURLOPT_USERPWD`, curl treats the first colon as the username
separator and a null byte as the end of the string. Usernames containing a
colon therefore cannot be represented through that curl option. Password
colons remain in the password. The wrapper does not validate these inputs.

The module directly imports `mcr.secure_string`, the part of `mcr.util` needed
for credential storage, and makes no curl calls. `SecureAllocator` wipes heap
storage when it is deallocated; small-string inline storage, source buffers,
and external copies retain the limitations described in [secure_string.md](secure_string.md).

Intentional differences from cpr are the C++23 module, namespace `mcr::options`, and
private `m_auth_string` / `m_auth_mode` names. Construction uses checked string
operations without cpr's unchecked combined-length arithmetic for `reserve`.
Public signatures and credential formatting are preserved.

`Session::SetAuth` maps the mode to `CURLOPT_HTTPAUTH` and supplies the
credential pointer to `CURLOPT_USERPWD`, following cpr. Run `mcpp build` and
`mcpp test` to verify all modes, empty
inputs, bounded views, UTF-8 and binary bytes, and copy/move ownership. The
tests use synthetic credentials and do not perform authentication requests.
