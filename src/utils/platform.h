#ifndef DOT_EXPLORER_PLATFORM_H
#define DOT_EXPLORER_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#ifndef PATH_MAX
#define PATH_MAX 32768
#endif
#define PLATFORM_PATH_SEPARATOR '\\'
#define PLATFORM_PATH_SEPARATOR_STRING "\\"
#else
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define PLATFORM_PATH_SEPARATOR '/'
#define PLATFORM_PATH_SEPARATOR_STRING "/"
#endif

static inline int platform_mkdir(const char *path, mode_t mode) {
#ifdef _WIN32
    (void)mode;
    return _mkdir(path);
#else
    return mkdir(path, mode);
#endif
}

static inline bool platform_realpath(const char *path, char *resolved, size_t size) {
#ifdef _WIN32
    DWORD attributes;
    DWORD length;
    if (!path || !resolved || size == 0) {
        errno = EINVAL;
        return false;
    }
    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        resolved[0] = '\0';
        errno = ENOENT;
        return false;
    }
    length = GetFullPathNameA(path, (DWORD)size, resolved, NULL);
    if (length == 0) {
        resolved[0] = '\0';
        errno = EINVAL;
        return false;
    }
    if ((size_t)length >= size) {
        resolved[0] = '\0';
        errno = ENAMETOOLONG;
        return false;
    }
    return true;
#else
    char *tmp;
    if (!path || !resolved || size == 0) {
        errno = EINVAL;
        return false;
    }
    tmp = realpath(path, NULL);
    if (!tmp) return false;
    if (strlen(tmp) >= size) {
        free(tmp);
        resolved[0] = '\0';
        errno = ENAMETOOLONG;
        return false;
    }
    memcpy(resolved, tmp, strlen(tmp) + 1);
    free(tmp);
    return true;
#endif
}

#endif // DOT_EXPLORER_PLATFORM_H
