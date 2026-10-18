/**
 * @file test_file.cpp
 * @brief Verify file descriptor ownership, ordered collections, and standard filesystem interoperation.
 */
import std;
import mcr;

static_assert(std::is_same_v<decltype(mcr::File::filepath), std::string>);
static_assert(std::is_same_v<decltype(mcr::File::overriden_filename), std::string>);
static_assert(!std::is_default_constructible_v<mcr::File>);
static_assert(!std::is_convertible_v<std::string, mcr::File>);
static_assert(std::is_convertible_v<mcr::File, mcr::Files>);
static_assert(std::is_convertible_v<std::initializer_list<mcr::File>, mcr::Files>);
static_assert(std::is_convertible_v<std::initializer_list<std::string>, mcr::Files>);
static_assert(std::is_nothrow_move_constructible_v<mcr::Files>);
static_assert(std::is_nothrow_move_assignable_v<mcr::Files>);
static_assert(noexcept(std::declval<mcr::File const&>().HasOverridenFilename()));
static_assert(std::is_same_v<mcr::Files::iterator, std::vector<mcr::File>::iterator>);
static_assert(std::is_same_v<mcr::Files::const_iterator, std::vector<mcr::File>::const_iterator>);
static_assert(std::is_same_v<decltype(std::declval<mcr::Files&>().emplace_back(std::declval<mcr::File const&>())), void>);
static_assert(std::ranges::random_access_range<mcr::Files>);
static_assert(std::ranges::random_access_range<mcr::Files const>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_file: {}", message);
        }
        return condition;
    }

    auto same_file(mcr::File const& first, mcr::File const& second) -> bool {
        return first.filepath == second.filepath && first.overriden_filename == second.overriden_filename;
    }

    auto check_file() -> bool {
        std::string path{ "folder/../missing.txt" };
        std::string filename{ "upload.txt" };
        mcr::File file{ path, filename };
        path = "changed";
        filename = "changed";
        bool passed{ check(file.filepath == "folder/../missing.txt" && file.overriden_filename == "upload.txt" && file.HasOverridenFilename(), "a file must own the exact path and filename without accessing or normalizing a filesystem path") };
        mcr::File copied{ file };
        file.filepath = "other.txt";
        file.overriden_filename.clear();
        passed &= check(!file.HasOverridenFilename() && copied.filepath == "folder/../missing.txt" && copied.overriden_filename == "upload.txt", "public updates must affect override detection and leave copies independent");
        file.overriden_filename = " ";
        passed &= check(file.HasOverridenFilename(), "a whitespace-only filename must count as an override");
        mcr::File const empty{ "" };
        passed &= check(empty.filepath.empty() && empty.overriden_filename.empty() && !empty.HasOverridenFilename(), "empty paths must be accepted with no default filename override");

        std::array<char, 5> const name{ 'n', 'a', 'm', 'e', 'x' };
        mcr::File const bounded{
            "path",
            std::string_view{ name.data(), 4 }
        };
        passed &= check(bounded.overriden_filename == "name", "filename views must respect their length without null termination");
        std::string const binary_path{ "a\0b", 3 };
        std::string const binary_name{ "\0x", 2 };
        mcr::File const binary{ binary_path, binary_name };
        passed &= check(binary.filepath == binary_path && binary.overriden_filename == binary_name && binary.HasOverridenFilename(), "descriptors must retain embedded null bytes and determine override presence from string size");
        std::string const utf8{ "\xE4\xB8\xAD.txt" };
        mcr::File const unicode{ utf8, utf8 };
        passed &= check(unicode.filepath == utf8 && unicode.overriden_filename == utf8, "UTF-8 descriptor strings must be accepted verbatim");

        std::filesystem::path const native_path{ std::filesystem::path{ "uploads" } / "raw" / ".." / "report.txt" };
        mcr::File const from_path{ native_path.string() };
        passed &= check(from_path.filepath == native_path.string() && from_path.filepath != native_path.lexically_normal().string() && std::filesystem::path{ from_path.filepath }.filename() == "report.txt", "standard filesystem paths must interoperate explicitly while File retains their original spelling");
        return passed;
    }

    auto check_construction_and_iteration() -> bool {
        mcr::Files empty;
        mcr::Files const const_empty;
        mcr::Files const empty_files(std::initializer_list<mcr::File>{});
        mcr::Files const empty_paths(std::initializer_list<std::string>{});
        bool passed{ check(empty.begin() == empty.end() && const_empty.begin() == const_empty.end() && empty_files.cbegin() == empty_files.cend() && empty_paths.cbegin() == empty_paths.cend(), "default and both empty-list constructors must produce empty ranges") };
        mcr::File single_file{ "file1", "applefile" };
        mcr::Files single = single_file;
        single_file.filepath = "changed";
        passed &= check(std::ranges::distance(single) == 1 && same_file(*single.begin(), mcr::File{ "file1", "applefile" }), "single-file conversion must copy both descriptor fields");

        mcr::Files paths{ "file1", "file2", "", "file1" };
        std::array<mcr::File, 4> const expected{ mcr::File{ "file1" }, mcr::File{ "file2" }, mcr::File{ "" }, mcr::File{ "file1" } };
        passed &= check(std::ranges::equal(paths, expected, same_file), "path lists must preserve order, duplicates, and empty paths without overrides");
        mcr::Files files{
            mcr::File{ "file1",  "applefile" },
            mcr::File{ "file2", "bananafile" }
        };
        mcr::Files const& view{ files };
        passed &= check(view.begin() == view.cbegin() && view.end() == view.cend() && view.end() - view.begin() == 2 && view.begin()[1].overriden_filename == "bananafile", "const random-access iteration must expose complete descriptors");
        files.begin()->filepath = "updated";
        for (auto& entry : files) {
            entry.overriden_filename += ".txt";
        }
        passed &= check(view.begin()->filepath == "updated" && view.begin()[0].overriden_filename == "applefile.txt" && view.begin()[1].overriden_filename == "bananafile.txt", "mutable iteration must update both public fields in place");
        return passed;
    }

    auto check_mutation_and_assignment() -> bool {
        mcr::Files files;
        mcr::File source{ "source", "name" };
        files.emplace_back(source);
        files.push_back(source);
        source.filepath = "changed";
        source.overriden_filename = "changed";
        files.push_back(*files.begin());
        files.emplace_back(*files.begin());
        bool passed{ check(std::ranges::distance(files) == 4 && std::ranges::all_of(files, [](mcr::File const& file) { return file.filepath == "source" && file.overriden_filename == "name"; }), "both append methods must copy descriptors, including references to an existing element across vector growth") };
        files.pop_back();
        passed &= check(std::ranges::distance(files) == 3, "pop_back must remove exactly one descriptor");
        mcr::Files copied{ files };
        files.begin()->filepath = "original changed";
        mcr::Files moved{ std::move(copied) };
        mcr::Files assigned;
        passed &= check(&(assigned = moved) == &assigned && assigned.begin()->filepath == "source", "copy construction and assignment must retain independent descriptors and return the destination");
        moved.begin()->overriden_filename = "moved changed";
        mcr::Files move_assigned;
        passed &= check(&(move_assigned = std::move(moved)) == &move_assigned && move_assigned.begin()->overriden_filename == "moved changed" && assigned.begin()->overriden_filename == "name", "move assignment must transfer descriptors without modifying independent copies");
        auto* self{ &move_assigned };
        move_assigned = *self;
        move_assigned = std::move(*self);
        passed &= check(std::ranges::distance(move_assigned) == 3 && move_assigned.begin()->overriden_filename == "moved changed", "self-copy and self-move must preserve the collection, matching cpr");
        copied.push_back(mcr::File{ "reused" });
        passed &= check((copied.end() - 1)->filepath == "reused", "a moved-from collection must remain usable for append operations");
        while (files.begin() != files.end()) {
            files.pop_back();
        }
        files.emplace_back(mcr::File{ "after removal" });
        passed &= check(std::ranges::distance(files) == 1 && files.begin()->filepath == "after removal", "a drained collection must remain reusable");
        assigned = mcr::Files{};
        passed &= check(assigned.begin() == assigned.end(), "assigning an empty collection must replace all prior entries");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_file() };
    passed &= check_construction_and_iteration();
    passed &= check_mutation_and_assignment();
    if (!passed) {
        return 1;
    }
    std::println("test_file: ok");
    return 0;
}
