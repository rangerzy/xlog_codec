#include XLOG_HEADER
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *version = XLOG_VERSION_FUNCTION();
    if (version == NULL || strcmp(version, "0.1.0") != 0) {
        fputs("unexpected library version\n", stderr);
        return 1;
    }
    puts(version);
    return 0;
}
