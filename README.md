# mcr

基于 libcurl 的 C++23 模块化 HTTP 客户端库，使用 [mcpp](https://github.com/mcpp-community/mcpp) 构建和管理依赖，API 设计参考 [cpr](https://github.com/libcpr/cpr)。

通过 `import mcr;` 即可使用全部公开接口，包括同步请求、基于 future 的异步请求、协程、完成回调、批量请求和文件下载。库还提供可复用的 Session、JSON 请求与响应、表单与文件上传、代理、TLS 配置、取消和服务端发送事件（SSE）。

[快速开始](#快速开始) · [文档索引](docs/README.md) · [可运行示例](example/README.md) · [源码结构](docs/structure.md)

## 环境与依赖

需要 mcpp，以及支持 C++23 模块和 `import std;` 的编译器与标准库。安装方式见 [mcpp 官方说明](https://github.com/mcpp-community/mcpp#install)。当前 [Windows CI](.github/workflows/windows-ci.yml) 使用 mcpp `2026.9.11.1`、LLVM `22.1.8`，并依赖 Visual Studio 的 MSVC 工具和 Windows SDK。

依赖版本以 [mcpp.toml](mcpp.toml) 为准，由 mcpp 解析和管理：

| 依赖 | 版本 | 用途 |
| --- | --- | --- |
| `compat.curl` | `8.21.0` | HTTP 传输 |
| `nlohmann.json` | `3.12.0` | JSON 序列化与解析 |
| `compat.openssl` | `3.5.1` | Linux/macOS 的 OpenSSL 上下文支持 |

Windows 配置使用 Schannel；Linux/macOS 配置启用 OpenSSL 上下文支持。自动化构建和测试目前覆盖 Windows，TLS 测试的环境要求及跳过条件见 [Session 文档](docs/session.md)。协议和 TLS 功能的实际可用性取决于所链接的 libcurl 及其后端。

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
        if (response.error) {
            std::println("请求失败：{}", response.error.message);
        } else {
            std::println("HTTP {}：{}", response.status_code, response.text);
            result = mcr::status::is_success(response.status_code) ? 0 : 1;
        }
    } catch (std::exception const& error) {
        std::println("请求异常：{}", error.what());
    }
    curl_global_cleanup();
    return result;
}
```

`import mcr;` 导出库的全部公开模块；直接调用 curl 的 C API 时仍需包含其头文件。也可以按需导入 `mcr.api`、`mcr.session` 等模块，见 [模块与命名空间说明](docs/structure.md)。

## 选择请求方式

| 场景 | 接口 | 结果 |
| --- | --- | --- |
| 一次同步请求 | `Get`、`Post` 等七种 HTTP 方法 | `Response` |
| 复用连接和配置 | `Session` | `Response` |
| 在线程池执行请求 | `GetAsync` 等 | `AsyncResponse`，通过 `Get()` 取结果 |
| 在协程中等待请求 | `GetCoro` 等 | `Task<Response>`，支持 `co_await` 和 `sync_wait` |
| 请求完成后执行回调 | `GetCallback` 等 | 包装回调结果的 `AsyncWrapper` |
| 同步并发批次 | `MultiGet` 等、`MultiPerform` | 按输入顺序排列的响应 |
| 可分别取消的异步批次 | `MultiGetAsync` 等 | 按输入顺序排列的可取消任务 |
| 下载到文件或回调 | `Download`、`DownloadAsync`、`DownloadCoro` | 响应元数据 |

JSON 使用 `JsonBody` 发送，通过 `Response::Json()` 或 `TryJson()` 解析；具体规则见 [JSON 文档](docs/json.md)。

使用时需要遵循以下约定：

- 传输错误保存在 `Response::error`；HTTP 4xx/5xx 仍是普通响应，需要检查 `status_code`。配置失败和用户回调异常会抛出，异步请求通过结果消费接口传播异常。
- `Body`、`JsonBody`、`Payload` 拥有数据；`BodyView` 和 Multipart 中的 `Buffer` 借用数据，底层存储必须有效到请求结束。
- 同一个 Session 的配置与请求串行使用；Session 的异步方法需要通过 `std::shared_ptr` 管理会话。
- 在启动工作线程前初始化 curl，完成全部请求后关闭已使用的异步运行时，再销毁剩余句柄并清理 curl。`Async::Cleanup()` 和 `Coro::Cleanup()` 的生命周期相互独立，均为永久关闭，详见 [异步运行时](docs/async.md) 和 [协程请求](docs/coro.md)。

mcr 采用 C++23 模块和独立命名空间，并增加 JSON、协程及基于 `std::expected` 的结果接口。与 cpr 有意保留的 API 和行为差异记录在各模块文档中。

## 开发与文档

全部使用文档采用简体中文，API 标识符、命令和真实程序输出保留原文。新增文档请加入 [文档索引](docs/README.md)。

```sh
mcpp build --configure-only
mcpp test
```

第一条命令生成编辑器所需的编译数据库；测试为 `tests/test_*.cpp` 中的独立程序，HTTP 测试使用本地服务。贡献规范见 [AGENTS.md](AGENTS.md)，发布流程见 [版本标签与 Windows CI](docs/ci.md)。

## 许可证

采用 [MIT 许可证](LICENSE)。
