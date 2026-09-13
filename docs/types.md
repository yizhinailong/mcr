# Common request types

Import `mcr` or `mcr.types` to use the types in namespace `mcr`.
The module is implemented in `src/utils/types.cppm`; its import name and public namespace are unchanged.

```cpp
import std;
import mcr;

mcr::Url url{ "https://example.test", "/api" };
url += mcr::Url{ "/items" };
mcr::Header headers{ { "Content-Type", "application/json" } };
std::println("{}: {}", url.Str(), headers.at("content-type"));
```

- `CprOffT` aliases the configured curl's `curl_off_t`. `CprPfArgT`
  aliases `CprOffT` for progress callbacks in the current curl dependency.
  Consumers do not need to include curl headers to use either alias.
- `StringHolder<T>` owns its text and returns `T` from concatenation.
  `Url` and `UserAgent` support strings, string views, C strings, byte ranges, and lists of
  fragments. Views and ranges are copied; explicit lengths preserve embedded
  null bytes. Both types store their text without validation or encoding.
- `UserAgent` is exported by `mcr.types` alongside `Url`. Code previously
  importing `mcr.user_agent` should use `mcr.types` or the entry module `mcr`.
- `Str()`, `CStr()`, `Data()`, explicit conversion to `std::string`, stream
  output, copy/move operations, comparisons, and void-returning `+=` preserve
  cpr's behavior. The protected `m_str` member remains available to derived
  string option types.
- `Header` is a map using `CaseInsensitiveCompare`. Names compare without case;
  values and the spelling of the first inserted key remain unchanged. As in
  cpr, comparison uses `std::tolower` with unsigned bytes in the current C
  locale. Keep that locale stable while the map contains keys.

Differences from the referenced cpr implementation:

- Public types live in `mcr` and are exported through C++23 modules.
- Type aliases and public member functions use PascalCase; non-public data
  members use the `m_` prefix. Consumers and derived option types must update
  these names when migrating from cpr or the previous library interface:

  | Previous / cpr name | Current name |
  | --- | --- |
  | `cpr_off_t` | `CprOffT` |
  | `cpr_pf_arg_t` | `CprPfArgT` |
  | `str()` | `Str()` |
  | `c_str()` | `CStr()` |
  | `data()` | `Data()` |
  | `str_` | `m_str` |

- Only the current curl dependency is supported; legacy progress callback
  argument types and curl version compatibility branches are omitted.
- `StringHolder<T>::operator+=(StringHolder<T> const&)` appends the other
  holder's stored string; cpr's attempt to append the holder itself does not
  compile when instantiated.
- `operator!=(char const*)` compares string contents, correcting cpr's pointer
  comparison so it agrees with `operator==` for independently stored strings.

Run `mcpp build` and `mcpp test` to validate the module and its regression tests.
