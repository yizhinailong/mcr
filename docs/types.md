# 公共请求类型

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.types`，使用 mcr 命名空间中的公共类型。
实现位于 `src/utils/types.cppm`，模块名与公开命名空间不受路径影响。

```cpp
import std;
import mcr;

mcr::Url url{ "https://example.test", "/api" };
url += mcr::Url{ "/items" };
mcr::Header headers{ { "Content-Type", "application/json" } };
std::println("{}: {}", url.Str(), headers.at("content-type"));
```

- CprOffT 是当前 curl 的 curl_off_t 别名，CprPfArgT 在当前依赖下也别名到 CprOffT，
  用于进度回调；使用这些类型无需包含 curl 头文件。
- `StringHolder<T>` 拥有文本，拼接返回 T。
  Url 和 UserAgent 接收字符串、视图、C 字符串、字节范围及片段列表；
  视图和范围会复制，显式长度保留内嵌空字节，不验证或编码文本。
- UserAgent 与 Url 同由 mcr.types 导出。原先导入 mcr.user_agent 的代码
  应改用 mcr.types 或总入口 mcr。
- Str、CStr、Data、显式 std::string 转换、流输出、复制移动、比较及返回 void 的 +=
  保留 cpr 行为，派生类可访问受保护的 m_str。
- Header 是使用 CaseInsensitiveCompare 的映射，字段名称忽略大小写，
  值和首次插入键的拼写不变。比较与 cpr 一样在当前 C locale 下使用 std::tolower，
  输入先转为无符号字节；映射包含键期间必须保持该 locale 稳定。

## 命名迁移与差异

公开类型位于 mcr，通过 C++23 模块导出。
类型别名和公开成员函数采用 PascalCase，非公开字段采用 m_ 前缀：

| 原名称或 cpr 名称 | 当前名称 |
| --- | --- |
| cpr_off_t | CprOffT |
| cpr_pf_arg_t | CprPfArgT |
| str() | Str() |
| c_str() | CStr() |
| data() | Data() |
| str_ | m_str |

仅支持声明的 curl 依赖，不保留旧进度回调类型或版本兼容分支。
`StringHolder<T>::operator+=(StringHolder<T> const&)` 追加另一个对象存储的字符串，
修正上游直接追加包装对象而在实例化时无法编译的问题。
operator!=(char const*) 比较字符串内容，修正上游指针比较，使其与 operator== 一致。

运行 `mcpp build` 和 `mcpp test`，验证模块及回归行为。
