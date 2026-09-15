/**
 * @file test_multipart.cpp
 * @brief Verify multipart field flags, owned descriptors, borrowed buffers, and vector construction.
 */
import std;
import mcr;

static_assert(!std::is_default_constructible_v<mcr::Part>);
static_assert(!std::is_default_constructible_v<mcr::Multipart>);
static_assert(std::is_same_v<decltype(mcr::Part::value), std::string>);
static_assert(std::is_same_v<decltype(mcr::Part::data), mcr::Buffer::data_t>);
static_assert(std::is_same_v<decltype(mcr::Part::datalen), std::size_t>);
static_assert(std::is_same_v<decltype(mcr::Part::files), mcr::Files>);
static_assert(std::is_same_v<decltype(mcr::Multipart::parts), std::vector<mcr::Part>>);
static_assert(std::is_constructible_v<mcr::Part, std::string_view, mcr::File const&>);
static_assert(std::is_convertible_v<std::initializer_list<mcr::Part>, mcr::Multipart>);
static_assert(!std::is_convertible_v<std::vector<mcr::Part>, mcr::Multipart>);
static_assert(std::is_nothrow_constructible_v<mcr::Multipart, std::vector<mcr::Part>&&>);
static_assert(!std::is_nothrow_constructible_v<mcr::Multipart, std::vector<mcr::Part> const&&>);
static_assert(std::is_nothrow_move_constructible_v<mcr::Part>);
static_assert(std::is_nothrow_move_assignable_v<mcr::Part>);
static_assert(std::is_nothrow_move_constructible_v<mcr::Multipart>);
static_assert(std::is_nothrow_move_assignable_v<mcr::Multipart>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_multipart: {}", message);
        }
        return condition;
    }

    auto is_text(mcr::Part const& part) -> bool {
        return !part.is_file && !part.is_buffer && part.data == nullptr && part.datalen == 0 && part.files.begin() == part.files.end();
    }

    auto check_text_and_numbers() -> bool {
        std::string     name{ "field" };
        std::string     value{ "a\0b", 3 };
        std::string     content_type{ "application/octet-stream" };
        mcr::Part const text{ name, value, content_type };
        name = "changed";
        value.clear();
        content_type.clear();
        bool                   passed{ check(is_text(text) && text.name == "field" && text.value == std::string{ "a\0b", 3 } && text.content_type == "application/octet-stream", "text fields must own all supplied bytes and initialize non-text state to empty") };
        char const             bytes[]{ 'a', '\0', 'b', 'c' };
        std::string_view const bounded{ bytes, 3 };
        mcr::Part const        views{
            bounded,
            std::string_view{ bytes + 2, 2 },
            bounded
        };
        passed &= check(views.name == std::string{ "a\0b", 3 } && views.value == "bc" && views.content_type == views.name, "bounded name, value, and content-type views must copy their full lengths without requiring termination");
        mcr::Part const empty{ "", "" };
        passed &= check(is_text(empty) && empty.name.empty() && empty.value.empty() && empty.content_type.empty(), "empty text and an unspecified content type must be accepted");

        std::array<std::pair<std::int32_t, std::string_view>, 5> const numbers{
            std::pair<std::int32_t, std::string_view>{                                        0,           "0" },
            {                                       42,          "42" },
            {                                       -7,          "-7" },
            { std::numeric_limits<std::int32_t>::min(), "-2147483648" },
            { std::numeric_limits<std::int32_t>::max(),  "2147483647" }
        };
        for (auto const& [number, expected] : numbers) {
            mcr::Part const numeric{ "number", number, "application/number" };
            passed &= check(is_text(numeric) && numeric.name == "number" && numeric.value == expected && numeric.content_type == "application/number", "integer fields must format the full int32_t range as signed decimal text");
        }
        return passed;
    }

    auto check_file_fields() -> bool {
        mcr::Files source{
            mcr::File{ "missing/first.bin", "first-upload.bin" },
            mcr::File{ "missing/second.bin" }
        };
        mcr::Part const copied{ "files", source, "application/octet-stream" };
        source.begin()->filepath           = "changed";
        source.begin()->overriden_filename = "changed";
        bool passed{ check(copied.is_file && !copied.is_buffer && copied.value.empty() && copied.data == nullptr && copied.datalen == 0 && copied.name == "files" && copied.content_type == "application/octet-stream", "file fields must select file mode and leave text/buffer state empty") };
        passed &= check(std::ranges::distance(copied.files) == 2 && copied.files.begin()->filepath == "missing/first.bin" && copied.files.begin()->overriden_filename == "first-upload.bin" && (copied.files.begin() + 1)->filepath == "missing/second.bin", "lvalue file collections must be copied in order with filename overrides, without accessing files");

        auto const*     original_storage{ std::addressof(*source.begin()) };
        mcr::Part const moved{ "moved", std::move(source) };
        passed &= check(moved.is_file && !moved.is_buffer && moved.content_type.empty() && std::addressof(*moved.files.begin()) == original_storage && moved.files.begin()->filepath == "changed", "rvalue Files construction must transfer descriptor storage and retain file mode");
        mcr::Part const single{
            "file",
            mcr::File{ "unavailable.bin", "renamed.bin" }
        };
        passed &= check(single.is_file && std::ranges::distance(single.files) == 1 && single.files.begin()->filepath == "unavailable.bin" && single.files.begin()->overriden_filename == "renamed.bin", "a single File must implicitly become a one-file field");
        mcr::Part const empty{ "files", mcr::Files{} };
        passed &= check(empty.is_file && !empty.is_buffer && empty.files.begin() == empty.files.end() && empty.value.empty(), "an empty Files collection must retain file mode");
        return passed;
    }

    auto check_buffer_fields() -> bool {
        std::array<unsigned char, 4> bytes{ 0x00, 0x7f, 0x80, 0xff };
        std::filesystem::path const  filename{ std::filesystem::path{ "folder" } / "upload.bin" };
        auto                         part{ [&bytes, &filename] {
            auto      descriptor = mcr::Buffer::Create(bytes.begin(), bytes.end(), std::filesystem::path{ filename }).value();
            mcr::Part result{ "buffer", descriptor, "application/octet-stream" };
            descriptor.data    = nullptr;
            descriptor.datalen = 0;
            return result;
        }() };
        bool passed{ check(!part.is_file && part.is_buffer && part.files.begin() == part.files.end() && part.data == reinterpret_cast<char const*>(bytes.data()) && part.datalen == bytes.size(), "a buffer part must snapshot the borrowed pointer and length independently of its descriptor's lifetime") };
        passed &= check(part.name == "buffer" && part.value == filename.string() && part.value != filename.filename().string() && part.content_type == "application/octet-stream", "a buffer part must own the complete standard-path filename as a string without basename extraction");
        mcr::Part copied{ part };
        mcr::Part moved{ std::move(copied) };
        part.value  = "changed.bin";
        bytes[1]    = 0xff;
        passed     &= check(moved.value == filename.string() && moved.is_buffer && moved.data == part.data && moved.datalen == part.datalen && std::memcmp(moved.data, bytes.data(), bytes.size()) == 0, "part copies and moves must own metadata independently while continuing to borrow the same bytes");

        char const*     null_data{ nullptr };
        auto const      empty = mcr::Buffer::Create(null_data, null_data, std::filesystem::path{}).value();
        mcr::Part const empty_part{ "empty", empty };
        passed &= check(empty_part.is_buffer && !empty_part.is_file && empty_part.data == nullptr && empty_part.datalen == 0 && empty_part.value.empty() && empty_part.content_type.empty(), "empty buffers must retain buffer mode, a null/zero range, and an empty filename");
        return passed;
    }

    auto check_multipart_collections() -> bool {
        std::array<char, 3> bytes{ 'a', '\0', 'b' };
        auto const          buffer = mcr::Buffer::Create(bytes.begin(), bytes.end(), "bytes.bin").value();
        mcr::Multipart      form{
            {   "text",                                  "hello" },
            { "number",                                        5 },
            {   "file", mcr::File{ "missing.bin", "upload.bin" } },
            { "buffer",                                   buffer }
        };
        bool passed{ check(form.parts.size() == 4 && form.parts[0].value == "hello" && form.parts[1].value == "5" && form.parts[2].is_file && form.parts[3].is_buffer, "nested initializer lists must retain the order and modes of mixed parts") };
        form.parts.emplace_back("text", "duplicate");
        passed &= check(form.parts.size() == 5 && form.parts.back().name == form.parts.front().name && form.parts.back().value == "duplicate", "the public parts vector must support appending duplicate field names");

        mcr::Multipart const copied_vector{ form.parts };
        form.parts[0].value                    = "changed";
        form.parts[2].files.begin()->filepath  = "changed.bin";
        passed                                &= check(copied_vector.parts[0].value == "hello" && copied_vector.parts[2].files.begin()->filepath == "missing.bin" && copied_vector.parts[3].data == bytes.data(), "vector lvalue construction must copy text and nested file descriptors while borrowing buffer bytes");
        std::vector<mcr::Part> const const_source{ copied_vector.parts };
        mcr::Multipart const         const_rvalue{ std::move(const_source) };
        passed &= check(const_source.size() == 5 && const_rvalue.parts.size() == const_source.size() && const_rvalue.parts.data() != const_source.data() && const_rvalue.parts[0].value == "hello", "const vector rvalues must continue copying their descriptors");

        auto const*    vector_storage{ form.parts.data() };
        mcr::Multipart moved_vector{ std::move(form.parts) };
        passed &= check(moved_vector.parts.data() == vector_storage && moved_vector.parts.size() == 5 && moved_vector.parts[0].value == "changed", "nonconst vector rvalues must transfer storage without copying parts");
        form.parts.emplace_back("reused", "source");
        passed &= check(form.parts.back().name == "reused", "a moved-from parts vector must remain reusable");

        mcr::Multipart copy{ copied_vector };
        copy.parts[0].value = "copy only";
        mcr::Multipart moved{ std::move(copy) };
        mcr::Multipart assigned{};
        mcr::Multipart move_assigned{};
        passed                                    &= check(&(assigned = moved) == &assigned && &(move_assigned = std::move(moved)) == &move_assigned, "multipart copy and move assignment must return the destination");
        assigned.parts[2].files.begin()->filepath  = "assignment only";
        bytes[0]                                   = 'z';
        passed                                    &= check(copied_vector.parts[0].value == "hello" && move_assigned.parts[0].value == "copy only" && move_assigned.parts[2].files.begin()->filepath == "missing.bin" && assigned.parts[3].data[0] == 'z' && move_assigned.parts[3].data[0] == 'z', "multipart copies and moves must preserve independent owned fields and shared borrowed bytes");

        mcr::Multipart const         empty{};
        std::vector<mcr::Part> const no_parts;
        mcr::Multipart const         empty_copy{ no_parts };
        mcr::Multipart const         empty_move{ std::vector<mcr::Part>{} };
        passed &= check(empty.parts.empty() && empty_copy.parts.empty() && empty_move.parts.empty(), "empty lists and both vector constructors must produce empty multipart collections");
        return passed;
    }

} // namespace

int main() {
    try {
        bool passed{ check_text_and_numbers() };
        passed &= check_file_fields();
        passed &= check_buffer_fields();
        passed &= check_multipart_collections();
        if (!passed) {
            return 1;
        }
        std::println("test_multipart: ok");
        return 0;
    } catch (std::exception const& error) {
        std::println("test_multipart: unexpected exception: {}", error.what());
        return 1;
    }
}
