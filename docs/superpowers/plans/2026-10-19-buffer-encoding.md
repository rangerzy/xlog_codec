# Buffer encoding implementation plan

User approved single-call buffer encoding, formats 0x08/0x09/0x07, independent internal/public types and separate type/method files. Continue implementation without another approval round.

**Spec:** `docs/superpowers/specs/2026-10-19-buffer-encoding-design.md`

- [x] Run the existing full Debug baseline before changing product/build sources.
- [x] Define public config/types/API headers and independent private types, with explicit C ABI conversion.
- [x] Write public C/C++11 tests and observe missing-API failure before implementing the codec.
- [x] Implement explicit little-endian frame serialization, raw DEFLATE with complete sync flush, secure ephemeral ECDH and 16-round TEA.
- [x] Integrate PRIVATE dependencies, encode-only/decode-only builds, headers/licenses, relocated consumers and Windows CI checks.
- [x] Verify independent byte fixtures and encoder/decoder interoperation, null/empty input, key validation, limits, failure atomicity and release.
- [x] Run full Debug/Release CTest, standalone header compilation, export/dependency checks and independent review; fix material findings.

Final local verification: macOS ARM64 Debug and Release builds passed without warnings,
and full CTest passed 13/13 in each configuration. Six standalone C99/C++11 public-header
checks and the private codec check without public include paths passed. Independent
review found no important issue, and independently verified encryption using a receiver
scalar other than 1. Windows workflow checks were updated; Windows execution remains
unverified for this change. Detailed evidence is in `docs/verification.md`.

No new logging SDK, stateful append context, zstd format, file I/O, CLI or MCP is included. Do not change decoder ABI or compression behavior. No commit/push was requested.
