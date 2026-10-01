#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static inline bool test_get_temp_dir(char *buffer, size_t size, const char *suffix) {
    const char *tmpdir;
    const char *separator;
    size_t tmpdir_length;
    int written;
#ifdef _WIN32
    char tmp[MAX_PATH];
    DWORD len = GetTempPathA(sizeof(tmp), tmp);
    if (len == 0 || len >= sizeof(tmp) || !buffer || size == 0) {
        return false;
    }
    while (len > 1 && (tmp[len - 1] == '\\' || tmp[len - 1] == '/')) {
        tmp[--len] = '\0';
    }
    tmpdir = tmp;
    separator = "\\";
#else
    if (!buffer || size == 0) {
        return false;
    }
    tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir) {
        tmpdir = "/tmp";
    }
    separator = "/";
#endif
    tmpdir_length = strlen(tmpdir);
    if (tmpdir_length > 0 &&
        (tmpdir[tmpdir_length - 1] == '/' || tmpdir[tmpdir_length - 1] == '\\')) {
        separator = "";
    }
    written = snprintf(buffer, size, "%s%s%s", tmpdir, separator,
                       suffix ? suffix : "dot-explorer-test");
    return written >= 0 && (size_t)written < size;
}

#endif // TEST_HELPERS_H
