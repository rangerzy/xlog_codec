#ifndef XLOG_ENCODE_H
#define XLOG_ENCODE_H

#include <xlog_encode/xlog_encode_export.h>
#include <xlog_encode/xlog_encode_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed NUL-terminated version string. Do not modify or free. */
XLOG_ENCODE_EXPORT const char *xlog_encode_version(void);

/* Encode arbitrary binary bytes into one complete, independently decodable xlog
 * block. Empty input is valid; data may be NULL only when size is zero. Input and
 * payload are each limited to UINT32_MAX bytes. Options NULL uses the library's
 * compiled defaults; XLOG_ENCODE_OPTIONS_INIT applies the caller's defaults.
 * Failure clears output and returns no partial block. Error is optional and is
 * reset on every call. Free an old result before reusing its output argument.
 * Encryption requires the receiver's public key and never falls back to plain.
 * Exceptions do not cross the C ABI. */
XLOG_ENCODE_EXPORT xlog_encode_status xlog_encode_bytes(
    const void *data, size_t size, const xlog_encode_options *options,
    xlog_encode_buffer_result *output, xlog_encode_error *error);

/* Accepts NULL; clears the result, making repeated calls harmless. */
XLOG_ENCODE_EXPORT void xlog_encode_free_buffer(xlog_encode_buffer_result *output);

#ifdef __cplusplus
}
#endif
#endif
