/**
 * @file test_status_code.cpp
 * @brief Verify HTTP status classification boundaries through the library entry module.
 */
import std;
import mcr;

static_assert(std::is_same_v<decltype(mcr::status::HTTP_OK), long const>);
static_assert(mcr::status::is_informational(mcr::status::HTTP_EARLY_HINTS));
static_assert(mcr::status::is_success(mcr::status::HTTP_IM_USED));
static_assert(mcr::status::is_redirect(mcr::status::HTTP_PERMANENT_REDIRECT));
static_assert(mcr::status::is_client_error(mcr::status::HTTP_IM_A_TEAPOT));
static_assert(mcr::status::is_server_error(mcr::status::HTTP_NETWORK_AUTHENTICATION_REQUIRED));

int main() {
    /**
     * @brief A boundary code and its expected response class.
     */
    struct ClassificationCase {
        long code;          ///< Status code to classify.
        int response_class; ///< Hundreds digit for classes 1 through 5, or zero for no class.
    };

    ClassificationCase const cases[]{
        { (std::numeric_limits<long>::min)(), 0 },
        {                                 -1, 0 },
        {                                  0, 0 },
        {                                 99, 0 },
        {                                100, 1 },
        {                                199, 1 },
        {                                200, 2 },
        {                                299, 2 },
        {                                300, 3 },
        {                                399, 3 },
        {                                400, 4 },
        {                                499, 4 },
        {                                500, 5 },
        {                                599, 5 },
        {                                600, 0 },
        { (std::numeric_limits<long>::max)(), 0 },
    };

    bool passed{ true };
    for (auto const& entry : cases) {
        using namespace mcr::status;
        if (is_informational(entry.code) != (entry.response_class == 1) ||
            is_success(entry.code) != (entry.response_class == 2) ||
            is_redirect(entry.code) != (entry.response_class == 3) ||
            is_client_error(entry.code) != (entry.response_class == 4) ||
            is_server_error(entry.code) != (entry.response_class == 5)) {
            std::println("test_status_code: incorrect classification for {}", entry.code);
            passed = false;
        }
    }

    if (!passed) {
        return 1;
    }
    std::println("test_status_code: ok");
    return 0;
}
