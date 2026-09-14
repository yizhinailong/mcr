# 本地端口选项

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.transfer_options`，使用 `mcr::options::LocalPort`
和 `mcr::options::LocalPortRange`。

```cpp
import std;
import mcr;

mcr::options::LocalPort port = std::uint16_t{ 50000 };
mcr::options::LocalPortRange range = std::uint16_t{ 100 };
std::uint16_t port_number = port;
std::uint16_t range_value = range;
```

两种选项分别参考 cpr 的 `include/cpr/local_port.h` 和 `include/cpr/local_port_range.h`。
它们各保存一个私有 `std::uint16_t`，允许从该类型隐式构造，也提供 const 隐式转换运算符。
0 到 65535 的数值原样保存，不验证、规范化、绑定套接字或探测端口。

两者均无默认构造函数，支持复制、移动和赋值；从 `uint16_t` 赋值会隐式构造替换选项。
与 cpr 的区别是 C++23 模块、`mcr::options` 命名空间、私有字段
`m_local_port` / `m_local_port_range` 的命名，以及转换运算符上的 `[[nodiscard]]`。

`Session::SetLocalPort` 和 `SetLocalPortRange` 将数值转换为 `long`，
传给 `CURLOPT_LOCALPORT` 和 `CURLOPT_LOCALPORTRANGE`。curl 在新建连接时应用设置；
配置和连接复用规则见 [Session](session.md)。

运行 `mcpp build` 和 `mcpp test`。`test_local_port` 覆盖隐式转换、零值、最大值及独立的复制移动赋值。
