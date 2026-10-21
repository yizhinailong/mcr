# 请求配置选项

[文档索引](../README.md) · [项目首页](../../README.md)

`src/options/` 中的公开类型、枚举、函数和常量统一使用 `mcr::options` 命名空间。
TLS 选项标签位于 `mcr::options::ssl`，通过 `mcr::options::Ssl(...)` 组合。

目录按职责分成六个模块分区；`import mcr;` 导出全部选项。

| 文件（位于 `src/options/`） | 分区 | 主要类型 |
| --- | --- | --- |
| `auth.cppm` | `mcr:auth` | `AuthMode`、`Authentication`、`Bearer` |
| `proxy.cppm` | `mcr:proxy` | `Proxies`、`EncodedAuthentication`、`ProxyAuthentication` |
| `http.cppm` | `mcr:http` | `HttpVersion`、`AcceptEncoding`、`Redirect`、`Range`、`MultiRange` 及相关枚举、常量和函数 |
| `transfer_options.cppm` | `mcr:transfer_options` | 超时、速率、端口、Unix socket、DNS 覆盖、诊断和容量选项 |
| `interface.cppm` | `mcr:interface` | `Interface` |
| `ssl_options.cppm` | `mcr:ssl_options` | `VerifySsl`、`SslOptions`、`Ssl` 及 `ssl` 中的配置标签 |

```cpp
import std;
import mcr;

auto response = mcr::Get(
    mcr::Url{ "http://127.0.0.1:8080/hello" },
    mcr::options::Verbose{ true },
    mcr::options::Timeout{ std::chrono::seconds{ 5 } }
).value();

auto session_owner = mcr::Session::Create().value();
auto& session = *session_owner;
session.SetSslOptions(mcr::options::Ssl(
    mcr::options::ssl::CaInfo{ "test-root.pem" },
    mcr::options::ssl::TLSv1_2{}
)).value();
```

常用的小型选项集中在 `src/options/transfer_options.cppm` 的 `mcr:transfer_options`
分区，仅依赖标准库，由 `import mcr;` 统一导出。

| 用途 | 类型（位于 `mcr::options`） |
| --- | --- |
| 超时 | `Timeout`、`ConnectTimeout` |
| 传输速率 | `LowSpeed`、`LimitRate` |
| 连接选择 | `LocalPort`、`LocalPortRange`、`UnixSocket`、`Resolve` |
| 诊断与容量 | `Verbose`、`ReserveSize` |

`UnixSocket` 受 curl 构建能力限制，未编译该功能的后端通过 `NOT_BUILT_IN` 报告能力缺失。
传入空路径可清除 Unix socket 配置并恢复 TCP；不支持该功能的后端也接受清除操作。

这些类型原有的独立模块已合并到 `mcr:transfer_options` 分区。原先单独导入
`mcr.unix_socket`、`mcr.verbose`、`mcr.timeout`、`mcr.connect_timeout`、
`mcr.low_speed`、`mcr.limit_rate`、`mcr.local_port`、`mcr.local_port_range`、
`mcr.resolve` 或 `mcr.reserve_size` 的代码，统一改为 `import mcr;`。
合并保留类型名称；当前的错误返回规则见 [expected 接口迁移](../expected_migration.md)。

其他已合并模块按下表迁移；同组的多条导入合并为一条即可。

| 原模块 | 当前导入 |
| --- | --- |
| `mcr.bearer` | `import mcr;` |
| `mcr.proxies`、`mcr.proxy_auth` | `import mcr;` |
| `mcr.http_version`、`mcr.accept_encoding`、`mcr.redirect`、`mcr.range` | `import mcr;` |

`mcr:auth` 现在同时导出用户名密码认证和 Bearer 令牌。表中的旧模块不再提供独立文件。
选项以 `mcpp.toml` 声明的 curl 依赖为基准，`Bearer` 和全部 HTTP 版本枚举直接导出，
不为旧版 curl 保留条件分支。
安全字符串类型由 `mcr:secure_string` 分区实现，通过 `import mcr;` 使用；选项分区仅在内部依赖它。

迁移现有调用时，将 `mcr::Verbose`、`mcr::Timeout` 等选项类型改为
`mcr::options::Verbose`、`mcr::options::Timeout`；将 `mcr::Ssl` 和 `mcr::ssl`
分别改为 `mcr::options::Ssl` 和 `mcr::options::ssl`。旧命名空间不提供兼容别名。
重定向辅助函数和编码名称映射也分别改为 `mcr::options::any` 和
`mcr::options::ACCEPT_ENCODING_METHODS_STRING_MAP`。
`Session`、`Response`、`Url`、`Header`、请求正文及回调等其他类型继续使用 `mcr`。
