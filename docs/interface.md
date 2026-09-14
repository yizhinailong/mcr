# Interface：网络接口选择

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.interface`，使用 `mcr::options::Interface`。
该类型参考 cpr 的 `include/cpr/interface.h`，继承 `StringHolder<Interface>`。

```cpp
import std;
import mcr.interface;

mcr::options::Interface automatic;
mcr::options::Interface device{ "eth0" };
mcr::options::Interface from_view{ std::string_view{ "eth0" } };
mcr::options::Interface fragments{ "if!", "eth", "0" };
std::println("{}", device.Str());
```

构造时复制并拥有接口名称，不验证、规范化或查找接口。默认值和空字符串表示不显式选择接口。
支持字符串、字符串视图、C 字符串、指针与长度、字符串片段列表；视图和字节范围保留显式长度及内嵌空字节，片段直接拼接。单参数构造保留隐式转换。

继承 `Str()`、`CStr()`、`Data()`、显式字符串转换、比较、流输出及返回新 `Interface` 的拼接。
复制得到独立存储，移动为 `noexcept`。与 cpr 的区别是 C++23 模块、命名空间和
[公共请求类型](types.md) 中的访问器命名及修正。

`Session::SetInterface` 将非空名称传给 `CURLOPT_INTERFACE`，空名称传入空指针；
curl 在新建连接时应用选择，见 [Session](session.md)。

运行 `mcpp build` 和 `mcpp test`，验证构造、所有权、空值、二进制范围、复制移动及继承操作。
