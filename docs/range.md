# Range and MultiRange

Import `mcr` or `mcr.http` to use both range types.

```cpp
import std;
import mcr.http;

mcr::options::Range range{ 2, 3 };
std::println("{}", range.Str()); // 2-3
mcr::options::MultiRange ranges{ mcr::options::Range{ std::nullopt, 3 }, mcr::options::Range{ 5, 6 } };
std::println("{}", ranges.Str()); // 0-3, 5-6
```

`Range` follows cpr's `include/cpr/range.h`. Its explicit constructor takes two
optional `std::int64_t` endpoints. An absent start becomes `0`; an absent finish
becomes `-1`. The public `resume_from` and `finish_at` fields retain their signed
values and can be updated after construction. `Str()` reads their current
values, omits the digits of every negative endpoint, and inserts one hyphen.

| Range | Text |
| --- | --- |
| `Range{}` | `0-` |
| `Range{1, std::nullopt}` | `1-` |
| `Range{std::nullopt, 5}` | `0-5` |
| `Range{2, 3}` | `2-3` |
| `Range{-1, 500}` | `-500` |
| `Range{-1, -1}` | `-` |

No `bytes=` prefix is added. Endpoints are not reordered, clamped, or validated:
for example, `Range{10, 2}` produces `10-2`. All nonnegative 64-bit endpoints
format without narrowing, and negative values remain unchanged in storage.

`MultiRange` copies an initializer list of `Range` values into a private vector.
`Str()` joins each range's text with `", "`, preserving order, overlapping ranges,
and duplicates, with no trailing separator. An empty list (`MultiRange{}`)
produces an empty string. Changes to the original ranges do not affect the
stored copies. Both types support independent copy/move construction and
assignment, and every `Str()` call returns an owned string.

Intentional API differences from cpr are the C++23 module, namespace `mcr::options`,
`Str()` replacing `str()`, returning `std::string` without top-level const, and
the private `m_ranges` member name. Multi-range formatting iterates by const
reference. Formatting behavior is unchanged.

cpr's `Session::SetRange` and `SetMultiRange` pass the formatted string to
`CURLOPT_RANGE`; its `test/download_tests.cpp` covers whole, partial, and multipart
downloads using a local HTTP fixture. Session integration is not yet implemented
in this project. Run `mcpp build` and `mcpp test` to verify defaults, optional and
negative endpoints, signed 64-bit boundaries, multi-range formatting, and value
ownership without a network service.
