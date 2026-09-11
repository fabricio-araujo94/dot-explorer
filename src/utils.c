#include "utils.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

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

bool utils_join_path(char *buffer, size_t size, const char *base, const char *name) {
        const char separator = PLATFORM_PATH_SEPARATOR;
    size_t base_length;
    size_t name_start = 0;
    int written;

    if (!buffer || size == 0 || !base || !name) {
        errno = EINVAL;
        return false;
    }
    base_length = strlen(base);
    while (name[name_start] == '/' || name[name_start] == '\\') {
        name_start++;
    }
    if (base_length == 0) {
        written = snprintf(buffer, size, "%s", name + name_start);
    } else if (base[base_length - 1] == '/' || base[base_length - 1] == '\\') {
        written = snprintf(buffer, size, "%s%s", base, name + name_start);
    } else {
        written = snprintf(buffer, size, "%s%c%s", base, separator, name + name_start);
    }
    if (written < 0 || (size_t)written >= size) {
        buffer[size - 1] = '\0';
        errno = ENAMETOOLONG;
        return false;
    }
    return true;
}
