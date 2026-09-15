# CurlMultiHolder：multi 句柄所有权

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.curlmultiholder`，使用 `mcr::curl::CurlMultiHolder`。
其他接口与迁移规则见 [curl 后端](curl.md)。
实现参考 cpr 的 `include/cpr/curlmultiholder.h` 和 `cpr/curlmultiholder.cpp`：
`CurlMultiHolder::Create() -> Result<CurlMultiHolder>` 调用 curl_multi_init，公开 `CURLM* handle` 用于访问 libcurl，
析构调用 curl_multi_cleanup。

```cpp
#include <curl/curl.h>

import std;
import mcr.curlmultiholder;

/**
 * @brief 初始化 curl 并执行一次空 multi 操作。
 * @return 操作成功时返回零。
 */
auto main() -> int {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return 1;
    }
    int result{ 0 };
    try {
        auto multi = mcr::curl::CurlMultiHolder::Create().value();
        int running{ 0 };
        if (curl_multi_perform(multi.handle, &running) != CURLM_OK) {
            result = 1;
        }
    } catch (std::exception const&) {
        result = 1;
    }
    curl_global_cleanup();
    return result;
}
```

该包装只拥有 multi 句柄。经 curl_multi_add_handle 添加的 easy 句柄仍由 CurlHolder 等原拥有者管理。
清理 easy、销毁 multi 或替换移动赋值目标前，必须用 curl_multi_remove_handle 移除每个 easy。
这符合 libcurl 的清理顺序及 cpr 的 MultiPerform 用法。

关闭缓存连接可能触发套接字回调，因此回调数据必须保持有效直到 multi 清理结束。
销毁和替换必须在同一 multi 的回调之外执行，调用方负责 curl 全局初始化和清理。

与 cpr 的差异与 CurlHolder 保持一致：

- 初始化失败返回标明 curl_multi_init 的 `FAILED_INIT`，不依赖仅调试构建有效的断言。
- 禁止复制，防止共享所有权和重复清理。
- 移动为 noexcept，转移原句柄并保留选项及附加的 easy 句柄。
  源变为空，仍可安全销毁或赋值；移动赋值先释放目标旧句柄，自移动保持原值。

原始指针保持公开。直接赋值时，调用方必须释放旧句柄并将替换句柄的独占所有权交给包装。
该类型自身不提供 MultiPerform API。

运行 `mcpp build` 和 `mcpp test`，验证所有权和移动、空 multi 操作、
无需网络的两个 URL 失败、移除后 easy 复用、分配失败，以及通过 curl 自定义内存回调检查资源释放。
