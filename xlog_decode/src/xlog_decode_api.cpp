#include <xlog_decode/xlog_decode.h>

import xlog_decode.core;

const char *xlog_decode_version(void)
{
    return xlog::decode::version();
}
