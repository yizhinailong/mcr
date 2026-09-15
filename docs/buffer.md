# Buffer：借用的上传字节

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.buffer`，使用 `mcr::Buffer`。它参考 cpr 的
`include/cpr/buffer.h`，使用 `mcr.error` 和标准库，直接使用 `std::filesystem::path`。

```cpp
import std;
import mcr.buffer;

std::vector<unsigned char> bytes{ 'h', 'i', 0, 0xff };
auto buffer = mcr::Buffer::Create( bytes.begin(), bytes.end(), "upload.bin" ).value();
// buffer.data 指向 bytes；buffer.datalen 为 4，包含空字节。

std::filesystem::path filename{ "another.bin" };
auto named = mcr::Buffer::Create( bytes.cbegin(), bytes.cend(), std::move(filename) ).value();
```

公开字段保留 cpr 的名称和类型：

| 成员 | 类型 | 含义 |
| --- | --- | --- |
| `data_t` | `char const*` | 借用字节指针的别名 |
| `data` | `data_t` | 只读访问源字节，指针本身可修改 |
| `datalen` | `std::size_t` | 可修改的字节数 |
| `filename` | `std::filesystem::path const` | 拥有存储、不可修改的上传文件名 |

`Buffer::Create(begin, end, filename) -> Result<Buffer>` 接收两个同类型迭代器和按值传入的标准路径。
文件名字面量转换为临时路径；已有路径可用 `std::move`，或显式复制为临时路径以保留原值。
允许空文件名，路径不规范化、不提取基本文件名，也不打开或检查文件。

## 范围约束与生命周期

工厂函数中的 `static_assert` 要求 C++20 连续迭代器，元素大小必须为一个字节。
支持指针及字符串、vector、array、span 中指向 char、signed/unsigned char、`std::byte` 的迭代器。
多字节元素、list/deque 迭代器、`vector<bool>` 代理和反向迭代器不符合要求。
检查针对元素大小，不是类型白名单；`std::to_address` 避免调用重载的取地址运算符。
约束在工厂模板实例化时检查。

范围必须属于同一存活的连续序列；两个空指针也表示空范围。
相等迭代器直接产生 `data == nullptr`、`datalen == 0`，不解引用或相减。
非空范围通过 `end - begin` 计算长度，负距离返回 `BAD_FUNCTION_ARGUMENT`。
不相关、悬空等无效迭代器仍由调用方负责。

Buffer 不复制或拥有字节。包括异步上传在内的全部读取完成前，源存储必须存活且地址有效。
已有元素的修改可被看到，内嵌空字节保留；字符串结束符仅在传入范围包含它时计入。

没有默认构造函数。隐式复制和移动共享指针与长度。
由于 `filename` 为 const，移动时也复制路径，可能分配并抛异常；复制和移动赋值均不可用，与 cpr 一致。

## 与 cpr 的差异及验证

除模块、命名空间和直接使用标准文件系统外，通过返回结果的工厂进行范围检查。
上游仅检查旧随机访问迭代器类别，可能接受非连续存储，并在空范围上解引用 begin；
此处以工厂中的静态断言替代未使用的 `is_random_access_iterator`，为空范围和反向范围定义上述行为。
有效连续字节范围的语义保持一致。

运行 `mcpp build` 和 `mcpp test`，验证借用、二进制子范围、存储类型、空范围和反向范围、
文件名所有权及复制移动。[Part](multipart.md) 可以借用 Buffer，Session 按显式字节长度生成 MIME 部分；
底层存储必须保持到全部相关传输结束。
