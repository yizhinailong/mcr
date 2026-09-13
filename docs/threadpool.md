# Thread pool

Import `mcr.threadpool` to use `mcr::utils::ThreadPool`.

```cpp
import std;
import mcr.threadpool;

mcr::utils::ThreadPool pool{1, 4};
auto answer = pool.Submit([](int value) { return value * 2; }, 21);
std::println("{}", answer.get()); // 42
pool.Wait();
```

Construction creates a stopped pool. `Start(count)` clamps the initial worker
count to the configured minimum and maximum; `Start()` selects the minimum.
`Submit` automatically starts a stopped pool and grows it as work exceeds the
number of available workers. Idle workers above the minimum retire after the
configured timeout. A zero minimum allows every idle worker to retire; a later
submission starts another worker. Finished thread records are joined and reclaimed
on submission, startup, thread-limit changes, or shutdown.

`Submit(fn, args...)` returns `std::future<T>`, including `void` and reference
results. Callable exceptions are stored in that future. Callables and arguments
are decay-copied or moved into owned storage and invoked once as rvalues, like
`std::thread`; use `std::ref` or `std::cref` when a reference is required. Member
pointers and move-only callables and arguments are supported.

`Pause()` prevents workers from claiming additional tasks. Already claimed tasks
can continue. Submissions remain queued until `Resume()`. `Wait()` blocks on a
condition variable until the queue is empty and all claimed tasks have finished;
paused pending work therefore requires another caller to resume or stop the pool.
Concurrent producers can add work after `Wait()` observes an idle pool.

`Stop()` cancels queued work, waits for claimed tasks, and joins workers. Canceled
futures become ready with `std::future_errc::broken_promise`; canceled tasks are
not retained for the next start. The destructor performs the same shutdown. Call
`Wait()` before destruction if all queued work must finish. A running callable
cannot be interrupted by the pool.

Public methods synchronize configuration, state, counts, and the queue. During
shutdown, `Submit` throws `std::runtime_error`, while `Start` and additional `Stop`
calls return -1. Once `Stop` completes, the pool can restart. `IsStarted()` is true
when running or paused; `IsStopped()` becomes true after all workers are joined.
Both return false during shutdown. As with other C++ objects, destruction requires
other callers to have stopped accessing the pool. Calling `Wait` or `Stop` from
one of the pool's own tasks throws `std::logic_error`. Do not destroy a pool from
its own task, or synchronously wait for queued child tasks when all workers are
occupied by their parents.

Configuration uses `SetMinThreadNum`, `SetMaxThreadNum`, and `SetMaxIdleTime`, with
matching getters. Limits require `0 <= min <= max` and `max > 0`; the idle timeout
must be positive. Invalid values throw `std::invalid_argument`. Raising the
minimum creates workers in a started pool. Lowering the maximum lets excess
workers finish their claimed tasks before retiring, so the live count can exceed
the new maximum temporarily. Thread creation failures propagate their standard
exceptions; workers already created remain usable.

Differences from the referenced cpr implementation:

- Public types live in `mcr`, exported by C++23 modules. Defaults are named
  `DEFAULT_THREAD_POOL_MIN_THREAD_NUM`, `DEFAULT_THREAD_POOL_MAX_THREAD_NUM`, and
  `DEFAULT_THREAD_POOL_MAX_IDLE_TIME`. The maximum falls back to one when hardware
  concurrency is unknown.
- Configuration fields are private and accessed through synchronized methods.
  Queue checks and lifecycle transitions are synchronized; `Wait` does not spin.
- Shutdown cancels pending futures instead of retaining queued tasks for restart.
  Workers are joined without holding the pool mutex. Cancellation explicitly
  completes each future with `broken_promise`, avoiding the current Clang 22/MSVC
  `import std` double-free in `packaged_task`'s automatic abandonment path.
- Submission uses `std::invoke` and owned, forwarded arguments to support member
  pointers and move-only values. Use reference wrappers for lvalue references.
- cpr's lifecycle return conventions remain: `Start` and `Stop` return zero on a
  transition and -1 if unavailable; `Pause` and `Resume` return zero. Invalid
  configuration and unsupported self-waits report exceptions.

Run `mcpp test --timeout 20` to validate lifecycle, pause/resume, resizing, task
ownership and exceptions, concurrent producers, cancellation, and restart.
