# expected 接口迁移

[文档索引](README.md) · [错误处理](error.md) · [项目首页](../README.md)

mcr 使用 `Result<T> = std::expected<T, Error>` 报告可恢复的请求配置、资源初始化、
文件操作及任务提交失败。以下接口变化是相对于原接口及 cpr 的有意差异，调用方需要同步调整。

## 创建对象

构造函数无法返回 expected，因此可能报告操作失败的创建入口改为工厂：

| 原入口 | 当前入口及返回类型 |
| --- | --- |
| `Body(File)` | `Body::FromFile(file) -> Result<Body>` |
| `Buffer(begin, end, filename)` | `Buffer::Create(begin, end, filename) -> Result<Buffer>` |
| `CurlHolder()` | `curl::CurlHolder::Create() -> Result<curl::CurlHolder>` |
| `CurlMultiHolder()` | `curl::CurlMultiHolder::Create() -> Result<curl::CurlMultiHolder>` |
| `ConnectionPool()` | `ConnectionPool::Create() -> Result<ConnectionPool>` |
| `Session()` | `Session::Create() -> Result<std::shared_ptr<Session>>` |
| `AcceptEncoding({枚举...})` | `options::AcceptEncoding::Create({枚举...}) -> Result<options::AcceptEncoding>` |
| `EncodedAuthentication(username, password)` | `options::EncodedAuthentication::Create(username, password) -> Result<options::EncodedAuthentication>` |
| `Response(holder, body, headers, ...)` | `Response::FromCurl(holder, body, headers, ...) -> Result<Response>` |

`Body` 的内存构造、`Response` 的空响应构造，以及 AcceptEncoding 的默认构造和自定义字符串构造仍可使用。
Session 的工厂提供共享所有权，直接支持异步方法；不能再默认构造栈上的 Session。
MultiPerform 默认创建空批次，在执行时初始化 multi 句柄。

```cpp
auto body = mcr::Body::FromFile(mcr::File{ "request.bin" });
if (!body) {
    std::println("读取失败：{}", body.error().message);
} else {
    auto response = mcr::Post(mcr::Url{ "http://127.0.0.1:8080/echo" }, std::move(*body));
    // 检查 response，再读取响应或错误。
}
```

## 返回值与错误传播

| 操作 | 当前返回类型 |
| --- | --- |
| Session 配置、准备、注册和移除 | `Result<void>` |
| 同步请求、下载、Complete、单请求拦截器 | `Result<Response>` |
| 同步批量请求、批量拦截器 | `Result<std::vector<Response>>` |
| 单请求 `*Async` | `Result<AsyncResponse>`，其中 `AsyncResponse = utils::AsyncWrapper<Result<Response>>` |
| `Multi*Async` | `Result<std::vector<utils::AsyncWrapper<Result<Response>, true>>>` |
| HTTP `*Coro` | `Task<Result<Response>>` |
| `*Callback` | `Result<AsyncWrapper<回调返回类型, ...>>`；回调接收 `Result<Response>` |
| `async` | `Result<utils::AsyncWrapper<T, cancellable>>` |
| `ThreadPool::Submit` | `Result<std::future<T>>` |
| `Async::Startup`、`Coro::Startup`、`Coro::Cleanup` | `Result<void>` |
| `Timeout::Milliseconds` | `Result<long>` |
| `AcceptEncoding::Disabled` | `Result<bool>` |
| URL 编解码 | `Result<utils::SecureString>` |
| CurlContainer::GetContent、Cookies::GetEncoded、Session::GetFullRequestUrl | `Result<std::string>` |
| `utils::s_timestamp_to_t`、`utils::parse_cookies` | `Result<std::time_t>`、`Result<Cookies>` |
| `MultiPerform::GetSessions()` 的可变重载 | `Result<std::reference_wrapper<Sessions>>` |
| Session::GetDownloadFileLength、GetSharedPtrFromThis | `Result<CprOffT>`、`Result<std::shared_ptr<Session>>` |

每个配置步骤检查结果后才继续。失败的配置可能已应用前面的选项；修正后应重新设置完整配置。
批量配置失败在启动传输前返回；运行中的批量失败仍会解除已经挂载的 easy 句柄。
异步接口的外层结果表示提交是否成功，future 中的结果表示请求操作是否成功。
完成回调会收到成功或失败的请求结果，仍支持引用、void 和不可复制的返回值。

```cpp
auto created = mcr::Session::Create();
if (created) {
    auto& session = **created;
    auto configured = session.SetUrl(mcr::Url{ "http://127.0.0.1:8080/hello" });
    if (configured) {
        auto response = session.Get();
        if (!response) {
            std::println("请求操作失败：{}", response.error().message);
        }
    }
}
```

## 错误含义与边界

- `BAD_FUNCTION_ARGUMENT`：无效选项、反向缓冲区范围、超时或时间戳越界等。
- `FAILED_INIT`：curl 资源创建失败、运行时已关闭或线程池拒绝提交。
- `FILE_COULDNT_READ_FILE`、`READ_ERROR`、`WRITE_ERROR`：文件打开、读取或下载写入失败。
- `OUT_OF_MEMORY`：curl URL 编解码分配失败；不再用空字符串表示失败。
- `RECURSIVE_API_CALL`：Session、批次或协程运行时拒绝在当前状态下执行操作。
- 其他 curl 配置错误保留 `error_code_from_curl` 的映射；已移除的 TLS false start 返回 `NOT_BUILT_IN`。

`Result<Response>` 成功表示取得响应快照，传输状态继续保存在 `Response::error`，
HTTP 状态保存在 `Response::status_code`。网络错误仍可携带部分正文、响应头和计时信息；
HTTP 4xx/5xx 仍是普通响应。详见[错误处理](error.md)。

这不是关闭 C++ 异常支持：标准库分配和线程创建失败、用户回调异常继续传播。
通用 Task、AsyncWrapper 的无效状态操作、线程池配置与自身等待、Singleton 生命周期误用，
以及容器越界和 `Response::Json()` 保留原异常语义；JSON 解析可以使用已有的 `TryJson()`。
不要在失败结果上调用 `.value()`；先检查结果，再使用 `*result`、`result->member` 或 `result.error()`。
