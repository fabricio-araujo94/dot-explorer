#ifndef DOT_EXPLORER_PLATFORM_H
#define DOT_EXPLORER_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#ifndef PATH_MAX
#define PATH_MAX 32768
#endif
#define PLATFORM_PATH_SEPARATOR '\\'
#define PLATFORM_PATH_SEPARATOR_STRING "\\"
#else
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define PLATFORM_PATH_SEPARATOR '/'
#define PLATFORM_PATH_SEPARATOR_STRING "/"
#endif

typedef bool (*platform_drive_callback)(const char *path, void *context);

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
    DWORD length;
    if (!path || !resolved || size == 0) return false;
    length = GetFullPathNameA(path, (DWORD)size, resolved, NULL);
    if (length == 0 || (size_t)length >= size) {
        if (size > 0) resolved[0] = '\0';
        return false;
    }
    return true;
#else
    (void)size;
    return realpath(path, resolved) != NULL;
#endif
}

static inline bool platform_list_drives(platform_drive_callback callback, void *context) {
    if (!callback) return false;
#ifdef _WIN32
    {
        DWORD mask = GetLogicalDrives();
        if (mask == 0) return false;
        for (unsigned int i = 0; i < 26; ++i) {
            char drive[4] = { (char)('A' + i), ':', '\\', '\0' };
            if ((mask & (1u << i)) != 0 && !callback(drive, context)) return false;
        }
    }
#else
    return callback("/", context);
#endif
    return true;
}

#endif // DOT_EXPLORER_PLATFORM_H
