# 错误处理

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.error`，使用 `mcr::ErrorCode`、`Error` 和
`Result<T>`（即 `std::expected<T, mcr::Error>`）。
`Result<void>` 表示没有返回值的操作。实现位于 `src/utils/error.cppm`，公开接口仍在 mcr 命名空间。

`mcr::check_curl_error(curl_code, message)` 在 CURLE_OK 时返回成功的 `Result<void>`，
其他状态返回 `std::unexpected<mcr::Error>`。
错误拥有消息文本，按输入保留，缺省为空；有详细诊断时可传入 curl 错误缓冲区。

```cpp
import std;
import mcr;

/**
 * @brief 将 curl 状态转换为正文结果。
 * @param curl_code curl 状态码。
 * @param diagnostic 错误诊断。
 * @param body 请求正文。
 * @return 成功时返回正文，否则返回错误。
 */
auto finish_transfer(std::int32_t curl_code, std::string diagnostic,
                     std::string body) -> mcr::Result<std::string> {
    auto status = mcr::check_curl_error(curl_code, std::move(diagnostic));
    if (!status) {
        return std::unexpected{ std::move(status.error()) };
    }
    return body;
}

// !result 时可读取 result.error().code 和 result.error().message。
// mcr::to_string(result.error().code) 返回错误的符号名称。
```

## 请求结果

同步请求返回 `Result<Response>`。结果为失败时，`error()` 表示初始化、配置或本地操作失败；
结果为成功时，仍需检查 `response.error` 中的传输状态，以及 `response.status_code` 中的 HTTP 状态。
保留这两个响应字段可以在网络中断时继续读取已经收到的正文、响应头和计时信息。

```cpp
auto result = mcr::Get(mcr::Url{ "http://127.0.0.1:8080/hello" });
if (!result) {
    std::println("操作失败：{}", result.error().message);
} else if (result->error) {
    std::println("传输失败：{}", result->error.message);
} else {
    std::println("HTTP {}：{}", result->status_code, result->text);
}

auto submitted = mcr::GetAsync(mcr::Url{ "http://127.0.0.1:8080/hello" });
if (!submitted) {
    std::println("提交失败：{}", submitted.error().message);
} else {
    auto response = submitted->Get(); // Result<Response>，继续按上面的方式检查。
}
```

构造失败通过工厂报告；接口签名和兼容性变化见 [expected 接口迁移](expected_migration.md)。
`Result` 不承诺 `noexcept`：内存分配和线程创建可能抛出标准异常，用户回调异常继续传播。
通用 Task、AsyncWrapper 的无效状态操作以及部分容器的越界访问仍使用异常。
只有确认包含值后才使用 `*result`；失败结果的 `.value()` 会抛出 `std::bad_expected_access`。

## 与 cpr 的关系

- 保留错误名称和数值、默认 ErrorCode::OK 及 Error::operator bool()。
  Error 为 true 表示失败；`Result<T>` 为 true 表示包含成功结果。
- 根据声明的 curl 依赖显式映射状态，不能直接强制转换：
  curl 的超时值为 28，ErrorCode::OPERATION_TIMEDOUT 为 18。
  不支持或未知的 curl 状态映射为 UNKNOWN_ERROR。
- 保留函数内静态 `std::unordered_map<ErrorCode, std::string>`。
  字符串转换使用 mcr::to_string(code)，不向 std 添加重载。
  无效枚举值经 .at() 抛出 std::out_of_range，已定义的 UNKNOWN_ERROR 有独立条目。
- Error 按值接收消息，允许复制和移动，也可直接接收 ErrorCode。
  `Result<T>` 用于可恢复的操作失败，check_curl_error 可用于扩展 curl 操作。

Session 的传输失败保存在 Response::error，HTTP 4xx/5xx 仍是普通响应；
配置错误和用户回调异常的传播规则见 [Session](session.md)。

运行 `mcpp build` 和 `mcpp test`，验证模块与错误处理。
