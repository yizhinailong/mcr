# Singleton：单例生命周期

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.singleton`，派生自 `mcr::utils::Singleton<T>`：

```cpp
import mcr.singleton;

/**
 * @brief 演示只创建一次的服务。
 */
class Service final : public mcr::utils::Singleton<Service> {
    friend mcr::utils::Singleton<Service>;

    Service() = default;
    ~Service() = default;

public:
    /**
     * @brief 执行服务操作。
     */
    void Run() {}
};

/**
 * @brief 使用服务并永久关闭实例。
 * @return 关闭后不再提供实例时返回零。
 */
auto main() -> int {
    Service::GetInstance()->Run();
    Service::ExitInstance();
    return Service::GetInstance() == nullptr ? 0 : 1;
}
```

CRTP 基类替代 cpr 的 `CPR_SINGLETON_DECL` 和 `CPR_SINGLETON_IMPL` 宏。
派生类将对应的基类特化设为友元，默认构造保持私有以防止创建额外实例；
析构也可私有。基类禁止复制和移动，不需要独立实现定义。

与 cpr 的 `include/cpr/singleton.h` 和 `test/singleton_tests.cpp` 一致，
GetInstance 返回指针，每个派生类型惰性初始化一次，ExitInstance 后返回空指针。
构造异常正常传播，后续调用可以重试初始化。
关闭只析构一次，重复关闭无影响；进程退出时不会自动析构，必须显式 ExitInstance。
析构函数不得抛出异常。

支持 GetInstance 之间并发，也支持 ExitInstance 之间并发。
开始关闭前必须停止全部实例访问，并等待所有 GetInstance 调用结束。
返回的指针不延长实例生命周期。

有意差异是 C++23 模块、`mcr::utils` 命名空间和 CRTP 继承。
成功初始化前调用 ExitInstance 抛出 `std::logic_error`，
替代上游仅调试构建有效的断言；拒绝该调用后仍可初始化和关闭。

运行 `mcpp build` 和 `mcpp test`，验证模块与生命周期行为。
