#include "utils.h"
#include <stdio.h>

void format_size(size_t size, char *buf, size_t buf_size) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double dsize = size;

    while (dsize >= 1024 && unit < 4) {
        dsize /= 1024;
        unit++;
    }

    if (unit == 0) {
        snprintf(buf, buf_size, "%zu %s", size, units[unit]);
    } else {
        snprintf(buf, buf_size, "%.1f %s", dsize, units[unit]);
    }
}
