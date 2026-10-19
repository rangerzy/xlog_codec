#include <xlog_encode/xlog_encode.h>

import xlog_encode.core;

const char *xlog_encode_version(void)
{
    return xlog::encode::version();
}
