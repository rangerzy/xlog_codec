module;
#include <meta>
#include "xlog_version_config.h"

export module xlog_decode.core;

namespace {
struct VersionMetadata {
    int major = XLOG_VERSION_MAJOR;
    int minor = XLOG_VERSION_MINOR;
    int patch = XLOG_VERSION_PATCH;
};
static_assert(std::meta::nonstatic_data_members_of(
    ^^VersionMetadata, std::meta::access_context::current()).size() == 3);
}

export namespace xlog::decode {
const char *version() noexcept
{
    return XLOG_VERSION_STRING;
}
}
