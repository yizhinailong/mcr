# Proxies：代理地址

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.proxy`，使用 `mcr::options::Proxies`。

```cpp
import std;
import mcr.proxy;

mcr::options::Proxies proxies{
    { "http", "http://proxy.test:8080" },
    { "https", "socks5://proxy.test:1080" },
    { "no_proxy", "" },
};
if (proxies.Has("http")) {
    std::println("{}", proxies["http"]);
}
```

类型参考 cpr 的 `include/cpr/proxies.h` 和 `cpr/proxies.cpp`，内部拥有映射，不继承 `std::map`。
默认构造为空；初始化列表复制协议与地址，显式的 `std::map<std::string, std::string> const&`
构造函数复制已有映射。复制和赋值产生独立存储。重复的初始化列表键遵循 `std::map` 规则，
不保证保留哪一个等价条目。

`Has(protocol)` 精确、区分大小写地查找，不插入键；已有空值也返回 true。
非 const 的 `operator[]` 在缺少键时插入空字符串，返回 `std::string const&`。
其他插入不使该引用失效，但不能通过引用修改值；替换选项即可替换配置。

协议和地址原样保存，不规范化、验证 URL 或访问网络；空键、空值和内嵌空字节均保留。
`no_proxy` 与 `NO_PROXY` 是独立键。Session 将其解释为代理排除列表，两者都有时优先使用
`no_proxy`；空排除值覆盖环境中的排除列表。选项自身不读取或修改环境。

与 cpr 的区别是 C++23 模块、`mcr::options` 命名空间、私有成员 `m_hosts`、
用 `Has()` 替代 `has()`，以及用 `std::string_view` 查询。
透明比较器 `std::less<>` 支持不分配临时键的有界视图查找；下标插入缺失键时会复制视图。

`Session::SetProxies` 保存配置，并在每次传输前按目标协议应用代理和排除列表。
认证与请求复用见 [Session](session.md)。
运行 `mcpp build` 和 `mcpp test`，验证构造、所有权、精确查找、空值插入、二进制文本和复制移动，无需外部代理服务。
