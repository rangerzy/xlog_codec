#ifndef XLOG_ENCODE_TYPES_H
#define XLOG_ENCODE_TYPES_H

#include <xlog_encode/xlog_encode_config.h>
#include <stddef.h>
#include <stdint.h>

typedef enum xlog_encode_mode {
    XLOG_ENCODE_PLAIN = 0,       /* 0x08 */
    XLOG_ENCODE_DEFLATE = 1,     /* 0x09 */
    XLOG_ENCODE_DEFLATE_TEA = 2  /* 0x07 */
} xlog_encode_mode;

typedef enum xlog_encode_status {
    XLOG_ENCODE_OK = 0,
    XLOG_ENCODE_INVALID_ARGUMENT,
    XLOG_ENCODE_UNSUPPORTED_MODE,
    XLOG_ENCODE_KEY_REQUIRED,
    XLOG_ENCODE_INVALID_KEY,
    XLOG_ENCODE_INPUT_LIMIT,
    XLOG_ENCODE_OUTPUT_LIMIT,
    XLOG_ENCODE_COMPRESSION_ERROR,
    XLOG_ENCODE_CRYPTO_ERROR,
    XLOG_ENCODE_OUT_OF_MEMORY,
    XLOG_ENCODE_INTERNAL_ERROR
} xlog_encode_status;

typedef struct xlog_encode_error {
    xlog_encode_status code;
    char message[256];
} xlog_encode_error;

typedef struct xlog_encode_buffer_result {
    unsigned char *data; /* Library-owned; release with xlog_encode_free_buffer. */
    size_t size;
} xlog_encode_buffer_result;

typedef struct xlog_encode_options {
    xlog_encode_mode mode;
    const char *public_key_hex; /* Required only in DEFLATE_TEA mode. */
    uint16_t seq;
    uint8_t begin_hour; /* 0..23 */
    uint8_t end_hour;   /* 0..23; may be less than begin_hour across midnight. */
    size_t max_output_size; /* Complete block size. Zero disables this limit. */
} xlog_encode_options;

/* Expands in the caller, so rebuilding it applies configuration-header overrides. */
#define XLOG_ENCODE_OPTIONS_INIT \
    { XLOG_ENCODE_DEFAULT_MODE, XLOG_ENCODE_DEFAULT_PUBLIC_KEY_HEX, \
      XLOG_ENCODE_DEFAULT_SEQ, XLOG_ENCODE_DEFAULT_BEGIN_HOUR, \
      XLOG_ENCODE_DEFAULT_END_HOUR, XLOG_ENCODE_DEFAULT_MAX_OUTPUT_SIZE }

#endif
