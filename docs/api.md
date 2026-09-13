# 自由函数与兼容入口

对照本地 cpr 的 `api.h`、`ssl_ctx.h` / `ssl_ctx.cpp` 和
`cmake/cprver.h.in` 补齐入口。`import mcr;` 导出 HTTP API 和版本信息，也可以分别导入
`mcr.api`、`mcr.version`。SSL 上下文回调属于后端接口，需要显式导入 `mcr.ssl_ctx`。
HTTP 入口保留 cpr 的 `Get`、`Post` 等名称。

| 操作 | 返回值 | 执行方式 |
| --- | --- | --- |
| `Get` / `Post` / `Put` / `Head` / `Delete` / `Options` / `Patch` | `Response` | 临时 Session，同步请求 |
| 各方法的 `*Async` | `AsyncResponse` | 全局线程池中的独立请求 |
| 各方法的 `*Callback` | `mcr::utils::AsyncWrapper<回调返回类型, true>` | 请求完成后在线程池任务中调用 continuation |
| `MultiGet` / `MultiPost` / `MultiPut` / `MultiHead` / `MultiDelete` / `MultiOptions` / `MultiPatch` | `std::vector<Response>` | 由 MultiPerform 并发执行，结果保持参数顺序 |
| 各批量方法的 `Multi*Async` | `std::vector<mcr::utils::AsyncWrapper<Response, true>>` | 独立提交，可分别取消 |
| `Download(std::ofstream&, ...)` / `Download(WriteCallback const&, ...)` | `Response` | 同步下载，正文交给指定消费者 |
| `DownloadAsync(std::filesystem::path, ...)` | `AsyncResponse` | 在线程池中打开、下载并关闭目标文件 |

请求选项沿用 Session 的 `SetOption`。传输配置类型位于 `mcr::options`，详见[选项命名空间](options.md)：

```cpp
import std;
import mcr;

auto response = mcr::Post(
    mcr::Url{ "http://127.0.0.1:8080/echo" },
    mcr::options::Timeout{ std::chrono::seconds{ 3 } },
    mcr::Header{ { "Content-Type", "application/json" } },
    mcr::Body{ R"({"message":"hello"})" }
);

auto pending = mcr::GetAsync(mcr::Url{ "http://127.0.0.1:8080/hello" });
auto completed = pending.Get();

auto length = mcr::GetCallback(
    [](mcr::Response response) { return response.text.size(); },
    mcr::Url{ "http://127.0.0.1:8080/hello" }
);
std::println("{} bytes", length.Get());
```

- 选项按参数顺序应用。重复 Header 合并，名称忽略大小写，后面的同名值覆盖前面；
  空 Header 不清除前面参数中的字段。其他重复选项沿用对应 Session setter 的行为。
- Header 的值、左值、const 左值及 `std::ref` / `std::cref` 使用同一套合并规则，
  修正 cpr 模板只识别部分值类别的情况。
- `*Async`、`*Callback` 拷贝普通左值选项，移动右值选项，任务独立拥有这些对象。
  BodyView、Multipart Buffer、ConnectionPool、回调捕获的引用及显式 reference_wrapper
  仍借用底层对象，必须覆盖实际任务的生命周期。
- continuation 以持有的左值调用，支持不可复制的捕获、不可复制的返回值、引用及 `void`。
  请求准备异常、用户回调异常通过 future 的 `Get()` 传播；传输错误位于 `Response::error`。
- 零个选项是合法调用；例如 `Get()` 返回缺少 URL 的传输错误。
  零个批量参数返回空 vector；`MultiGet(std::tuple<>{})` 则执行一个缺少 URL 的请求。

批量接口的每个参数是一个请求的选项 tuple：

```cpp
auto first = std::tuple{
    mcr::Url{ "http://127.0.0.1:8080/first" },
    mcr::options::Timeout{ std::chrono::seconds{ 3 } }
};
auto second = std::tuple{ mcr::Url{ "http://127.0.0.1:8080/second" } };
auto responses = mcr::MultiGet(first, second);
auto tasks = mcr::MultiGetAsync(first, std::move(second));
auto result = tasks[0].Get();
(void)tasks[1].Cancel();
```

- 同步批次正确转发 tuple 的值类别，允许含不可复制选项的右值 tuple。
- 异步批次复制普通左值 tuple，移动右值 tuple；`std::tie` 或 tuple 中的引用元素
  仍然借用原对象。所有结果保持输入顺序，HTTP 4xx/5xx 和网络错误均占据原位置。
- `Multi*Async` 让任务和 Session 共享同一个原子取消标志。任务尚未启动时取消，
  不发起网络请求；传输中取消，通过 curl 进度回调终止，其他任务不受影响。
- 取消后的 wrapper 不允许 `Get` / `Wait`；`Share()` 沿用 AsyncWrapper 的基础 future
  行为。若通过基础 shared future 观察结果，启动前取消得到空 Response，传输中取消
  得到 `ABORTED_BY_CALLBACK`。持有 wrapper 到任务完成，可避免析构触发取消。
- 与 cpr 一致，单请求 `*Callback` 的取消标志只限制 wrapper 的结果访问，不会终止任务；
  普通 `*Async` 返回不可取消的 `AsyncResponse`。需要取消请求时使用 `Multi*Async`
  或显式 Session 的取消参数。

下载保留二进制数据：

```cpp
auto task = mcr::DownloadAsync(
    std::filesystem::path{ "response.bin" },
    mcr::Url{ "http://127.0.0.1:8080/binary" }
);
auto metadata = task.Get();
```

`DownloadAsync` 使用全局线程池，以 binary / trunc 模式打开目标路径，覆盖已有文件。
它在 future 就绪前检查关闭结果；文件打开或关闭失败抛出 `std::runtime_error`。
同步流下载在请求前拒绝未打开或已失败的流，调用方负责随后 flush / close 的检查。
失败的传输可能留下部分文件。与 cpr 的区别是统一使用线程池、显式处理文件错误，
并在 Windows 上避免文本模式转换字节。

curl 的全局初始化、清理和全局线程池生命周期沿用 [Session 约定](session.md) 和
[Async 约定](async.md)。显式清理 curl 前应等待所有异步任务完成；
`Async::Cleanup()` 需在任务之外调用。

文件系统及版本信息：

| cpr 入口 | mcr 对应项 |
| --- | --- |
| `cpr::fs` | 直接使用 `std::filesystem` |
| `CPR_VERSION` | `mcr::VERSION`，`std::string_view` |
| `CPR_VERSION_MAJOR` / `MINOR` / `PATCH` | `mcr::VERSION_MAJOR` / `VERSION_MINOR` / `VERSION_PATCH` |
| `CPR_VERSION_NUM` | `mcr::VERSION_NUM`，`0xAABBCC` |
| `CPR_LIBCURL_VERSION_NUM` | `mcr::CURL_VERSION_NUM`，构建时 curl 头文件版本 |

C++23 通过 `import std;` 直接使用 `std::filesystem`。模块不导出预处理宏，版本信息改为可用于
`static_assert` / `if constexpr` 的 `inline constexpr` 常量。
`build.mcpp` 从 `mcpp.toml` 的 `[package].version` 生成 `mcr.version`，输出位于
mcpp 的生成目录；修改版本会重新生成，无需维护第二份版本号。
完整版本字符串保留后缀，三段数字分别占 8 位；超出范围时构建失败。
`CURL_VERSION_NUM` 描述构建头文件，运行时动态库信息仍应通过 curl 自身接口查询。

SSL 上下文入口位于 `mcr::curl`，详见 [curl 后端命名空间](curl.md)：

```cpp
#include <curl/curl.h>
import mcr.ssl_ctx;

// ca_pem 是 NUL 结尾且在整个传输期间有效的 PEM bundle。
curl_easy_setopt(handle, CURLOPT_SSL_CTX_FUNCTION,
                 &mcr::curl::sslctx_function_load_ca_cert_from_buffer);
curl_easy_setopt(handle, CURLOPT_SSL_CTX_DATA, ca_pem.data());
```

- `mcr::curl::sslctx_function_load_ca_cert_from_buffer` 接收与本库链接的 OpenSSL 所创建的 SSL_CTX。
  先解析整份 PEM bundle，再向 trust store 添加证书；重复 CA 可重复加载。
  不抛出异常，不向标准错误流打印证书解析信息，也不取得传入指针的所有权。
- 空指针、空 bundle、无法解析的证书返回 `CURLE_ABORTED_BY_CALLBACK`；
  分配失败返回 `CURLE_OUT_OF_MEMORY`。解析到坏证书时，尚未修改 trust store。
  OpenSSL 在添加阶段发生错误时可能已加入部分证书。
- `mcr::curl::SSL_CTX_OPENSSL_ENABLED` 指示是否编译了 OpenSSL 上下文加载支持。
  manifest 与当前 compat.curl 的后端一致：Linux/macOS 直接声明 OpenSSL 3.5.1
  依赖并定义 `MCR_SSL_CTX_OPENSSL`；Windows 使用 Schannel，入口返回
  `CURLE_NOT_BUILT_IN`。不支持的后端不会假装已经装载 CA。
- 通用请求使用 `mcr::options::Ssl(mcr::options::ssl::CaBuffer{...})` 或 `mcr::options::ssl::CaInfoBlob`，由 curl 复制并管理 CA 数据，
  无需直接操作 SSL_CTX。Session 继续使用这条路径。

验证：

- `mcpp build`、`mcpp test`。
- `tests/test_api.cpp` 复用 `tests/fixtures/http_server.hpp` 的回环服务器，验证所有方法、
  Header 合并、异步所有权、结果类型、异常、真实并发、排队/传输中取消及二进制下载。
- `tests/test_version.cpp` 通过总入口检查生成常量，并与 manifest 对照。
- `tests/test_ssl_ctx.cpp` 在 OpenSSL 构建下生成短期自签 CA，检查坏证书、bundle、重复加载
  和实际 trust store 验证；Schannel 构建验证明确的不支持返回值。
  本机还使用现有 vcpkg OpenSSL 3.6.3 单独编译并通过该 OpenSSL 分支测试，
  没有改变主构建的 Schannel 后端；Linux/macOS 的完整 mcpp 构建尚未在本机执行。
