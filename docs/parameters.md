# Parameters：查询参数

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.fields`，使用 `mcr::Parameters` 和 `Parameter`。
总入口也导出 `mcr::curl::CurlHolder`；只导入 fields 时，如需命名该后端类型，应另导入 `mcr.curlholder`。
实现参考 cpr 的 `include/cpr/parameters.h`，与其他请求字段放在同一模块内。

```cpp
import mcr.fields;
import mcr.curlholder;

mcr::Parameters query{ { "q", "hello world" }, { "flag", "" } };
query.Add(mcr::Parameter{ "q", "x+y" });
auto raw = query.GetContent().value(); // q=hello world&flag&q=x+y

// 成功初始化 curl 后执行，holder 必须先于 curl 全局清理销毁。
auto holder = mcr::curl::CurlHolder::Create().value();
auto encoded = query.GetContent(holder).value();
// q=hello%20world&flag&q=x%2By
```

Parameters 公开继承 `mcr::curl::CurlContainer<mcr::Parameter>`。
默认构造产生空集合且 `encode == true`。
非 explicit 的 `std::initializer_list<Parameter> const&` 构造函数复制条目，
保留顺序、重复键和空字符串。复制产生独立存储，隐式移动为 `noexcept`，
复制移动均保留编码标志。

继承 `Add(Parameter const&)`、`Add(initializer_list)`、
两个 `GetContent()` 重载、公开 `encode` 及受保护的 `m_container_list`。
启用编码时，`GetContent(holder)` 对键和非空值进行百分号编码；
`GetContent()` 始终输出原始内容，不调用 curl。空值仅输出键，不带等号。
结果不包含开头的问号，该模块不构造 URL 或发送请求。

序列化、空条目分隔、二进制长度和句柄错误见 [CurlContainer](curl_container.md)。
该包装不增加验证或格式策略；除模块、命名空间及基类已说明的差异外，接口直接沿用 cpr。

运行 `mcpp build` 和 `mcpp test`。`tests/test_curl_container.cpp` 通过总入口验证
默认和列表构造、追加、所有权、复制移动、原始与编码输出、重复和空条目、
二进制/UTF-8 输入及句柄生命周期错误。
