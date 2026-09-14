/**
 * @file post_json.cpp
 * @brief 使用 JsonBody 发送 JSON POST 请求，并通过 TryJson 解析响应。
 */
import std;
import mcr;
import mcr_example.support;

/**
 * @brief 向指定地址发送 JSON，分别检查传输、HTTP 状态和 JSON 解析结果。
 * @param argc 命令行参数数量。
 * @param argv 程序名之后传入 URL，或使用 --help 查看帮助。
 * @return 成功或显示帮助时返回零，请求或解析失败时返回一，参数错误时返回二。
 */
auto main(int argc, char** argv) -> int {
    return example::run(
        argc,
        argv,
        "post_json",
        [](example::Arguments const& args) {
            mcr::Json const document{
                {    "name",       "Alice" },
                { "enabled",          true },
                { "message", "你好，JSON!" }
            };
            auto const response{
                mcr::Post(
                    mcr::Url{                     args.url },
                    mcr::Parameters{ { "message", "hello world" } },
                    mcr::JsonBody{                     document },
                    mcr::UserAgent{                "mcr_example" },
                    mcr::options::Timeout{   std::chrono::seconds{ 10 } }
                )
            };
            if (auto const result = example::print_response(response); result != 0) {
                return result;
            }
            auto const parsed = response.TryJson();
            if (!parsed) {
                std::println("JSON parse failed: {}", parsed.error().message);
                return 1;
            }
            std::println("JSON: {}", parsed->dump());
            return 0;
        },
        example::RuntimeKind::Synchronous
    );
}
