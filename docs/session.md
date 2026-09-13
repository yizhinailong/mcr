# Session、TLS、拦截器和批量请求

参考本地 cpr 的 `include/cpr/session.h`、`ssl_options.h`、`proxyauth.h`、
`interceptor.h`、`multiperform.h` 及对应实现和测试，将请求流程接入 C++23 模块。

```cpp
import std;
import mcr;

auto main() -> int {
    mcr::Session session;
    session.SetUrl(mcr::Url{ "http://127.0.0.1:8080/echo" });
    session.SetTimeout(mcr::options::Timeout{ std::chrono::seconds{ 5 } });
    session.SetBody(mcr::Body{ "hello" });
    auto response = session.Post();
    if (response.error) {
        std::println("{}", response.error.message);
        return 1;
    }
    std::println("{}: {}", response.status_code, response.text);
}
```

实现范围：

- `Get`、`Head`、`Post`、`Put`、`Patch`、`Delete`、`Options`，以及对应的
  `Prepare*`、`*Async` 和 `*Callback` 方法。
- 向 `WriteCallback` 或 `std::ofstream` 下载，同步/异步下载，
  `GetDownloadFileLength` 和供外部 curl 驱动使用的 `Complete` / `CompleteDownload`。
- 现有选项模块的 setter 和 `SetOption` 重载：URL、参数、请求头、认证、Cookie、
  Body / BodyView / JsonBody / Payload / Multipart、超时、重定向、压缩、限速、网络绑定、
  DNS 映射、代理地址、连接池、读写/进度/调试/SSE 回调。
- `Response` 的正文、最终响应头、原始响应头、Cookie、错误、URL、计时、
  字节数、连接地址和证书信息。
- `ProxyAuthentication`、`VerifySsl` / `SslOptions`、单请求及批量拦截器、
  `MultiPerform` 并发请求和下载。

TLS 和代理认证：

```cpp
mcr::Session session;
session.SetUrl(mcr::Url{ "https://localhost:8443/hello" });
session.SetSslOptions(mcr::options::Ssl(
    mcr::options::ssl::CaInfo{ "test-root.pem" },
    mcr::options::ssl::TLSv1_2{},
    mcr::options::ssl::MaxTLSv1_3{}
));
session.SetProxies(mcr::options::Proxies{ { "https", "http://127.0.0.1:8080" } });
session.SetProxyAuth(mcr::options::ProxyAuthentication{
    { "https", mcr::options::EncodedAuthentication{ "proxy-user", "proxy-password" } }
});
auto response = session.Get();
```

- 默认启用证书链和主机名校验。`SetVerifySsl` 同时切换两项；
  `mcr::options::ssl::VerifyPeer` / `mcr::options::ssl::VerifyHost` 可分别配置。
- 支持证书和私钥文件/内存数据、密码、CA 文件/目录/内存数据、公钥固定、CRL、
  OCSP 状态校验、TLS 版本上下限、ALPN、密码套件、会话缓存及吊销检查选项。
- `SetSslOptions` 替换整套配置，空字段清除旧凭据、CA blob 和公钥固定。
  `SslOptions::SetOption` 在文件和内存来源之间采用最后指定的选项。
  `CaBuffer` 与 `CaInfoBlob` 均通过 `CURLOPT_CAINFO_BLOB` 复制数据；
  证书和私钥 blob 也使用 curl 的复制标志，配置对象无需活到请求结束。
- 各 TLS 后端支持的格式和功能不同。不支持的可选功能保持默认值时被容忍，
  显式请求时由 curl 返回错误或由 setter 抛出异常。
  setter 失败可能已应用前面的选项；修正后应重新设置完整配置。
- Schannel 的 P12 文件可通过 `SslOptions::cert_file`、`cert_type = "P12"`
  和 `key_pass` 指定；内存 P12 使用 `cert_blob` 并清空 `cert_file`。
  本机 curl 8.21 / Schannel 对测试生成的 P12 返回 `SEC_E_UNKNOWN_CREDENTIALS`，
  因此本次未验证双向 TLS 成功握手。库保留该传输错误；
  curl 上游也记录了相关的 [Schannel 客户端证书限制](https://github.com/curl/curl/issues/17626)。
- 代理认证按目标 URL 的协议选择，替换映射或不再使用对应代理时清除旧凭据。
  `EncodedAuthentication` 的访问器返回百分号编码后的值；交给 curl 的独立
  用户名/密码选项前解码，确保 `$`、`@` 等字符按原始值认证。

拦截器在注册顺序上进入，并可修改选项、返回合成响应或多次调用 `Proceed` 重试。
重试只进入当前拦截器之后的链；新请求及异常后的请求从链首开始。
下载重试保留当前下载目标，但不会自动回卷上传源或下载流。

```cpp
class RequestHeader final : public mcr::Interceptor {
public:
    auto Intercept(mcr::Session& session) -> mcr::Response override {
        session.UpdateHeader(mcr::Header{ { "X-Client", "mcr" } });
        return Proceed(session);
    }
};

// session 为上面的 Session；异步请求也经过同一条拦截链。
session.AddInterceptor(std::make_shared<RequestHeader>());
```

并发批量请求：

```cpp
auto first = std::make_shared<mcr::Session>();
auto second = std::make_shared<mcr::Session>();
first->SetUrl(mcr::Url{ "http://127.0.0.1:8080/first" });
second->SetUrl(mcr::Url{ "http://127.0.0.1:8080/second" });
mcr::MultiPerform multi;
multi.AddSession(first);
multi.AddSession(second);
auto responses = multi.Get(); // 并发传输；结果仍依次对应 first、second。
multi.RemoveSession(first);  // 释放归属后，可以再次直接调用 first->Get()。
```

- `Get` 等方法为全批次选择同一 HTTP 方法；`AddSession(session, HttpMethod::...)`
  配合 `Perform()` 可为各 Session 指定不同方法。未指定方法时 `Perform()` 抛出异常。
- `Download` 接收与 Session 数量相同、顺序对应的 `WriteCallback` 或
  `std::ofstream&`（也支持 `std::ref`）。`PerformDownload` 要求方法已设为
  `DOWNLOAD_REQUEST`。空批次允许零个参数；下载与普通请求不能在同一批次混合执行。
  注册时 `UNDEFINED` 不参与混合检查，执行前检查最终方法组合。
- Session 从注册到移除或批次销毁期间归该批次使用，不能重复注册、跨批次注册或
  直接启动 Session 请求；可以在传输前修改配置。推荐用 `AddSession` / `RemoveSession`
  修改成员，通过 `GetSessions()` 修改的列表会在下一次批次操作前校验并同步归属。
- `InterceptorMulti::Intercept` 拦截整个批次，`Proceed` 重新准备并执行下游链。
  可以修改 `GetSessions()` 中的方法，再用 `PrepareDownloadSession` 指定下载目标。
  批量请求沿用 cpr 的行为，不执行各 Session 的单请求拦截器。
- 网络错误保留在对应位置的 `Response::error` 中；无效注册、curl multi 错误或
  用户回调异常会抛出。所有退出路径均解除 easy handle 的挂载，之后批次仍可复用。
  下载目标在本次调用结束时清除，再次下载必须重新提供目标。
- 仅允许移动未执行请求的批次；移动后更新 Session 归属，移出后的对象可重新使用。

生命周期与错误约定：

- 同一个 Session 的配置与请求必须串行使用。异步请求需要
  `std::make_shared<mcr::Session>()`；任务持有 Session 直到执行结束。
- Body、JsonBody 和 Payload 拥有数据；BodyView 和 Multipart 的 Buffer 借用数据。
  借用的数据、回调捕获对象、下载流和 ConnectionPool 必须覆盖使用它们的生命周期。
- Content 会跨请求保留，调用 `RemoveContent()` 清除。
- `SetJsonBody` 保存序列化 JSON，发送时仅在缺少显式 Content-Type 的情况下补齐
  `application/json`；响应提供 `Json()` / `TryJson()`，详见 [JSON 支持](json.md)。
  ReadCallback 独立保留，可通过 `SetReadCallback({})` 清除。
- `SetCancellationParam()` 使用共享原子标志，设置为 true 会终止传输。
  `AsyncResponse` 本身沿用不可取消的默认 AsyncWrapper 类型。
- curl 配置失败抛出异常；传输失败写入 `Response::error`，HTTP 4xx/5xx 保留为普通响应。
  用户回调的异常在 curl 返回之后重新抛出，异步调用通过 `Get()` 取得异常。
- 与现有 CurlHolder 一致，调用方负责需要显式管理的 curl 全局初始化/清理；
  Session 不执行进程级清理。库中其他 curl 使用者尚未结束时不能清理 curl。

有意区别于 cpr：

- Session、MultiPerform 及两种拦截器使用 `mcr` 命名空间；传输配置使用 `mcr::options`，
  TLS 选项标签使用 `mcr::options::ssl`，详见[选项命名空间](options.md)。为避免模块循环，这些会话和拦截器类型
  同属 `mcr.session`，统一通过 `import mcr.session;` 使用。会话、TLS、拦截器和批量执行
  均直接实现在 `src/session/session.cppm` 中。
  总模块 `mcr` 也导出 `mcr.ssl_options` 和 `mcr.proxy`。
- 公共方法统一为 `Intercept` / `Proceed` 和 `ProxyAuthentication::Has`。
  TLS 替换、blob 复制、代理凭据解码、批次归属检查和异常恢复采用上述行为。
- 当前 curl 8.21 依赖不再支持的 SSLv2、SSLv3、NPN 不提供选项。
  保留 `SslFastStart` 类型，显式启用它固定抛出异常；不保留旧版 curl 的设置路径。
- 参数追加到已有 query，且位于 fragment 之前。
- 每次准备请求都清除上一请求的方法和 curl 内容配置，避免复用时残留；
  保留 Multipart 的 GET 仍使用 GET。HEAD 与 Download 忽略存储的 Content，
  之后仍可再次发送它。
- HeaderCallback 同时保留 Response 中的原始及解析后响应头。
- Download 的消费者仅作用于本次下载，后续请求恢复既有 WriteCallback / SSE / 正文缓冲。
  SetWriteCallback 与 SetServerSentEventCallback 相互替换；空回调恢复正文缓冲。
- 显式 Expect 请求头被保留；未提供时关闭 curl 的自动 100-continue。
- Multipart 文本使用显式字节长度，因此保留嵌入的空字符。SSE 解析器在每次请求前重置。
- 不忽略 MIME 文件准备错误；不存在的上传文件在准备阶段抛出异常，避免发送空文件字段。
- Response 在完成时保存所有元数据和证书，不持有活动句柄；后续请求不会改变旧响应。
  无证书或空 Response 的 GetCertInfos 安全返回空容器。
- Response 的默认移动操作沿用成员的异常说明，不强制承诺 `noexcept`。
- Session 销毁前重置 curl 选项，使外部保留的 CurlHolder 不引用已销毁的回调或正文数据。

本地验证：`mcpp build`、`mcpp test`。
`tests/test_session.cpp` 使用 `tests/fixtures/http_server.hpp` 中跨 Windows / POSIX 的回环 HTTP 服务，使用系统分配的端口，
不依赖 Python、外部网络或固定端口。覆盖连接复用、方法切换、上传下载、请求选项、
重定向、错误恢复、取消、回调异常、SSE、连接池、异步生命周期、代理认证、拦截器
重试、批次归属和下载。并发测试通过必须同时到达两个请求才能应答的服务端屏障验证。

`tests/test_ssl_options.cpp` 检查类型组合、配置替换及本地 HTTPS。
Windows 上需要 PATH 中的 PowerShell 7.5 或更高版本（.NET 9+），通过
`tests/fixtures/https_server.ps1` / `.cs` 在运行时生成临时 CA、服务端和客户端证书，
不会向系统证书库安装信任。覆盖默认拒绝不可信证书、CA 文件/内存、主机名校验、
公钥固定、TLS 1.2、证书快照、P12 错误密码及凭据清除。
仅在出现上述精确 Schannel 错误时打印 `SKIP`，保留文件与复制后 blob 的错误一致性检查；
其余环境要求双向 TLS 成功。非 Windows 当前只运行类型与 setter 检查并显式跳过 HTTPS。

无需手动管理 Session 的请求可使用 [自由函数 API](api.md)，包括同步、异步、批量和下载入口。
