# File 与 Files：上传文件描述

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.file`，使用 `mcr::File` 和 `Files`。
实现将 cpr 的 `include/cpr/file.h` 和 `cpr/file.cpp` 合并在模块内，
通过 `import std;` 直接使用 `std::filesystem`，不提供文件系统兼容层或命名空间别名。

```cpp
import std;
import mcr.file;

std::filesystem::path path{ std::filesystem::path{ "uploads" } / "report.txt" };
mcr::File file{ path.string(), "download.txt" };
bool overridden = file.HasOverridenFilename(); // true
mcr::Files single = file;
mcr::Files paths{ "first.txt", "second.txt" };
mcr::Files named{ mcr::File{ "first.txt", "first-upload.txt" }, file };
named.push_back(mcr::File{ "third.txt" });
```

## 文件描述与集合

File 保留上游公开字符串字段 `filepath` 和 `overriden_filename`，包括后者的拼写。
显式构造必须提供路径，可选的替换文件名默认为空。
路径按值接收并移动到存储，文件名从 `std::string_view` 按长度复制，无需空字符结尾。
字段均可修改，保留所有输入字节。

`HasOverridenFilename() const noexcept` 替代上游的 `hasOverridenFilename()`，
只判断替换文件名是否非空。空白也算替换名，不去空白、提取基本文件名或验证文件名；
清空字段即可取消替换。

File 是描述符：构造和复制不打开、读取、创建、检查或规范化文件。
相对路径、空路径和不存在的路径均原样保存。
需要路径操作时显式使用 `std::filesystem::path`，再将 `.string()` 传给该字符串接口。

Files 拥有私有 vector `m_files`，支持空默认构造、从一个 File 隐式构造，
以及 File 描述符或路径字符串的初始化列表。
路径列表创建不带替换名的 File；顺序、重复项、空路径和替换名均保留。
`Files{}` 和两种显式指定元素类型的空初始化列表均可用。

保留标准容器名称 `begin`、`end`、`cbegin`、`cend`、
`emplace_back`、`push_back`、`pop_back`。
可变迭代器允许修改两个字段，const 迭代只读。
两种追加函数均接收 `File const&`、复制一项并返回 void；
`emplace_back` 不转发任意构造参数。`pop_back` 要求集合非空。
迭代器和引用遵循 vector 的失效规则。

复制得到独立描述符，移动无异常地转移 vector 存储。
复制移动赋值返回目标，自赋值和自移动保持原值。

## 请求集成与验证

与 cpr 的区别是模块、命名空间、方法和私有字段命名、替换文件名使用
`std::string_view`，以及迭代器访问器上的 `noexcept`；描述符和集合行为保持一致。

Session 将 `filepath` 传给 `curl_mime_filedata`，传输文件名使用替换名，
或 `std::filesystem::path(filepath).filename().string()`。
[Multipart](multipart.md) 提供相应描述符，Session 在准备请求时序列化它们。

运行 `mcpp build` 和 `mcpp test`，验证所有权、替换名、列表构造、遍历、追加删除、
复制移动和标准路径互操作；描述符类型测试不访问真实文件。
