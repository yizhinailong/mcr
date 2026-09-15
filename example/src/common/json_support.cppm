/**
 * @file json_support.cppm
 * @brief 检查 JSON 示例的传输、HTTP 状态与响应解析结果。
 */
export module mcr_example.json_support;

import std;
import mcr;
import mcr_example.support;

export namespace example {
    /**
     * @brief 输出响应，并通过 TryJson 解析成功请求的 JSON 正文。
     * @param response 已完成的响应。
     * @return HTTP 2xx 且 JSON 解析成功时返回零，否则返回一。
     */
    auto print_json_response(mcr::Response const& response) -> int {
        if (auto const result = print_response(response); result != 0) {
            return result;
        }
        auto const parsed = response.TryJson();
        if (!parsed) {
            std::println("JSON parse failed: {}", parsed.error().message);
            return 1;
        }
        std::println("JSON: {}", parsed->dump());
        return 0;
    }

    /**
     * @brief Inspect request failures before attempting JSON parsing.
     * @param response Request result.
     * @return Zero for successful HTTP and JSON results, otherwise one.
     */
    auto print_json_response(mcr::Result<mcr::Response> const& response) -> int {
        return response ? print_json_response(*response) : print_error(response.error());
    }
} // namespace example
