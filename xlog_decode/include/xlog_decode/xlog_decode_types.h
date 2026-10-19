#ifndef XLOG_DECODE_TYPES_H
#define XLOG_DECODE_TYPES_H

#include <xlog_decode/xlog_decode_config.h>
#include <stddef.h>
#include <stdint.h>

typedef enum xlog_decode_status {
    XLOG_DECODE_OK = 0,
    XLOG_DECODE_INVALID_ARGUMENT,
    XLOG_DECODE_INVALID_FORMAT,
    XLOG_DECODE_TRUNCATED_INPUT,
    XLOG_DECODE_UNSUPPORTED_FORMAT,
    XLOG_DECODE_KEY_REQUIRED,
    XLOG_DECODE_INVALID_KEY,
    XLOG_DECODE_DECOMPRESSION_ERROR,
    XLOG_DECODE_OUTPUT_LIMIT,
    XLOG_DECODE_OUT_OF_MEMORY,
    XLOG_DECODE_INTERNAL_ERROR
} xlog_decode_status;

typedef struct xlog_decode_error {
    xlog_decode_status code;
    size_t offset; /* Starting offset of the failing block. */
    char message[256];
} xlog_decode_error;

typedef struct xlog_decode_block_header {
    uint8_t magic;
    uint16_t seq;
    uint8_t begin_hour;
    uint8_t end_hour;
    uint32_t payload_length;
    unsigned char public_key[64];
} xlog_decode_block_header;

typedef struct xlog_decode_block_view {
    xlog_decode_block_header header;
    const unsigned char *payload; /* Borrowed; keep the input alive and unchanged. */
    size_t payload_size;
    size_t offset;
    size_t encoded_size; /* 73-byte header + payload + terminator. */
} xlog_decode_block_view;

typedef struct xlog_decode_buffer_result {
    unsigned char *data; /* Library-owned; use xlog_decode_free_buffer(). */
    size_t size;
} xlog_decode_buffer_result;

typedef struct xlog_decode_options {
    const char *private_key_hex; /* NULL/empty means no key; otherwise exactly 64 hex digits. */
    size_t max_output_size; /* Zero means unlimited. Applies to the complete output. */
} xlog_decode_options;

/* Expands in the caller, so rebuilding the caller applies header overrides. */
#define XLOG_DECODE_OPTIONS_INIT \
    { XLOG_DECODE_DEFAULT_PRIVATE_KEY_HEX, XLOG_DECODE_DEFAULT_MAX_OUTPUT_SIZE }

#endif
