/**
 * @file test_secure_string.cpp
 * @brief Verify allocator compatibility, string operations, and memory wiping before release.
 */
import std;
import mcr;

namespace {

    void const* g_watched_storage{ nullptr };
    std::size_t g_watched_bytes{ 0 };
    bool        g_release_observed{ false };
    bool        g_release_zeroed{ false };

    /**
     * @brief Inspect only the watched allocation while it still exists, before free.
     */
    void observe_release(void* storage) noexcept {
        if (storage == g_watched_storage && g_watched_storage != nullptr) {
            auto const* bytes = static_cast<unsigned char const*>(storage);
            g_release_zeroed  = true;
            for (std::size_t index{ 0 }; index < g_watched_bytes; ++index) {
                g_release_zeroed &= bytes[index] == 0;
            }
            g_release_observed = true;
            g_watched_storage  = nullptr;
        }
    }

    void watch_release(void const* storage, std::size_t bytes) noexcept {
        g_watched_storage  = storage;
        g_watched_bytes    = bytes;
        g_release_observed = false;
        g_release_zeroed   = false;
    }

    void require(bool condition, std::string_view message) {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    void require_wiped() {
        require(g_release_observed, "the watched allocation must reach the release hook");
        require(g_release_zeroed, "every allocated byte must be zero before storage is released");
    }

} // namespace

/**
 * @brief Pair replacement allocation and release functions to inspect memory without use-after-free.
 */
void* operator new(std::size_t bytes) {
    while (true) {
        if (auto* storage = std::malloc(bytes == 0 ? 1 : bytes)) {
            return storage;
        }
        auto handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc{};
        }
        handler();
    }
}

void* operator new[](std::size_t bytes) {
    return ::operator new(bytes);
}

void operator delete(void* storage) noexcept {
    observe_release(storage);
    std::free(storage);
}

void operator delete(void* storage, std::size_t) noexcept {
    ::operator delete(storage);
}

void operator delete[](void* storage) noexcept {
    ::operator delete(storage);
}

void operator delete[](void* storage, std::size_t) noexcept {
    ::operator delete(storage);
}

namespace {

    using mcr::utils::SecureAllocator;
    using mcr::utils::SecureString;
    using CharTraits = std::allocator_traits<SecureAllocator<char>>;

    static_assert(std::is_same_v<CharTraits::value_type, char>);
    static_assert(std::is_same_v<CharTraits::rebind_alloc<int>, SecureAllocator<int>>);
    static_assert(CharTraits::is_always_equal::value);
    static_assert(std::is_nothrow_copy_constructible_v<SecureAllocator<char>>);
    static_assert(std::is_nothrow_move_constructible_v<SecureAllocator<char>>);
    static_assert(std::is_nothrow_copy_assignable_v<SecureAllocator<char>>);
    static_assert(std::is_nothrow_move_assignable_v<SecureAllocator<char>>);
    static_assert(std::is_nothrow_constructible_v<SecureAllocator<int>, SecureAllocator<char> const&>);
    static_assert(std::is_nothrow_constructible_v<SecureAllocator<int>, SecureAllocator<char>&&>);
    static_assert(std::is_nothrow_assignable_v<SecureAllocator<int>&, SecureAllocator<char> const&>);
    static_assert(std::is_nothrow_assignable_v<SecureAllocator<int>&, SecureAllocator<char>&&>);
    static_assert(noexcept(std::declval<SecureAllocator<char>&>().deallocate(nullptr, 0)));
    static_assert(std::is_same_v<SecureString::allocator_type, SecureAllocator<char>>);
    static_assert(std::is_nothrow_move_constructible_v<SecureString>);
    static_assert(std::is_nothrow_move_assignable_v<SecureString>);

    void check_allocator_compatibility() {
        SecureAllocator<char>       chars;
        SecureAllocator<char> const equal;
        SecureAllocator<int>        copied{ equal };
        SecureAllocator<int>        moved{ std::move(chars) };
        require(copied.IsEqual(equal) && equal == moved && !(copied != equal), "allocators must compare equal across element types");
        require(&(copied = equal) == &copied && &(moved = std::move(chars)) == &moved, "cross-type assignments must return the destination");

        auto* empty = chars.allocate(0);
        chars.deallocate(empty, 0);

        std::vector<int, SecureAllocator<int>> values{ 1, 2, 3 };
        values.push_back(4);
        require(values.size() == 4 && values.back() == 4, "allocator_traits must support standard containers");
        std::list<int, SecureAllocator<int>> nodes{ 5, 6 };
        require(nodes.front() == 5 && nodes.back() == 6, "node containers must rebind the allocator");

        struct alignas(64) Aligned {
            int value;
        };

        SecureAllocator<Aligned> aligned;
        auto*                    storage = aligned.allocate(2);
        require(reinterpret_cast<std::uintptr_t>(storage) % alignof(Aligned) == 0, "allocation must preserve extended alignment");
        aligned.deallocate(storage, 2);
    }

    template <typename T>
    void check_raw_wipe() {
        SecureAllocator<T>    allocator;
        SecureAllocator<T>    equal;
        constexpr std::size_t COUNT{ 7 };
        auto*                 storage = allocator.allocate(COUNT);
        std::memset(storage, 0xA5, COUNT * sizeof(T));
        watch_release(storage, COUNT * sizeof(T));
        equal.deallocate(storage, COUNT);
        require_wiped();
    }

    void check_destroyed_objects() {
        struct Value {
            int value;

            explicit Value(int input) : value{ input } {}

            Value& operator=(Value const&) = delete;

            ~Value() { value = -1; }
        };

        using Traits = std::allocator_traits<SecureAllocator<Value>>;
        SecureAllocator<Value> allocator;
        auto*                  storage = Traits::allocate(allocator, 2);
        Traits::construct(allocator, storage, 41);
        Traits::construct(allocator, storage + 1, 42);
        require(storage[0].value == 41 && storage[1].value == 42, "allocator_traits must construct non-default-constructible elements");
        Traits::destroy(allocator, storage);
        Traits::destroy(allocator, storage + 1);
        watch_release(storage, 2 * sizeof(Value));
        Traits::deallocate(allocator, storage, 2);
        require_wiped();
    }

    void check_string_operations() {
        SecureString empty;
        require(empty.empty() && empty.c_str()[0] == '\0', "default strings must be empty and null-terminated");
        std::string  source(80, 's');
        SecureString owned{ std::string_view{ source } };
        source.front() = 'x';
        require(owned.front() == 's', "construction must own an independent copy of input text");

        char const   bytes[]{ 'a', '\0', 'b', 'c' };
        SecureString binary{ bytes, sizeof(bytes) };
        require(std::string_view{ binary } == std::string_view{ bytes, sizeof(bytes) }, "construction and views must preserve embedded null bytes");
        require(binary.c_str()[binary.size()] == '\0', "binary strings must also have a trailing terminator");
        SecureString credentials{ "user" };
        credentials += ':';
        credentials += std::string_view{ "password" };
        require(credentials == "user:password", "string appends must support cpr's credential construction pattern");

        SecureString copied{ owned };
        copied.front() = 'c';
        require(owned.front() == 's' && copied.front() == 'c', "string copies must have independent storage");
        SecureString moved{ std::move(copied) };
        SecureString assigned;
        assigned = moved;
        SecureString move_assigned;
        move_assigned = std::move(moved);
        require(assigned == move_assigned && assigned.size() == 80 && assigned.front() == 'c', "copy and move assignment must preserve string contents");
        moved.assign("reused");
        require(moved == "reused", "moved-from strings must remain assignable");
        std::string const ordinary{ assigned };
        require(ordinary.size() == assigned.size() && ordinary.front() == 'c', "explicit conversion to an ordinary string must remain available");
        assigned.swap(binary);
        require(assigned.size() == sizeof(bytes) && binary.size() == 80, "string swap must support equal allocators");
    }

    void check_string_wipe() {
        {
            SecureString secret(96, 's');
            auto const   allocation_size = secret.capacity() + 1;
            secret.resize(secret.capacity(), 'x');
            watch_release(secret.data(), allocation_size);
            secret.clear();
        }
        require_wiped();

        SecureString growing(96, 'g');
        auto const   old_capacity = growing.capacity();
        growing.resize(old_capacity, 'x');
        watch_release(growing.data(), old_capacity + 1);
        growing.reserve(old_capacity * 2 + 1);
        require_wiped();
        require(growing.size() == old_capacity && growing.front() == 'g', "reallocation must preserve the string while wiping its previous buffer");

        SecureString destination(96, 'd');
        destination.resize(destination.capacity(), 'x');
        {
            SecureString replacement(128, 'r');
            watch_release(destination.data(), destination.capacity() + 1);
            destination = std::move(replacement);
        }
        require_wiped();
        require(destination.size() == 128 && destination.front() == 'r', "move assignment must wipe the discarded destination allocation");
    }

} // namespace

int main() {
    try {
        check_allocator_compatibility();
        check_raw_wipe<char>();
        check_raw_wipe<std::uint32_t>();
        check_destroyed_objects();
        check_string_operations();
        check_string_wipe();
    } catch (std::exception const& error) {
        std::println("test_secure_string: {}", error.what());
        return 1;
    }
    std::println("test_secure_string: ok");
    return 0;
}
