# AsyncWrapper

Import `mcr` or `mcr.async_wrapper` to use `mcr::utils::AsyncWrapper<RetType, is_cancellable>`.
It follows cpr's `include/cpr/async_wrapper.h` and owns a `std::future<RetType>`.
Wrappers can be moved, but cannot be copied. Results may be values, references,
`void`, or move-only types.

```cpp
import std;
import mcr;

mcr::utils::ThreadPool pool{ 1, 2 };
auto result = mcr::utils::AsyncWrapper{ pool.Submit([] { return 42; }) };
result.Wait();
std::println("{}", result.Get());

auto state = std::make_shared<std::atomic_bool>(false);
auto future = pool.Submit([state] { return state->load() ? 0 : 7; });
auto cancellable = mcr::utils::AsyncWrapper{ std::move(future), std::shared_ptr{ state } };
auto cancellation = cancellable.Cancel();
```

The deduction guides select the ordinary specialization when given a future
and the cancellable specialization when also given a shared atomic flag.
Only the ordinary specialization can be default-constructed. Both accept an
invalid future. The ordinary future constructor is explicit; both constructors
take their handles by rvalue reference and consume them on success.

Public future operations use the project's naming convention:

| cpr | mcr |
| --- | --- |
| `get()` | `Get()` |
| `valid()` | `Valid()` |
| `wait()` | `Wait()` |
| `wait_for(duration)` | `WaitFor(duration)` |
| `wait_until(deadline)` | `WaitUntil(deadline)` |
| `share()` | `Share()` |
| `Cancel()`, `IsCancelled()` | Unchanged |

`Get()` waits for and consumes the future, returning its result or propagating
the stored exception. Wait operations retain the result, and timed waits
forward `ready`, `timeout`, and `deferred` status values. Deferred work runs on
an untimed wait or `Get`, as with `std::future`. Calls to `Get` or waits on an
invalid future throw `std::logic_error` with an operation-specific diagnostic.
`Share()` transfers ownership into `std::shared_future` without throwing; an
invalid wrapper produces an invalid shared future. The wrapper then has no
future state.

The cancellable specialization publicly derives from the ordinary one and
requires a nonnull `std::shared_ptr<std::atomic_bool>`. The same flag must be
observed by the task, for example through `mcr::CancellationCallback`.
`Cancel()` changes the flag to true; it cannot force an uncooperative task to
stop. `IsCancelled()` reports the flag independently of the future's state.
`Valid()` is false when cancelled or when the future has been consumed, shared,
or moved out.

`CancellationResult` retains cpr's enumerators and values: `failure = 0`
(reserved and not returned here), `success = 1`, and `invalid_operation = 2`.
The first successful flag transition returns `success`. Repeated cancellation,
or cancellation without a valid future, returns `invalid_operation`.
A ready but unconsumed future can still be cancelled. Concurrent `Cancel`
calls on wrappers sharing a flag produce exactly one successful transition,
provided their future handles and object lifetimes remain unchanged.

Cancellation is checked before `Get()` and wait operations, which throw
`std::logic_error` if it was already requested. It does not wake a wait that
has already started, and there is no second cancellation check after waiting.
The inherited `Share()` does not check cancellation, matching cpr; the resulting
shared future can retrieve a result even after cancellation.

Destruction signals the retained flag before releasing the future, including
after `Get()` or `Share()`. A shared future therefore does not detach the task
from the wrapper's cancellation-on-destruction behavior. Move assignment signals
the old flag before releasing its old future, then takes the incoming handles.
Self-move leaves a cancellable wrapper unchanged. Future release can block when
it drops the last reference to a running `std::async` state. Wrappers are value
types with a nonvirtual base destructor, as in cpr.

Intentional differences and fixes beyond module and method naming:

- A null cancellation flag throws `std::invalid_argument` before the source
  future is consumed, instead of allowing cpr's later null dereference.
- Moved-from cancellable wrappers are safe to query: `Valid()` and
  `IsCancelled()` are false, `Cancel()` returns `invalid_operation`, and result
  access throws for the invalid future. Destruction does not affect the owner.
- Move assignment cancels the replaced operation before releasing its future;
  cpr's defaulted assignment can wait on a task without signalling cancellation.
- Cancellation uses an atomic exchange rather than a separate load and store,
  so racing requests cannot both report success.
- Private members use `m_` names, diagnostics name `mcr` operations, and safe
  state queries, cancellation, and ordinary future construction are `noexcept`.

Only the flag is synchronized. Coordinate `Get`, `Share`, moving, and destruction
with other accesses to the same wrapper. The wrapper does not create threads
or introduce global async helpers; it works with futures from `std::async`,
promises, and the existing `ThreadPool`.

Run `mcpp build` and `mcpp test`. Tests adapt the standalone wrapper behavior
from cpr's `test/multiasync_tests.cpp` using controlled local tasks, covering
future status and result types, exceptions, sharing, concurrent cancellation,
moved-from access, destruction and replacement of running tasks, and integration
with `CancellationCallback` and `ThreadPool`.
