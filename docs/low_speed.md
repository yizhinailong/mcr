# LowSpeed：低速超时

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.transfer_options`，使用 `mcr::options::LowSpeed`。

```cpp
import std;
import mcr.transfer_options;

using namespace std::chrono_literals;
mcr::options::LowSpeed option{ 1000, 1s };
option.limit = 2048;
option.time = 2min; // 保存为 120 秒。
```

构造参数依次为最低传输速率和观察时长，两者均必填。公开字段
`std::int32_t limit` 的单位为字节/秒，`std::chrono::seconds time` 的单位为秒。
零值、负值及字段类型的完整范围均原样保存。字段可修改，复制、移动和赋值得到独立数值。

API 参考 cpr 的 `include/cpr/low_speed.h`，采用 C++23 模块、`mcr::options`
命名空间和 Doxygen 注释。不提供上游已弃用的整数时长构造函数：
使用 `LowSpeed{1000, 1s}`，而非 `LowSpeed{1000, 1}`。
分钟、小时等整数时长在可表示时可隐式转换为秒；毫秒和浮点时长必须显式转换，不会静默截断。

`Session::SetLowSpeed` 将速率和秒数分别传给 `CURLOPT_LOW_SPEED_LIMIT` 和
`CURLOPT_LOW_SPEED_TIME`。curl 执行低速超时检查，失败通过 `Response::error` 返回。

运行 `mcpp build` 和 `mcpp test`。`test_low_speed` 验证单位、有符号边界、
字段修改和数值独立性，并在编译期检查旧整数时长签名及隐式有损转换不可用。
