# 请求配置选项

`src/options/` 中的公开类型、枚举、函数和常量统一使用 `mcr::options` 命名空间。
TLS 选项标签位于 `mcr::options::ssl`，通过 `mcr::options::Ssl(...)` 组合。

```cpp
import std;
import mcr;

auto response = mcr::Get(
    mcr::Url{ "http://127.0.0.1:8080/hello" },
    mcr::options::Verbose{ true },
    mcr::options::Timeout{ std::chrono::seconds{ 5 } }
);

mcr::Session session;
session.SetSslOptions(mcr::options::Ssl(
    mcr::options::ssl::CaInfo{ "test-root.pem" },
    mcr::options::ssl::TLSv1_2{}
));
```

常用的小型选项集中在 `src/options/transfer_options.cppm`，仅依赖标准库，
通过 `import mcr.transfer_options;` 单独导入。`import mcr;` 仍导出全部选项。

| 用途 | 类型（位于 `mcr::options`） |
| --- | --- |
| 超时 | `Timeout`、`ConnectTimeout` |
| 传输速率 | `LowSpeed`、`LimitRate` |
| 连接选择 | `LocalPort`、`LocalPortRange`、`UnixSocket`、`Resolve` |
| 诊断与容量 | `Verbose`、`ReserveSize` |

这些类型原有的独立模块已合并。原先单独导入 `mcr.unix_socket`、`mcr.verbose`、
`mcr.timeout`、`mcr.connect_timeout`、`mcr.low_speed`、`mcr.limit_rate`、
`mcr.local_port`、`mcr.local_port_range`、`mcr.resolve` 或 `mcr.reserve_size` 的代码，
统一改为 `import mcr.transfer_options;`。类型名称、构造方式和行为保持不变。
认证、代理、重定向、协议选择和 TLS 等选项继续使用各自的模块，例如
`import mcr.auth;` 和 `import mcr.ssl_options;`。

迁移现有调用时，将 `mcr::Verbose`、`mcr::Timeout` 等选项类型改为
`mcr::options::Verbose`、`mcr::options::Timeout`；将 `mcr::Ssl` 和 `mcr::ssl`
分别改为 `mcr::options::Ssl` 和 `mcr::options::ssl`。旧命名空间不提供兼容别名。
重定向辅助函数和编码名称映射也分别改为 `mcr::options::any` 和
`mcr::options::ACCEPT_ENCODING_METHODS_STRING_MAP`。
`Session`、`Response`、`Url`、`Header`、请求正文及回调等其他类型继续使用 `mcr`。
