/**
 * @file multi_post_json_async.cpp
 * @brief 使用 mcr::MultiPostAsync 发送 JSON 并解析响应。
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
        "multi_post_json_async",
        [](example::Arguments const& args) {
            mcr::Json const first_document{
                {    "name",       "Alice" },
                { "enabled",          true },
                { "message", "你好，JSON!" },
                { "request",       "first" }
            };
            mcr::Json const second_document{
                {    "name",       "Alice" },
                { "enabled",          true },
                { "message", "你好，JSON!" },
                { "request",      "second" }
            };
            auto first{
                std::tuple{
                           mcr::Url{ args.url },
                           mcr::Parameters{ { "request", "first" } },
                           mcr::JsonBody{ first_document },
                           mcr::UserAgent{ "mcr_example" },
                           mcr::options::Timeout{ std::chrono::seconds{ 10 } } }
            };
            auto second{
                std::tuple{
                           mcr::Url{ args.url },
                           mcr::Parameters{ { "request", "second" } },
                           mcr::JsonBody{ second_document },
                           mcr::UserAgent{ "mcr_example" },
                           mcr::options::Timeout{ std::chrono::seconds{ 10 } } }
            };
            auto pending{ mcr::MultiPostAsync(std::move(first), std::move(second)) };
            if (!pending) {
                return example::print_error(pending.error());
            }
            int result{};
            for (auto& task : *pending) {
                result |= example::print_json_response(task.Get());
            }
            return result;
        },
        example::RuntimeKind::Async
    );
}
