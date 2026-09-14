# SecureString：安全字符串

[文档索引](README.md) · [项目首页](../README.md)

导入 `mcr` 或 `mcr.secure_string`，使用 `mcr::utils::SecureAllocator<T>`
和 `SecureString`，设计参考 cpr 的 `include/cpr/secure_string.h`。

```cpp
import std;
import mcr.secure_string;

mcr::utils::SecureString credentials{ "username" };
credentials += ':';
credentials += std::string_view{ "password" };
std::string_view view{ credentials }; // 借用存储，不延长生命周期。
```

SecureString 是 `std::basic_string<char, std::char_traits<char>, SecureAllocator<char>>` 的别名。
保留标准字符串的构造、复制、移动、赋值、视图及修改行为，支持内嵌空字节。
复制仍使用安全分配器；显式复制到 `std::string` 后，普通存储不具备相同擦除行为。

SecureAllocator 将分配和对齐委托给 `std::allocator<T>`。
它没有状态，实例经 IsEqual、==、!= 比较相等，跨元素类型也成立。
支持跨类型构造和赋值及 `std::allocator_traits` 的重绑定。
小写 allocate 和 deallocate 是标准分配器接口要求。

## 擦除范围

释放内存前，`deallocate(p, n)` 覆写全部 `n * sizeof(T)` 字节，包括未使用容量。
通过 volatile 无符号字节指针写入，使清零成为
[C++ volatile 访问规则](https://eel.is/c++draft/intro.abstract) 下的可观察操作。
元素必须已经析构，指针和数量必须与相等分配器返回的分配匹配。

擦除发生在释放分配时，包括字符串析构或重新分配释放堆缓冲区。
短字符串的内联存储不经过分配器，因此不擦除。
clear、erase、缩小 resize 和赋值可能保留分配及旧文本。
该别名不锁定内存、不清理源缓冲区，也不擦除外部副本。

与 cpr 的区别是 C++23 模块、命名空间，以及用 volatile 字节写入替代
`std::fill_n(p, n, T{})`。这样可避免普通清零写入被消除，覆盖完整分配，
且不在元素生命周期结束后向元素赋值。
分配直接委托给标准分配器，不私有继承它；公开分配器接口保留，显式声明 is_always_equal。

运行 `mcpp build`、`mcpp test` 和 `mcpp test test_secure_string --profile release`。
测试验证重绑定、对齐、字符串所有权和操作，并在替换的 delete 函数实际释放之前检查字节，
不读取已经释放的内存。
