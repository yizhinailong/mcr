# Secure string

Import `mcr.secure_string` to use `mcr::utils::SecureAllocator<T>` and
`mcr::utils::SecureString`, following cpr's `include/cpr/secure_string.h`.

```cpp
import std;
import mcr.secure_string;

int main() {
    mcr::utils::SecureString credentials{ "username" };
    credentials += ':';
    credentials += std::string_view{ "password" };
    std::string_view view{ credentials }; // Borrows storage; does not extend its lifetime.
}
```

`SecureString` is an alias for
`std::basic_string<char, std::char_traits<char>, SecureAllocator<char>>`.
It retains standard string construction, copying, moving, assignment, views,
and mutation, including embedded null bytes. Copying produces another string
with the secure allocator. Explicitly copying into `std::string` produces
ordinary storage without this wiping behavior.

`SecureAllocator<T>` delegates allocation and alignment to `std::allocator<T>`.
It is stateless: instances compare equal through `IsEqual`, `==`, and `!=`,
including across element types. Cross-type construction and assignment and
`std::allocator_traits` rebinding are supported. The lowercase `allocate` and
`deallocate` names are required by the standard allocator interface.

Before releasing an allocation, `deallocate(p, n)` overwrites all
`n * sizeof(T)` bytes, including unused capacity. It writes through a volatile
unsigned-byte pointer so that the clearing stores are observable operations
under the [C++ volatile access rules](https://eel.is/c++draft/intro.abstract).
Elements must already have been destroyed, and the pointer and count must
match an allocation obtained from an equal allocator.

Wiping happens when storage is deallocated, including when string destruction
or reallocation releases a heap buffer. Small-string inline storage never
passes through the allocator and is not wiped. `clear()`, `erase()`, shrinking
`resize()`, and assignment can retain allocations and old text. This alias
does not lock memory, clear source buffers, or erase external copies.

Intentional differences from cpr are C++23 modules and namespace `mcr::utils`,
plus volatile byte stores in place of `std::fill_n(p, n, T{})`. Byte stores
avoid removable ordinary clearing writes, cover the complete allocation,
and do not assign to elements after their lifetimes have ended. Allocation
is delegated directly instead of privately inheriting from `std::allocator`;
the public allocator interface is preserved and `is_always_equal` is explicit.

Run `mcpp build`, `mcpp test`, and `mcpp test test_secure_string --profile release`.
The secure-string test checks allocator rebinding and alignment, string
ownership and operations, and the bytes passed to replacement delete functions
before they actually release storage. It never reads freed memory.
