/**
 * @file test_buffer.cpp
 * @brief Verify borrowed upload bytes, empty ranges, and standard filesystem filename ownership.
 */
import std;
import mcr;

static_assert(std::is_same_v<mcr::Buffer::data_t, char const*>);
static_assert(std::is_same_v<decltype(mcr::Buffer::data), char const*>);
static_assert(std::is_same_v<decltype(mcr::Buffer::datalen), std::size_t>);
static_assert(std::is_same_v<decltype(mcr::Buffer::filename), std::filesystem::path const>);
static_assert(!std::is_default_constructible_v<mcr::Buffer>);
static_assert(std::is_copy_constructible_v<mcr::Buffer>);
static_assert(std::is_move_constructible_v<mcr::Buffer>);
static_assert(!std::is_copy_assignable_v<mcr::Buffer>);
static_assert(!std::is_move_assignable_v<mcr::Buffer>);
static_assert(std::same_as<decltype(mcr::Buffer::Create(static_cast<char const*>(nullptr), static_cast<char const*>(nullptr), "")), mcr::Result<mcr::Buffer>>);
static_assert(!std::is_default_constructible_v<mcr::Buffer>);
static_assert(!std::is_constructible_v<mcr::Buffer, char const*, char const*, std::filesystem::path&>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_buffer: {}", message);
        }
        return condition;
    }

    auto check_borrowed_bytes() -> bool {
        std::string content{ "hello world" };
        auto        buffer = mcr::Buffer::Create(content.begin(), content.end(), "test_file").value();
        bool        passed{ check(buffer.data == content.data() && buffer.datalen == content.size() && std::string_view{ buffer.data, buffer.datalen } == "hello world", "string iterators must borrow the complete source without copying") };
        content[0]  = 'H';
        passed     &= check(buffer.data[0] == 'H', "source mutations without reallocation must be visible through the buffer");

        std::string const binary{ "a\0b\0c", 5 };
        auto const        subrange  = mcr::Buffer::Create(binary.cbegin() + 1, binary.cend() - 1, "binary").value();
        passed                     &= check(subrange.data == binary.data() + 1 && subrange.datalen == 3 && std::string_view{ subrange.data, subrange.datalen } == std::string_view{ "\0b\0", 3 }, "const subranges must preserve offsets and embedded null bytes");

        char const raw[]{ "hello world" };
        auto const without_null  = mcr::Buffer::Create(std::begin(raw), std::end(raw) - 1, "raw").value();
        auto const with_null     = mcr::Buffer::Create(raw, raw + std::size(raw), "raw").value();
        passed                  &= check(without_null.data == raw && without_null.datalen == 11 && with_null.datalen == 12 && with_null.data[11] == '\0', "pointer ranges must include a terminator only when it is inside the supplied range");

        std::vector<unsigned char>     bytes{ 0x00, 0x7f, 0x80, 0xff };
        auto const                     unsigned_buffer = mcr::Buffer::Create(bytes.begin(), bytes.end(), "unsigned").value();
        std::vector<signed char> const signed_bytes{ -1, 0, 1 };
        auto const                     signed_buffer  = mcr::Buffer::Create(signed_bytes.cbegin(), signed_bytes.cend(), "signed").value();
        passed                                       &= check(unsigned_buffer.data == reinterpret_cast<char const*>(bytes.data()) && unsigned_buffer.datalen == bytes.size() && std::memcmp(unsigned_buffer.data, bytes.data(), bytes.size()) == 0, "unsigned byte vectors must retain every byte without conversion");
        passed                                       &= check(signed_buffer.data == reinterpret_cast<char const*>(signed_bytes.data()) && signed_buffer.datalen == signed_bytes.size() && std::memcmp(signed_buffer.data, signed_bytes.data(), signed_bytes.size()) == 0, "signed byte vectors must expose the original representation");

        std::array<std::byte, 3> const storage{ std::byte{ 0x00 }, std::byte{ 0x80 }, std::byte{ 0xff } };
        auto const                     byte_array = mcr::Buffer::Create(storage.begin(), storage.end(), "bytes").value();
        std::span<std::byte const>     view{ storage };
        auto const                     byte_span  = mcr::Buffer::Create(view.begin(), view.end(), "span").value();
        passed                                   &= check(byte_array.data == reinterpret_cast<char const*>(storage.data()) && byte_array.datalen == storage.size() && byte_span.data == byte_array.data && byte_span.datalen == byte_array.datalen && std::memcmp(byte_span.data, storage.data(), storage.size()) == 0, "std::byte arrays and spans must share the original storage");
        return passed;
    }

    auto check_empty_and_reversed_ranges() -> bool {
        char const*                      null_data{ nullptr };
        auto const                       null_buffer = mcr::Buffer::Create(null_data, null_data, "empty").value();
        std::vector<unsigned char> const empty_vector;
        auto const                       vector_buffer = mcr::Buffer::Create(empty_vector.begin(), empty_vector.end(), "empty").value();
        std::string const                empty_string;
        auto const                       string_buffer = mcr::Buffer::Create(empty_string.begin(), empty_string.end(), "empty").value();
        std::array<std::byte, 0> const   empty_array;
        auto const                       array_buffer = mcr::Buffer::Create(empty_array.begin(), empty_array.end(), "empty").value();
        std::string                      content{ "bytes" };
        auto const                       end_buffer = mcr::Buffer::Create(content.end(), content.end(), "empty").value();
        bool                             passed{ true };
        for (auto const* buffer : { &null_buffer, &vector_buffer, &string_buffer, &array_buffer, &end_buffer }) {
            passed &= check(buffer->data == nullptr && buffer->datalen == 0 && buffer->filename == "empty", "empty ranges must retain the filename and become null/zero without dereferencing or subtracting iterators");
        }

        {
            auto const failure  = mcr::Buffer::Create(content.data() + content.size(), content.data(), "reversed");
            passed             &= check(!failure && failure.error().code == mcr::ErrorCode::BAD_FUNCTION_ARGUMENT, "failure must return the expected error code");
        }
        return passed;
    }

    auto check_filename_and_copies() -> bool {
        std::array<char, 3>         bytes{ 'a', 'b', 'c' };
        std::filesystem::path const expected{ std::filesystem::path{ "uploads" } / "raw" / ".." / "report.bin" };
        std::filesystem::path       filename{ expected };
        auto                        original = mcr::Buffer::Create(bytes.begin(), bytes.end(), std::move(filename)).value();
        filename                             = "changed";
        bool passed{ check(original.filename == expected && original.filename != expected.lexically_normal() && original.filename.filename() == "report.bin", "the filename must be owned as a standard path without normalization or filesystem access") };

        mcr::Buffer copied{ original };
        mcr::Buffer moved{ std::move(original) };
        bytes[1]            = 'x';
        passed             &= check(copied.data == bytes.data() && moved.data == bytes.data() && copied.datalen == bytes.size() && moved.datalen == bytes.size() && copied.data[1] == 'x' && moved.data[1] == 'x', "copies and moves must continue borrowing the same live bytes");
        passed             &= check(copied.filename == expected && moved.filename == expected && original.filename == expected, "the const filename must be copied even by the implicit move constructor");

        copied.data         = bytes.data() + 1;
        copied.datalen      = 1;
        passed             &= check(copied.data[0] == 'x' && copied.datalen == 1 && moved.data == bytes.data() && moved.datalen == bytes.size(), "public pointer and length updates must leave other descriptors unchanged");
        auto const unnamed  = mcr::Buffer::Create(bytes.begin(), bytes.end(), std::filesystem::path{}).value();
        passed             &= check(unnamed.filename.empty() && unnamed.datalen == bytes.size(), "an empty upload filename must be accepted");
        std::filesystem::path const unicode_path{ u"\u4e2d\u6587.bin" };
        auto const                  unicode  = mcr::Buffer::Create(bytes.begin(), bytes.end(), std::filesystem::path{ unicode_path }).value();
        passed                              &= check(unicode.filename.native() == unicode_path.native(), "native standard filesystem filename representation must be retained");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_borrowed_bytes() };
    passed &= check_empty_and_reversed_ranges();
    passed &= check_filename_and_copies();
    if (!passed) {
        return 1;
    }
    std::println("test_buffer: ok");
    return 0;
}
