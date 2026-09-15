# CurlContainer：请求字段存储与编码

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.fields`，使用 `mcr::Parameter` 和 `Pair`；
导入 `mcr` 或 `mcr.curl_container`，还可使用 `mcr::curl::CurlContainer<T>`。
后端模块位于 `src/curl/curl_container.cppm`，同时导出 `mcr.curlholder`。
请求记录保留在 mcr 命名空间，编码容器位于 mcr::curl。
实现参考 cpr 的 `include/cpr/curl_container.h` 和 `cpr/curl_container.cpp`。

Parameter 和 Pair 是独立类型，各有两个拥有存储的公开字符串字段 key 和 value。
构造按值接收并移动两个字符串，保留空字符串和内嵌空字节；两者均没有默认构造。

```cpp
import mcr.curl_container;

mcr::curl::CurlContainer<mcr::Parameter> query{ { "q", "hello world" }, { "flag", "" } };
query.Add(mcr::Parameter{ "q", "another value" });
auto raw = query.GetContent().value(); // q=hello world&flag&q=another value

// 成功初始化 curl 后执行，holder 必须先于 curl 全局清理销毁。
auto holder = mcr::curl::CurlHolder::Create().value();
auto encoded = query.GetContent(holder).value();
// q=hello%20world&flag&q=another%20value
```

## 编码规则

CurlContainer 默认拥有空 vector，公开标志 encode 为 true。
初始化列表按顺序复制元素，Add(element) 和 Add(initializer_list) 追加副本并保留重复键。
修改源元素不影响存储；支持复制移动构造和赋值，复制保留独立元素和编码标志。
受保护 vector 名为 `m_container_list`。

两种元素有意采用不同序列化规则：

| 元素 | encode 为 true 时的 GetContent(holder) | 空值 |
| --- | --- | --- |
| Parameter | 键和值均百分号编码 | 仅输出键 |
| Pair | 键原样输出，值百分号编码 | 输出 `key=` |

encode 为 false 时，GetContent(holder) 原样输出两个字段。
无参数的 GetContent 始终输出原始字段，与 encode 无关，不需要句柄或 curl 初始化。
Pair 的键即使启用编码也不转义，与 cpr 一致：
键 `a b`、值 `x+y` 分别生成 `a%20b=x%2By` 和 `a b=x%2By`。

结果是拥有存储的字符串，不带开头的问号。
保留顺序、重复键和二进制字符串长度，不修改元素。
curl 将空格编码为 %20、字面加号编码为 %2B；原始输出不规范化或检查分隔字符。

分隔符沿用 cpr 行为：只有已有输出非空时才在下一项前追加 &。
因此开头键和值均为空的 Parameter 会消失，已有文本后的空项可能生成空段或末尾 &；
`{ { "", "" }, { "a", "" }, { "", "" } }` 得到 `a&`。
键和值均空的 Pair 始终输出 `=`。

只有启用编码的非空容器才使用传入句柄。
空容器或 encode 为 false 时允许移出后的句柄；其余情况沿用
CurlHolder::UrlEncode 的显式错误结果。两个 `GetContent()` 重载均返回 `Result<std::string>`；编码分配失败返回 `OUT_OF_MEMORY`。

## 与 cpr 的差异及验证

差异包括模块、命名空间、受保护字段命名、返回 std::string 不带顶层 const，
以及直接复制 const 输入，不对它们使用无效的 std::move。
模板显式限制元素为 Parameter 或 Pair；不支持的类型在编译期失败，
而不是因缺失上游模板定义在链接期失败。
编码调用已有 UrlEncode，并按显式长度追加结果。

容器为 [Parameters](parameters.md) 和 [Payload](payload.md) 提供基类。
运行 `mcpp build` 和 `mcpp test`。测试参考 cpr 的 `test/structures_tests.cpp`，
通过两个公开包装验证序列化策略、编码开关、空项、重复项、追加、二进制/UTF-8、
独立所有权、Payload 迭代器范围、派生访问和句柄生命周期。
