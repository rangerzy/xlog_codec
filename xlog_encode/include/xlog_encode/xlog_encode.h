#ifndef XLOG_ENCODE_H
#define XLOG_ENCODE_H

#include <xlog_encode/xlog_encode_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed NUL-terminated version string. Do not modify or free. */
XLOG_ENCODE_EXPORT const char *xlog_encode_version(void);

#ifdef __cplusplus
}
#endif
#endif
