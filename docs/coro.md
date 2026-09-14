# 协程请求

`import mcr;` 提供 `mcr::Task<T>`、`mcr::sync_wait`、`mcr::Coro`，以及
`GetCoro` / `PostCoro` / `PutCoro` / `HeadCoro` / `DeleteCoro` /
`OptionsCoro` / `PatchCoro` / `DownloadCoro`。

公共请求函数位于 `src/api.cppm`（`mcr.api`）；任务和运行时位于
`src/coro/task.cppm`（`mcr.task`）及 `src/coro/runtime.cppm`（`mcr.coro_runtime`）。
只使用任务类型时可单独 `import mcr.task;`。`mcr.api` 同时导出任务类型；
运行时生命周期可单独通过 `import mcr.coro_runtime;` 访问。

## 基本用法

```cpp
import std;
import mcr;

auto fetch_text(mcr::Url url) -> mcr::Task<std::string> {
    auto response = co_await mcr::GetCoro(
        std::move(url), mcr::options::Timeout{ std::chrono::seconds{ 5 } }
    );
    if (response.error) {
        throw std::runtime_error{ response.error.message };
    }
    co_return std::move(response.text);
}

// 在已完成 curl 全局初始化的普通函数中：
auto text = mcr::sync_wait(fetch_text(mcr::Url{ "https://example.com" }));
mcr::Coro::Cleanup(); // 所有协程工作结束后，在 curl_global_cleanup() 之前调用。
```

HTTP 方法和选项沿用同步 API，包括 Header 合并、Body / JsonBody / Payload、
代理、TLS、重定向、回调和响应解析。每次调用拥有自己的 Session。
这批 API 提供一次性请求；Session 的同步拦截器不能直接作为协程调度器使用。

`GetCoro` 等调用时立即复制普通左值选项、移动右值选项，首次启动时才配置并提交请求。
`BodyView`、Multipart 中的 Buffer、`std::ref` / `std::cref`、回调捕获对象和
ConnectionPool 仍有借用语义，必须存活到请求结束。对用户自行编写的协程，建议像
示例一样按值接收需拥有的数据；协程的引用参数不会自动延长被引用对象的生命周期。

## Task 的启动、等待与所有权

`Task<T>` 仅移动、单次消费，支持值、仅可移动的值、左值引用和 `void`；
不支持右值引用结果。任务有三种使用方式：

- `co_await task_expression`：在协程中启动并等待。
- `task.Start()`：立即执行到挂起点，显式启动任务但保留结果；重复调用不重复执行。
- `mcr::sync_wait(std::move(task))`：在普通线程中启动并阻塞等待结果。

对命名对象使用 `co_await std::move(task)`；等待会转移其所有权。
默认构造或移动后的 Task 为无效状态，可通过 `Valid()` 查询；等待或启动无效任务抛出
`std::logic_error`。同一个 Task 对象上的操作需要由调用方同步。

未启动 Task 析构时直接释放协程帧，不执行请求。启动后，协程自行持有运行期间的帧，
最终完成时先销毁帧再通知结果消费者。丢弃已启动 Task 不会阻塞、自动取消或销毁
仍在等待的帧；它会运行到完成。需要停止时显式取消，再等待其退出。
外部协程框架等待 Task 时，必须保证等待中的父协程不被提前强制销毁。

可以先启动多个任务，再依次取结果；仅创建惰性任务不会开始并发请求：

```cpp
auto first = mcr::GetCoro(mcr::Url{ "http://127.0.0.1:8080/first" });
auto second = mcr::GetCoro(mcr::Url{ "http://127.0.0.1:8080/second" });
first.Start();
second.Start();

auto a = mcr::sync_wait(std::move(first));
auto b = mcr::sync_wait(std::move(second));
```

## 网络与恢复线程

运行时首次提交时自动启动，也可以显式 `mcr::Coro::Startup()`。它拥有：

- 一个 I/O 线程，通过 `curl_multi_perform` / `curl_multi_poll` 管理多个 easy handle。
  提交和取消通过 `curl_multi_wakeup` 唤醒等待。每轮独立处理完成的请求，
  无需等待同批的其他请求。
- 一个 continuation 线程，串行恢复完成请求的协程。用户在 `co_await` 之后的代码
  不保证回到原调用线程。连续等待下一个请求会再次挂起，从而释放该线程。

所有 curl 读写、Header、Progress、Debug 等传输回调在 I/O 线程执行，需及时返回。
较长的后续计算会延迟其他协程恢复，适合交给应用自己的执行器处理；网络循环仍可推进。
DNS 是否需要额外线程或会阻塞，取决于构建 libcurl 时选用的解析器。

I/O 和 continuation 线程中调用 `sync_wait` 或 `Coro::Cleanup()` 会抛出
`std::logic_error`，避免等待自身退出。任务在初始调用线程内尚未挂起的代码，
以及已经完成任务的等待，不承诺线程切换。

此运行时直接使用 libcurl 与 C++23 标准库，不依赖 Asio，也不使用
`GlobalThreadPool` 等待网络结果。`Async::Cleanup()` 与协程运行时的生命周期相互独立。

## 取消与错误

`Task::Cancel()` 发出协作取消信号，第一次返回 `true`，重复请求返回 `false`。
`GetStopSource()` 返回可复制的 `std::stop_source`，可以在 Task 移动给消费者之后、
从其他线程请求取消。mcr Task 等待子 Task 时自动传播父任务的取消信号：

```cpp
auto request = mcr::GetCoro(mcr::Url{ "http://127.0.0.1:8080/stream" });
auto cancellation = request.GetStopSource();
request.Start();
cancellation.request_stop();
auto response = mcr::sync_wait(std::move(request));
```

启动前取消的普通 HTTP 请求不会联系服务器。运行中的请求由 I/O 线程从 multi 中
移除，随后恢复协程。取消结果位于 `Response::error`，代码为
`ErrorCode::ABORTED_BY_CALLBACK`；已完成的结果不会被随后到达的取消信号改写。
取消自身不会直接销毁协程帧。通用 Task 中的用户计算和外部 awaiter 仍需主动配合取消。

传输失败（含超时）保留在 `Response::error` 中；HTTP 4xx / 5xx 是普通响应。
选项配置、请求准备、用户回调和运行时异常会在 `co_await` 或 `sync_wait` 处抛出。
一个请求失败不会阻止其他请求完成；multi 本身的致命错误会结束运行时并通知所有待完成请求。

`DownloadCoro(path, options...)` 在首次启动时以 binary / trunc 模式打开目标文件，
请求完成后关闭文件，再返回正文为空的响应元数据。打开和关闭失败抛出异常；失败或取消
可能留下部分文件。传给 Task 的路径和选项均在调用时捕获。

## 清理

`mcr::Coro::Cleanup()` 停止接受新请求，取消排队和正在传输的请求，排空完成队列，
等待 I/O 与 continuation 线程退出后释放 multi handle。正常清理以
`ABORTED_BY_CALLBACK` 结束尚未完成的传输，已经完成的请求保留其结果。
方法可重复调用，也可以和提交、取消并发调用；开始关闭之后的新提交会抛出
`std::logic_error`。关闭期间恢复的协程如再发起新请求，同样会收到该异常。

清理是永久关闭，随后 `Startup()` 或启动新的 HTTP 协程任务会失败。
应用应先结束自己的工作链，在运行时线程之外调用清理，并在它返回之后才能执行
`curl_global_cleanup()`。静态析构提供退出时的兜底，但不替代显式的 curl 生命周期顺序。
清理只等待本运行时处理的 HTTP 请求及其恢复过程；用户协程若转而等待其他执行器，
仍需要由应用等待整条任务链。

## 上游参考与验证

HTTP API、选项和错误语义参考
[cpr api.h](https://github.com/libcpr/cpr/blob/master/include/cpr/api.h) 和
[cpr 异步测试](https://github.com/libcpr/cpr/blob/master/test/async_tests.cpp)。
`Task`、`*Coro`、停止令牌传播和独立的 multi 运行时是 mcr 的扩展，
与 cpr 的线程池 / future 异步封装不同。底层遵循
[libcurl multi](https://curl.se/libcurl/c/libcurl-multi.html)、
[poll](https://curl.se/libcurl/c/curl_multi_poll.html) 和
[wakeup](https://curl.se/libcurl/c/curl_multi_wakeup.html) 的约定。

`tests/test_task.cpp` 覆盖惰性执行、所有权、异常、父子取消和完成竞争；
`tests/test_coro.cpp` 使用本地 HTTP fixture 验证各方法、并发传输、线程分工、
取消、下载，以及关闭时的活动请求和排队请求。运行 `mcpp test` 验证整个库。
