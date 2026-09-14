# LimitRate：传输速率上限

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.transfer_options`，使用 `mcr::options::LimitRate`。

```cpp
import mcr.transfer_options;

mcr::options::LimitRate limited{ 1024, 2048 }; // 下载和上传速率，单位为字节/秒。
mcr::options::LimitRate unlimited{ 0, 0 };
limited.uprate = 4096;
```

选项参考 cpr 的 `include/cpr/limit_rate.h`。构造参数依次为下载和上传速率，均为必填项；
没有默认或单参数构造函数。公开字段 `downrate`、`uprate` 使用 `std::int64_t`，
可表示超过 32 位范围的速率。

传给 curl 时，零表示不限速。所有有符号 64 位值均原样保存，包括负值，不截断或验证。
字段可独立修改；复制、移动和赋值保留数值，不共享状态。

`Session::SetLimitRate` 分别将字段传给 `CURLOPT_MAX_RECV_SPEED_LARGE` 和
`CURLOPT_MAX_SEND_SPEED_LARGE`，由 curl 在传输期间执行限速。
与 cpr 的区别是 C++23 模块、`mcr::options` 命名空间、构造参数命名和 Doxygen 注释；
公开字段和运行行为保持一致。

运行 `mcpp build` 和 `mcpp test`。`test_limit_rate` 验证参数顺序、零值、负值、
超过 32 位的速率、64 位边界，以及复制移动后的独立修改。
