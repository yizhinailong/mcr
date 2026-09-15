/**
 * @file test_body.cpp
 * @brief Verify owned body constructors, binary file reads, and inherited string operations.
 */
import std;
import mcr.body;

static_assert(std::derived_from<mcr::Body, mcr::StringHolder<mcr::Body>>);
static_assert(std::is_convertible_v<std::string, mcr::Body>);
static_assert(std::is_convertible_v<std::string_view, mcr::Body>);
static_assert(std::is_convertible_v<char const*, mcr::Body>);
static_assert(std::is_convertible_v<mcr::Buffer const&, mcr::Body>);
static_assert(std::same_as<decltype(mcr::Body::FromFile(std::declval<mcr::File const&>())), mcr::Result<mcr::Body>>);
static_assert(!std::is_convertible_v<mcr::Body, std::string>);
static_assert(std::is_nothrow_move_constructible_v<mcr::Body>);
static_assert(std::is_nothrow_move_assignable_v<mcr::Body>);
static_assert(std::has_virtual_destructor_v<mcr::Body>);
static_assert(std::is_same_v<decltype(std::declval<mcr::Body const&>() + "suffix"), mcr::Body>);
static_assert(std::is_same_v<decltype(std::declval<mcr::Body const&>().Str()), std::string const&>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_body: {}", message);
        }
        return condition;
    }

    /**
     * @brief Own one temporary file in a newly created directory and remove only those paths.
     */
    struct TemporaryFile {
        std::filesystem::path directory{ std::filesystem::temp_directory_path() / std::format("mcr_body_{}", std::chrono::steady_clock::now().time_since_epoch().count()) };
        std::filesystem::path path{ directory / "body input.bin" };

        TemporaryFile() {
            if (!std::filesystem::create_directory(directory)) {
                throw std::runtime_error{ "Unable to create a unique body test directory" };
            }
        }

        TemporaryFile(TemporaryFile const&)                    = delete;
        auto operator=(TemporaryFile const&) -> TemporaryFile& = delete;

        ~TemporaryFile() {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(directory, error);
        }

        auto Write(std::string_view bytes) const -> void {
            std::ofstream stream;
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            stream.open(path, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            stream.close();
        }
    };

    auto check_text_and_operations() -> bool {
        std::string     source{ "x=5\0tail", 8 };
        mcr::Body const from_string   = source;
        mcr::Body const from_view     = std::string_view{ source }.substr(2, 4);
        mcr::Body const from_c_string = source.c_str();
        mcr::Body const from_range{ source.data(), source.size() };
        source.assign("changed");
        bool            passed{ check(from_string.Str() == std::string{ "x=5\0tail", 8 } && from_range == from_string && from_view.Str() == std::string{ "5\0ta", 4 } && from_c_string == "x=5", "all text constructors must own their input, retaining explicit lengths and C-string termination rules") };
        mcr::Body const temporary(std::string(128, 'x'));
        passed &= check(temporary.Str() == std::string(128, 'x'), "moving a temporary string must leave the body owning its bytes");
        mcr::Body const fragments{ "x=", "5&", "y=13" };
        mcr::Body const binary_fragments{
            std::string{ "a\0", 2 },
            "b"
        };
        passed &= check(fragments == "x=5&y=13" && binary_fragments.Str() == std::string{ "a\0b", 3 }, "fragment lists must concatenate all bytes in order without separators");
        passed &= check(mcr::Body{}.Str().empty() && mcr::Body{ "" }.Str().empty() && mcr::Body(std::string_view{}).Str().empty() && mcr::Body(nullptr, 0).Str().empty() && mcr::Body(std::initializer_list<std::string>{}).Str().empty(), "all empty text construction forms must produce an empty owned string");

        mcr::Body copied{ from_string };
        copied += "suffix";
        mcr::Body moved{ std::move(copied) };
        mcr::Body assigned;
        mcr::Body move_assigned;
        passed   &= check(&(assigned = moved) == &assigned && &(move_assigned = std::move(moved)) == &move_assigned, "copy and move assignment must return the destination");
        assigned += "changed";
        passed   &= check(move_assigned.Str() == from_string.Str() + "suffix" && assigned.Str() == move_assigned.Str() + "changed" && from_string.Str().size() == 8, "copies must own independent bytes and moves must preserve destination contents");
        passed   &= check((from_string + mcr::Body{ "!" }).Str() == from_string.Str() + "!" && (from_c_string + "!") == "x=5!", "inherited concatenation must construct a Body containing the combined bytes");
        mcr::Body appended{ "a" };
        appended += std::string{ "b" };
        appended += mcr::Body{ "c" };
        passed   &= check(appended == "abc" && appended != "other", "inherited append and comparison operations must remain available");
        auto converted{ static_cast<std::string>(from_string) };
        converted.clear();
        std::ostringstream stream;
        stream << from_string;
        passed &= check(stream.str() == from_string.Str() && from_string.Data()[3] == '\0' && from_string.CStr()[from_string.Str().size()] == '\0', "inherited conversion, streaming, and byte access must retain owned binary contents and their final terminator");
        return passed;
    }

    auto check_buffer_ownership() -> bool {
        std::array<unsigned char, 5> bytes{ 0x01, 0x00, 0x80, 0xff, 0x02 };
        std::string const            expected{ reinterpret_cast<char const*>(bytes.data() + 1), 3 };
        auto                         buffer = mcr::Buffer::Create(bytes.begin() + 1, bytes.end() - 1, "filename is ignored").value();
        mcr::Body const              body   = buffer;
        bytes.fill(0x42);
        buffer.data    = nullptr;
        buffer.datalen = 0;
        bool            passed{ check(body.Str() == expected, "Buffer construction must copy its exact binary subrange independently of later source and descriptor changes") };
        mcr::Body const from_destroyed_source{ [] {
            std::string const source{ "a\0b", 3 };
            auto const        descriptor = mcr::Buffer::Create(source.begin(), source.end(), "temporary").value();
            return mcr::Body{ descriptor };
        }() };
        passed &= check(from_destroyed_source.Str() == std::string{ "a\0b", 3 }, "the body must remain usable after both the Buffer and its backing storage are destroyed");
        char const* null_data{ nullptr };
        auto const  empty  = mcr::Buffer::Create(null_data, null_data, "empty").value();
        passed            &= check(mcr::Body{ empty }.Str().empty() && mcr::Body{ buffer }.Str().empty(), "null/zero Buffer ranges must construct an empty body");
        return passed;
    }

    auto check_file_ownership_and_errors() -> bool {
        TemporaryFile temporary;
        bool          passed{ true };
        // Include empty files, full read blocks, partial final blocks, and many blocks.
        for (std::size_t length : { 0u, 1u, 16383u, 16384u, 16385u, 32768u, 100003u }) {
            std::string expected(length, '\0');
            for (std::size_t index{ 0 }; index < length; ++index) {
                expected[index] = static_cast<char>(index % 256);
            }
            temporary.Write(expected);
            mcr::File  descriptor{ temporary.path.string(), "this-override-is-not-a-file" };
            auto const body  = mcr::Body::FromFile(descriptor).value();
            passed          &= check(body.Str() == expected, "file construction must read every binary byte through EOF, including nulls, CR/LF, control bytes, and a partial final block");
            temporary.Write("replaced");
            passed &= check(std::filesystem::remove(temporary.path) && body.Str() == expected, "the stream must close after construction and replacing or removing the file must not affect the body");
        }

        auto const missing_path{ temporary.directory / "missing.bin" };
        for (auto const& path : { missing_path.string(), std::string{} }) {
            auto const body  = mcr::Body::FromFile(mcr::File{ path });
            passed          &= check(!body && body.error().code == mcr::ErrorCode::FILE_COULDNT_READ_FILE && body.error().message == "Can't open the file for HTTP request body!", "missing and empty paths must return the open failure diagnostic");
        }
        auto const directory  = mcr::Body::FromFile(mcr::File{ temporary.directory.string() });
        passed               &= check(!directory && (directory.error().code == mcr::ErrorCode::FILE_COULDNT_READ_FILE || directory.error().code == mcr::ErrorCode::READ_ERROR), "unreadable directories must fail at open or read");
        return passed;
    }

} // namespace

int main() {
    try {
        bool passed{ check_text_and_operations() };
        passed &= check_buffer_ownership();
        passed &= check_file_ownership_and_errors();
        if (!passed) {
            return 1;
        }
        std::println("test_body: ok");
        return 0;
    } catch (std::exception const& error) {
        std::println("test_body: unexpected exception: {}", error.what());
        return 1;
    }
}
