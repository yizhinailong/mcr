/**
 * @file post_json_coro.cpp
 * @brief 使用 mcr::PostCoro 发送 JSON 并解析响应。
 */
import std;
import mcr;
import mcr_example.support;
import mcr_example.json_support;

/**
 * @brief 在协程帧内持有 URL 与 JSON 数据，等待 POST 请求完成。
 * @param url HTTP 请求地址。
 * @return 返回完整响应的协程任务。
 */
auto request(std::string url) -> mcr::Task<mcr::Response> {
    mcr::Json const document{
        {    "name",       "Alice" },
        { "enabled",          true },
        { "message", "你好，JSON!" }
    };
    mcr::Url endpoint{ std::move(url) };
    co_return co_await mcr::PostCoro(
        endpoint,
        mcr::Parameters{
            { "message", "hello world" }
    },
        mcr::JsonBody{ document },
        mcr::UserAgent{ "mcr_example" },
        mcr::options::Timeout{ std::chrono::seconds{ 10 } }
    );
}

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
        "post_json_coro",
        [](example::Arguments const& args) {
            return example::print_json_response(mcr::sync_wait(request(args.url)));
        },
        example::RuntimeKind::Coroutine
    );
}
