/**
 * @file test_bearer.cpp
 * @brief Verify bearer token ownership, view lengths, and polymorphic extension points.
 */
import std;
import mcr.auth;

static_assert(!std::is_default_constructible_v<mcr::options::Bearer>);
static_assert(std::is_convertible_v<std::string_view, mcr::options::Bearer>);
static_assert(std::is_copy_constructible_v<mcr::options::Bearer>);
static_assert(std::is_copy_assignable_v<mcr::options::Bearer>);
static_assert(std::is_nothrow_move_constructible_v<mcr::options::Bearer>);
static_assert(std::is_nothrow_move_assignable_v<mcr::options::Bearer>);
static_assert(std::is_nothrow_destructible_v<mcr::options::Bearer>);
static_assert(std::has_virtual_destructor_v<mcr::options::Bearer>);
static_assert(std::is_same_v<decltype(std::declval<mcr::options::Bearer const&>().GetToken()), char const*>);
static_assert(noexcept(std::declval<mcr::options::Bearer const&>().GetToken()));

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_bearer: {}", message);
        }
        return condition;
    }

    auto check_construction() -> bool {
        bool passed{ true };
        for (std::string_view const value : { "", "the_token", " Bearer already prefixed ", "a:b+c/%=&", "\xE4\xB8\xAD" }) {
            mcr::options::Bearer const token  = value;
            passed                  &= check(token.GetToken() && std::string_view{ token.GetToken() } == value, "construction must preserve token bytes without adding a prefix, trimming, or encoding");
        }
        mcr::options::Bearer const empty{ std::string_view{} };
        passed &= check(empty.GetToken() && *empty.GetToken() == '\0', "a default empty view must produce a valid empty C string");
        std::array<char, 6> const bytes{ 't', 'o', 'k', 'e', 'n', 'x' };
        mcr::options::Bearer const bounded{
            std::string_view{ bytes.data(), 5 }
        };
        mcr::options::Bearer const zero_length{
            std::string_view{ bytes.data(), 0 }
        };
        passed &= check(std::string_view{ bounded.GetToken() } == "token" && *zero_length.GetToken() == '\0', "views must respect their lengths without relying on a null terminator");
        std::string const binary{ "a\0b\0c", 5 };
        mcr::options::Bearer const token{ binary };
        passed &= check(std::string_view{ token.GetToken(), binary.size() } == binary && token.GetToken()[binary.size()] == '\0', "token storage must preserve embedded nulls and add the final terminator");
        passed &= check(std::string_view{ token.GetToken() } == "a", "C-string consumers must observe only the prefix before an embedded null");
        return passed;
    }

    auto check_ownership() -> bool {
        bool passed{ true };
        for (std::size_t const length : { 1U, 512U }) {
            std::string       source(length, 't');
            std::string const expected{ source };
            mcr::options::Bearer original{ source };
            source.assign(length, 'x');
            passed &= check(std::string_view{ original.GetToken() } == expected, "short and heap-backed tokens must own their input views");
            mcr::options::Bearer copied{ original };
            original  = mcr::options::Bearer{ "replacement" };
            passed   &= check(std::string_view{ copied.GetToken() } == expected && std::string_view{ original.GetToken() } == "replacement", "copies must retain independent token values");
            mcr::options::Bearer moved{ std::move(copied) };
            passed &= check(std::string_view{ moved.GetToken() } == expected && copied.GetToken(), "moves must preserve the destination token and leave the source queryable");
            copied  = mcr::options::Bearer{ "reused" };
            passed &= check(&(original = moved) == &original && std::string_view{ original.GetToken() } == expected, "copy assignment must replace token bytes and return the destination");
            passed &= check(&(moved = std::move(copied)) == &moved && std::string_view{ moved.GetToken() } == "reused", "move assignment must replace token bytes and return the destination");
        }
        auto retained = [] {
            std::string temporary{ "temporary token" };
            return mcr::options::Bearer{ temporary };
        }();
        passed &= check(std::string_view{ retained.GetToken() } == "temporary token", "tokens must remain valid after their source strings are destroyed");
        return passed;
    }

    class EditableBearer : public mcr::options::Bearer {
    public:
        using mcr::options::Bearer::Bearer;

        auto SetToken(std::string_view token) -> void { m_token_string = token; }
    };

    class OverriddenBearer final : public mcr::options::Bearer {
    private:
        bool& m_destroyed;

    public:
        explicit OverriddenBearer(bool& destroyed) : Bearer{ "stored" }, m_destroyed{ destroyed } {}

        ~OverriddenBearer() noexcept override { m_destroyed = true; }

        auto GetToken() const noexcept -> char const* override { return "overridden"; }
    };

    auto check_polymorphism() -> bool {
        EditableBearer     editable{ "original" };
        mcr::options::Bearer const& base{ editable };
        editable.SetToken("updated token");
        bool passed{ check(std::string_view{ base.GetToken() } == "updated token", "derived classes must be able to update the protected secure-string storage") };
        bool destroyed{ false };
        {
            std::unique_ptr<mcr::options::Bearer> polymorphic{ std::make_unique<OverriddenBearer>(destroyed) };
            passed &= check(std::string_view{ polymorphic->GetToken() } == "overridden", "GetToken must dispatch virtually through the base interface");
        }
        passed &= check(destroyed, "deletion through the base interface must run the derived destructor");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_construction() };
    passed &= check_ownership();
    passed &= check_polymorphism();
    if (!passed) {
        return 1;
    }
    std::println("test_bearer: ok");
    return 0;
}
