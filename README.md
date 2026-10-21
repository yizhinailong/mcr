# mcr

基于 libcurl 的 C++23 模块化 HTTP 客户端库，使用 [mcpp](https://github.com/mcpp-community/mcpp) 构建和管理依赖，API 设计参考 [cpr](https://github.com/libcpr/cpr)。

通过 `import mcr;` 即可使用全部公开接口，包括同步请求、基于 future 的异步请求、协程、完成回调、批量请求和文件下载，以及按编译期方法参数分派的 `request<M>` 系列入口。库还提供可复用的 Session、表单与文件上传、代理、TLS 配置、取消和服务端发送事件（SSE）。

[快速开始](#快速开始) · [文档索引](docs/README.md) · [可运行示例](example/README.md) · [源码结构](docs/structure.md) · [expected 接口迁移](docs/expected_migration.md)

## 环境与依赖

需要 mcpp，以及支持 C++23 模块和 `import std;` 的编译器与标准库。安装方式见 [mcpp 官方说明](https://github.com/mcpp-community/mcpp#install)。当前 [Windows CI](.github/workflows/windows-ci.yml) 和 [Linux CI](.github/workflows/linux-ci.yml) 使用 mcpp `2026.10.19.1`、LLVM `23.1.3`，分别在 Windows 2025 和 Ubuntu 24.04 上构建与测试。两个工作流在分支推送和 PR 时独立触发；Windows 依赖 Visual Studio 的 MSVC 工具和 Windows SDK。

依赖版本以 [mcpp.toml](mcpp.toml) 为准，由 mcpp 解析和管理：

| 依赖 | 版本 | 用途 |
| --- | --- | --- |
| `compat.curl` | `8.21.0` | HTTP 传输 |
| `compat.openssl` | `3.5.1` | Linux/macOS 的 OpenSSL 上下文支持 |

Windows 配置使用 Schannel；Linux/macOS 配置启用 OpenSSL 上下文支持。自动化构建和测试目前覆盖 Windows 和 Linux，TLS 测试的环境要求及跳过条件见 [Session 文档](docs/session/session.md)。协议和 TLS 功能的实际可用性取决于所链接的 libcurl 及其后端。

<a id="快速开始"></a>

## 快速开始

在仓库根目录检查环境、构建库并运行测试：

```sh
mcpp self doctor
mcpp build
mcpp test
```

根项目是库，没有默认可执行程序。运行示例需要进入 `example/`，该目录提供 46 个独立请求示例和本地 HTTP 服务。准备好 [uv](https://docs.astral.sh/uv/) 后，在第一个终端从仓库根目录执行：

```sh
cd example
mcpp build
uv run scripts/verify.py --serve --port 8080
```

保持服务运行，在第二个终端从仓库根目录执行：

```sh
cd example
mcpp run get -- http://127.0.0.1:8080/echo
mcpp run post_async -- http://127.0.0.1:8080/echo
mcpp run get_coro -- http://127.0.0.1:8080/echo
```

GET 示例会输出 `Status code: 200` 和 `Text: GET single`。下载、批量请求和完整验证步骤见 [示例说明](example/README.md)。

## 在项目中使用

在消费项目的 `mcpp.toml` 中声明本地依赖，路径相对于该配置文件：

```toml
[dependencies]
mcr = { path = "../mcr" }
```

下面是完整的同步请求程序，可保存为消费项目的 `src/main.cpp`。它连接上面启动的本地服务，检查传输错误和 HTTP 状态，并在 Session 及响应销毁后清理 curl：

```cpp
/**
 * @file main.cpp
 * @brief 向本地 HTTP 服务发送请求。
 */
#include <curl/curl.h>

import std;
import mcr;

/**
 * @brief 初始化 curl，发送请求并输出结果。
 * @return 请求成功时返回零，否则返回一。
 */
auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    int result{ 1 };
    try {
        auto const response = mcr::Get(
            mcr::Url{ "http://127.0.0.1:8080/echo" },
            mcr::Parameters{ { "message", "hello world" } },
            mcr::options::Timeout{ std::chrono::seconds{ 5 } }
        );
        if (!response) {
            std::println("请求准备失败：{}", response.error().message);
        } else if (response->error) {
            std::println("传输失败：{}", response->error.message);
        } else {
            std::println("HTTP {}：{}", response->status_code, response->text);
            result = mcr::status::is_success(response->status_code) ? 0 : 1;
        }
    } catch (std::exception const& error) {
        std::println("请求异常：{}", error.what());
    }
    curl_global_cleanup();
    return result;
}
```

`import mcr;` 导出库的全部公开接口；直接调用 curl 的 C API 时仍需包含其头文件。组件实现为 `mcr:api`、`mcr:session` 等模块分区，外部只需导入总入口，见 [模块与命名空间说明](docs/structure.md)。

## 选择请求方式

| 场景 | 接口 | 结果 |
| --- | --- | --- |
| 一次同步请求 | `Get`、`Post` 等七种 HTTP 方法 | `Result<Response>` |
| 泛型代码按编译期方法参数请求 | `request<M>`、`request_async<M>`、`request_coro<M>` | `Result<Response>`、`Result<AsyncResponse>`、`Task<Result<Response>>` |
| 复用连接和配置 | `Session::Create()` | 创建结果及请求的 `Result<Response>` |
| 在线程池执行请求 | `GetAsync` 等 | `Result<AsyncResponse>`，提交成功后通过 `Get()` 取请求结果 |
| 在协程中等待请求 | `GetCoro` 等 | `Task<Result<Response>>`，支持 `co_await` 和 `sync_wait` |
| 排队启动或让出协程执行 | `Coro::Spawn`、`Coro::Schedule` | `Result<Task<T>>`、可等待的 `Result<void>` |
| 请求完成后执行回调 | `GetCallback` 等 | `Result<AsyncWrapper<回调返回类型, true>>` |
| 同步并发批次 | `MultiGet` 等、`MultiPerform` | 按输入顺序排列的响应 |
| 可分别取消的异步批次 | `MultiGetAsync` 等 | 按输入顺序排列的可取消任务 |
| 下载到文件或回调 | `Download`、`DownloadAsync`、`DownloadCoro` | 响应元数据 |

`request<M>` 系列以 `mcr::method::Get` 等标签或 `mcr::Method` 枚举值在编译期选择方法，同步、线程池异步和协程三种形式与同名命名入口行为一致。它们适合需要把方法作为模板参数传递的封装；回调、批量和下载仍使用命名接口，详见 [请求 API](docs/api.md#编译期方法请求)。

库只传输字节，不依赖具体 JSON 实现。发送 JSON 时由调用方序列化后放入 `Body` 并显式设置 `Content-Type`；响应正文保存在 `Response::text`，可用任意 JSON 库自行解析。同步、异步、协程、回调和两种批量调用都遵循相同语义，运行目标见 [示例说明](example/README.md)。

使用时需要遵循以下约定：

- 配置和本地操作失败通过 `Result` 返回。传输错误保存在 `Response::error`；HTTP 4xx/5xx 仍是普通响应，需要检查 `status_code`。用户回调与标准库资源异常继续传播。
- `Body`、`Payload` 拥有数据；`BodyView` 和 Multipart 中的 `Buffer` 借用数据，底层存储必须有效到请求结束。
- 同一个 Session 的配置与请求串行使用；Session 的异步方法需要通过 `std::shared_ptr` 管理会话。
- 在启动工作线程前初始化 curl，完成全部请求后关闭已使用的异步运行时，再销毁剩余句柄并清理 curl。`Async::Cleanup()` 和 `Coro::Cleanup()` 的生命周期相互独立，均为永久关闭，详见 [异步运行时](docs/session/async.md) 和 [协程请求](docs/coro/coro.md)。

mcr 采用 C++23 模块和独立命名空间，并增加协程、编译期方法模板入口及基于 `std::expected` 的结果接口。与 cpr 有意保留的 API 和行为差异记录在各模块文档中。

## 开发与文档

全部使用文档采用简体中文，API 标识符、命令和真实程序输出保留原文。新增文档请加入 [文档索引](docs/README.md)。

```sh
mcpp build --configure-only
mcpp test
```

第一条命令生成编辑器所需的编译数据库；测试为 `tests/test_*.cpp` 中的独立程序，HTTP 测试使用本地服务。贡献规范见 [AGENTS.md](AGENTS.md)，发布流程见 [版本标签与 Windows/Linux CI](docs/ci.md)。

安装 Doxygen 后，在仓库根目录先创建 `target/doxygen`，再执行 `doxygen Doxyfile`，生成的 API 文档首页为
`target/doxygen/html/index.html`。Documentation 工作流会在 `main` 上的源码或文档更新时自动生成并发布 GitHub Pages。
首次启用所需的 GitHub 设置、源码范围、中文页面和可选图表的配置见 [Doxygen 使用说明](docs/doxygen.md)。

## 许可证

采用 [MIT 许可证](LICENSE)。
