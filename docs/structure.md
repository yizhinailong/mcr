# 源码目录

[文档索引](README.md) · [项目首页](../README.md)

`src/` 按职责组织，根目录保留库入口和请求 API。基础类型与错误处理位于 `src/utils/`。

`docs/` 的子目录与 `src/` 组件对应：`request/`、`http/`、`session/`、`coro/`、
`options/`、`curl/`、`utils/`。请求 API、源码结构、Doxygen、CI 和迁移说明留在
`docs/` 根目录，完整列表见[文档索引](README.md)。

| 位置 | 文件 | 职责 |
| --- | --- | --- |
| `src/` | `mcr.cppm`、`api.cppm` | 库入口和一次性请求 API |
| `src/request/` | `body.cppm`、`buffer.cppm`、`file.cppm`、`multipart.cppm`、`fields.cppm` | 请求正文、上传数据、查询参数和表单 |
| `src/http/` | `response.cppm`、`cookies.cppm`、`cert_info.cppm`、`status_code.cppm`、`sse.cppm` | 响应、HTTP 元数据和事件流 |
| `src/session/` | `session.cppm`、`async.cppm`、`callback.cppm`、`connection_pool.cppm`、`global_init.cppm` | 会话、异步运行时、回调、连接共享、批量执行和 curl 全局初始化守卫 |
| `src/coro/` | `task.cppm`、`runtime.cppm` | `mcr::Task<T>` 与 curl multi 协程运行时（含定时等待），公共请求函数仍位于 `src/api.cppm`，详见[协程请求](coro/coro.md) |
| `src/options/` | `auth.cppm`、`proxy.cppm`、`http.cppm`、`transfer_options.cppm`、`interface.cppm`、`ssl_options.cppm` | 请求配置，详见[选项说明](options/options.md) |
| `src/curl/` | `curlholder.cppm`、`curlmultiholder.cppm`、`curl_container.cppm`、`ssl_ctx.cppm` | curl 句柄、容器编码和 TLS 后端支持，详见[curl 模块](curl/curl.md) |
| `src/utils/` | `types.cppm`、`error.cppm`、`secure_string.cppm`、`singleton.cppm`、`threadpool.cppm`、`async_wrapper.cppm`、`util.cppm` | 公共类型、错误结果、通用工具及 HTTP/curl 辅助函数，详见[工具模块](utils/utils.md) |

库代码直接在 `.cppm` 中实现。`session.cppm` 包含会话、TLS、拦截器和批量执行的
声明及实现；`ssl_ctx.cppm` 包含 SSL 上下文回调的完整实现。

`Body` 和 `BodyView` 合并在 `mcr:body` 分区；`Parameters` 和 `Payload` 合并在
`mcr:fields` 分区。类型名称、所有权语义和序列化行为保持不变。

`src/` 中的组件都是 `mcr` 的模块分区。例如 `src/request/body.cppm` 声明
`export module mcr:body;`，依赖通过 `import :other;` 引入。分区不能被外部代码单独导入；
`src/mcr.cppm` 作为主模块接口单元重新导出全部分区，外部代码只需 `import mcr;`。

| 文件 | 分区 |
| --- | --- |
| `src/mcr.cppm` | `mcr`（主模块接口单元） |
| `src/api.cppm` | `mcr:api` |
| `src/request/body.cppm` | `mcr:body` |
| `src/request/fields.cppm` | `mcr:fields` |
| `src/http/response.cppm` | `mcr:response` |
| `src/session/session.cppm` | `mcr:session` |
| `src/options/http.cppm` | `mcr:http` |
| `src/utils/types.cppm` | `mcr:types` |
| `src/utils/error.cppm` | `mcr:error` |

`import mcr;` 统一导出全部公开接口，包括请求 API、会话、请求与响应数据、配置选项、
异步结果、状态码、版本信息，以及 curl 后端、安全字符串、单例、线程池和解析工具。
`mcr:fields` 单独导出 `Parameter`、`Pair`，不重新导出 curl 容器分区。
新增公开分区时，同时将其加入 `src/mcr.cppm` 的 `export import :name;` 列表。

旧模块按下表迁移：

| 原导入 | 当前导入 |
| --- | --- |
| `import mcr.body_view;` | `import mcr;` |
| `import mcr.parameters;`、`import mcr.payload;` | `import mcr;` |
| `import mcr.interceptor;`、`import mcr.multiperform;` | `import mcr;` |

以上旧模块不再提供独立文件。

公共基础类型、错误结果、请求数据、响应和会话类型使用 `mcr`，
状态码使用 `mcr::status`，配置选项使用 `mcr::options`，工具使用 `mcr::utils`，
curl 后端接口使用 `mcr::curl`。

从仓库根目录运行 `mcpp test` 可构建并验证整理后的模块分区。
