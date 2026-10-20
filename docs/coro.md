# 协程请求

[文档索引](README.md) · [项目首页](../README.md)

`import mcr;` 提供 `mcr::Task<T>`、`mcr::sync_wait`、`mcr::Coro`、
`mcr::this_coro::stop_token`，以及
`GetCoro` / `PostCoro` / `PutCoro` / `HeadCoro` / `DeleteCoro` /
`OptionsCoro` / `PatchCoro` / `DownloadCoro`。

公共请求函数位于 `src/api.cppm`（`mcr:api` 分区）；任务和运行时位于
`src/coro/task.cppm`（`mcr:task` 分区）及 `src/coro/runtime.cppm`（`mcr:coro_runtime` 分区）。
这些分区均由 `import mcr;` 统一导出；`mcr:api` 同时导出任务类型。

## 基本用法

```cpp
import std;
import mcr;

auto fetch_text(mcr::Url url) -> mcr::Task<mcr::Result<std::string>> {
    auto response = co_await mcr::GetCoro(
        std::move(url), mcr::options::Timeout{ std::chrono::seconds{ 5 } }
    );
    if (!response) {
        co_return std::unexpected{ std::move(response.error()) };
    }
    if (response->error) {
        co_return std::unexpected{ std::move(response->error) };
    }
    co_return std::move(response->text);
}

// 在已完成 curl 全局初始化的普通函数中：
auto text = mcr::sync_wait(fetch_text(mcr::Url{ "https://example.com" }));
if (!text) {
    std::println("请求失败：{}", text.error().message);
}
mcr::Coro::Cleanup().value(); // 所有协程工作结束后，在 curl_global_cleanup() 之前调用。
```

HTTP 方法和选项沿用同步 API，包括 Header 合并、Body / Payload、
代理、TLS、重定向、回调和响应解析。每次调用拥有自己的 Session。
这批 API 提供一次性请求；Session 的同步拦截器不能直接作为协程调度器使用。

`GetCoro` 等调用时立即复制普通左值选项、移动右值选项，首次启动时才配置并提交请求。
`BodyView`、Multipart 中的 Buffer、`std::ref` / `std::cref`、回调捕获对象和
ConnectionPool 仍有借用语义，必须存活到请求结束。对用户自行编写的协程，建议像
示例一样按值接收需拥有的数据；协程的引用参数不会自动延长被引用对象的生命周期。

## Task 的启动、等待与所有权

`Task<T>` 仅移动、单次消费，支持值、仅可移动的值、左值引用和 `void`；
不支持右值引用结果。任务有四种使用方式：

- `co_await task_expression`：在协程中启动并等待。
- `task.Start()`：立即执行到挂起点，显式启动任务但保留结果；重复调用不重复执行。
- `mcr::Coro::Spawn(std::move(task))`：排队启动并返回 `Result<Task<T>>`，
  保留任务结果和取消入口；首次执行发生在后续处理线程。
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

auto a = mcr::sync_wait(std::move(first)).value();
auto b = mcr::sync_wait(std::move(second)).value();
```

## 排队启动与协作调度

`Coro::Spawn` 在返回前将未启动任务的协程帧所有权交给运行时，
提交成功后可以等待返回的 Task、保存其停止源，或丢弃结果让任务自行完成。
任务可能在 `Spawn` 返回前已经由另一线程启动，但不会在提交调用栈中直接执行。
已启动的任务只转移结果所有权，不重复启动或改变当前执行位置。
空任务抛出 `std::logic_error`；运行时初始化或关闭导致的拒绝通过 Result 返回，
被拒绝的未启动任务会释放协程帧，不执行函数体。

```cpp
auto first = mcr::Coro::Spawn(mcr::GetCoro(mcr::Url{ "http://127.0.0.1:8080/first" })).value();
auto second = mcr::Coro::Spawn(mcr::GetCoro(mcr::Url{ "http://127.0.0.1:8080/second" })).value();
auto a = mcr::sync_wait(std::move(first)).value();
auto b = mcr::sync_wait(std::move(second)).value();
```

在协程中使用 `(co_await mcr::Coro::Schedule()).value()` 将后续代码排入同一恢复队列。
从普通线程调用时切换到后续处理线程；已经位于该线程时，让排在前面的工作先执行。
每次调用返回一个仅移动、单次使用的 awaiter。若运行时拒绝调度，等待立即结束，
返回失败的 `Result<void>`，代码继续在当前线程运行，调用方应检查结果。

`Spawn` 和 `Schedule` 调度任务执行位置；外部 awaiter 仍可能在其他线程恢复协程。
需要再次进入后续处理线程时，显式等待 `Schedule()`。排队期间协程帧必须有效；
`Task` 自行管理已启动帧，其他协程框架需要由调用方保证等待者的生命周期。

## 网络与恢复线程

运行时首次提交时自动启动，也可以显式 `mcr::Coro::Startup()`。它拥有：

- 一个 I/O 线程，通过 `curl_multi_perform` / `curl_multi_poll` 管理多个 easy handle。
  提交和取消通过 `curl_multi_wakeup` 唤醒等待。每轮独立处理完成的请求，
  无需等待同批的其他请求。
- 一个后续处理线程，串行恢复完成请求的协程。用户在 `co_await` 之后的代码
  不保证回到原调用线程。连续等待下一个请求会再次挂起，从而释放该线程。
  `Spawn` 的初始执行、`Schedule` 的恢复与已完成的 HTTP 请求共用先进先出的队列，
  执行用户代码时不持有运行时锁。

所有 curl 读写、Header、Progress、Debug 等传输回调在 I/O 线程执行，需及时返回。
较长的后续计算会延迟其他协程恢复，适合交给应用自己的执行器处理；网络循环仍可推进。
DNS 是否需要额外线程或会阻塞，取决于构建 libcurl 时选用的解析器。

I/O 和后续处理线程中调用 `sync_wait` 仍抛出 `std::logic_error`；
`Coro::Cleanup()` 返回 `RECURSIVE_API_CALL`，避免等待自身退出。任务在初始调用线程内尚未挂起的代码，
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
auto response = mcr::sync_wait(std::move(request)).value();
```

用户协程可以通过 `auto stop = co_await mcr::this_coro::stop_token;` 取得自己的停止令牌，
并在计算循环或外部异步操作之间检查 `stop.stop_requested()`。查询不挂起，
也不会因令牌已经取消而自动抛出异常。排队等待 `Spawn` 启动期间也可以请求取消。

启动前取消的普通 HTTP 请求不会联系服务器。运行中的请求由 I/O 线程从 multi 中
移除，随后恢复协程。取消结果位于 `Response::error`，代码为
`ErrorCode::ABORTED_BY_CALLBACK`；已完成的结果不会被随后到达的取消信号改写。
取消自身不会直接销毁协程帧。通用 Task 中的用户计算和外部 awaiter 仍需主动配合取消。

传输失败（含超时）保留在 `Response::error` 中；HTTP 4xx / 5xx 是普通响应。
HTTP 协程返回 `Task<Result<Response>>`。选项配置、请求准备及 curl multi 失败通过 Result 返回；
用户回调和标准库资源异常仍在 `co_await` 或 `sync_wait` 处传播。
一个请求失败不会阻止其他请求完成；multi 本身的致命错误会结束运行时并通知所有待完成请求。

`DownloadCoro(path, options...)` 在首次启动时以 binary / trunc 模式打开目标文件，
请求完成后关闭文件，再返回正文为空的响应元数据。打开和关闭失败返回 `WRITE_ERROR`；失败或取消
可能留下部分文件。传给 Task 的路径和选项均在调用时捕获。

## 清理

`mcr::Coro::Cleanup()` 停止接受新请求，取消排队和正在传输的请求，排空完成队列，
等待 I/O 与后续处理线程退出后释放 multi handle。正常清理以
`ABORTED_BY_CALLBACK` 结束尚未完成的传输，已经完成的请求保留其结果。
方法可重复调用，也可以和提交、取消并发调用；开始关闭之后的新提交返回 `FAILED_INIT`。
关闭期间恢复的协程如再发起新请求，同样会收到该错误。
清理还会排空已经接受的 `Spawn` 启动和 `Schedule` 恢复；任务继续发起 HTTP 请求或
再次调度时会收到 `FAILED_INIT`。尚未开始的通用任务仍会执行其函数体，用户计算的停止由
任务自身的协作取消逻辑负责。

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

排队启动和协程上下文查询参考
[Asio 1.38.2 co_spawn](https://github.com/chriskohlhoff/asio/blob/asio-1-38-2/include/asio/impl/co_spawn.hpp) 和
[C++20 协程说明](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/composition/cpp20_coroutines.html)。
mcr 使用 `Coro::Spawn` 返回可消费的 Task，通过 `Schedule` 显式调度；
父子 Task 使用 C++ 协程句柄的对称转移，网络仍由 libcurl 驱动。
这些接口不依赖 Asio 的完成令牌、executor 或 `awaitable_thread` 帧栈。

`tests/test_task.cpp` 覆盖惰性执行、所有权、异常、父子取消和完成竞争；
`tests/test_coro.cpp` 使用本地 HTTP 测试服务 验证各方法、并发传输、线程分工、
排队启动、协作调度、取消、下载，以及关闭时的活动请求、排队请求和已接受的恢复工作。
运行 `mcpp test` 验证整个库。
