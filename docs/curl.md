# curl 后端命名空间

`src/curl/` 中的后端类型、函数和常量使用 `mcr::curl` 命名空间。
请求条目 `Parameter`、`Pair` 保留在 `mcr`，与 `mcr::curl::CurlContainer<T>`
在同一模块中导出。声明和实现直接放在各自的 `.cppm` 中，各模块保留现有导入名。

| 模块导入名 | 公开接口 |
| --- | --- |
| `mcr.curlholder` | `mcr::curl::CurlHolder`，管理 easy 句柄及关联资源 |
| `mcr.curlmultiholder` | `mcr::curl::CurlMultiHolder`，管理 multi 句柄 |
| `mcr.curl_container` | `mcr::curl::CurlContainer<T>`，提供请求容器存储和编码；请求条目为 `mcr::Parameter`、`mcr::Pair` |
| `mcr.ssl_ctx` | `mcr::curl::sslctx_function_load_ca_cert_from_buffer` 和 `mcr::curl::SSL_CTX_OPENSSL_ENABLED` |

`import mcr;` 仍导出这些接口。迁移现有调用时，为原 `mcr` 下的后端接口名称添加
`curl::`，例如 `mcr::CurlHolder` 改为 `mcr::curl::CurlHolder`。
旧命名空间不提供兼容别名。

```cpp
#include <curl/curl.h>
import mcr.curlholder;
import mcr.curlmultiholder;

auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    try {
        mcr::curl::CurlHolder easy;
        mcr::curl::CurlMultiHolder multi;
    } catch (...) {
        curl_global_cleanup();
        return 1;
    }
    curl_global_cleanup();
    return 0;
}
```

调用方继续负责 curl 的全局初始化和清理，并确保所有句柄先于全局清理销毁。
`mcr::Session::GetCurlHolder()` 现在返回 `std::shared_ptr<mcr::curl::CurlHolder>`。
SSL 上下文回调的后端限制和使用方式见 [API 文档](api.md)。
