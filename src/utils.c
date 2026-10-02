#include "utils.h"
#include <ctype.h>
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

bool set_error_path(char *error_path, size_t error_path_size, const char *path) {
    int written;

    if (!error_path || error_path_size == 0) {
        return false;
    }
    written = snprintf(error_path, error_path_size, "%s", path ? path : "");
    if (written < 0 || (size_t)written >= error_path_size) {
        error_path[error_path_size - 1] = '\0';
        errno = ENAMETOOLONG;
        return false;
    }
    return true;
}

const char *path_find_last_separator(const char *path) {
    const char *separator;
    const char *backslash;

    if (!path) return NULL;
    separator = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    return backslash && (!separator || backslash > separator) ? backslash : separator;
}

const char *path_basename(const char *path) {
    const char *separator = path_find_last_separator(path);
    return separator ? separator + 1 : path;
}

bool is_absolute_path(const char *path) {
    if (!path || !*path) return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    return isalpha((unsigned char)path[0]) && path[1] == ':' &&
           (path[2] == '/' || path[2] == '\\');
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
