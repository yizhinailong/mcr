/**
 * @file post_json_callback.cpp
 * @brief 使用 mcr::PostCallback 发送 JSON 并解析响应。
 */
import std;
import mcr;
import mcr_example.support;
import mcr_example.json_support;

/**
 * @brief 向指定地址发送 JSON，检查请求结果并解析响应。
 * @param argc 命令行参数数量。
 * @param argv 程序名之后传入 URL，或使用 --help 查看帮助。
 * @return 成功或显示帮助时返回零，请求或解析失败时返回一，参数错误时返回二。
 */
auto main(int argc, char** argv) -> int {
    return example::run(
        argc,
        argv,
        "post_json_callback",
        [](example::Arguments const& args) {
            mcr::Json const document{
                {    "name",       "Alice" },
                { "enabled",          true },
                { "message", "你好，JSON!" }
            };
            auto on_response = [](mcr::Response response) {
                return example::print_json_response(response);
            };
            auto completion{
                mcr::PostCallback(
                    std::move(on_response),
                    mcr::Url{                     args.url },
                    mcr::Parameters{ { "message", "hello world" } },
                    mcr::JsonBody{                     document },
                    mcr::UserAgent{                "mcr_example" },
                    mcr::options::Timeout{   std::chrono::seconds{ 10 } }
                )
            };
            return completion.Get();
        },
        example::RuntimeKind::Async
    );
}
