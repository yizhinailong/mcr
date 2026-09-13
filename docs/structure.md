# 源码目录

`src/` 按职责组织，根目录保留库入口、请求 API、基础类型和错误处理。

| 位置 | 文件 | 职责 |
| --- | --- | --- |
| `src/` | `mcr.cppm`、`api.cppm`、`types.cppm`、`error.cppm` | 库入口、一次性请求 API、公共类型和错误结果 |
| `src/request/` | `body.cppm`、`body_view.cppm`、`buffer.cppm`、`file.cppm`、`multipart.cppm`、`parameters.cppm`、`payload.cppm` | 请求正文、上传数据、查询参数和表单 |
| `src/http/` | `response.cppm`、`cookies.cppm`、`cert_info.cppm`、`status_code.cppm`、`sse.cppm` | 响应、HTTP 元数据和事件流 |
| `src/session/` | `session.cppm`、`async.cppm`、`callback.cppm`、`connection_pool.cppm`、`interceptor.cppm`、`multiperform.cppm` | 会话、异步运行时、回调、连接共享和批量执行 |
| `src/options/` | `auth.cppm`、`proxy.cppm`、`http.cppm`、`transfer_options.cppm`、`interface.cppm`、`ssl_options.cppm` | 请求配置，详见[选项说明](options.md) |
| `src/curl/` | `curlholder.cppm`、`curlmultiholder.cppm`、`curl_container.cppm`、`ssl_ctx.cppm`、`ssl_ctx.cpp` | curl 句柄、容器编码和 TLS 后端支持，详见[curl 模块](curl.md) |
| `src/utils/` | `secure_string.cppm`、`singleton.cppm`、`threadpool.cppm`、`async_wrapper.cppm`、`util.cppm` | 通用工具及 HTTP/curl 辅助函数，详见[工具模块](utils.md) |

`src/session/` 还包含 `session_ssl.cpp`、`session_interceptor.cpp` 和
`multiperform.cpp`。这三个实现单元都声明 `module mcr.session;`，与
`session.cppm` 一起维护。拦截器和批量执行类型也归属 `mcr.session`，
`mcr.interceptor`、`mcr.multiperform` 提供转导出入口。

文件路径、模块名和命名空间分别管理。目录归类保留了所有现有导入方式：

```cpp
import mcr.body;       // src/request/body.cppm
import mcr.response;   // src/http/response.cppm
import mcr.session;    // src/session/session.cppm
import mcr.http;       // src/options/http.cppm：HTTP 配置选项
```

`import mcr;` 继续导出完整公共接口。请求数据、响应和会话类型使用 `mcr`，
状态码使用 `mcr::status`，配置选项使用 `mcr::options`，工具使用 `mcr::utils`，
curl 后端接口使用 `mcr::curl`。

从仓库根目录运行 `mcpp test` 可构建并验证整理后的模块和会话实现单元。
