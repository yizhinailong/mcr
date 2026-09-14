# ThreadPool：线程池

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.threadpool`，使用 `mcr::utils::ThreadPool`。

```cpp
import std;
import mcr.threadpool;

mcr::utils::ThreadPool pool{ 1, 4 };
auto answer = pool.Submit([](int value) { return value * 2; }, 21);
std::println("{}", answer.get()); // 42
pool.Wait();
```

## 启动与任务所有权

构造产生已停止的线程池。`Start(count)` 将初始线程数限制在配置的最小与最大值之间，
`Start()` 选择最小值。Submit 自动启动已停止的池，并在工作量超过可用线程时增加线程。
超过最小数量的空闲线程会在超时后退出；最小值为零时允许全部空闲线程退出，
随后提交会再次启动线程。提交、启动、调整线程数或关闭时，会汇合并回收已结束的线程记录。

`Submit(fn, args...)` 返回 `std::future<T>`，包括 void 和引用结果，任务异常保存在 future 中。
可调用对象和参数按退化后的类型复制或移动到独立存储，并像 `std::thread` 一样以右值调用一次。
需要引用时使用 `std::ref` 或 `std::cref`；支持成员指针及仅可移动的可调用对象和参数。

## 暂停、等待与停止

Pause 阻止工作线程领取新任务，已领取的任务可继续执行，新提交留在队列直到 Resume。
Wait 通过条件变量等待队列为空且已领取任务全部完成。
暂停状态下有排队任务时，需要其他调用方恢复或停止线程池；
Wait 观察到空闲之后，并发生产者仍可提交新任务。

Stop 取消排队任务，等待已领取任务并汇合线程。
被取消的 future 以 `std::future_errc::broken_promise` 就绪，不保留到下一次启动。
析构执行同样的关闭；若必须完成全部排队工作，应在析构前 Wait。
池不能中断正在执行的任务。

公开方法对配置、状态、计数和队列进行同步。
关闭期间 Submit 抛出 `std::runtime_error`，Start 和额外的 Stop 返回 -1；
Stop 完成后可以重启。IsStarted 在运行或暂停时为 true，
IsStopped 在全部线程汇合后为 true，关闭过程中两者均为 false。

析构前其他调用方必须停止访问该对象。
在线程池自己的任务中调用 Wait 或 Stop 会抛出 `std::logic_error`。
不可在池自身任务中销毁池；当全部线程被父任务占用时，也不可同步等待队列中的子任务。

## 配置、差异与验证

配置接口为 SetMinThreadNum、SetMaxThreadNum、SetMaxIdleTime 及对应 getter。
要求 `0 <= min <= max`、`max > 0` 且空闲时长为正，无效值抛出 `std::invalid_argument`。
提高最小值会为已启动的池创建线程；降低最大值允许多余线程完成已领取任务后退出，
因此实时数量可能暂时超过新上限。线程创建失败传播标准异常，已创建线程仍可用。

与 cpr 的有意差异：

- 类型位于 `mcr::utils`，通过 C++23 模块导出。默认常量为
  `DEFAULT_THREAD_POOL_MIN_THREAD_NUM`、`DEFAULT_THREAD_POOL_MAX_THREAD_NUM`、
  `DEFAULT_THREAD_POOL_MAX_IDLE_TIME`；无法确定硬件并发数时，最大值回退为一。
- 配置字段私有且通过同步方法访问，队列检查和生命周期转换均同步，Wait 不忙等。
- 关闭取消排队 future，不保留到重启；汇合线程时不持有池互斥量。
  取消显式以 broken_promise 完成 future，规避 Clang 22/MSVC 的 `import std;`
  在 packaged_task 自动放弃路径上的重复释放问题。
- 提交使用 `std::invoke` 和拥有存储的转发参数，支持成员指针及仅可移动值。
  内部队列使用 C++23 的 `std::move_only_function` 直接持有 packaged_task，
  无需为复制任务包装器额外引入共享所有权。提交失败时显式取消任务，
  在池互斥量之外释放捕获资源。
- 保留 cpr 的生命周期返回约定：Start、Stop 在发生转换时返回零，不可执行时返回 -1；
  Pause、Resume 返回零。无效配置和不支持的自身等待通过异常报告。

运行 `mcpp test --timeout 20`，验证生命周期、暂停恢复、扩缩容、任务所有权、
异常、并发生产者、取消和重启。
`tests/test_threadpool_allocation.cpp` 逐一注入提交时的内存分配失败，
检查独占捕获的释放、已接受任务的取消及线程池恢复。
