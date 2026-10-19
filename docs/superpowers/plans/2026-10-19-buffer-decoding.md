# Buffer Decoding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Deliver a buffer-only xlog decoder through the existing C ABI.

**Architecture:** A private module implements framing, ECDH/TEA and raw DEFLATE. The ordinary C++ adapter validates ABI arguments, contains exceptions and owns result allocation. Public headers describe borrowed block views, owned output and configurable defaults.

**Tech Stack:** GCC 16/C++26 Modules, OpenSSL Crypto 3.0+, zlib, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-19-buffer-decoding-design.md`

## Global Constraints

- No file I/O, ZIP, MCP or CLI product interfaces.
- Never copy the reference project's default private key; initial default is empty.
- Preserve C/C++11 consumer compatibility, private Modules, macOS ARM64 and Windows UCRT64.
- Strict failure without partial output; all errors include the failing block offset.
- Modern framing recognizes 0x06..0x0d; decode supports 0x06/0x07/0x08/0x09 only.
- Keep independent, relocatable library delivery and configuration-header synchronization.

## Review Focus

- Valid first block followed by garbage or unsupported codec: recognition checks every frame; decode distinguishes unsupported codec.
- Zero-length and NULL inputs: no out-of-bounds access; empty recognition is false and empty decode fails.
- DEFLATE flush markers and trailing data: accept complete sync-flush segments; reject false marker suffixes and unfinished streams.
- Key/public-point validation: no empty, zero, out-of-range scalar or invalid EC point can silently decrypt.
- Aggregate output budget: multiple valid blocks may still exceed the whole-result limit.

### Task 1: Public contract and failing buffer tests

**Files:** Public decode header/config header, `tests/decode_buffer.cpp`, `tests/decode_c.c`, `tests/CMakeLists.txt`.

**Interfaces:** Functions are `xlog_decode_is_xlog_buffer`, `xlog_decode_parse_block`, `xlog_decode_block`, `xlog_decode_bytes`, `xlog_decode_free_buffer`. C structs are `xlog_decode_block_header`, `xlog_decode_block_view`, `xlog_decode_buffer_result`, `xlog_decode_options`, `xlog_decode_error`.

- [x] Declare the exact documented public C contract and defaults.
- [x] Add independent known-byte fixtures and behavior assertions for the review focus above, all framing truncation boundaries, binary output and view lifetime rules.
- [x] Build the tests before implementation; expect undefined references to the new API.

### Task 2: Framing and decoding core

**Files:** `xlog_decode/src/xlog_decode.cppm`, `xlog_decode/src/xlog_decode_api.cpp`, `xlog_decode/src/buffer_codec.h/.cpp`.

**Interfaces:** Private module parses views and decodes to byte vectors; public adapter exports the declared C functions.

- [x] Implement complete structural recognition and transactional block parsing, then run the framing tests.
- [x] Implement raw DEFLATE, validated secp256k1 ECDH and 16-round little-endian TEA, then run the independent known-answer tests.
- [x] Enforce per-call output limits, initialize all outputs, contain allocation/internal exceptions and provide same-library result release.
- [x] Run all new public API tests and verify expected failure offsets and empty outputs.

### Task 3: Build, delivery and documentation

**Files:** Dependency CMake, decode CMake, header delivery helper, package tests, Windows CI and README files.

- [x] Link compatible OpenSSL Crypto/zlib privately; preserve encode-only configuration with `XLOG_BUILD_DECODE=OFF`.
- [x] Deliver the new configuration header in both build and install trees; verify deletion recovery and header update synchronization.
- [x] Exercise real buffer decoding from relocated C and C++11 consumers.
- [x] Document API use, default overrides, ownership, errors and dependencies.
- [x] Run Debug/Release builds and the complete CTest suite, inspect exports/dependencies and obtain an independent code review; fix material findings before delivery.

## Execution record

- User authorized implementation after confirming the design recommendations; continue without another approval round.
- Existing workspace was clean; work proceeds on `codex/buffer-decode` without creating another checkout.
- Baseline: all 8 existing Debug CTests passed before changes.
- Implemented and reviewed the full buffer API; no product file/ZIP/CLI/MCP interfaces were added.
- Strict compressed-input handling rejects an empty compressed payload; a correctly encoded empty DEFLATE stream remains valid.
- Review finding fixed: require OpenSSL 3.0+ to match the delivered Apache 2.0 license; no legacy OpenSSL requirement came from the user.
- Final verification: Debug 10/10 and Release 10/10 CTests passed after the review fix, including relocated C/C++11 decoding and build/install parity.
- Windows CI updated; actual Windows execution remains unverified in this local task.
