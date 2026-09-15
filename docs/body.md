# Body：拥有存储的请求正文

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.body`，使用 `mcr::Body`。模块同时导出 `mcr.buffer`、
`mcr.file` 和 `mcr.types`。`Body` 继承 `StringHolder<Body>`，拥有请求字节，
设计参考 cpr 的 `include/cpr/body.h`。

```cpp
import std;
import mcr.body;

mcr::Body text{ "x=", "5&y=13" };
std::array<unsigned char, 3> bytes{ 'a', 0, 'b' };
auto buffer = mcr::Buffer::Create( bytes.begin(), bytes.end(), "ignored.bin" ).value();
mcr::Body binary = buffer; // 复制三个字节并拥有存储。
auto from_file = mcr::Body::FromFile( mcr::File{ "request.bin" } ).value(); // 立即读取文件。
```

默认构造产生空正文。内存来源保留非 explicit 构造，文件读取使用 `Body::FromFile` 工厂：

| 输入 | 行为 |
| --- | --- |
| 按值传入的 `std::string` | 移入存储 |
| `std::string_view` | 按视图长度复制 |
| `char const*` | 从有效非空指针复制到首个空字符 |
| `char const*`、`std::size_t` | 复制指定长度的可读字节 |
| `std::initializer_list<std::string>` | 无分隔符拼接片段 |
| `Buffer const&` | 复制 `data` 和 `datalen` 指定的字节，忽略文件名 |
| `Body::FromFile(File const&)` | 以二进制模式打开 `filepath`，读取到文件结束 |

显式长度保留内嵌空字节，无需结束符。零长度允许空指针，包括空 Buffer；其余输入必须是有效可读范围。
构造后修改或销毁源字符串、缓冲区不影响正文。
不进行编码、去空白、媒体类型推断，也不增加从 `BodyView` 的自动转换。

## 文件与所有权

`Body::FromFile(file) -> Result<Body>` 通过 `std::ifstream` 同步读取，原样使用 `File::filepath`，
忽略 `overriden_filename`。空文件得到空正文，二进制模式保留 Windows 上的 CR/LF 和控制字节。
实现按固定大小的块读取到正常 EOF，不预先查询文件大小。全部正文仍驻留内存，
并发修改文件不保证得到原子快照。

打开失败返回 `FILE_COULDNT_READ_FILE`，消息为 `Can't open the file for HTTP request body!`。
正常 EOF 前的读取失败返回 `READ_ERROR`，消息为
`Can't read the file for HTTP request body!`。分配和容量错误分别传播
`std::bad_alloc`、`std::length_error`。所有退出路径均通过 RAII 关闭文件；
构造完成后删除或替换源文件不影响正文。

继承的接口包括 `Str()`、`CStr()`、`Data()`、比较、返回 Body 的拼接、
返回 void 的 `+=`、显式 `std::string` 转换和流输出。
复制拥有独立字节，移动为 `noexcept`。析构函数重写基类虚析构，类仍可派生，受保护存储名为 `m_str`。

## 与 cpr 的差异及验证

除模块、命名空间及继承的命名规范外，文件读取会检查每次读取直到 EOF。
上游先定位文件长度、调整字符串大小，再单次读取，未检查定位和读取失败；
此处避免将失败的大小查询转换成无符号分配量，并在读取错误时返回失败，防止暴露部分正文或填充字节。
Buffer 字段已具备所需类型，无需多余转换。

运行 `mcpp build` 和 `mcpp test`，验证构造所有权、Buffer 子范围与生命周期、
继承操作、空文件、二进制及多块读取、文件失败。测试自行创建并清理临时文件。
`Session::SetBody` 拥有正文并按显式长度发送；本地 HTTP 传输与复用见 [Session](session.md)。
