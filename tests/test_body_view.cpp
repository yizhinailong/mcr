/**
 * @file test_body_view.cpp
 * @brief Verify body-view borrowing, binary and empty input, and Buffer interoperability.
 */
import std;
import mcr.body;

static_assert(std::is_final_v<mcr::BodyView>);
static_assert(std::is_trivially_copyable_v<mcr::BodyView>);
static_assert(std::is_nothrow_default_constructible_v<mcr::BodyView>);
static_assert(std::is_nothrow_constructible_v<mcr::BodyView, std::string_view>);
static_assert(std::is_nothrow_constructible_v<mcr::BodyView, char const*>);
static_assert(std::is_nothrow_constructible_v<mcr::BodyView, char const*, std::size_t>);
static_assert(std::is_nothrow_constructible_v<mcr::BodyView, mcr::Buffer const&>);
static_assert(std::is_convertible_v<std::string_view, mcr::BodyView>);
static_assert(std::is_convertible_v<char const*, mcr::BodyView>);
static_assert(std::is_convertible_v<mcr::Buffer const&, mcr::BodyView>);
static_assert(std::is_constructible_v<mcr::BodyView, std::string const&>);
static_assert(!std::is_convertible_v<std::string, mcr::BodyView>);
static_assert(!std::is_convertible_v<mcr::BodyView, std::string_view>);
static_assert(std::is_same_v<decltype(std::declval<mcr::BodyView const&>().Str()), std::string_view>);
static_assert(noexcept(std::declval<mcr::BodyView const&>().Str()));

namespace {

    constexpr auto check_constant_views() -> bool {
        mcr::BodyView const empty;
        mcr::BodyView const null_range{ nullptr, 0 };
        mcr::BodyView const text{ "x=5" };
        char const          bytes[]{ 'a', '\0', 'b' };
        mcr::BodyView const binary{ bytes, std::size(bytes) };
        mcr::BodyView const from_view{
            std::string_view{ bytes + 1, 2 }
        };
        mcr::BodyView copied{ binary };
        mcr::BodyView moved{ std::move(copied) };
        copied = text;
        moved  = std::move(copied);
        return empty.Str().empty() && empty.Str().data() == nullptr && null_range.Str().empty() && null_range.Str().data() == nullptr && text.Str() == "x=5" && binary.Str().size() == 3 && binary.Str().data() == bytes && from_view.Str() == std::string_view{ "\0b", 2 } && moved.Str() == "x=5";
    }

    static_assert(check_constant_views());

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_body_view: {}", message);
        }
        return condition;
    }

    auto check_strings_and_copies() -> bool {
        std::string         source{ "x=5\0tail", 8 };
        mcr::BodyView const from_string{ source };
        mcr::BodyView const from_c_string = source.c_str();
        mcr::BodyView const from_view     = std::string_view{ source }.substr(2, 4);
        mcr::BodyView const from_range{ source.data(), source.size() };
        bool                passed{ check(from_string.Str().data() == source.data() && from_string.Str().size() == source.size() && from_range.Str() == from_string.Str(), "direct string and pointer/length construction must borrow all bytes, including embedded nulls") };
        passed &= check(from_c_string.Str() == "x=5" && from_c_string.Str().data() == source.data(), "C-string construction must borrow the prefix before the first null");
        passed &= check(from_view.Str().data() == source.data() + 2 && from_view.Str() == std::string_view{ "5\0ta", 4 }, "string views must preserve their exact offset and length");

        mcr::BodyView copied{ from_string };
        mcr::BodyView moved{ std::move(copied) };
        mcr::BodyView assigned{ "previous" };
        mcr::BodyView move_assigned;
        passed    &= check(&(assigned = from_string) == &assigned && &(move_assigned = std::move(moved)) == &move_assigned, "copy and move assignment must return the destination view");
        source[2]  = '7';
        for (auto const* view : { &assigned, &move_assigned }) {
            passed &= check(view->Str().data() == source.data() && view->Str().size() == source.size() && view->Str()[2] == '7', "copies and moves must borrow the same source and observe mutations without reallocation");
        }
        passed &= check(from_c_string.Str() == "x=7" && from_view.Str()[0] == '7', "all constructors must observe changes to their source bytes");
        auto* self{ &assigned };
        assigned  = *self;
        assigned  = std::move(*self);
        passed   &= check(assigned.Str().data() == source.data() && assigned.Str().size() == source.size(), "self-copy and self-move must preserve the view");

        auto returned_view{ assigned.Str() };
        returned_view.remove_prefix(2);
        assigned  = mcr::BodyView{ "replacement" };
        passed   &= check(returned_view.data() == source.data() + 2 && returned_view.size() == 6 && move_assigned.Str().data() == source.data(), "Str must return an independent descriptor and rebinding must leave other views intact");
        return passed;
    }

    auto check_buffers_and_empty_ranges() -> bool {
        std::array<unsigned char, 4> bytes{ 0x00, 0x7f, 0x80, 0xff };
        mcr::Buffer                  buffer{ bytes.begin(), bytes.end(), "ignored.bin" };
        mcr::BodyView const          body = buffer;
        bool                         passed{ check(body.Str().data() == buffer.data && body.Str().size() == bytes.size() && std::memcmp(body.Str().data(), bytes.data(), bytes.size()) == 0, "Buffer conversion must borrow the original binary range without interpreting it as text") };
        buffer.data    += 1;
        buffer.datalen  = 1;
        passed         &= check(body.Str().data() == reinterpret_cast<char const*>(bytes.data()) && body.Str().size() == bytes.size(), "a view must snapshot the Buffer pointer and length rather than refer to its mutable fields");

        auto const after_descriptor_destruction{ [&bytes] {
            mcr::Buffer const temporary{ bytes.begin(), bytes.end(), "temporary.bin" };
            return mcr::BodyView{ temporary };
        }() };
        bytes[1]  = 0xff;
        passed   &= check(after_descriptor_destruction.Str().data() == body.Str().data() && std::memcmp(after_descriptor_destruction.Str().data(), bytes.data(), bytes.size()) == 0, "destroying a Buffer descriptor must leave views usable while the source storage remains alive");

        char const*         null_data{ nullptr };
        mcr::Buffer const   empty_buffer{ null_data, null_data, "empty.bin" };
        mcr::BodyView const from_empty_buffer{ empty_buffer };
        mcr::BodyView const from_empty_view{ std::string_view{} };
        mcr::BodyView const empty_c_string{ "" };
        char const          sentinel{ 'x' };
        mcr::BodyView const empty_nonnull_range{ &sentinel, 0 };
        passed &= check(from_empty_buffer.Str().empty() && from_empty_buffer.Str().data() == nullptr && from_empty_view.Str().empty() && from_empty_view.Str().data() == nullptr, "empty Buffer and default string-view construction must preserve null/zero ranges");
        passed &= check(empty_c_string.Str().empty() && empty_c_string.Str().data() != nullptr && empty_nonnull_range.Str().empty() && empty_nonnull_range.Str().data() == &sentinel, "zero-length views must retain a supplied nonnull address");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_strings_and_copies() };
    passed &= check_buffers_and_empty_ranges();
    if (!passed) {
        return 1;
    }
    std::println("test_body_view: ok");
    return 0;
}
