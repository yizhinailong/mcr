/**
 * @file secure_string.cppm
 * @brief A standard-compatible allocator that wipes released storage and its string alias.
 */
export module mcr.secure_string;

import std;

export namespace mcr::utils {

    /**
     * @brief Overwrite every allocated byte before returning storage to std::allocator.
     * @tparam T Object type satisfying std::allocator's requirements.
     * @note Stateless allocator instances compare equal, including across value types.
     * Objects must be destroyed before deallocation, as with std::allocator.
     */
    template <typename T>
    struct SecureAllocator {
        using value_type           = T;              ///< Element type used by allocator_traits.
        using is_always_equal      = std::true_type; ///< All instances of this allocator are interchangeable.

        /**
         * @brief Construct an allocator without state.
         */
        SecureAllocator() noexcept = default;

        /**
         * @brief Copy an allocator for another element type.
         * @tparam U Source element type.
         */
        template <typename U>
        SecureAllocator(SecureAllocator<U> const&) noexcept {}

        /**
         * @brief Move an allocator for another element type.
         * @tparam U Source element type.
         */
        template <typename U>
        SecureAllocator(SecureAllocator<U>&&) noexcept {}

        /**
         * @brief Assign another stateless allocator.
         * @tparam U Source element type.
         * @return This allocator.
         */
        template <typename U>
        auto operator=(SecureAllocator<U> const&) noexcept -> SecureAllocator& {
            return *this;
        }

        /**
         * @brief Move-assign another stateless allocator.
         * @tparam U Source element type.
         * @return This allocator.
         */
        template <typename U>
        auto operator=(SecureAllocator<U>&&) noexcept -> SecureAllocator& {
            return *this;
        }

        /**
         * @brief Allocate uninitialized storage using the standard allocator.
         * @param n Number of elements to allocate.
         * @return Storage with sufficient size and alignment for n elements.
         * @throws std::bad_array_new_length If n exceeds the supported element count.
         * @throws std::bad_alloc If allocation fails.
         */
        [[nodiscard]] auto allocate(std::size_t n) -> T* {
            return std::allocator<T>{}.allocate(n);
        }

        /**
         * @brief Wipe the full allocation with volatile byte stores, then release it.
         * @param p Storage obtained from an equal allocator, with no live elements remaining.
         * @param n Element count originally supplied to allocate.
         */
        auto deallocate(T* p, std::size_t n) noexcept -> void {
            auto* bytes = reinterpret_cast<unsigned char volatile*>(p);
            for (std::size_t index{ 0 }; index < n * sizeof(T); ++index) {
                bytes[index] = 0;
            }
            std::allocator<T>{}.deallocate(p, n);
        }

        /**
         * @brief Compare allocator identity, retaining cpr's named equality interface.
         * @tparam U Other allocator's element type.
         * @return True because all instances use the standard allocation facility.
         */
        template <typename U>
        [[nodiscard]] auto IsEqual(SecureAllocator<U> const&) const noexcept -> bool {
            return true;
        }
    };

    /**
     * @brief Compare two stateless secure allocators.
     * @tparam T Left element type.
     * @tparam U Right element type.
     * @param lhs Left allocator.
     * @param rhs Right allocator.
     * @return True for every pair of secure allocators.
     */
    template <typename T, typename U>
    [[nodiscard]] auto operator==(SecureAllocator<T> const& lhs, SecureAllocator<U> const& rhs) noexcept -> bool {
        return lhs.IsEqual(rhs);
    }

    /**
     * @brief Compare two secure allocators for inequality.
     * @tparam T Left element type.
     * @tparam U Right element type.
     * @param lhs Left allocator.
     * @param rhs Right allocator.
     * @return False for every pair of secure allocators.
     */
    template <typename T, typename U>
    [[nodiscard]] auto operator!=(SecureAllocator<T> const& lhs, SecureAllocator<U> const& rhs) noexcept -> bool {
        return !lhs.IsEqual(rhs);
    }

    /**
     * @brief A standard string that wipes heap allocations when they are released.
     * @note Small-string inline storage is not wiped by the allocator. clear, erase,
     * shrinking resize, and assignment need not release storage or wipe removed text.
     * Views and copies outside this allocator are not cleared.
     */
    using SecureString = std::basic_string<char, std::char_traits<char>, SecureAllocator<char>>;

} // namespace mcr::utils
