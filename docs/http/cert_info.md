# CertInfo：证书信息

[文档索引](../README.md) · [项目首页](../../README.md)

导入 `mcr`，使用 `mcr::CertInfo`。

```cpp
import std;
import mcr;

mcr::CertInfo certificate{
    "Subject:CN = test-server",
    "Issuer:C = GB, O = Example, CN = Sub CA",
};
certificate.emplace_back("Version:2");
for (auto const& entry : certificate) {
    std::println("{}", entry);
}
```

类型参考 cpr 的 `include/cpr/cert_info.h` 和 `cpr/cert_info.cpp`，
内部拥有 `std::vector<std::string>`。
默认构造为空，初始化列表保留顺序、重复项、空字符串、内嵌空字节和多行文本，
不解析或验证证书。

公开接口包含可变和 const 的 operator[]、begin/end，以及只读 cbegin/cend；
迭代器别名对应 vector 类型。
emplace_back 和 push_back 均接收单个 `std::string const&`，复制并返回 void，与 cpr 一致。
pop_back 删除末项，要求集合非空；下标必须有效。引用和迭代器遵循 vector 的失效规则。

复制构造拥有独立条目，移动构造转移存储。与上游一致，不支持复制或移动赋值。
`std::vector<CertInfo>` 仍可通过复制或移动插入构造证书链，并以移动构造扩容。

有意差异是 C++23 模块、命名空间、私有字段 m_cert_info、下标按值传递，
以及移动构造和迭代器访问器上的显式 noexcept。
保留标准容器方法名称，与 Cookies 一致。

Response 在传输完成时保存 curl 证书信息快照，
GetCertInfos 保留 [cpr 的值返回签名](https://github.com/libcpr/cpr/blob/master/include/cpr/response.h)，
返回独立副本，无证书时返回空集合；修改副本不会改变响应快照。
`Response::FromCurl` 接受借用的 `curl::CurlHolder const&`，先验证句柄再组装响应；
也接受可空的共享指针引用，均不保留或复制句柄的共享所有权。
本地 TLS 覆盖见 [Session](../session/session.md)。
运行 `mcpp build` 和 `mcpp test`，验证条目所有权、修改、遍历、追加删除及证书链存储；
类型测试不要求 TLS 连接。
