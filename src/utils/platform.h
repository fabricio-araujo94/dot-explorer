#ifndef DOT_EXPLORER_PLATFORM_H
#define DOT_EXPLORER_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <utime.h>
#include <wchar.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#ifndef PATH_MAX
#define PATH_MAX 32768
#endif
#define PLATFORM_PATH_SEPARATOR '\\'
#define PLATFORM_PATH_SEPARATOR_STRING "\\"
// Windows has no symbolic links in stat(); always report regular entries.
#ifndef S_ISLNK
#define S_ISLNK(m) 0
#endif
#ifndef S_IRWXU
#define S_IRWXU 0700
#endif
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

// Windows refuses to unlink read-only files, so clear the attribute first.
static inline int platform_remove_file(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_READONLY) != 0) {
        SetFileAttributesA(path, attributes & ~FILE_ATTRIBUTE_READONLY);
    }
#endif
    return unlink(path);
}

// The UCRT utime() rejects directories, so set timestamps through the Win32
// API, which accepts both files and directories.
static inline bool platform_set_times(const char *path, time_t atime, time_t mtime) {
#ifdef _WIN32
    FILETIME access_time, write_time;
    ULARGE_INTEGER value;
    HANDLE handle;

    if (atime < 0) atime = 0;
    if (mtime < 0) mtime = 0;
    handle = CreateFileA(path, FILE_WRITE_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    value.QuadPart = ((ULONGLONG)atime + 11644473600ULL) * 10000000ULL;
    access_time.dwLowDateTime = value.LowPart;
    access_time.dwHighDateTime = value.HighPart;
    value.QuadPart = ((ULONGLONG)mtime + 11644473600ULL) * 10000000ULL;
    write_time.dwLowDateTime = value.LowPart;
    write_time.dwHighDateTime = value.HighPart;
    if (!SetFileTime(handle, NULL, &access_time, &write_time)) {
        CloseHandle(handle);
        return false;
    }
    return CloseHandle(handle) != 0;
#else
    struct utimbuf times = { atime, mtime };
    return utime(path, &times) == 0;
#endif
}

// mingw-w64 (UCRT) neither declares nor provides wcwidth(), so the Windows
// build needs a fallback approximation of the printable column width.
static inline int platform_wcwidth(wchar_t character) {
#ifdef _WIN32
    unsigned int code = (unsigned int)character;

    if (code < 32u || (code >= 0x7Fu && code < 0xA0u)) return 0;
    if ((code >= 0x0300u && code <= 0x036Fu) ||
        (code >= 0x0483u && code <= 0x0489u) ||
        (code >= 0x0591u && code <= 0x05BDu) ||
        (code >= 0x0610u && code <= 0x061Au) ||
        (code >= 0x064Bu && code <= 0x065Fu) ||
        (code >= 0x200Bu && code <= 0x200Fu) ||
        (code >= 0xFE00u && code <= 0xFE0Fu) ||
        (code >= 0xFE20u && code <= 0xFE2Fu)) {
        return 0;
    }
    if ((code >= 0x1100u && code <= 0x115Fu) ||
        (code >= 0x2E80u && code <= 0x303Eu) ||
        (code >= 0x3041u && code <= 0x33FFu) ||
        (code >= 0x3400u && code <= 0x4DBFu) ||
        (code >= 0x4E00u && code <= 0x9FFFu) ||
        (code >= 0xA000u && code <= 0xA4CFu) ||
        (code >= 0xAC00u && code <= 0xD7A3u) ||
        (code >= 0xF900u && code <= 0xFAFFu) ||
        (code >= 0xFE30u && code <= 0xFE6Fu) ||
        (code >= 0xFF00u && code <= 0xFF60u) ||
        (code >= 0xFFE0u && code <= 0xFFE6u) ||
        (code >= 0x1F300u && code <= 0x1F64Fu) ||
        (code >= 0x1F900u && code <= 0x1F9FFu)) {
        return 2;
    }
    return 1;
#else
    return wcwidth(character);
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
