# 异步运行时与全局任务提交

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.async`，使用 `mcr::GlobalThreadPool`、`async` 和 `Async`。
模块同时导出 `mcr.async_wrapper`，实现参考 cpr 的 `include/cpr/async.h` 和 `cpr/async.cpp`。
总入口还导出底层工具和常量；只导入 async 时，如需直接命名单例或线程池工具接口，
应另导入 `mcr.singleton` 或 `mcr.threadpool`。
库级入口位于 `mcr`，底层工具位于 `mcr::utils`，见 [工具模块](utils.md)。

```cpp
import std;
import mcr.async;

/**
 * @brief 提交任务，取回结果后关闭全局线程池。
 * @return 结果正确时返回零。
 */
auto main() -> int {
    mcr::Async::Startup(1, 4).value();
    auto result = mcr::async([](int value) { return value * 2; }, 21).value();
    auto answer = result.Get(); // 42
    mcr::GlobalThreadPool::GetInstance()->Wait();
    mcr::Async::Cleanup();
    return answer == 42 ? 0 : 1;
}
```

## 提交与取消

GlobalThreadPool 公开继承 `ThreadPool` 和 `Singleton<GlobalThreadPool>`，替代 cpr 的单例宏。
受保护构造使用线程池默认配置。`GetInstance()` 惰性创建唯一实例，
工作线程直到 Startup、Start 或任务提交时才启动。线程池不可复制或移动。

`async<is_cancellable = false>(fn, args...)` 将可调用对象和参数转发给
`mcr::utils::ThreadPool::Submit`，返回 `mcr::utils::AsyncWrapper<Result, is_cancellable>`。
已停止的池自动启动。参数按退化后的类型复制或移动，
支持成员指针、仅可移动的可调用对象/参数/结果、void 和引用结果。
需要保留参数引用时使用 `std::ref` 或 `std::cref`。
`async` 返回 `Result<AsyncWrapper<T, cancellable>>`，提交被拒绝时返回 `FAILED_INIT`；任务异常通过 Get 取回。丢弃普通包装对象不会等待任务结束。

`async<true>` 为包装对象创建私有共享取消标志，但与 cpr 一样，不把标志传给任务。
因此 Cancel 只改变包装状态并禁止后续 Get/等待，排队和运行中的任务仍会执行；
销毁包装对象也不会终止任务。Share 保留底层 future 语义，可读取已取消包装对象的结果。
需要任务观察取消时，应自行构造 AsyncWrapper 并向任务传入同一标志，见 [AsyncWrapper](async_wrapper.md)。

## 生命周期

| cpr | mcr |
| --- | --- |
| `async(fn, args...)` | `async(fn, args...)` |
| `async::startup(...)` | `Async::Startup(...)` |
| `async::cleanup()` | `Async::Cleanup()` |
| `GlobalThreadPool::GetInstance()` / `ExitInstance()` | 名称不变 |

`Startup(min, max, idle)` 缺省使用 `DEFAULT_THREAD_POOL_*` 常量。
对已停止的池，要求 `0 <= min <= max`、`max > 0` 且空闲时长为正，
验证全部参数后应用配置并启动最少数量的线程。无效参数不改变旧配置。
设置最小/最大值的顺序允许将两个边界一起提高到旧上限以上，或降低到旧下限以下。

池运行或暂停时，Startup 直接返回并忽略全部参数（包括无效值），与 cpr 一致。
Startup 必须与其他配置、启动、关闭和提交操作协调。

`Cleanup()` 永久关闭单例：用 `std::future_errc::broken_promise` 取消排队任务，
等待已领取任务并汇合工作线程。若需完成全部排队任务，先调用池的 `Wait()`。
成功清理后重复调用无影响；初始化之前清理抛出 `std::logic_error`，之后仍可初始化。
进程退出时没有自动单例析构。

清理后 GetInstance 返回空指针，async 和 Startup 返回 `FAILED_INIT`。
`Startup` 返回 `Result<void>`，停止状态下的无效启动参数返回 `BAD_FUNCTION_ARGUMENT`。
已销毁单例无法重启；需要再次启动时，应对存活线程池使用 `Stop()`。
清理必须在线程池工作线程之外执行，并先停止新提交及其他实例访问。
借用指针不延长池的生命周期；生命周期稳定期间支持并发提交。

## 与 cpr 的差异及验证

除命名和模块适配外，增加清理后访问检查、修改配置前的完整验证和适应有效边界的 setter 顺序。
可取消提交会先分配标志再提交，避免分配失败后已有任务运行却没有返回包装对象。
底层单例、线程池和包装对象的生命周期修正仍适用。

运行 `mcpp build` 和 `mcpp test`，使用本地可调用对象和 promise 控制的任务，
验证惰性启动、配置与重启、转发及结果类型、异常、并发生产者、取消语义，
以及包含排队和活动任务的永久清理。
