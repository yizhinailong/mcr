# Authentication：用户名与密码认证

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.auth`，使用 `mcr::options::AuthMode` 和 `Authentication`。
接口参考 cpr 的 `include/cpr/auth.h` 和 `cpr/auth.cpp`。

```cpp
import mcr.auth;

mcr::options::Authentication auth{ "user", "password", mcr::options::AuthMode::BASIC };
auto mode = auth.GetAuthMode();
auto credentials = auth.GetAuthString(); // 借用指向 "user:password" 的指针。
```

`AuthMode` 是底层类型为 `std::uint8_t` 的作用域枚举，序号沿用 cpr：

| 模式 | 序号 | 对应的 curl 策略 |
| --- | --- | --- |
| `BASIC` | 0 | `CURLAUTH_BASIC` |
| `DIGEST` | 1 | `CURLAUTH_DIGEST` |
| `NTLM` | 2 | `CURLAUTH_NTLM` |
| `NEGOTIATE` | 3 | `CURLAUTH_NEGOTIATE` |
| `ANY` | 4 | `CURLAUTH_ANY` |
| `ANYSAFE` | 5 | `CURLAUTH_ANYSAFE` |

序号不是 curl 的认证位掩码。`ANY` 让 curl 在支持的方法中选择；
`ANYSAFE` 排除 Basic。实际支持取决于 curl 构建和服务端。

构造必须提供用户名视图、密码视图和模式，没有默认构造或默认模式。
输入按视图长度复制到拥有所有权的 `utils::SecureString`，中间插入冒号：
两个空视图得到 `":"`，仅用户名为空得到 `":password"`，仅密码为空得到 `"username:"`。
视图无需以空字符结尾；不进行 URL/Base64 编码、转义、去空白或模式验证。

`GetAuthString() const noexcept` 返回借用的 `char const*`，
`GetAuthMode() const noexcept` 返回枚举。构造后可修改或销毁原始输入。
复制拥有独立凭据，移动采用字符串的正常移动语义。赋值、移动和销毁可能使借用指针失效；
移出后的对象仍可查询或赋值，凭据内容未指定。

存储保留冒号和内嵌空字节。传给 `CURLOPT_USERPWD` 后，curl 用第一个冒号分隔用户名，
在第一个空字节处结束；因此该选项不能表示包含冒号的用户名，密码中的冒号可以保留。
包装类型不验证这些输入。

模块只直接依赖 `mcr.secure_string`，构造和查询不调用 curl。
安全分配器会擦除释放的堆存储；短字符串内联存储、来源和外部副本的限制见 [SecureString](secure_string.md)。

与 cpr 的区别是模块、`mcr::options` 命名空间、私有字段 `m_auth_string` / `m_auth_mode`，
以及使用有检查的字符串操作，避免上游为 `reserve` 计算组合长度时的未检查算术；公开签名和凭据格式保留。

`Session::SetAuth` 将模式映射到 `CURLOPT_HTTPAUTH`，将凭据传给 `CURLOPT_USERPWD`。
运行 `mcpp build` 和 `mcpp test`，验证模式、空输入、有界视图、UTF-8、二进制字节及复制移动。
认证类型测试使用模拟凭据，不执行认证请求。
