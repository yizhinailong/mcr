# ConnectionPool：共享连接与 TLS 会话

[文档索引](../README.md) · [项目首页](../../README.md)

导入 `mcr`，使用 `mcr::ConnectionPool`。
实现参考 cpr 的 `include/cpr/connection_pool.h` 和 `cpr/connection_pool.cpp`。
它创建 CURLSH 句柄，启用 `CURL_LOCK_DATA_CONNECT` 和 `CURL_LOCK_DATA_SSL_SESSION`，
共享可复用连接和 TLS 会话；不启用 Cookie 或 DNS 共享。

```cpp
#include <curl/curl.h>

import std;
import mcr;

// 成功初始化 curl 后执行，全部资源必须先于 curl 全局清理销毁。
{
    auto pool = mcr::ConnectionPool::Create().value();
    auto first = mcr::curl::CurlHolder::Create().value();
    auto second = mcr::curl::CurlHolder::Create().value();
    pool.SetupHandler(first).value();
    pool.SetupHandler(second).value();
    // 通过 libcurl 配置并顺序执行请求。
    // 每个 CurlHolder 都保留共享状态，直到 easy 句柄清理结束。
}
```

`SetupHandler(curl::CurlHolder&) const` 通过 `CURLOPT_SHARE` 将空闲 easy 句柄附加到共享句柄，
并将共享状态的所有权保存在 holder 中。它不执行请求或接管 easy 句柄。
高级接口 `SetupHandler(CURL*) const` 只设置共享关系，借用连接池的生命周期。
复制构造共享缓存与锁存储，复制赋值与 cpr 一样被删除。
没有消费源对象的移动操作：从右值构造调用复制构造，源仍可用；从右值赋值也不可用。

## 生命周期与线程限制

`Session::SetConnectionPool()` 和接收 CurlHolder 的重载保留共享状态，支持临时连接池。
即使通过 `GetCurlHolder()` 使句柄比 Session 活得更久，或移动 CurlHolder，
共享状态仍保留到 `curl_easy_cleanup()` 完成；`curl_easy_reset()` 不会解除共享关系。

使用原始 `CURL*` 重载时，全部附加的 easy 句柄清理或显式解除共享之前，必须保留至少一个连接池副本。
解除方式为 `curl_easy_setopt(easy, CURLOPT_SHARE, static_cast<CURLSH*>(nullptr))`。
原始 easy 指针不持有 C++ 连接池的所有权。

最后一个所有者调用 `curl_share_cleanup()` 并检查结果，成功后才释放互斥量；
清理过程中保留加锁/解锁回调。若原始借用句柄仍附加，libcurl 返回 `CURLSHE_IN_USE`，
库保留共享句柄和锁存储，防止回调访问已销毁的互斥量。
解除这些借用句柄后，在 curl 全局清理前调用 `ConnectionPool::CollectUnused()` 重试回收；
返回值为仍未释放的状态数量。`Create()` 也会重试回收。
常规 Session 与 CurlHolder 用法无需显式回收。

跨线程使用连接池必须串行化。libcurl 不支持多个线程并发共享连接缓存，即使提供锁回调也是如此。
并发工作线程应使用各自的池，或在一个线程中用同一个 multi 句柄驱动并发传输。
该限制来自 `CURL_LOCK_DATA_CONNECT`，cpr 的异步示例不消除限制。
调用方也负责 curl 的全局初始化和清理。见 [libcurl 线程安全说明](https://curl.se/libcurl/c/threadsafe.html)。

## 与 cpr 的差异及验证

- 按 curl 数据类型分别索引锁，遵循 `CURLSHOPT_LOCKFUNC` / `CURLSHOPT_UNLOCKFUNC`，
  不使用上游覆盖全部共享数据的单个互斥量。回调不允许 C++ 异常穿过 libcurl。
- `ConnectionPool::Create() -> Result<ConnectionPool>` 在 curl 初始化或共享配置失败时返回标明操作的 `FAILED_INIT`，自动释放部分创建的状态。不支持 TLS 会话共享属于配置错误，
  C++ 分配失败传播 `std::bad_alloc`。
- `SetupHandler` 返回 `Result<void>`；空句柄返回 `BAD_FUNCTION_ARGUMENT`，设置 CURLOPT_SHARE 失败返回映射后的 curl 错误；上游忽略这些 curl 错误。
- Session 与 CurlHolder 的接入保留共享状态，清理失败时保留回调及存储；这是对上游借用生命周期的扩展。
- 先分配共享锁存储，再初始化 curl；配置选项前先建立 curl 所有权，以覆盖构造失败路径。
- 私有成员采用 `m_` 命名，创建入口和返回类型见 [expected 接口迁移](../expected_migration.md)。

运行 `mcpp build` 和 `mcpp test`。测试使用绑定动态回环端口的 HTTP/1.1 服务：
三个独立请求建立三条连接，四个共享池副本的请求只建立一条。
还验证临时池、局部副本销毁、外部持有及移动句柄、延迟回收、句柄替换与解除共享、空输入及初始化/TLS 缓存配置中的分配失败，
并通过内存回调检查释放。该测试不执行 TLS 握手或不受支持的跨线程并发共享。
