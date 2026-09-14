# BodyView：借用的请求正文

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.body`，使用 `mcr::BodyView`。它参考 cpr 的
`include/cpr/body_view.h`，为保存私有 `std::string_view m_body` 的 final 类。
与 Body 共用模块；模块还导出 Buffer、File 和公共请求类型。

```cpp
import std;
import mcr.body;

std::string source{ "x=5" };
mcr::BodyView body{ source };
std::string_view borrowed = body.Str();

std::array<unsigned char, 3> bytes{ 'a', 0, 'b' };
mcr::Buffer buffer{ bytes.begin(), bytes.end(), "body.bin" };
mcr::BodyView binary = buffer; // 三个字节，包含内嵌空字节。
```

构造均为非 explicit：

| 构造方式 | 行为 |
| --- | --- |
| `BodyView{}` | 数据指针为空的空视图 |
| `BodyView{std::string_view}` | 保存确切指针和长度 |
| `BodyView{char const*}` | 扫描有效、非空的 C 字符串到首个空字符 |
| `BodyView{char const*, std::size_t}` | 保存确切范围，不扫描 |
| `BodyView{Buffer const&}` | 保存 `data`、`datalen`，忽略文件名 |

指针与长度必须描述有效可读范围。零长度允许空指针，非空指针加零长度也会保留原指针。
显式长度不要求结束符，保留内嵌空字节；单指针重载即使表示空正文，也需要非空、已终止的字符串。
无效输入违反 `std::string_view` 的前提条件，由调用方负责。

`Str() const` 按值返回 `std::string_view`，替代 cpr 的 `str()`。
调整返回视图的边界或重新绑定 BodyView 不影响其他描述符。
不提供到 `std::string_view` 的隐式转换。
可利用字符串的视图转换从 `std::string` 直接构造；
从字符串隐式转换成 BodyView 需要两次用户定义转换，因此不可用，与 cpr 一致。

## 生命周期

BodyView 不复制或拥有字节。所有消费者（包括异步请求）结束前，源存储必须存活并保持地址有效。
修改已有元素可通过所有视图看到；重新分配、缩小到视图范围以内或销毁源对象可能使视图失效。
从临时拥有型字符串构造不会延长其生命周期。

从 Buffer 构造只保存当时的指针和长度，随后可销毁或修改 Buffer 描述符；
底层字节存储仍须有效。复制移动和赋值只复制借用描述符，不转移字节所有权。
赋值返回目标，支持自赋值；析构不释放字节。`BodyView` 保持可平凡复制，以适配 `Session::SetBodyView`。

与 cpr 的区别是模块、命名空间、`Str()` 命名，以及构造、赋值和访问上的显式
`constexpr` / `noexcept`。Buffer 已暴露 `char const*` 和 `std::size_t`，无需多余强制转换。

运行 `mcpp build` 和 `mcpp test`，验证常量求值、借用复制移动、二进制及空范围、Buffer 互操作。
`Session::SetBodyView` 在传输中继续借用字节，见 [Session](session.md)。
