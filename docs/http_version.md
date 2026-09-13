# HttpVersion

Import `mcr` or `mcr.http` to use `HttpVersionCode` and `HttpVersion`.
They follow cpr's `include/cpr/http_version.h`.

```cpp
import mcr.http;

mcr::options::HttpVersion automatic;
mcr::options::HttpVersion explicit_version{ mcr::options::HttpVersionCode::VERSION_1_1 };
automatic.code = mcr::options::HttpVersionCode::VERSION_1_0;
```

`HttpVersionCode` is a scoped enum with underlying type `std::uint8_t`.
`HttpVersion` defaults its public `code` field to `VERSION_NONE` and explicitly
accepts an enum value. Construction stores even unnamed enum values verbatim;
validation belongs to the code applying the option. Copying, moving, and
assignment preserve independent values. Construction supports constant
evaluation and does not throw.

The module includes `<curl/curlver.h>` in its global module fragment and uses
`LIBCURL_VERSION_NUM` to select the available enumerators at build time:

| Enumerator | Ordinal | Minimum curl | Corresponding curl option value |
| --- | --- | --- | --- |
| `VERSION_NONE` | 0 | Always available | `CURL_HTTP_VERSION_NONE` |
| `VERSION_1_0` | 1 | Always available | `CURL_HTTP_VERSION_1_0` |
| `VERSION_1_1` | 2 | Always available | `CURL_HTTP_VERSION_1_1` |
| `VERSION_2_0` | 3 | 7.33.0 | `CURL_HTTP_VERSION_2_0` |
| `VERSION_2_0_TLS` | 4 | 7.47.0 | `CURL_HTTP_VERSION_2TLS` |
| `VERSION_2_0_PRIOR_KNOWLEDGE` | 5 | 7.49.0 | `CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE` |
| `VERSION_3_0` | 6 | 7.66.0 | `CURL_HTTP_VERSION_3` |
| `VERSION_3_0_ONLY` | 7 | 7.88.0 | `CURL_HTTP_VERSION_3ONLY` |

These are cpr's contiguous ordinal values, not raw libcurl constants: curl uses
30 and 31 for its HTTP/3 policies. Do not pass a cast of `code` directly to
`CURLOPT_HTTP_VERSION`; the session layer must map the named values as cpr's
`Session::SetHttpVersion` does. This module only stores the preference; session
integration is not yet implemented.

`VERSION_2_0` attempts HTTP/2 with HTTP/1.1 fallback. `VERSION_2_0_TLS` limits
that attempt to HTTPS and uses HTTP/1.1 for cleartext HTTP. Prior knowledge mode
uses HTTP/2 directly for cleartext requests without HTTP/1.1 Upgrade; HTTPS uses
ALPN, offering only HTTP/2 since curl 8.10.0. `VERSION_3_0` permits fallback to
earlier protocols, while `VERSION_3_0_ONLY` does not. Libcurl can prioritize
reusing an existing connection over the requested version.

The presence of an enumerator does not guarantee that the linked curl backend
supports that protocol. The exported enum is fixed when this module is built;
importing it does not export preprocessor macros or reevaluate version guards
in the consuming source file.

Intentional differences from cpr are the C++23 module and namespace, Doxygen
documentation, `constexpr`/`noexcept` on the explicit constructor, and the
`VERSION_3_0_ONLY` guard. That guard uses `0x075800` (7.88.0), its first official
release according to curl's `symbols-in-versions`, instead of cpr's development
snapshot threshold `0x075701`. Protocol comments follow the local libcurl
documentation, including HTTP/3 fallback and current prior knowledge behavior.

Run `mcpp build` and `mcpp test`. The standalone test checks enum availability
against the build headers, ordinal compatibility, explicit construction,
default state, constant evaluation, independent field updates, and unnamed
codes. These are option tests and do not negotiate HTTP protocols.
