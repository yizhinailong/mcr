# GlobalInit

[文档索引](../README.md) · [项目首页](../../README.md)

`mcr::GlobalInit` 位于 `mcr:global_init` 分区，用 RAII 管理 curl 的进程级初始化，
并在最后一个守卫销毁时按依赖顺序关闭库的进程级运行时。使用守卫不需要包含
`<curl/curl.h>`，也不需要手动调用 `curl_global_init()` 或 `curl_global_cleanup()`。

```cpp
import std;
import mcr;

auto main() -> int {
    auto curl = mcr::GlobalInit::Create();
    if (!curl) {
        std::println("初始化失败：{}", curl.error().message);
        return 1;
    }
    auto const response = mcr::Get(mcr::Url{ "http://127.0.0.1:8080/echo" });
    return response && !response->error ? 0 : 1;
} // response 先销毁，随后守卫关闭运行时并清理 curl。
```

## 是否需要守卫

守卫是可选的。不创建守卫时，第一个 curl 句柄会触发 libcurl 的隐式初始化，
`CurlHolder` 用内部互斥锁串行化句柄创建，进程退出时由操作系统回收 curl 全局资源，
行为与 cpr 一致。

以下情况建议使用守卫：

- 进程中还有其他代码直接使用 curl，需要在启动其他线程前完成初始化；
- 使用 `Async`、协程运行时或 `ConnectionPool`，需要确定的关闭顺序；
- 使用 valgrind、LeakSanitizer 等工具检查退出时的资源泄漏。

## 生命周期

- `Create()` 返回 `Result<GlobalInit>`。首个守卫调用 `curl_global_init(CURL_GLOBAL_DEFAULT)`，
  失败时返回对应的 curl 错误码。
- 守卫只能移动，不能复制。守卫按进程计数，嵌套创建和释放非最后一个守卫不会影响运行中的组件。
- 最后一个守卫销毁时依次执行：
  1. 已创建协程运行时时调用 `Coro::Cleanup()`；
  2. 已创建全局线程池时调用 `Async::Cleanup()`；
  3. 调用 `ConnectionPool::CollectUnused()` 回收已释放的共享状态；
  4. 调用 `curl_global_cleanup()`。
- 最后的清理是永久的：之后 `Coro::Startup()`、`Async::Startup()` 和 `GlobalInit::Create()`
  均返回 `FAILED_INIT`。守卫应覆盖整个使用期，通常放在 `main()` 开头。

若最后一个守卫在协程运行时线程或全局线程池任务中销毁，
运行时清理失败，或仍有连接池共享状态无法回收，守卫会跳过 `curl_global_cleanup()`，
把剩余资源留给进程退出时回收，避免释放仍在使用的 curl 状态。

销毁最后一个守卫前，应先结束 Session、响应、协程任务、异步结果，以及调用方直接持有的
`CurlHolder`、`CurlMultiHolder` 等 curl 句柄。

## 与手动管理混用

已经自行调用 `curl_global_init()` 和 `curl_global_cleanup()` 的程序可以继续这样做。
同一进程只选择一种方式管理清理顺序；若同时使用，自行调用的 `curl_global_cleanup()`
必须在最后一个守卫销毁之后执行。

## 与 cpr 的有意差异

cpr 不提供全局初始化守卫，依赖 libcurl 的隐式初始化且不调用全局清理。
mcr 保留同样的默认行为，并额外提供 `GlobalInit`，用于管理协程运行时、全局线程池和连接池
这些进程级组件的关闭顺序。
