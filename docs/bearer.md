# Bearer

Import `mcr` or `mcr.auth` to use `mcr::options::Bearer`, following cpr's
`include/cpr/bearer.h`. Derived classes that explicitly name the secure string type
used by protected token storage must also import `mcr.secure_string`.

```cpp
import std;
import mcr.auth;

mcr::options::Bearer token{ "the_token" };
mcr::options::Bearer from_view = std::string_view{ "another_token" };
auto borrowed = token.GetToken(); // Points to "the_token", without a prefix.
```

The class is conditionally exported when the module is built with
`LIBCURL_VERSION_NUM >= 0x073D00` (7.61.0), the version introducing
`CURLAUTH_BEARER`. It uses `<curl/curlver.h>` in the global module fragment.
Older headers produce a module without `Bearer`. Importing the module does
not export the version macro or reevaluate the condition in the consumer.

The non-explicit constructor copies exactly the bytes of a `std::string_view`
into `utils::SecureString`. Views need not be null-terminated, and their backing
storage need not outlive the object. There is no default constructor; use an
empty view to store an empty token. Whitespace, UTF-8, colons, and embedded nulls
are preserved without normalization or validation. No `Bearer ` prefix, URL
encoding, or Base64 encoding is added.

`GetToken() const noexcept` is virtual. Its base implementation returns a
borrowed `char const*` into the owned, null-terminated string. C-string consumers
such as curl stop at the first embedded null. Assignment, moving, derived
mutation, and destruction can invalidate a borrowed pointer.

Copy construction and assignment create independent token values. Move
construction and assignment are explicitly defaulted and `noexcept`, preserving
cpr's move support despite the virtual destructor. Moved-from base objects
remain valid to query or assign to, with unspecified token contents. The virtual
`noexcept` destructor supports deletion of derived objects through `Bearer*`.

Derived classes can update protected `m_token_string` and override `GetToken()`
with a nonthrowing implementation. This member replaces cpr's `token_string_`
using the project's naming convention. The C++23 module, namespace, direct
secure-string dependency, and protected member name are the intentional API
differences; token and polymorphic behavior are preserved.

The secure allocator wipes released heap allocations. Small-string inline
storage, source buffers, and external copies have the limitations described
in [secure_string.md](secure_string.md). Constructing and querying this wrapper
does not call curl or require curl initialization.

cpr's `Session::SetBearer` selects `CURLAUTH_BEARER` through `CURLOPT_HTTPAUTH`
and supplies `GetToken()` to `CURLOPT_XOAUTH2_BEARER`. Session integration is not
yet implemented here. Run `mcpp build` and `mcpp test` to verify bounded views,
empty and binary tokens, copy/move ownership, protected access, virtual dispatch,
and derived destruction. These tests use synthetic tokens without HTTP requests.
