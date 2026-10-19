#ifndef XLOG_DECODE_H
#define XLOG_DECODE_H

#include <xlog_decode/xlog_decode_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed NUL-terminated version string. Do not modify or free. */
XLOG_DECODE_EXPORT const char *xlog_decode_version(void);

#ifdef __cplusplus
}
#endif
#endif
