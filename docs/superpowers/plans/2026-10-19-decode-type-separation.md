# Decode type separation implementation plan

User approved independent public/internal types and separate type/method files. Existing API names, enum values, structure layouts, default configuration, ownership and decoding behavior remain unchanged.

## Tasks

- [x] Preserve pre-refactor C and C++11 consumer binaries and exported-symbol list; run the existing Debug baseline.
- [x] Compile a consumer that includes the new public types header before the method header; observe the missing-header failure.
- [x] Move public enum/struct definitions and options initializer unchanged to `xlog_decode_types.h`. Keep `xlog_decode.h` as the compatible function entry header.
- [x] Add independent private `decode_types.h`: C++ status, failure, block metadata/view, options and byte/key aliases. No public-header dependencies or type aliases to public C structs.
- [x] Make core/codec use only private types, and convert fields/status explicitly at the C ABI adapter. Keep domain types outside function declaration/implementation files; keep cleanup semantics unchanged.
- [x] Deliver the new public types header, test incremental synchronization/deletion recovery, and reject private header leakage in the package.
- [x] Run public API tests, unchanged pre-refactor binaries, standalone public/private header compilation, exported-symbol comparison, complete Debug and Release CTest suites, and independent review.

## Scope

No changes to encoder behavior, compression/encryption algorithms, supported formats, default keys, file/MCP/CLI scope, toolchain or dependencies. No commits or remote publication requested.

## Verification record

Public enum/struct/initializer/prototype declarations were compared byte-for-byte against the saved pre-refactor header. The six public exports are unchanged. Existing C and C++11 binaries built with the old header passed against the new library. Invalid C block views were also characterized against the pre-refactor Release library before running against the new implementation.

Standalone public/private compilation and independent SDK delivery checks passed. Final macOS ARM64 Debug and Release builds had no warnings, with all 10 CTests passing in each configuration. Independent review found no important issues. Windows CI was not executed locally.
