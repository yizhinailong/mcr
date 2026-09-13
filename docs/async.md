# Global async helpers

Import `mcr` or `mcr.async` to use `GlobalThreadPool`, `async`, and `Async`.
The module re-exports `mcr.async_wrapper` for its public result and cancellation types
and follows cpr's `include/cpr/async.h` and `cpr/async.cpp`. The `mcr` entry module
also exports the underlying utility types and constants. When importing only `mcr.async`,
add `mcr.singleton` or `mcr.threadpool` to name those utility interfaces directly.
These library-level entry points use namespace `mcr`; the underlying thread pool,
singleton, future wrapper, and their constants use `mcr::utils` (see [utility namespaces](utils.md)).

```cpp
import std;
import mcr.async;

int main() {
    mcr::Async::Startup(1, 4);
    auto result = mcr::async([](int value) { return value * 2; }, 21);
    auto answer = result.Get(); // 42
    mcr::GlobalThreadPool::GetInstance()->Wait();
    mcr::Async::Cleanup();
    return answer == 42 ? 0 : 1;
}
```

`GlobalThreadPool` publicly derives from `ThreadPool` and
`Singleton<GlobalThreadPool>`, replacing cpr's singleton macros. Its protected
constructor uses the existing pool defaults. `GetInstance()` initializes one
pool lazily; it does not start workers until `Startup`, `Start`, or submission.
Copying and moving the pool are unavailable.

`async<is_cancellable = false>(fn, args...)` forwards the callable and arguments
to `mcr::utils::ThreadPool::Submit` and returns `mcr::utils::AsyncWrapper<Result, is_cancellable>`.
The pool starts automatically if stopped. Arguments are decay-copied or moved,
and member pointers, move-only callables/arguments/results, `void`, and reference
results are supported. Use `std::ref` or `std::cref` to retain argument references.
Callable exceptions are retrieved through `Get()`; submission failures throw
from `async`. Discarding an ordinary wrapper does not wait for its task to finish.

`async<true>` creates a private shared cancellation flag for its wrapper. As in
cpr's helper, it does not pass that flag to the task. `Cancel()` therefore changes
wrapper state and blocks subsequent `Get`/wait calls, while queued and running
tasks still execute. Destruction of the wrapper also does not stop its task.
`Share()` retains the underlying future semantics and can retrieve a cancelled
wrapper's result. For a task that must observe cancellation, construct an
`AsyncWrapper` with a flag also supplied to that task, as described in
[async_wrapper.md](async_wrapper.md).

The lifecycle API follows project naming conventions:

| cpr | mcr |
| --- | --- |
| `async(fn, args...)` | `async(fn, args...)` |
| `async::startup(...)` | `Async::Startup(...)` |
| `async::cleanup()` | `Async::Cleanup()` |
| `GlobalThreadPool::GetInstance()` / `ExitInstance()` | Unchanged |

`Startup(min, max, idle)` uses the existing `DEFAULT_THREAD_POOL_*` values when
arguments are omitted. For a stopped pool it requires `0 <= min <= max`,
`max > 0`, and a positive idle duration, applies the configuration, then starts
the minimum number of workers. Invalid settings leave the prior configuration
unchanged. The order of the minimum/maximum setters permits increasing both
limits above the previous maximum or decreasing both below the previous minimum.
Calls while running or paused return immediately and ignore their arguments,
including invalid ones, matching cpr. Startup must be coordinated with other
configuration, startup, shutdown, and submission operations.

`Cleanup()` delegates to the existing singleton's permanent shutdown. It cancels
queued tasks with `std::future_errc::broken_promise`, waits for claimed tasks, and
joins workers. Call `Wait()` before cleanup to finish all queued work. Repeated
cleanup is harmless after a successful cleanup. Cleanup before initialization
throws `std::logic_error` and still permits later initialization. There is no
automatic singleton destruction at process exit.

After cleanup, `GetInstance()` returns null and `async` and `Startup` throw
`std::logic_error`. A destroyed singleton cannot restart; use `Stop()` on the
live pool when a later restart is needed. Cleanup must run outside pool workers
after new submissions and other instance access have stopped. Borrowed pool
pointers do not extend its lifetime. Concurrent task submissions are supported
while the singleton's lifetime is stable.

Intentional differences beyond naming and module adaptation are the checks for
use after cleanup, complete startup validation before changing settings, and
the setter ordering required by this project's validated thread pool. For
cancellable submissions, the flag is allocated before submitting work, so a
flag allocation failure cannot leave a task running without a returned wrapper.
The existing singleton, pool, and wrapper lifecycle fixes continue to apply.

Run `mcpp build` and `mcpp test`. Tests use local callables and promise-controlled
tasks to verify lazy startup, configuration and restart, forwarding and result
types, exceptions, concurrent producers, cancellation semantics, and permanent
cleanup with both queued and active work.
