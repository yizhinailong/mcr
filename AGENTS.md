# Repository Guidelines

## Project Purpose & Reference

`mcr` is an HTTP client library project implemented with the official upstream [cpr](https://github.com/libcpr/cpr) library as its reference. Consult the official repository's APIs, implementation, and tests when developing request methods, options, sessions, responses, and error handling. Adapt designs to C++23 modules and `mcpp`, and document intentional API or behavior differences in pull requests.

Target the dependency versions declared in `mcpp.toml`; do not maintain compatibility branches for older library versions. Keep platform and TLS backend handling where required by the supported environments.

## Project Structure & Module Organization

Source files are grouped by responsibility. Public module names are independent of file paths. Request configuration types use `mcr::options`, with TLS option tags in `mcr::options::ssl`. Reusable utilities use `mcr::utils`, and curl backend interfaces use `mcr::curl`.

- `src/`: library entry module `mcr.cppm` and one-shot request API `api.cppm`.
- `src/request/`: request bodies and borrowed views in `mcr.body`, query parameters and form payloads in `mcr.fields`, plus upload buffers, files, and multipart data. Public types remain in `mcr`.
- `src/http/`: responses, cookies, certificate metadata, status codes, and Server-Sent Events. Public types remain in `mcr`, with status constants in `mcr::status`; existing module names such as `mcr.response` and `mcr.sse` are preserved.
- `src/session/`: sessions, asynchronous runtime, callbacks, and connection pools. `session.cppm` contains the session, TLS, interceptor, and batch implementations; import `mcr.session` for `Session`, `Interceptor`, `InterceptorMulti`, and `MultiPerform`.
- `src/options/`: request configuration in namespace `mcr::options`, grouped into six modules. `mcr.auth` provides authentication and bearer tokens; `mcr.proxy` provides proxy addresses and credentials; `mcr.http` provides protocol versions, encodings, redirects, and ranges; `mcr.transfer_options` provides timing, rates, connection selection, capacity, and diagnostics. Network interface selection and TLS configuration remain in `mcr.interface` and `mcr.ssl_options`. Keep related small option types together.
- `src/utils/`: reusable utilities in namespace `mcr::utils` for secure strings, singleton lifecycle, thread pools, and future wrappers, alongside HTTP parsing and curl callback helpers. Shared HTTP types (`types.cppm`) and errors/results (`error.cppm`) also live here; their public interfaces remain in `mcr`. Module names remain independent of paths, including `mcr.types`, `mcr.error`, `mcr.threadpool`, and `mcr.util`.
- `src/curl/`: curl easy/multi handle ownership, request container encoding, and SSL context support in namespace `mcr::curl`, including module interfaces and their implementations. The request record types `Parameter` and `Pair` remain in namespace `mcr`, alongside `mcr::curl::CurlContainer<T>` in the same module. These modules retain their existing module names, such as `mcr.curlholder`, `mcr.curl_container`, and `mcr.ssl_ctx`.
- `tests/test_*.cpp`: standalone tests, with shared local HTTP fixtures under `tests/fixtures/`.
- `mcpp.toml`: package metadata and dependency declarations for `mcr`.
- `.clang-format`: repository formatting configuration.
- `target/`, `.mcpp/`, and `compile_commands.json`: generated output or local state ignored by Git; do not commit them.

See [the source layout guide](docs/structure.md) for directory contents and module naming.

Keep `mcr.cppm` as the complete public entry point: re-export every public `mcr.*` module, including request APIs, data types, options, asynchronous results, status codes, version information, curl backend interfaces, and general utilities. External code and tests may use `import mcr;` alone to access these interfaces, or import individual modules as needed. Component modules should use ordinary imports for curl backend and general utility dependencies. `mcr.fields` selectively exports the public `Parameter` and `Pair` records without re-exporting the curl container module.

## Build, Test, and Development Commands

Run from the repository root with `mcpp` and a C++23 toolchain supporting `import std;`.

- `mcpp self doctor`: diagnose the local build environment.
- `mcpp build`: compile the application.
- `mcpp run`: build and run the default executable.
- `mcpp run -- example`: pass a command-line argument.
- `mcpp test`: discover, build, and execute tests under `tests/`.
- `mcpp build --configure-only`: generate the editor compilation database.
- `mcpp clean`: remove generated build output under `target/`.

## Coding Style & Naming Conventions

Follow `.clang-format`: four-space indentation, spaces instead of tabs, and no enforced column limit. Format changed C++ files, for example:

```sh
clang-format -i src/api.cppm tests/test_api.cpp
```

Preserve C++23 module style, including `import std;`. Keep library declarations and implementations directly in `.cppm` files under `src/`; do not create separate `.cpp` implementation units. Use lowercase filenames with underscores, such as `argument_parser.cppm`. Tests remain standalone `.cpp` programs. No separate lint configuration is checked in.

Use `std::filesystem` directly through `import std;`, without a filesystem namespace alias or wrapper module.

Use Doxygen documentation comments consistently in library code, internal helpers, and tests:

- Write documentation blocks in multiline form, even when they contain only a short `@brief`. Put `/**` and `*/` on their own lines, and prefix each content line with ` * `. Do not use single-line `/** ... */` documentation blocks.
- Place each block immediately before the declaration it documents (before `template` for templates). Use `@brief`, and add `@tparam`, `@param`, `@return`, `@throws`, and `@note` where applicable. Put each tag on its own line; omit tags that do not apply.
- Use a file-level block with `@file` and `@brief` at the beginning of C++ source files.
- Use trailing `///<` comments for member and enumerator descriptions. Ordinary `//` comments remain appropriate for implementation explanations and namespace closing labels.
- Preserve this layout when running `clang-format`; follow the existing `.clang-format` without modifying it.

```cpp
/**
 * @brief Check whether this task still owns an unconsumed result.
 * @return True if a result remains available.
 */
[[nodiscard]] bool Valid() const noexcept;

/**
 * @brief Wake the runtime when cancellation is requested.
 */
void Wake() noexcept;

bool m_stopping{ false }; ///< Whether shutdown has begun.
```

## Testing Guidelines

Tests are standalone programs with their own `main()`; no external framework is configured. Follow `tests/test_*.cpp`, with one executable per file. Return zero on success and nonzero on failure. Cover changed behavior and edge cases, then run `mcpp test`. For HTTP tests, use a local fixture server with controlled responses. The smoke test checks compilation and execution; no coverage threshold is configured.

## Commit & Pull Request Guidelines

Recent commits use prefixes such as `feat:` and `style:` with short, imperative descriptions. Follow that pattern, for example `feat: add argument parsing`.

Keep changes focused. In each pull request, describe the behavior change, link related issues when applicable, and report validation commands and results. Include before-and-after terminal output when changing CLI behavior.
