# 文档索引

[项目首页](../README.md) · [可运行示例](../example/README.md)

文档统一使用简体中文，保留 API 标识符、命令、协议值和真实终端输出。代码块未包含完整 `main()` 时视为用法片段；涉及 curl 的片段需在成功初始化 curl 后执行，并在清理 curl 前结束其资源生命周期。完整运行方式见项目首页和示例说明。

## 开始使用

| 文档 | 内容 |
| --- | --- |
| [请求 API](api.md) | 同步、异步、回调、批量与下载入口，以及版本常量 |
| [Session](session.md) | 会话复用、响应、TLS、代理认证、拦截器和批量执行 |
| [JSON 请求与响应](json.md) | `JsonBody`、序列化、响应解析与诊断 |
| [异步运行时](async.md) | 全局线程池、任务提交和清理 |
| [协程请求](coro.md) | `Task`、并发传输、取消和运行时生命周期 |
| [错误处理](error.md) | `Error`、`ErrorCode` 和 `Result<T>` |

## 请求数据与响应信息

| 文档 | 内容 |
| --- | --- |
| [公共请求类型](types.md) | `Url`、`UserAgent`、`Header` 和字符串基础类型 |
| [Body](body.md) | 拥有存储的请求正文 |
| [BodyView](body_view.md) | 借用存储的请求正文视图 |
| [Parameters](parameters.md) | 查询参数 |
| [Payload](payload.md) | 表单键值对 |
| [Buffer](buffer.md) | 借用的连续上传字节 |
| [File 与 Files](file.md) | 上传文件描述与集合 |
| [Part 与 Multipart](multipart.md) | 混合文本、文件和缓冲区的分段表单 |
| [CertInfo](cert_info.md) | 响应证书信息 |

响应、Cookie、状态码和 SSE 的请求集成见 [Session](session.md)，对应模块见 [源码结构](structure.md)。

## 请求配置

| 文档 | 内容 |
| --- | --- |
| [配置选项总览](options.md) | 六个选项模块、命名空间和旧导入迁移 |
| [Authentication](auth.md) | 用户名、密码和认证模式 |
| [Bearer](bearer.md) | Bearer 令牌 |
| [Proxies](proxies.md) | 代理地址和排除列表 |
| [HttpVersion](http_version.md) | HTTP 协议策略 |
| [请求与连接超时](timeout.md) | `Timeout` 和 `ConnectTimeout` |
| [LowSpeed](low_speed.md) | 低速超时 |
| [LimitRate](limit_rate.md) | 上传和下载速率上限 |
| [本地端口](local_port.md) | `LocalPort` 和 `LocalPortRange` |
| [Interface](interface.md) | 网络接口选择 |
| [Resolve](resolve.md) | DNS 映射覆盖 |
| [Range 与 MultiRange](range.md) | 字节范围 |
| [ConnectionPool](connection_pool.md) | 连接和 TLS 会话共享、生命周期与线程限制 |

TLS 标签、证书与代理认证的完整说明见 [Session](session.md)；编码、重定向、Unix 套接字及诊断选项的模块归属见 [配置选项总览](options.md)。

## 工具与 curl 后端

| 文档 | 内容 |
| --- | --- |
| [工具模块总览](utils.md) | `mcr::utils` 及模块迁移 |
| [AsyncWrapper](async_wrapper.md) | future 包装、结果访问和取消 |
| [ThreadPool](threadpool.md) | 任务所有权、工作线程和停止行为 |
| [Singleton](singleton.md) | 惰性初始化与永久关闭 |
| [SecureString](secure_string.md) | 安全分配器的内存擦除范围 |
| [HTTP 与 curl 辅助函数](util.md) | 解析、URL 编解码和回调适配 |
| [curl 后端总览](curl.md) | 句柄和 SSL 上下文接口 |
| [CurlContainer](curl_container.md) | `Parameter`、`Pair` 和编码规则 |
| [CurlMultiHolder](curlmultiholder.md) | multi 句柄所有权与清理顺序 |

## 项目维护

| 文档 | 内容 |
| --- | --- |
| [源码结构](structure.md) | 文件布局、模块导出与命名空间 |
| [Doxygen 使用说明](doxygen.md) | 生成 API 文档、配置源码范围、中文页面和诊断 |
| [版本标签与 Windows CI](ci.md) | 版本变更、自动测试和发布 |
| [仓库规范](../AGENTS.md) | 开发、格式、测试与贡献要求 |
