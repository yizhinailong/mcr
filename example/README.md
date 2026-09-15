# 请求接口示例

[项目首页](../README.md) · [文档索引](../docs/README.md) · [请求 API](../docs/api.md)

参考 cpr 的 [HTTP API](https://github.com/libcpr/cpr/blob/master/include/cpr/api.h)
和 [FetchContent 示例](https://github.com/libcpr/example-cmake-fetch-content)。
覆盖 [src/api.cppm](../src/api.cppm) 中全部公开请求入口：7 种 HTTP 方法 × 6 种调用形式，以及
4 个下载入口（包括 `Download` 的两个重载），提供 46 个入口示例。
另有覆盖六种调用方式的 JSON POST 示例，共 52 个独立可执行程序。

每个入口都有一个按功能命名的 `.cpp` 文件和同名 mcpp 目标。
示例直接调用对应 API；[support.cppm](src/common/support.cppm) 只共用命令行解析、
curl 与异步运行时生命周期、响应输出和错误处理。
[json_support.cppm](src/common/json_support.cppm) 复用 JSON 响应检查与输出，
各 JSON 示例直接展示对应请求 API 和 `JsonBody` 的构造。

此目录使用自己的 `mcpp.toml`，通过 `mcr = { path = ".." }` 引用本地库。
主项目的 `mcpp.toml` 保持库配置，根目录的 `mcpp build` 仍构建库。

请求示例显式检查 `Result`，操作失败输出错误码和诊断；传输失败与 HTTP 状态分别处理。
接口变化见 [expected 接口迁移](../docs/expected_migration.md)。

## 快速开始

准备好支持 C++23 和 `import std;` 的 mcpp 工具链，以及 uv。
Python 版本由 uv 按脚本内的声明选择或下载；脚本没有第三方 Python 依赖。
可以先在仓库根目录检查环境：

```sh
mcpp self doctor
uv --version
```

### 1. 编译示例并启动本地服务

打开第一个终端，从仓库根目录执行：

```sh
cd example
mcpp build
uv run scripts/verify.py --serve --port 8080
```

`mcpp build` 编译全部 52 个示例。服务启动后会输出：

```text
Serving http://127.0.0.1:8080/echo, /json and /binary (Ctrl+C to stop)
```

保持此终端运行，测试结束后按 Ctrl+C 关闭服务。
如果 8080 端口已被占用，可改为 `--port 8081`，同时修改后续请求 URL 的端口。

### 2. 在另一个终端运行请求

打开第二个终端，同样从仓库根目录进入 `example/`：

```sh
cd example
mcpp run get -- http://127.0.0.1:8080/echo
```

正常情况下会打印 `Status code: 200`、响应头和 `Text: GET single`。
`mcpp run` 会先构建所选目标，再执行请求。目标名就是示例文件名去掉 `.cpp`；
`--` 后面的参数传给示例程序。

继续在第二个终端运行其他调用方式：

```sh
mcpp run post -- http://127.0.0.1:8080/echo
mcpp run post_async -- http://127.0.0.1:8080/echo
mcpp run post_coro -- http://127.0.0.1:8080/echo
mcpp run post_callback -- http://127.0.0.1:8080/echo
mcpp run multi_post -- http://127.0.0.1:8080/echo
mcpp run multi_post_async -- http://127.0.0.1:8080/echo
```

把目标名替换为下方覆盖清单中的任意名称，即可运行对应接口。
本地服务接受所有 7 种 HTTP 方法，`/echo` 返回请求方法与正文；HEAD 只返回响应头。

### 3. 下载文件或验证错误处理

以下命令仍在 `example/` 中执行。`target/` 已由前面的构建创建：

```sh
mcpp run download -- http://127.0.0.1:8080/binary target/download.bin
mcpp run download_callback -- http://127.0.0.1:8080/binary
mcpp run download_async -- http://127.0.0.1:8080/binary target/download_async.bin
mcpp run download_coro -- http://127.0.0.1:8080/binary target/download_coro.bin
mcpp run get -- http://127.0.0.1:8080/status/404
mcpp run get -- --help
```

三个文件下载入口必须提供输出路径，文件以二进制截断模式打开，会覆盖已有内容；
输出文件的父目录必须已经存在。`download_callback` 把字节收集到内存并打印十六进制内容。
请求 `/status/404` 会显示 HTTP 404 和正文 `not found`，并返回退出码 `1`。

## JSON 请求示例

六个示例使用 `JsonBody` 发送包含字符串、布尔值和中文的 JSON 对象。
请求会自动设置 `Content-Type: application/json`，响应先检查传输与 HTTP 状态，
再通过 `TryJson()` 解析并打印 JSON。

| 调用方式 | 文件 / 运行目标 | 请求与结果消费 |
| --- | --- | --- |
| 同步 | [post_json](src/sync/post_json.cpp) | `Post` 返回响应，直接检查和解析 |
| 异步 | [post_json_async](src/async/post_json_async.cpp) | `PostAsync` 提交任务，通过 `Get()` 取回响应 |
| 协程 | [post_json_coro](src/coro/post_json_coro.cpp) | 在协程中 `co_await PostCoro`，主函数通过 `sync_wait` 等待 |
| 完成回调 | [post_json_callback](src/callback/post_json_callback.cpp) | 在 `PostCallback` 的回调中解析响应，`Get()` 取回退出码 |
| 同步批量 | [multi_post_json](src/multi/multi_post_json.cpp) | `MultiPost` 并发发送两个 JSON 请求，按输入顺序处理响应 |
| 异步批量 | [multi_post_json_async](src/multi_async/multi_post_json_async.cpp) | `MultiPostAsync` 返回任务集合，逐一 `Get()` 并解析 |

保持上面的本地服务运行，在另一个终端从仓库根目录执行：

```sh
cd example
mcpp run post_json -- http://127.0.0.1:8080/json
mcpp run post_json_async -- http://127.0.0.1:8080/json
mcpp run post_json_coro -- http://127.0.0.1:8080/json
mcpp run post_json_callback -- http://127.0.0.1:8080/json
mcpp run multi_post_json -- http://127.0.0.1:8080/json
mcpp run multi_post_json_async -- http://127.0.0.1:8080/json
```

`/json` 返回收到的 JSON 文档。终端会显示 `Status code: 200`，随后输出：

```text
JSON: {"enabled":true,"message":"你好，JSON!","name":"Alice"}
```

批量示例的 JSON 正文还分别包含 `"request":"first"` 和 `"request":"second"`。
即使服务端先完成第二个请求，响应仍按 first、second 的顺序输出。
异步和协程示例会在消费全部结果后关闭对应运行时，再清理 curl。

如果将地址改为 `/echo`，服务仍返回 HTTP 200，但正文不是合法 JSON；
示例会输出 `JSON parse failed:` 和诊断信息，并以退出码 `1` 结束。
JSON 正文所有权、媒体类型优先级和解析规则见 [JSON 请求与响应](../docs/json.md)。

## 目录

| 目录               | 内容                             |
| ------------------ | -------------------------------- |
| `src/sync/`        | 7 个同步请求入口与 JSON POST 示例 |
| `src/async/`       | 7 个 future 异步请求入口与 JSON POST 示例 |
| `src/coro/`        | 7 个协程请求入口与 JSON POST 示例 |
| `src/callback/`    | 7 个完成回调入口与 JSON POST 示例 |
| `src/multi/`       | 7 个同步批量入口与 JSON POST 示例 |
| `src/multi_async/` | 7 个异步批量入口与 JSON POST 示例 |
| `src/download/`    | 4 个下载入口                     |
| `src/common/`      | 命令行、运行时生命周期与普通 / JSON 响应输出 |
| `scripts/`         | 本地 HTTP 服务与完整性验证       |

## 覆盖清单

表中每个文件名也是运行目标，例如 `mcpp run post_async -- <URL>`。

| HTTP 方法 | 同步                            | 异步                                         | 协程                                      | 完成回调                                              | 批量同步                                     | 批量异步                                                       |
| --------- | ------------------------------- | -------------------------------------------- | ----------------------------------------- | ----------------------------------------------------- | -------------------------------------------- | -------------------------------------------------------------- |
| GET       | [get](src/sync/get.cpp)         | [get_async](src/async/get_async.cpp)         | [get_coro](src/coro/get_coro.cpp)         | [get_callback](src/callback/get_callback.cpp)         | [multi_get](src/multi/multi_get.cpp)         | [multi_get_async](src/multi_async/multi_get_async.cpp)         |
| POST      | [post](src/sync/post.cpp)       | [post_async](src/async/post_async.cpp)       | [post_coro](src/coro/post_coro.cpp)       | [post_callback](src/callback/post_callback.cpp)       | [multi_post](src/multi/multi_post.cpp)       | [multi_post_async](src/multi_async/multi_post_async.cpp)       |
| PUT       | [put](src/sync/put.cpp)         | [put_async](src/async/put_async.cpp)         | [put_coro](src/coro/put_coro.cpp)         | [put_callback](src/callback/put_callback.cpp)         | [multi_put](src/multi/multi_put.cpp)         | [multi_put_async](src/multi_async/multi_put_async.cpp)         |
| HEAD      | [head](src/sync/head.cpp)       | [head_async](src/async/head_async.cpp)       | [head_coro](src/coro/head_coro.cpp)       | [head_callback](src/callback/head_callback.cpp)       | [multi_head](src/multi/multi_head.cpp)       | [multi_head_async](src/multi_async/multi_head_async.cpp)       |
| DELETE    | [delete](src/sync/delete.cpp)   | [delete_async](src/async/delete_async.cpp)   | [delete_coro](src/coro/delete_coro.cpp)   | [delete_callback](src/callback/delete_callback.cpp)   | [multi_delete](src/multi/multi_delete.cpp)   | [multi_delete_async](src/multi_async/multi_delete_async.cpp)   |
| OPTIONS   | [options](src/sync/options.cpp) | [options_async](src/async/options_async.cpp) | [options_coro](src/coro/options_coro.cpp) | [options_callback](src/callback/options_callback.cpp) | [multi_options](src/multi/multi_options.cpp) | [multi_options_async](src/multi_async/multi_options_async.cpp) |
| PATCH     | [patch](src/sync/patch.cpp)     | [patch_async](src/async/patch_async.cpp)     | [patch_coro](src/coro/patch_coro.cpp)     | [patch_callback](src/callback/patch_callback.cpp)     | [multi_patch](src/multi/multi_patch.cpp)     | [multi_patch_async](src/multi_async/multi_patch_async.cpp)     |

| 文件 / 目标                                             | 公开入口                              | 功能                                               |
| ------------------------------------------------------- | ------------------------------------- | -------------------------------------------------- |
| [download](src/download/download.cpp)                   | `Download(std::ofstream&, ...)`       | 向调用方持有的二进制流下载，显式关闭并检查写入结果 |
| [download_callback](src/download/download_callback.cpp) | `Download(WriteCallback const&, ...)` | 通过回调收集原始字节，打印长度与十六进制内容       |
| [download_async](src/download/download_async.cpp)       | `DownloadAsync(path, ...)`            | 在线程池中下载，通过 `Get()` 等待文件关闭          |
| [download_coro](src/download/download_coro.cpp)         | `DownloadCoro(path, ...)`             | 通过 `co_await` 下载，以 `sync_wait` 等待完成      |

同步入口为 `Get`、`Post`、`Put`、`Head`、`Delete`、`Options`、`Patch`；
其余列对应 `*Async`、`*Coro`、`*Callback`、`Multi*`、`Multi*Async`。
协程入口是 mcr 扩展。Session、选项和工具类的用法见[文档索引](../docs/README.md)。

## 请求参数与返回结果

所有请求显式接收 URL，也可以换成自己的 HTTP 服务或公网地址：

```sh
mcpp run get -- https://api.github.com/repos/libcpr/cpr/contributors
```

公网请求需要可用的网络连接，服务端须支持所选 HTTP 方法。
文件下载失败时可能留下部分内容。

单请求示例附加 `message=hello world` 查询参数；普通 POST、PUT、PATCH 示例发送
`text/plain` 正文，六种 JSON 示例发送 `application/json` 正文。批量示例向同一 URL 发送两个请求，分别带
`request=first` 和 `request=second`，按输入顺序输出所有结果。
请求使用 10 秒超时。

`*Async` 演示取回 future，`*Callback` 在完成回调中处理响应并返回退出码，
`*Coro` 在拥有 URL 的协程函数内直接 `co_await`。
异步与协程运行时在所有结果消费完毕后关闭，最后清理 curl。

| 退出码 | 含义 |
| --- | --- |
| `0` | HTTP 2xx，或成功显示 `--help` |
| `1` | HTTP 非 2xx、配置或本地操作失败、传输错误、JSON 解析失败或运行异常 |
| `2` | 缺失或多余的命令行参数 |

配置和本地操作失败会输出错误码及诊断。例如，下载目标的父目录不存在时：

```text
Request failed: WRITE_ERROR: mcr::Download: output stream is not writable.
```

## 用 uv 验证全部示例

[verify.py](scripts/verify.py) 使用 [uv 单文件脚本](https://docs.astral.sh/uv/guides/scripts/)形式，
在文件头通过 PEP 723 元数据声明 `requires-python = ">=3.11"` 和 `dependencies = []`。
脚本只使用标准库，通过 `uv run` 单文件执行。
下面的命令均在 `example/` 中运行，查看脚本参数可执行：

```sh
uv run scripts/verify.py --help
```

| 参数 | 用途 |
| --- | --- |
| `--serve` | 启动供手动运行示例使用的本地 HTTP 服务 |
| `--port 8080` | 设置 `--serve` 的监听端口，默认 8080 |
| `--bin-dir <目录>` | 自动启动临时服务并验证该目录下的全部示例 |

`--serve` 和 `--bin-dir` 二选一。完整验证会自动使用空闲端口启动服务，
无需先运行 `--serve`，也可以与手动服务同时运行。

先执行 `mcpp build`。可执行文件位于 `target/<目标平台>/<构建指纹>/bin/`。
运行前面的 `mcpp run get ...` 时，终端会打印 `Running` 后的可执行文件路径；
去掉末尾的 `/get.exe`（Linux/macOS 为 `/get`），就是要传给 `--bin-dir` 的目录。
例如 Windows MSVC 目标使用以下路径形式，需替换其中的构建指纹：

```sh
uv run scripts/verify.py --bin-dir "target/x86_64-windows-msvc/<构建指纹>/bin"
```

路径相对于当前终端目录解析，也接受绝对路径；包含空格时使用引号。
该目录中应包含全部 52 个可执行文件，不能只编译其中一个目标。

脚本对照 `src/api.cppm` 检查是否遗漏入口或下载重载，并检查 JSON 的六种调用方式是否齐全，
然后用临时的本地服务运行全部 52 个示例，检查真实 HTTP 方法、查询参数、正文、批量结果顺序、二进制下载内容、
JSON 请求媒体类型与响应解析、HTTP 404、传输失败、下载文件打开失败和 CLI 退出码。
JSON 示例还验证 HTTP 200 携带非法 JSON 时的失败路径，以及批量 JSON 响应与输入的对应顺序。HTTP 验证仅访问本机，
下载文件放在临时目录中并在验证结束后清理。全部通过时最后输出：

```text
52 request examples passed; every public request declaration is covered.
```

验证脚本成功时退出码为 `0`，失败时返回非零并显示出错的示例和检查信息。
库的现有测试仍在仓库根目录运行 `mcpp test`；若当前在 `example/` 中：

```sh
cd ..
mcpp test
```
