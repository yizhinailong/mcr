# 请求与连接超时

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.transfer_options`，使用 `mcr::options::Timeout` 和 `ConnectTimeout`。

```cpp
import std;
import mcr;

using namespace std::chrono_literals;

mcr::options::Timeout const from_integer{ 1500 };
mcr::options::Timeout timeout{ 2s };
std::println("{} ms", timeout.Milliseconds()); // 2000 ms
timeout.ms = 500ms;
```

## 总请求超时

`Timeout` 接收整数毫秒数 `std::int32_t` 或任意 `std::chrono::duration<Rep, Period>`，
两种构造均允许隐式转换，没有默认构造函数。`Timeout{0}` 传给 curl 时表示不设置总请求超时。
公开字段 `ms` 保存 `std::chrono::milliseconds`，可在构造后修改。

时长通过 `duration_cast` 转换，向零截断不足一毫秒的部分：
`1999us` 变为 `1ms`，`-1999us` 变为 `-1ms`。
调用方必须保证转换运算和毫秒表示不溢出，浮点时长还必须为有限值。

`Milliseconds()` 返回 curl 所需的 `long`。超过 `LONG_MAX` 抛出
`std::overflow_error`，低于 `LONG_MIN` 抛出 `std::underflow_error`，诊断包含原始计数。
检查在读取时执行，因此也适用于通过 `ms` 修改的值；零和可表示的负值原样返回。

接口参考 cpr 的 `include/cpr/timeout.h` 和 `cpr/timeout.cpp`。
有意差异是模块、命名空间、诊断前缀 `mcr::options::Timeout`，以及整数构造参数按值传递。

## 连接超时

`ConnectTimeout` 参考 cpr 的 `include/cpr/connect_timeout.h`，公开继承 `Timeout`，
不增加状态，复用 `ms`、`Milliseconds()` 及其范围检查和诊断。
独立类型使 Session 能区分连接超时和总请求超时。

```cpp
mcr::options::ConnectTimeout connect{ 1500 };
mcr::options::ConnectTimeout from_milliseconds = 500ms;
mcr::options::ConnectTimeout from_seconds{ 2s };
connect.ms = 750ms;
```

两个非 explicit 构造函数分别接收 `std::int32_t` 和
`std::chrono::milliseconds const&`，不提供通用时长构造或默认构造。
从秒、分钟直接构造可通过 chrono 的无损转换完成；从秒复制初始化需要两次用户定义转换，因此不可用。
不足毫秒的时长和浮点时长必须先显式 `duration_cast` 为毫秒。

`Session::SetTimeout` 和 `SetConnectTimeout` 分别设置 `CURLOPT_TIMEOUT_MS` 和
`CURLOPT_CONNECTTIMEOUT_MS`。连接超时涵盖 DNS 解析和建立连接所需的协议握手；
零使用 curl 默认的 300 秒连接超时，总请求超时仍可施加更短限制。见
[libcurl 连接超时说明](https://curl.se/libcurl/c/CURLOPT_CONNECTTIMEOUT_MS.html)。

运行 `mcpp build` 和 `mcpp test`，验证转换、截断、字段修改、连接超时构造约束及平台相关范围检查。
