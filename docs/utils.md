# 基础工具命名空间

[文档索引](README.md) · [项目首页](../README.md)

`src/utils/` 集中放置基础类型、错误处理和通用工具。通用工具使用 `mcr::utils`；
`types.cppm`、`error.cppm` 中的公共接口继续使用 `mcr`。
各模块保持现有导入名。`import mcr;` 统一导出全部工具及公共基础类型、错误结果和异步结果类型，
也可以按需单独导入对应模块。

| 模块导入名 | 主要接口 |
| --- | --- |
| `mcr.types` | `mcr::Url`、`mcr::UserAgent`、`mcr::Header`、`mcr::StringHolder<T>` 及 curl 兼容类型 |
| `mcr.error` | `mcr::ErrorCode`、`mcr::Error`、`mcr::Result<T>` 及错误转换函数 |
| `mcr.secure_string` | `mcr::utils::SecureAllocator<T>`、`mcr::utils::SecureString` |
| `mcr.singleton` | `mcr::utils::Singleton<T>` |
| `mcr.threadpool` | `mcr::utils::ThreadPool` 和 `DEFAULT_THREAD_POOL_*` 常量 |
| `mcr.async_wrapper` | `mcr::utils::AsyncWrapper<T>`、`mcr::utils::CancellationResult` |
| `mcr.util` | `mcr::utils::parse_header`、URL 编解码和 curl 回调适配函数 |

```cpp
import std;
import mcr;

mcr::utils::ThreadPool pool{ 1, 2 };
auto result = mcr::utils::AsyncWrapper{ pool.Submit([] { return 42; }).value() };
std::println("{}", result.Get());

mcr::utils::SecureString token{ "example-token" };
std::filesystem::path destination{ "response.bin" };
auto encoded = mcr::utils::url_encode("hello world").value();
```

迁移调用时，将原 `mcr::util` 下的名称改为 `mcr::utils`，将原 `mcr` 下的
`ThreadPool`、`Singleton`、`AsyncWrapper`、`CancellationResult`、
`DEFAULT_THREAD_POOL_*` 移至 `mcr::utils`。旧命名空间不提供兼容别名。

文件系统相关代码通过 `import std;` 直接使用 `std::filesystem`。

库级异步入口继续使用 `mcr::async`、`mcr::Async` 和 `mcr::GlobalThreadPool`。
`mcr::AsyncResponse` 是 `mcr::utils::AsyncWrapper<mcr::Response>` 的别名。
