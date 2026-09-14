# Bearer：令牌认证

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.auth`，使用 `mcr::options::Bearer`，接口参考 cpr 的 `include/cpr/bearer.h`。
总入口同时导出安全字符串；只导入 `mcr.auth` 时，如需直接命名安全字符串类型，还需导入 `mcr.secure_string`。

```cpp
import std;
import mcr.auth;

mcr::options::Bearer token{ "the_token" };
mcr::options::Bearer from_view = std::string_view{ "another_token" };
auto borrowed = token.GetToken(); // 指向 "the_token"，不包含前缀。
```

项目以 `mcpp.toml` 的 curl 依赖为基准，始终导出该类型，不保留旧 curl 头文件的条件声明，
模块本身无需 curl 版本头文件。

非 explicit 构造函数将 `std::string_view` 的确切字节复制到 `utils::SecureString`。
视图不必以空字符结尾，源存储不必与对象同寿命。没有默认构造函数，可用空视图构造空令牌。
空白、UTF-8、冒号和内嵌空字节原样保留，不验证、规范化或添加 `Bearer ` 前缀，也不进行 URL/Base64 编码。

虚函数 `GetToken() const noexcept` 返回指向所拥有字符串的借用 `char const*`。
curl 等 C 字符串消费者在第一个空字节处停止。
赋值、移动、派生类修改和销毁可能使借用指针失效。

复制构造和赋值产生独立令牌。移动构造和赋值显式默认化且为 `noexcept`，
保留虚析构函数存在时的移动能力；移出后的对象仍可查询或赋值，令牌内容未指定。
虚 `noexcept` 析构函数允许经 `Bearer*` 删除派生对象。

派生类可修改受保护的 `m_token_string`，并以不抛异常的实现重写 `GetToken()`。
与 cpr 的区别是模块、命名空间、直接依赖安全字符串、无条件导出及受保护成员命名；
令牌和多态行为保持一致。内存擦除范围见 [SecureString](secure_string.md)。

`Session::SetBearer` 通过 `CURLOPT_HTTPAUTH` 选择 `CURLAUTH_BEARER`，
并将令牌传给 `CURLOPT_XOAUTH2_BEARER`。构造和查询不需要初始化 curl。
运行 `mcpp build` 和 `mcpp test`，验证有界视图、空值与二进制令牌、复制移动、
受保护访问、虚派发和派生类析构；类型测试不执行 HTTP 请求。
