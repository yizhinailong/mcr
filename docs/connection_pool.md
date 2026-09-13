# ConnectionPool

Import `mcr` or `mcr.connection_pool` to use `mcr::ConnectionPool`.
The implementation follows cpr's `include/cpr/connection_pool.h` and
`cpr/connection_pool.cpp`. It creates a `CURLSH` handle and enables
`CURL_LOCK_DATA_CONNECT` and `CURL_LOCK_DATA_SSL_SESSION` to share reusable
connections and TLS sessions. It does not enable cookie or DNS sharing.

```cpp
#include <curl/curl.h>

import std;
import mcr;

// After successful curl_global_init(), and before curl_global_cleanup():
{
    mcr::ConnectionPool pool;
    mcr::curl::CurlHolder first;
    mcr::curl::CurlHolder second;
    pool.SetupHandler(first.handle);
    pool.SetupHandler(second.handle);
    // Configure and perform the requests sequentially through libcurl.
    // Easy handles are destroyed before the pool at the end of this scope.
}
```

`SetupHandler(CURL*) const` attaches an idle easy handle using `CURLOPT_SHARE`.
It does not perform requests or take ownership of the easy handle. Copy
construction shares the same caches and lock storage; copy assignment remains
deleted as in cpr. There is no consuming move operation: constructing from an
rvalue uses the copy constructor, leaving the source usable. Assignment from
an rvalue is also unavailable.

Keep at least one pool copy alive until every attached easy handle has been
cleaned up or explicitly detached using
`curl_easy_setopt(easy, CURLOPT_SHARE, static_cast<CURLSH*>(nullptr))`.
An easy handle does not retain ownership of the C++ pool. The final copy disables
the lock and unlock callbacks and calls `curl_share_cleanup()` before releasing
the mutex storage. Libcurl refuses cleanup while easy handles are still attached;
destroying the final copy too early violates this lifetime requirement.

Pool use must be serialized across threads. Libcurl explicitly does not support
sharing connection caches between concurrent threads, even with lock callbacks.
Use separate pools for concurrent workers, or use one multi handle to drive
concurrent transfers on a single thread. This restriction comes from
`CURLSHOPT_SHARE` for `CURL_LOCK_DATA_CONNECT`; cpr's async example does not remove
it. The caller also manages curl's process-wide initialization and cleanup.

Intentional differences and fixes:

- Locks are indexed by curl data type, following `CURLSHOPT_LOCKFUNC` and
  `CURLSHOPT_UNLOCKFUNC`, instead of cpr's single mutex for all shared data.
  Callbacks cannot propagate C++ exceptions through libcurl.
- A failed `curl_share_init()` or `curl_share_setopt()` throws
  `std::runtime_error` identifying the operation; partially created state is
  released automatically. Unsupported TLS session sharing is reported as a
  configuration error. C++ allocation failures propagate `std::bad_alloc`.
- `SetupHandler(nullptr)` throws `std::invalid_argument`; a failed
  `CURLOPT_SHARE` throws `std::runtime_error`. cpr ignores these curl errors.
- Shared lock storage is allocated before initializing curl, and curl ownership
  is established before configuring any options, covering construction failures.
- Private members use the project's `m_` naming convention. Public API names
  remain unchanged.

Run `mcpp build` and `mcpp test`. The standalone test adapts cpr's sequential
connection reuse test with an HTTP/1.1 server bound to an ephemeral loopback
port. Three independent requests open three connections; four requests using
pool copies open only one. It also checks copy lifetime, easy-handle replacement
and detachment, null input, and curl allocation failures during initialization
and TLS cache configuration. Memory callbacks verify that resources are freed.
The test does not perform TLS handshakes or unsupported concurrent pool transfers.
