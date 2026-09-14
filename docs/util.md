# HTTP 与 curl 辅助函数

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.util`，使用 `mcr::utils` 中的自由函数。
命名空间和迁移规则见 [工具模块](utils.md)。
实现参考 cpr 的 `include/cpr/util.h`、`cpr/util.cpp` 和 `test/util_tests.cpp`。

```cpp
import std;
import mcr.util;

std::string status;
std::string reason;
auto headers = mcr::utils::parse_header(
    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n",
    &status, &reason);
std::println("{}: {}", reason, headers.at("content-type"));
```

API 使用 C++23 模块、`mcr::utils` 命名空间、snake_case 自由函数名，
借用输入使用 `std::string_view`。

| cpr | mcr::utils |
| --- | --- |
| `parseHeader` | `parse_header` |
| `parseCookies` | `parse_cookies` |
| `readUserFunction` | `read_user_function` |
| `headerUserFunction` | `header_user_function` |
| `writeFunction` | `write_function` |
| `writeFileFunction` | `write_file_function` |
| `writeUserFunction` | `write_user_function` |
| `writeSSEFunction` | `write_sse_function` |
| `progressUserFunction` | `progress_user_function` |
| `debugUserFunction` | `debug_user_function` |
| `split` | `split` |
| `urlEncode`、`urlDecode` | `url_encode`、`url_decode` |
| `isTrue` | `is_true` |
| `sTimestampToT` | `s_timestamp_to_t` |

## HTTP 解析

`parse_header` 接受 LF 和 CRLF，去除字段值两端空白，
对忽略大小写的同名字段保留最后值。
每个 HTTP/ 状态行重置头映射，因此重定向和中间响应后只留下最终响应；
最终空行后的字段作为尾部字段保留。
没有状态行时，可选的状态输出保持原值。

与 cpr 不同，新状态没有原因短语时会清除旧原因，
状态词间的空格和制表符一致处理，原因中的冒号不会被误认为头字段。
没有冒号的畸形行被忽略；该函数不是完整 HTTP 语法验证器。

`parse_cookies` 借用 `curl_slist const*`，空指针返回空集合。
按顺序复制 Netscape 格式记录，保留重名和域文本（含 #HttpOnly_），默认 encode 为 true。
缺失列补空，超过第七列的内容忽略。
缺失或无效过期时间抛出 std::invalid_argument，超过 time_t 范围抛出 std::out_of_range；
时间还必须能由 `std::chrono::system_clock::time_point` 表示。
原始列表仍归调用方所有。

`split` 保留开头及中间空字段，省略末尾分隔符后的空字段，空输入返回空集合，
与上游 std::getline 循环一致。支持内嵌空字节，也可用空字节作分隔符。
`is_true` 只接受 ASCII 的 true 大小写变体，不去空白，不受 locale 影响，并安全处理高位字节。
`s_timestamp_to_t` 接受带前导空白和正负号的十进制前缀，
沿用标准字符串转整数规则，并检查平台完整 time_t 范围。
Unix Cookie 时间戳以秒为单位，修正上游头文件中的毫秒注释。

## 回调适配与 URL 编解码

适配器借用缓冲区和回调对象。读取回调可以减少字节数；
成功的零字节读取表示 EOF，返回 false 则产生 CURL_READFUNC_ABORT。
头、正文和 SSE 消费者返回 true 时报告完整块长度，false 时报告零；
SSE 解析状态跨块保留。

字符串和文件写入保留二进制字节，文件应以二进制模式打开。
与 cpr 不同，write_file_function 在流报告失败时返回零，
调用方仍需检查刷新和关闭阶段的错误。
进度回调返回零继续、返回一终止，包括 CancellationCallback 的实例化。
当前 curl 依赖提供 CURL_PROGRESSFUNC_CONTINUE，无需旧版本分支。
调试回调接收借用视图，始终返回零。

直接调用适配器时，异常正常传播；直接将它们注册给 curl 时，不得让异常穿过 C 边界。
[Session](session.md) 使用自己的回调入口捕获异常，在 curl 返回后重新抛出。
回调和目标指针必须在调用期间有效，字节数必须能由相应类型表示。

`url_encode` 和 `url_decode` 通过临时 CurlHolder 返回 SecureString，
沿用其长度检查、保留二进制数据的解码，以及 curl 转换失败时返回空结果的规则。
解码时加号保持字面值。重复转换可复用 CurlHolder 并调用 UrlEncode / UrlDecode。
curl 的全局初始化和清理由调用方管理。

运行 `mcpp build` 和 `mcpp test`。测试覆盖上游解析和 URL 示例、
响应状态切换、Cookie 所有权、时间戳边界、回调字节数与取消、SSE 分块和二进制文件写入，无需网络服务。
