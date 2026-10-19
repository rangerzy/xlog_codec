#ifndef XLOG_DECODE_H
#define XLOG_DECODE_H

#include <xlog_decode/xlog_decode_export.h>
#include <xlog_decode/xlog_decode_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed NUL-terminated version string. Do not modify or free. */
XLOG_DECODE_EXPORT const char *xlog_decode_version(void);

/* Validate every modern frame (0x06..0x0d), without decoding its payload.
 * Empty/invalid input returns 0. Structural validity does not imply codec support. */
XLOG_DECODE_EXPORT int xlog_decode_is_xlog_buffer(const void *data, size_t size);

/* Parse one frame at offset. On failure block is cleared. Optional error is reset
 * on every call. Input must be a readable buffer of at least size bytes. */
XLOG_DECODE_EXPORT xlog_decode_status xlog_decode_parse_block(
    const void *data, size_t size, size_t offset,
    xlog_decode_block_view *block, xlog_decode_error *error);

/* Decode a parsed block, or every frame in a nonempty buffer. Supports
 * 0x06/0x08 (plain), 0x09 (raw DEFLATE), 0x07 (ECDH/TEA + raw DEFLATE).
 * NULL options uses the library's compiled defaults. Results are binary, without
 * an added NUL. Free an old result before reusing its output argument. Failure
 * clears output; no partial bytes escape. Exceptions never cross the C ABI. */
XLOG_DECODE_EXPORT xlog_decode_status xlog_decode_block(
    const xlog_decode_block_view *block, const xlog_decode_options *options,
    xlog_decode_buffer_result *output, xlog_decode_error *error);
XLOG_DECODE_EXPORT xlog_decode_status xlog_decode_bytes(
    const void *data, size_t size, const xlog_decode_options *options,
    xlog_decode_buffer_result *output, xlog_decode_error *error);

/* Accepts NULL; resets the result so repeated calls are harmless. */
XLOG_DECODE_EXPORT void xlog_decode_free_buffer(xlog_decode_buffer_result *output);

#ifdef __cplusplus
}
#endif
#endif
