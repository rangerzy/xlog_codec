#ifndef XLOG_ENCODE_CONFIG_H
#define XLOG_ENCODE_CONFIG_H

#ifndef XLOG_ENCODE_DEFAULT_MODE
#define XLOG_ENCODE_DEFAULT_MODE XLOG_ENCODE_DEFLATE_TEA
#endif

/* Generated receiver public key, paired with the decoder default private key.
 * secp256k1, 128 hex digits (X || Y), without 04 prefix. */
#ifndef XLOG_ENCODE_DEFAULT_PUBLIC_KEY_HEX
#define XLOG_ENCODE_DEFAULT_PUBLIC_KEY_HEX \
    "9be8a4dd65f1cd37a109a487a5875ec53a1757b961e550cdce6b759dcab5753d" \
    "77ab0997d44e7cca0d42a88d65a041de0ba46233eee00bde10eb004a38f497ec"
#endif

#ifndef XLOG_ENCODE_DEFAULT_SEQ
#define XLOG_ENCODE_DEFAULT_SEQ 0u
#endif
#ifndef XLOG_ENCODE_DEFAULT_BEGIN_HOUR
#define XLOG_ENCODE_DEFAULT_BEGIN_HOUR 0u
#endif
#ifndef XLOG_ENCODE_DEFAULT_END_HOUR
#define XLOG_ENCODE_DEFAULT_END_HOUR 0u
#endif

/* Complete encoded block, including its header and terminator. Zero is unlimited. */
#ifndef XLOG_ENCODE_DEFAULT_MAX_OUTPUT_SIZE
#define XLOG_ENCODE_DEFAULT_MAX_OUTPUT_SIZE (256u * 1024u * 1024u)
#endif

#endif
