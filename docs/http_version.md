# HttpVersion：HTTP 协议策略

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.http`，使用 `mcr::options::HttpVersionCode` 和 `HttpVersion`。
它们参考 cpr 的 `include/cpr/http_version.h`。

```cpp
import mcr.http;

mcr::options::HttpVersion automatic;
mcr::options::HttpVersion explicit_version{ mcr::options::HttpVersionCode::VERSION_1_1 };
automatic.code = mcr::options::HttpVersionCode::VERSION_1_0;
```

`HttpVersionCode` 为底层类型是 `std::uint8_t` 的作用域枚举。
`HttpVersion` 的公开 `code` 字段默认为 `VERSION_NONE`，也可通过显式构造函数传入枚举。
构造支持常量求值且不抛异常；即使是未命名枚举值也原样保存，由应用选项的代码验证。
复制、移动和赋值保留独立数值。

全部策略按 `mcpp.toml` 声明的 curl 依赖导出，不为旧头文件保留兼容分支：

| 枚举值 | 序号 | 对应的 curl 常量 |
| --- | --- | --- |
| `VERSION_NONE` | 0 | `CURL_HTTP_VERSION_NONE` |
| `VERSION_1_0` | 1 | `CURL_HTTP_VERSION_1_0` |
| `VERSION_1_1` | 2 | `CURL_HTTP_VERSION_1_1` |
| `VERSION_2_0` | 3 | `CURL_HTTP_VERSION_2_0` |
| `VERSION_2_0_TLS` | 4 | `CURL_HTTP_VERSION_2TLS` |
| `VERSION_2_0_PRIOR_KNOWLEDGE` | 5 | `CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE` |
| `VERSION_3_0` | 6 | `CURL_HTTP_VERSION_3` |
| `VERSION_3_0_ONLY` | 7 | `CURL_HTTP_VERSION_3ONLY` |

序号沿用 cpr，不是 libcurl 的原始常量；curl 的 HTTP/3 策略值为 30 和 31。
不可直接将 `code` 强制转换后传给 `CURLOPT_HTTP_VERSION`。
`Session::SetHttpVersion` 负责映射，并拒绝未命名枚举值。

`VERSION_2_0` 尝试 HTTP/2，允许回退到 HTTP/1.1；`VERSION_2_0_TLS` 仅在 HTTPS 下尝试 HTTP/2，明文 HTTP 使用 HTTP/1.1。
预先获知模式在明文连接上直接使用 HTTP/2，不执行 HTTP/1.1 Upgrade；当前依赖的 HTTPS ALPN 仅提供 HTTP/2。
`VERSION_3_0` 允许回退到更早协议，`VERSION_3_0_ONLY` 不允许。libcurl 可能优先复用已有连接。

枚举存在不代表所链接的 curl 支持相应协议，实际能力取决于 curl 的构建配置。
与 cpr 的区别是模块、命名空间、Doxygen 注释、显式构造函数的 `constexpr` / `noexcept`，以及无条件导出的枚举。

运行 `mcpp build` 和 `mcpp test`。测试覆盖全部策略、序号、显式构造、默认状态、
常量求值、字段独立修改和未命名枚举值，不实际协商 HTTP 协议。
