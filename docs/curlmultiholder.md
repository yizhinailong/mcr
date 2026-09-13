# CurlMultiHolder

Import `mcr.curlmultiholder` to use `mcr::curl::CurlMultiHolder`.
See [curl backend namespaces](curl.md) for the other backend interfaces and migration details.
It follows cpr's `include/cpr/curlmultiholder.h` and
`cpr/curlmultiholder.cpp`: construction calls `curl_multi_init()`, the public
`CURLM* handle` provides access to libcurl, and destruction calls
`curl_multi_cleanup()`.

```cpp
#include <curl/curl.h>

import std;
import mcr.curlmultiholder;

int main() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    int result{ 0 };
    try {
        mcr::curl::CurlMultiHolder multi;
        int running{ 0 };
        if (curl_multi_perform(multi.handle, &running) != CURLM_OK) {
            result = 1;
        }
    } catch (std::exception const&) {
        result = 1;
    }
    curl_global_cleanup();
    return result;
}
```

The holder owns only the multi handle. Easy handles added through
`curl_multi_add_handle()` retain their existing owners, such as `CurlHolder`.
Remove each easy handle with `curl_multi_remove_handle()` before cleaning it
up, destroying the multi holder, or replacing the destination of a move
assignment. This follows libcurl's cleanup order and cpr's `MultiPerform`.
Callback data must remain alive until multi cleanup finishes, since closing
cached connections can invoke socket callbacks. Destruction and replacement
must occur outside callbacks on the same multi handle. The caller manages
process-wide curl initialization and cleanup.

Intentional differences from cpr, matching the existing `CurlHolder`:

- Initialization failure throws `std::runtime_error` naming `curl_multi_init`
  instead of relying on a debug-only assertion.
- Copying is deleted to prevent shared ownership and double cleanup.
- Moving is `noexcept` and transfers the original handle, preserving options
  and attached easy handles. The source becomes null and remains safe to
  destroy or assign to. Move assignment releases the destination's old handle;
  self-move leaves it unchanged.

The raw pointer remains public for cpr compatibility. If assigning to it
directly, the caller must release the previous handle and transfer exclusive
ownership of the replacement. The holder does not add a `MultiPerform` API.

Run `mcpp build` and `mcpp test`. The standalone test checks ownership and
move behavior, empty multi operations, two queued URL failures without network
access, easy-handle reuse after removal, allocation failure, and resource
release using libcurl's custom memory callbacks.
