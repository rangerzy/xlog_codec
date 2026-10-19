#ifndef XLOG_DECODE_CONFIG_H
#define XLOG_DECODE_CONFIG_H

/* Generated receiver private key, paired with the encoder default public key. */
#ifndef XLOG_DECODE_DEFAULT_PRIVATE_KEY_HEX
#define XLOG_DECODE_DEFAULT_PRIVATE_KEY_HEX \
    "8d077f412186dbd3a2c4cc522e7b603b33cb50336ec4c229c688bb0e9c548552"
#endif

/* Maximum decoded bytes per call. Zero disables the limit. */
#ifndef XLOG_DECODE_DEFAULT_MAX_OUTPUT_SIZE
#define XLOG_DECODE_DEFAULT_MAX_OUTPUT_SIZE (256u * 1024u * 1024u)
#endif

#endif
