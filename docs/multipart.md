# Part 与 Multipart：分段表单

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.multipart`，使用 `mcr::Part` 和 `Multipart`。
模块同时导出 `mcr.buffer` 和 `mcr.file`，实现仅依赖标准库，
将 cpr 的 `include/cpr/multipart.h` 和 `cpr/multipart.cpp` 合并在模块内。

```cpp
import std;
import mcr.multipart;

std::array<unsigned char, 3> bytes{ 'a', 0, 'b' };
mcr::Buffer buffer{ bytes.begin(), bytes.end(), "upload.bin" };
mcr::Multipart form{
    { "text", "hello" },
    { "number", 5, "application/number" },
    { "file", mcr::File{ "report.txt", "download.txt" } },
    { "files", mcr::Files{ "first.bin", "second.bin" } },
    { "buffer", buffer, "application/octet-stream" }
};
form.parts.emplace_back("text", "another value");
```

## 表单部分与所有权

Part 有五个构造函数，接收字段名、下表中的输入及可选媒体类型（默认为空）。
名称、文本值和媒体类型从 `std::string_view` 复制到拥有存储的字符串，
保留空字符串、有界视图和内嵌空字节，不编码、规范化或验证。

| 输入 | 存储的 value | `is_file` | `is_buffer` |
| --- | --- | --- | --- |
| `std::string_view` | 复制的文本 | false | false |
| `std::int32_t` | `std::to_string` 生成的有符号十进制文本 | false | false |
| `Files const&` | 空，文件描述复制到 files | true | false |
| `Files&&` | 空，文件描述移动到 files | true | false |
| `Buffer const&` | `buffer.filename.string()` | false | true |

公开字段保留上游名称和类型：字符串 `name`、`value`、`content_type`，
`Buffer::data_t data`、`std::size_t datalen`、布尔值 `is_file`、`is_buffer` 和 `Files files`。
文本和文件部分的 data 为空、datalen 为零，文本和缓冲区部分的 files 为空。
没有默认构造函数。字段可修改，修改模式标志时必须保持关联字段一致。

单个 File 可隐式转换为单元素 Files，因此可直接传入。
文件顺序、重复描述符和替换文件名均保留；空 Files 也选择文件模式。
构造描述符不打开或检查路径，不执行 I/O。

Buffer 构造仅复制元数据：data 和 datalen 保存当时的借用范围，
value 拥有通过标准路径 `.string()` 转换得到的完整文件名，不剥离目录。
空 Buffer 仍选择缓冲区模式，数据为空、长度为零。
随后可销毁或修改 Buffer 描述符，但底层字节必须存活且地址有效到全部消费者结束。
复制移动 Part 或 Multipart 不延长字节生命周期。

## 集合与请求集成

Multipart 拥有公开 `std::vector<Part> parts`。
非 explicit 的初始化列表构造按顺序复制条目，支持嵌套列表和 `Multipart{}`，
但没有默认构造函数，`Multipart multipart;` 无效。
两个 vector 构造均为 explicit：const 引用复制，非 const 右值引用无异常地转移存储；
const 右值绑定 const 引用并复制。重复字段名和空 vector 均保留。

两种类型均支持隐式复制移动构造和赋值，移动为 `noexcept`。
复制拥有独立字符串和文件描述，但仍借用同一缓冲区地址与长度。
parts 的修改遵循 vector 的引用及迭代器失效规则。

与 cpr 的区别是模块、命名空间、只读文本参数使用 `std::string_view`、整数按值传递，
以及 vector 右值构造使用非 const 右值并真正移动；上游 const 右值重载会复制全部 Part。
const 输入仍复制，文件系统转换直接使用标准库。

`Session::SetMultipart` 保存描述符，并在准备请求时构造 curl MIME 树。
文本和缓冲区使用显式长度，保留内嵌空字节；文件通过 `curl_mime_filedata` 提供。
借用缓冲区和源文件必须在传输期间可用，见 [Session](session.md)。

运行 `mcpp build` 和 `mcpp test`，验证构造、整数边界、文件复制移动、
缓冲区生命周期、混合集合和 vector 复制移动；描述符类型测试无需文件或网络 I/O。
