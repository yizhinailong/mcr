# JSON 请求与响应

`import mcr;` 提供 `Json`、`JsonBody`、`JsonError` 和响应解析接口。
也可以分别导入 `mcr.json`、`mcr.response`、`mcr.session` 或 `mcr.api`。
`Json` 是已有依赖 `nlohmann::json` 的公开别名，保留其构造、访问、`get<T>()`、
`parse()` 和 `dump()` 接口。需要直接命名第三方接口时，显式 `import nlohmann.json;`。

```cpp
import std;
import mcr;

auto main() -> int {
    mcr::Json document{
        { "name", "Alice" },
        { "enabled", true }
    };
    auto response = mcr::Post(
        mcr::Url{ "http://127.0.0.1:8080/users" },
        mcr::JsonBody{ document }
    );
    if (response.error) {
        std::println("请求失败：{}", response.error.message);
        return 1;
    }
    auto result = response.TryJson();
    if (!result) {
        std::println("JSON 解析失败：{}", result.error().message);
        return 1;
    }
    std::println("HTTP {}：{}", response.status_code, result->dump());
}
```

## 请求正文

`JsonBody{ value }` 在构造时调用 `value.dump()`，保存独立的紧凑 UTF-8 字符串。
之后修改或销毁原始 JSON 不影响正文。`Str()` 返回只读字符串引用。
字符串中的引号、换行和空字节按 JSON 规则转义；非法 UTF-8 在构造时抛出
`Json::type_error`，内存分配失败正常传播。序列化采用 nlohmann 的默认规则，
包括数值和二进制值的处理，详见 [dump 文档](https://json.nlohmann.me/api/basic_json/dump/)。

JSON 对象、数组、字符串、数值、布尔值和 `null` 都可以作为根值。
`JsonBody{ Json(nullptr) }` 发送四个字节 `null`，不是空正文。
输入的 JSON 字符串值会被再次加引号和转义；已经序列化的文本可以使用
`JsonBody{ Json::parse(text) }`，或者使用原有 `Body{ text }` 并显式设置请求头。
复制 JSON 或接收解析结果时使用 `auto value = response.Json();`、`Json copy = value;`。
单元素花括号构造 `Json{ value }` 遵循 nlohmann 的初始化列表规则，可能创建单元素数组。

```cpp
mcr::Session session;
session.SetUrl(mcr::Url{ "http://127.0.0.1:8080/echo" });
session.SetJsonBody(mcr::JsonBody{ mcr::Json{ { "count", 3 } } });
auto response = session.Post();
auto value = response.Json(); // 解析失败时抛出 JSON 异常
```

- `SetJsonBody` 和 `SetOption` 均接受 `JsonBody` 的 const 引用或右值。
  正文与 Body、BodyView、Payload、Multipart 互相替换，最后设置者生效。
- 正文跨请求保留，直到被替换或调用 `RemoveContent()`。HEAD 和 Download
  暂时忽略正文，后续请求仍可发送它。
- 实际发送 JSON 正文且没有显式 `Content-Type` 时，自动添加 `application/json`。
  显式请求头忽略名称大小写并始终优先，包括显式空值；与正文选项的参数位置无关。
  普通 Header 选项仍遵循既有合并与覆盖规则。`Accept` 沿用原有行为。
- 自动类型仅加入当次发送的请求头，不修改 `GetHeader()` 返回的持久化 Header。
  切换正文、移除正文、HEAD 或 Download 均不会残留自动 JSON 类型。
  用户显式配置的请求头则继续保留。
- 同步、异步、回调和批量入口都通过 Session 使用同一实现。
  异步入口复制普通左值、移动右值，持有序列化字节；显式 `std::ref` / `std::cref`
  仍需要调用方维持被引用选项的生命周期。
  `JsonBody` 构造发生在提交任务前，构造异常也在调用线程中传播。

## 响应解析和错误

| 接口 | 结果 |
| --- | --- |
| `Response::Json() const` | 返回独立 `mcr::Json`，JSON 错误抛出原始异常 |
| `Response::TryJson() const` | 返回 `std::expected<mcr::Json, JsonError>`，捕获 JSON 异常并保存诊断 |

两者每次都解析当前 `text`，不缓存、不修改原文，不要求响应媒体类型为 JSON。
使用 nlohmann 的默认解析规则，注释和尾随逗号不启用。
空正文、纯空白或不合法的 JSON 返回解析错误；有效的 `null` 返回成功。
HEAD、204、下载以及正文交给 WriteCallback/SSE 消费的响应可能没有缓冲正文，
调用方应根据业务决定是否需要解析。

`JsonError` 包含 nlohmann 异常编号 `id`、拥有存储的诊断 `message`，以及可选的
`byte`。语法错误的字节位置从 1 开始，EOF 可以是输入长度加 1；数值溢出等
其他 JSON 异常没有字节位置。`TryJson()` 只捕获 JSON 异常，不标记 `noexcept`，
内存分配等资源异常仍向外传播。

传输错误位于 `Response::error`，HTTP 结果位于 `status_code`，JSON 解析结果单独返回。
显式解析不会检查或改写前两者，因此也可以读取 HTTP 4xx/5xx 的 JSON 错误正文。
调用方需要自行判断业务所要求的 HTTP 成功状态。

## 兼容性和验证

cpr 使用 `Body` 和显式 `Content-Type` 发送 JSON，并在 `Response::text` 中保存响应，
参见 [cpr 请求测试](https://github.com/libcpr/cpr/blob/master/test/post_tests.cpp) 和
[Response 定义](https://github.com/libcpr/cpr/blob/master/include/cpr/response.h)。
`JsonBody`、默认媒体类型及响应解析方法是 mcr 的便利扩展；原始 Body 的字节语义不变。

公开 `Content` variant 在末尾新增 `JsonBody`，原有分支索引不变；使用穷尽式
`std::visit` 的调用方需要增加该分支的处理。公开 JSON 类型依赖 nlohmann，
不提供跨 JSON 实现的抽象适配层。

运行 `mcpp test test_json` 验证模块导出、序列化所有权、解析诊断、请求头优先级、
正文切换和异步/批量请求；运行 `mcpp test` 验证完整测试集。HTTP 测试使用本地 fixture。
