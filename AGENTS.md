# 仓库规范

## 项目目标与参考

`mcr` 是 HTTP 客户端库，以官方上游 [cpr](https://github.com/libcpr/cpr) 为实现参考。
开发请求方法、选项、会话、响应和错误处理时，应查阅上游 API、实现与测试，
适配 C++23 模块和 mcpp，并在拉取请求中说明有意保留的 API 或行为差异。

以 `mcpp.toml` 声明的依赖版本为目标，不维护旧版本兼容分支。
保留受支持环境所需的平台和 TLS 后端处理。

## 项目结构与模块组织

源码按职责分组，公开模块名独立于文件路径。
请求配置使用 `mcr::options`，TLS 标签使用 `mcr::options::ssl`，
通用工具使用 `mcr::utils`，curl 后端接口使用 `mcr::curl`。

- `src/`：库入口 `mcr.cppm` 和一次性请求 API `api.cppm`。
- `src/request/`：mcr.body 中的正文和借用视图，mcr.fields 中的查询参数与表单，
  以及 JSON、上传缓冲区、文件和分段表单；公开类型位于 mcr。
- `src/http/`：响应、Cookie、证书信息、状态码和服务端发送事件（SSE）。
  公开类型位于 mcr，状态常量位于 mcr::status，保留 mcr.response、mcr.sse 等现有模块名。
- `src/session/`：会话、异步运行时、回调和连接池。
  session.cppm 包含会话、TLS、拦截器和批量实现；
  导入 mcr.session 使用 Session、Interceptor、InterceptorMulti 和 MultiPerform。
- `src/coro/`：Task 和 curl multi 协程运行时，公开请求函数仍位于 src/api.cppm。
- `src/options/`：mcr::options 下的六个配置模块。
  mcr.auth 提供用户名密码认证和 Bearer；mcr.proxy 提供代理地址和凭据；
  mcr.http 提供协议版本、编码、重定向和范围；
  mcr.transfer_options 提供时间、速率、连接选择、容量和诊断选项。
  网络接口和 TLS 配置分别位于 mcr.interface、mcr.ssl_options。
  相关的小型选项类型应放在一起。
- `src/utils/`：mcr::utils 下的安全字符串、单例生命周期、线程池、future 包装，
  以及 HTTP 解析和 curl 回调辅助函数。公共 HTTP 类型 types.cppm 和错误/结果 error.cppm
  也放在此目录，但公开接口仍位于 mcr。
  模块名独立于路径，保留 mcr.types、mcr.error、mcr.threadpool、mcr.util 等名称。
- `src/curl/`：mcr::curl 下的 easy/multi 句柄所有权、请求容器编码和 SSL 上下文支持，
  包括模块接口及其实现。请求记录 Parameter 和 Pair 保留在 mcr，
  与 `mcr::curl::CurlContainer<T>` 位于同一模块。
  保留 mcr.curlholder、mcr.curl_container、mcr.ssl_ctx 等模块名。
- `tests/test_*.cpp`：独立测试程序，共用的本地 HTTP 测试设施位于 tests/fixtures/。
- `example/`：使用独立 mcpp.toml 的请求示例及本地验证脚本。
- `mcpp.toml`：包元数据和依赖声明。
- `.clang-format`：仓库格式配置。
- `target/`、`.mcpp/`、`compile_commands.json`：Git 忽略的生成内容或本地状态，不得提交。

目录内容及模块命名见 [源码结构](docs/structure.md)，使用文档见 [文档索引](docs/README.md)。

保持 mcr.cppm 为完整公开入口：重新导出全部公开 mcr.* 模块，
包括请求 API、数据类型、选项、异步结果、状态码、版本信息、curl 后端和通用工具。
外部代码及测试可以仅使用 `import mcr;`，也可以按需导入组件模块。
组件内部对 curl 后端和通用工具依赖使用普通 import；
mcr.fields 只选择性导出公开记录 Parameter、Pair，不重新导出 curl 容器模块。

## 构建、测试与开发命令

从仓库根目录执行，环境须提供 mcpp 和支持 `import std;` 的 C++23 工具链。

- `mcpp self doctor`：诊断本地环境。
- `mcpp build`：构建库。
- `mcpp test`：发现、构建并运行 tests/ 下的测试。
- `mcpp build --configure-only`：生成编辑器编译数据库。
- `mcpp clean`：清理 target/ 下的生成构建内容。

根项目没有默认可执行程序。运行请求示例时进入 example/，
执行 `mcpp run get -- http://127.0.0.1:8080/echo` 等命令；
启动本地服务和选择其他目标的步骤见 [示例说明](example/README.md)。

## 代码风格与命名

遵循现有 .clang-format：四空格缩进，不使用制表符，不强制列宽上限。
格式化修改过的 C++ 文件，例如：

```sh
clang-format -i src/api.cppm tests/test_api.cpp
```

保留 C++23 模块风格，包括 `import std;`。
库声明和实现直接放在 src/ 的 .cppm 中，不创建独立 .cpp 实现单元。
文件名使用小写及下划线，例如 argument_parser.cppm。
测试仍是独立 .cpp 程序。仓库没有独立的代码检查配置。

通过 `import std;` 直接使用 `std::filesystem`，不创建文件系统命名空间别名或包装模块。

库代码、内部辅助函数和测试统一使用 Doxygen 文档注释：

- 即使只有简短 @brief，也使用多行块；/** 和 */ 各占一行，内容行以 ` * ` 开头，
  不使用单行文档块。
- 注释紧邻其声明之前，模板注释放在 template 前。
  使用 @brief，并按需要添加 @tparam、@param、@return、@throws、@note；
  每个标签独占一行，不适用的标签省略。
- C++ 源文件开头添加带 @file 和 @brief 的文件级块。
- 成员和枚举值使用尾随 `///<` 描述；实现解释和命名空间结束标签可使用普通 // 注释。
- 格式化时保持此布局，遵循已有 .clang-format，不修改该配置文件。

```cpp
/**
 * @brief 检查任务是否仍拥有未消费的结果。
 * @return 仍有结果可用时返回 true。
 */
[[nodiscard]] bool Valid() const noexcept;

/**
 * @brief 请求取消时唤醒运行时。
 */
void Wake() noexcept;

bool m_stopping{ false }; ///< 是否已经开始关闭。
```

## 文档语言与组织

README、docs/ 和 example/README.md 统一使用简体中文。
API 标识符、文件名、命令、协议值及真实终端输出保留原文。
文档描述当前可用行为；有意差异和迁移说明单独说明，避免将临时开发记录写成长期约定。
新增使用文档需加入 docs/README.md，并检查相对链接。
源代码已有注释的翻译不属于日常文档整理的默认范围。

## 测试要求

测试各自提供 main，不使用外部测试框架。
遵循 tests/test_*.cpp，每个文件生成一个可执行程序，成功返回零，失败返回非零。
覆盖修改的行为与边界情况，再运行 `mcpp test`。
HTTP 测试使用可控制响应的本地服务。
冒烟测试检查编译与执行，未配置覆盖率阈值。

## 提交与拉取请求

沿用 feat:、style: 等前缀，描述简短并使用祈使表达，例如 `feat: add argument parsing`。

保持改动聚焦。拉取请求说明行为变化，按需关联问题，并报告验证命令与结果。
修改命令行行为时附上修改前后的终端输出。
