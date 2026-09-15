# Payload：表单键值对

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.fields`，使用 `mcr::Payload` 和 `Pair`。
总入口也导出 `mcr::curl::CurlHolder`；只导入 fields 时，如需命名该后端类型，
应另导入 `mcr.curlholder`。实现参考 cpr 的 `include/cpr/payload.h`，在模块内完成。

```cpp
import std;
import mcr.fields;
import mcr.curlholder;

mcr::Payload form{ { "name", "hello world" }, { "flag", "" } };
form.Add(mcr::Pair{ "name", "x+y" });
auto raw = form.GetContent().value(); // name=hello world&flag=&name=x+y

std::vector<mcr::Pair> pairs{ { "first", "one" }, { "last", "two" } };
mcr::Payload from_range{ pairs.cbegin(), pairs.cend() };
mcr::Payload empty{};

// 成功初始化 curl 后执行，holder 必须先于 curl 全局清理销毁。
auto holder = mcr::curl::CurlHolder::Create().value();
auto encoded = form.GetContent(holder).value();
// name=hello%20world&flag=&name=x%2By
```

Payload 公开继承 `mcr::curl::CurlContainer<mcr::Pair>`。
非 explicit 的 `std::initializer_list<Pair> const&` 构造函数复制条目，
保留顺序、重复键和空字符串。它没有默认构造函数：
`Payload payload;` 无效，`std::is_default_constructible_v<Payload>` 为 false；
`Payload{}` 通过空初始化列表构造，行为与 cpr 一致。

范围构造接收两个同类型、可复制的迭代器，单次遍历并对每项调用 `Add(*iterator)`。
支持指针、可变和 const 容器迭代器、非连续迭代器及产生 Pair 的单遍输入迭代器，
不要求随机访问或预先计算距离。存储顺序取决于遍历顺序，反向迭代器产生反序条目。
相等迭代器（包括两个空指针）产生空集合，不解引用。
范围必须有效且终点可达；不支持独立哨兵类型，不合适的迭代器在函数体实例化时报错。

每项均通过 `Add(Pair const&)` 复制，即使使用移动迭代器也是如此。
构造后可修改或销毁来源。两种构造都启用编码；复制拥有独立存储，隐式移动为 `noexcept`，
复制移动同时保留条目顺序和编码标志。

继承 `Add()`、两个 `GetContent()` 重载、公开 `encode` 和受保护的 `m_container_list`。
启用编码时，`GetContent(holder)` 仅百分号编码值，键原样输出。
每对始终含等号（包括空值），条目用 `&` 连接；`GetContent()` 始终输出原始字节，不调用 curl。
二进制长度、转义和句柄错误见 [CurlContainer](curl_container.md)。
该模块不发送请求或创建媒体类型请求头。

除模块、命名空间及基类差异外，构造接口沿用 cpr。
运行 `mcpp build` 和 `mcpp test`。`tests/test_curl_container.cpp` 验证列表与范围构造、
所有权、单遍和移动迭代器、空范围、原始与编码输出、追加、复制移动及句柄错误。
