#include XLOG_HEADER
#include <cstdio>
#include <cstring>

int main()
{
    const char *version = XLOG_VERSION_FUNCTION();
    if (version == nullptr || std::strcmp(version, "0.1.0") != 0) {
        std::fputs("unexpected library version\n", stderr);
        return 1;
    }
    std::puts(version);
    return 0;
}
