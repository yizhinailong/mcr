# Resolve：DNS 映射覆盖

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.transfer_options`，使用 `mcr::options::Resolve`。

```cpp
import mcr;

mcr::options::Resolve defaults{ "www.example.com", "127.0.0.1" }; // 端口 80 和 443。
mcr::options::Resolve custom{ "www.example.com", "127.0.0.1", { 8080 } };
mcr::options::Resolve empty_ports{ "www.example.com", "::1", {} }; // 同样使用端口 80 和 443。
```

公开字段 `host` 和 `addr` 拥有各自的 `std::string`；
`ports` 为 `std::set<std::uint16_t>`，端口去重并排序。
省略端口或传入空集合时使用 `{80, 443}`；非空集合原样保留，包括端口 0，不额外添加默认端口。

主机名和地址原样保存，不验证、规范化或查询 DNS。字段可修改。
默认端口只在从主机名和地址构造时补齐；随后清空 `ports`、复制、移动或赋值均不会恢复默认端口。

选项参考 cpr 的 `include/cpr/resolve.h`。有意差异包括 C++23 模块、
`mcr::options` 命名空间、显式使用 `std::uint16_t`，以及按值接收并移动拥有所有权的构造输入。

`Session::SetResolves` 将字段转换为 curl 条目并设置 `CURLOPT_RESOLVE`；
`SetResolve` 用一个映射替换列表，空列表清除覆盖。
运行 `mcpp build` 和 `mcpp test`，验证默认值、自定义端口、文本所有权及字段修改。
