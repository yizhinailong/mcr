# AsyncWrapper：异步结果与取消

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.async_wrapper`，使用
`mcr::utils::AsyncWrapper<RetType, is_cancellable>`。
它参考 cpr 的 `include/cpr/async_wrapper.h`，拥有一个 `std::future<RetType>`，
可移动、不可复制，结果支持值、引用、void 和仅可移动类型。

```cpp
import std;
import mcr;

mcr::utils::ThreadPool pool{ 1, 2 };
auto result = mcr::utils::AsyncWrapper{ pool.Submit([] { return 42; }).value() };
result.Wait();
std::println("{}", result.Get());

auto state = std::make_shared<std::atomic_bool>(false);
auto future = pool.Submit([state] { return state->load() ? 0 : 7; }).value();
auto cancellable = mcr::utils::AsyncWrapper{ std::move(future), std::shared_ptr{ state } };
auto cancellation = cancellable.Cancel();
```

推导指引在仅传入 future 时选择普通版本，同时传入共享原子标志时选择可取消版本。
只有普通版本可默认构造；两者都允许无效 future。
普通 future 构造函数为 explicit；两种构造都以右值引用接收句柄，并在成功时消费它们。

## 结果访问

| cpr | mcr |
| --- | --- |
| `get()` | `Get()` |
| `valid()` | `Valid()` |
| `wait()` | `Wait()` |
| `wait_for(duration)` | `WaitFor(duration)` |
| `wait_until(deadline)` | `WaitUntil(deadline)` |
| `share()` | `Share()` |
| `Cancel()`、`IsCancelled()` | 名称不变 |

Get 等待并消费 future，返回结果或传播保存的异常。等待操作保留结果，
限时等待转发 ready、timeout、deferred 状态。
与 `std::future` 一致，延迟任务在不限时等待或 Get 时执行。
对无效 future 调用 Get 或等待会抛出带操作诊断的 `std::logic_error`。

Share 不抛异常，将所有权转给 `std::shared_future`；
无效包装产生无效 shared future，操作后包装不再有 future 状态。

## 取消与所有权

可取消版本公开继承普通版本，要求非空 `std::shared_ptr<std::atomic_bool>`。
任务必须观察同一标志，例如通过 `mcr::CancellationCallback`。
Cancel 将标志设置为 true，不能强制终止不配合的任务。
IsCancelled 独立于 future 状态报告标志；取消、消费、共享或移出后 Valid 为 false。

`CancellationResult` 保留 cpr 的名称和值：

| 枚举值 | 数值 | 含义 |
| --- | --- | --- |
| `failure` | 0 | 保留，本实现不返回 |
| `success` | 1 | 首次成功设置取消标志 |
| `invalid_operation` | 2 | 重复取消或没有有效 future |

已就绪但未消费的 future 仍可取消。
共享同一标志的包装对象并发调用 Cancel 时，只会有一次成功转换，
前提是 future 句柄和对象生命周期不变。

Get 和等待操作在开始前检查取消，已取消则抛出 `std::logic_error`。
取消不唤醒已经开始的等待，等待完成后也不再次检查。
继承的 Share 不检查取消，因此得到的 shared future 可在取消后读取结果，与 cpr 一致。

析构先设置保留的取消标志，再释放 future，即使已经 Get 或 Share 也是如此；
shared future 不会解除包装对象的析构取消行为。
移动赋值先取消并释放原操作，再接收新句柄；自移动保持不变。
释放 future 若丢弃运行中 `std::async` 状态的最后一个引用，可能阻塞。
包装是值类型，基类析构函数不是虚函数。

## 与 cpr 的差异及验证

- 空取消标志在消费源 future 前抛出 `std::invalid_argument`，避免稍后解引用空指针。
- 移出后的可取消包装可安全查询：Valid 和 IsCancelled 为 false，Cancel 返回 invalid_operation，
  访问结果按无效 future 抛出；析构不影响新拥有者。
- 移动赋值先通知原任务取消；上游默认赋值可能在未通知取消时等待原任务。
- 原子 exchange 代替分开的 load/store，避免并发取消同时报告成功。
- 私有字段采用 `m_`，诊断使用 mcr 操作名；安全状态查询、取消和普通 future 构造为 `noexcept`。

仅取消标志经过同步。同一包装对象的 Get、Share、移动、析构与其他访问需要协调。
包装自身不创建线程，可接收 `std::async`、promise 和 ThreadPool 的 future。

运行 `mcpp build` 和 `mcpp test`。测试参考 cpr 的 `test/multiasync_tests.cpp`，
用受控本地任务验证状态、结果类型、异常、共享、并发取消、移出后访问、
运行任务的析构和替换，以及 CancellationCallback 和 ThreadPool 集成。
