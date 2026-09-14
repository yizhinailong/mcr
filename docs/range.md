# Range 与 MultiRange：字节范围

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.http`，使用 `mcr::options::Range` 和 `MultiRange`。

```cpp
import std;
import mcr.http;

mcr::options::Range range{ 2, 3 };
std::println("{}", range.Str()); // 2-3
mcr::options::MultiRange ranges{ mcr::options::Range{ std::nullopt, 3 }, mcr::options::Range{ 5, 6 } };
std::println("{}", ranges.Str()); // 0-3, 5-6
```

`Range` 参考 cpr 的 `include/cpr/range.h`。显式构造函数接收两个可选的
`std::int64_t` 端点；起点缺省为 `0`，终点缺省为 `-1`。
公开字段 `resume_from`、`finish_at` 保留有符号数值，可在构造后修改。
`Str()` 读取当前值，省略负端点的数字，并在中间添加一个连字符。

| 范围 | 输出 |
| --- | --- |
| `Range{}` | `0-` |
| `Range{1, std::nullopt}` | `1-` |
| `Range{std::nullopt, 5}` | `0-5` |
| `Range{2, 3}` | `2-3` |
| `Range{-1, 500}` | `-500` |
| `Range{-1, -1}` | `-` |

不添加 `bytes=` 前缀，也不排序、限制或验证端点；例如 `Range{10, 2}` 输出 `10-2`。
全部非负 64 位端点均可无窄化地格式化，负值在存储中保持不变。

`MultiRange` 将初始化列表复制到私有 vector。`Str()` 用 `", "` 连接各范围，
保留顺序、重叠和重复项，不产生末尾分隔符。`MultiRange{}` 输出空字符串。
原始范围的修改不影响副本。两种类型均支持独立的复制移动和赋值，每次 `Str()` 返回拥有存储的字符串。

与 cpr 的区别是 C++23 模块、`mcr::options` 命名空间、用 `Str()` 替代 `str()`、
返回值不带顶层 const，以及私有成员 `m_ranges` 的命名。多范围格式化通过 const 引用遍历，输出行为不变。

`Session::SetRange` 和 `SetMultiRange` 将结果传给 `CURLOPT_RANGE`，准备 PUT 请求时清除该选项，见 [Session](session.md)。
运行 `mcpp build` 和 `mcpp test`，验证默认值、可选与负端点、64 位边界、多范围格式和所有权；选项测试无需网络服务。
