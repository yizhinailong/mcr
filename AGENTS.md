# Repository Guidelines

## Project Purpose & Reference

`request-mcpp` is an HTTP client library project implemented with [cpr](https://github.com/yizhinailong/cpr) as its reference. Consult cpr's APIs, implementation, and tests when developing request methods, options, sessions, responses, and error handling. Adapt designs to C++23 modules and `mcpp`, and document intentional API or behavior differences in pull requests.

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

Keep `mcr.cppm` limited to public request APIs, data types, options, asynchronous results, status codes, and version information. Public modules must use ordinary imports for curl backend and general utility dependencies; explicitly import those modules when naming their interfaces in backend code or tests. `mcr.fields` selectively exports the public `Parameter` and `Pair` records without re-exporting the curl container module.

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

Use Doxygen documentation comments: `/** ... */` with `@brief`, `@param`, `@tparam`, `@return`, and `@throws` where applicable, and `///<` for member descriptions.

## Testing Guidelines

Tests are standalone programs with their own `main()`; no external framework is configured. Follow `tests/test_*.cpp`, with one executable per file. Return zero on success and nonzero on failure. Cover changed behavior and edge cases, then run `mcpp test`. For HTTP tests, use a local fixture server with controlled responses. The smoke test checks compilation and execution; no coverage threshold is configured.

## Commit & Pull Request Guidelines

Recent commits use prefixes such as `feat:` and `style:` with short, imperative descriptions. Follow that pattern, for example `feat: add argument parsing`.

Keep changes focused. In each pull request, describe the behavior change, link related issues when applicable, and report validation commands and results. Include before-and-after terminal output when changing CLI behavior.
