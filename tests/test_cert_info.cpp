/**
 * @file test_cert_info.cpp
 * @brief Verify certificate entry ownership, ordering, iteration, mutation, and chain storage.
 */
import std;
import mcr;

static_assert(std::is_convertible_v<std::initializer_list<std::string>, mcr::CertInfo>);
static_assert(std::is_copy_constructible_v<mcr::CertInfo>);
static_assert(std::is_nothrow_move_constructible_v<mcr::CertInfo>);
static_assert(!std::is_copy_assignable_v<mcr::CertInfo>);
static_assert(!std::is_move_assignable_v<mcr::CertInfo>);
static_assert(std::is_same_v<mcr::CertInfo::iterator, std::vector<std::string>::iterator>);
static_assert(std::is_same_v<mcr::CertInfo::const_iterator, std::vector<std::string>::const_iterator>);
static_assert(std::is_same_v<decltype(std::declval<mcr::CertInfo&>()[0]), std::string&>);
static_assert(std::ranges::random_access_range<mcr::CertInfo>);
static_assert(std::ranges::random_access_range<mcr::CertInfo const>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_cert_info: {}", message);
        }
        return condition;
    }

    auto check_construction_and_iteration() -> bool {
        mcr::CertInfo empty;
        mcr::CertInfo const const_empty;
        mcr::CertInfo const empty_list(std::initializer_list<std::string>{});
        bool passed{ check(empty.begin() == empty.end() && empty.cbegin() == empty.cend() && const_empty.begin() == const_empty.end() && const_empty.cbegin() == const_empty.cend() && empty_list.begin() == empty_list.end(), "default and empty-list collections must expose empty mutable and const ranges") };

        std::string subject{ "Subject:CN = test-server" };
        std::string const binary{ "Field:a\0b", 9 };
        std::string const multiline{ "Cert:line one\nline two\r\n" };
        std::vector<std::string> const expected{ subject, "Issuer:C = GB, O = Example, CN = Sub CA", "Version:2", "", subject, binary, multiline };
        mcr::CertInfo entries = { subject, "Issuer:C = GB, O = Example, CN = Sub CA", "Version:2", "", subject, binary, multiline };
        subject.assign("changed");
        passed &= check(std::ranges::equal(entries, expected), "construction must copy cpr-style entries in order, preserving duplicates, empty entries, binary bytes, and line endings");
        mcr::CertInfo const& view{ entries };
        passed &= check(view.begin() == view.cbegin() && view.end() == view.cend() && std::ranges::equal(view, expected) && entries.end() - entries.begin() == 7 && *(view.begin() + 5) == binary, "const and random-access iteration must expose all stored entries");

        entries[0] = "Subject:CN = updated";
        entries.begin()[1] = "Issuer:updated";
        for (auto& entry : entries) {
            entry += "!";
        }
        passed &= check(entries[0] == "Subject:CN = updated!" && entries[1] == "Issuer:updated!" && entries[3] == "!" && entries[4] == expected[4] + "!" && entries[5] == binary + "!", "subscript and mutable iteration must update entries independently without truncating bytes");
        return passed;
    }

    auto check_append_and_remove() -> bool {
        mcr::CertInfo entries;
        std::string source{ "Subject:copied" };
        entries.emplace_back(source);
        entries.push_back(source);
        source.assign("changed");
        std::string const binary{ "a\0b", 3 };
        entries.emplace_back(binary);
        entries.push_back("");
        entries.push_back(entries[0]);
        entries.emplace_back(entries[2]);
        bool passed{ check(std::ranges::equal(entries, std::vector<std::string>{ "Subject:copied", "Subject:copied", binary, "", "Subject:copied", binary }), "both append methods must copy full strings and support appending an existing entry across vector growth") };
        entries.pop_back();
        passed &= check(std::ranges::distance(entries) == 5 && *(entries.end() - 1) == "Subject:copied", "pop_back must remove only the last entry");
        while (entries.begin() != entries.end()) {
            entries.pop_back();
        }
        passed &= check(entries.cbegin() == entries.cend(), "removing all entries must leave an empty range");
        entries.emplace_back("reused");
        passed &= check(std::ranges::distance(entries) == 1 && entries[0] == "reused", "a drained collection must remain reusable");
        return passed;
    }

    auto check_copy_move_and_chain() -> bool {
        std::string const large_entry(256, 'x');
        mcr::CertInfo original{ "Subject:leaf", large_entry };
        mcr::CertInfo copied{ original };
        original[0] = "Subject:changed";
        original.pop_back();
        mcr::CertInfo moved{ std::move(copied) };
        bool passed{ check(original[0] == "Subject:changed" && std::ranges::distance(original) == 1 && moved[0] == "Subject:leaf" && moved[1] == large_entry, "copy and move construction must retain independent owned strings") };

        std::vector<mcr::CertInfo> chain;
        chain.emplace_back(moved);
        moved[0] = "Subject:root";
        chain.emplace_back(std::move(moved));
        chain.emplace_back(std::initializer_list<std::string>{ "Subject:extra" });
        passed &= check(chain.size() == 3 && chain[0][0] == "Subject:leaf" && chain[1][0] == "Subject:root" && chain[2][0] == "Subject:extra" && chain[0][1] == large_entry, "certificate chains must support copy insertion, move insertion, and outer-vector growth");
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_construction_and_iteration() };
    passed &= check_append_and_remove();
    passed &= check_copy_move_and_chain();
    if (!passed) {
        return 1;
    }
    std::println("test_cert_info: ok");
    return 0;
}
