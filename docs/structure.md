# 源码目录

`src/` 按职责组织，根目录保留库入口和请求 API。基础类型与错误处理位于 `src/utils/`。

| 位置 | 文件 | 职责 |
| --- | --- | --- |
| `src/` | `mcr.cppm`、`api.cppm` | 库入口和一次性请求 API |
| `src/request/` | `body.cppm`、`buffer.cppm`、`file.cppm`、`multipart.cppm`、`fields.cppm` | 请求正文、上传数据、查询参数和表单 |
| `src/http/` | `response.cppm`、`cookies.cppm`、`cert_info.cppm`、`status_code.cppm`、`sse.cppm` | 响应、HTTP 元数据和事件流 |
| `src/session/` | `session.cppm`、`async.cppm`、`callback.cppm`、`connection_pool.cppm` | 会话、异步运行时、回调、连接共享和批量执行 |
| `src/options/` | `auth.cppm`、`proxy.cppm`、`http.cppm`、`transfer_options.cppm`、`interface.cppm`、`ssl_options.cppm` | 请求配置，详见[选项说明](options.md) |
| `src/curl/` | `curlholder.cppm`、`curlmultiholder.cppm`、`curl_container.cppm`、`ssl_ctx.cppm` | curl 句柄、容器编码和 TLS 后端支持，详见[curl 模块](curl.md) |
| `src/utils/` | `types.cppm`、`error.cppm`、`secure_string.cppm`、`singleton.cppm`、`threadpool.cppm`、`async_wrapper.cppm`、`util.cppm` | 公共类型、错误结果、通用工具及 HTTP/curl 辅助函数，详见[工具模块](utils.md) |

库代码直接在 `.cppm` 中实现。`session.cppm` 包含会话、TLS、拦截器和批量执行的
声明及实现；`ssl_ctx.cppm` 包含 SSL 上下文回调的完整实现。

`Body` 和 `BodyView` 合并在 `mcr.body`；`Parameters` 和 `Payload` 合并在
`mcr.fields`。类型名称、所有权语义和序列化行为保持不变。

| 原导入 | 当前导入 |
| --- | --- |
| `mcr.body_view` | `import mcr.body;` |
| `mcr.parameters`、`mcr.payload` | `import mcr.fields;` |
| `mcr.interceptor`、`mcr.multiperform` | `import mcr.session;` |

以上旧模块不再提供独立文件。同组的多条导入可以合并成一条。
文件路径、模块名和命名空间分别管理：

```cpp
import mcr.body;       // src/request/body.cppm
import mcr.fields;     // src/request/fields.cppm
import mcr.response;   // src/http/response.cppm
import mcr.session;    // src/session/session.cppm
import mcr.http;       // src/options/http.cppm：HTTP 配置选项
import mcr.types;      // src/utils/types.cppm
import mcr.error;      // src/utils/error.cppm
```

`import mcr;` 继续导出完整公共接口。公共基础类型、错误结果、请求数据、响应和会话类型使用 `mcr`，
状态码使用 `mcr::status`，配置选项使用 `mcr::options`，工具使用 `mcr::utils`，
curl 后端接口使用 `mcr::curl`。

从仓库根目录运行 `mcpp test` 可构建并验证整理后的模块。
