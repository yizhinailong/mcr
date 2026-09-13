# Singleton

Import `mcr.singleton` and derive from `mcr::utils::Singleton<T>`:

```cpp
import mcr.singleton;

class Service final : public mcr::utils::Singleton<Service> {
    friend mcr::utils::Singleton<Service>;

    Service() = default;
    ~Service() = default;

public:
    void Run() {}
};

int main() {
    Service::GetInstance()->Run();
    Service::ExitInstance();
    return Service::GetInstance() == nullptr ? 0 : 1;
}
```

The CRTP base replaces cpr's `CPR_SINGLETON_DECL` and `CPR_SINGLETON_IMPL`
macros. The derived class grants friendship to its base specialization and
keeps its default constructor private to prevent separate instances. Its
destructor can also be private. Copying and moving are disabled by the base.
No separate implementation definition is needed.

As in cpr's `include/cpr/singleton.h` and `test/singleton_tests.cpp`,
`GetInstance()` returns a pointer, initializes lazily once per derived type,
and returns `nullptr` after `ExitInstance()`. Constructor exceptions propagate
and a later call retries initialization. Shutdown destroys the instance once;
repeated shutdown calls have no effect. There is no automatic destruction at
process exit; call `ExitInstance()` explicitly. Destructors must not throw.

Concurrent `GetInstance()` calls are supported, as are concurrent
`ExitInstance()` calls. Callers must stop all access to the instance and finish
all `GetInstance()` calls before starting shutdown. Returned pointers do not
extend the lifetime of the instance.

Intentional differences from cpr are the C++23 modules, namespace `mcr::utils`, and
CRTP inheritance instead of macros. Calling `ExitInstance()` before successful
initialization throws `std::logic_error` instead of using cpr's debug-only
assertion. This rejected call leaves both initialization and shutdown available.

Run `mcpp build` and `mcpp test` to validate the module and lifecycle behavior.
