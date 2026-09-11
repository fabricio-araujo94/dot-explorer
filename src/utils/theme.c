#include "theme.h"
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#define THEME_PAIR_NORMAL 2
#define THEME_PAIR_DIRECTORY 5
#define THEME_PAIR_EXECUTABLE 6
#define THEME_PAIR_IMAGE 7
#define THEME_PAIR_ARCHIVE 8
#define THEME_PAIR_SYMLINK 9

static bool extension_is(const char *ext, const char *const *extensions, size_t count) {
    if (!ext || !*ext) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const char *left = ext;
        const char *right = extensions[i];
        while (*left && *right &&
               tolower((unsigned char)*left) == tolower((unsigned char)*right)) {
            left++;
            right++;
        }
        if (*left == '\0' && *right == '\0') {
            return true;
        }
    }
    return false;
}

FileTheme get_file_color_and_icon(mode_t mode, const char *ext) {
    static const char *const image_extensions[] = {
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg"
    };
    static const char *const archive_extensions[] = {
        ".zip", ".tar", ".gz", ".bz2", ".xz", ".7z", ".rar"
    };
    FileTheme theme = { THEME_PAIR_NORMAL, " " };

    if (S_ISLNK(mode)) {
        theme.color_pair = THEME_PAIR_SYMLINK;
        theme.icon = "\xEF\x83\x81";
    } else if (S_ISDIR(mode)) {
        theme.color_pair = THEME_PAIR_DIRECTORY;
        theme.icon = "\xEF\x81\xBB";
    } else if ((mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) {
        theme.color_pair = THEME_PAIR_EXECUTABLE;
        theme.icon = "\xEF\x80\x93";
    } else if (extension_is(ext, image_extensions,
                            sizeof(image_extensions) / sizeof(image_extensions[0]))) {
        theme.color_pair = THEME_PAIR_IMAGE;
        theme.icon = "\xEF\x80\xBE";
    } else if (extension_is(ext, archive_extensions,
                            sizeof(archive_extensions) / sizeof(archive_extensions[0]))) {
        theme.color_pair = THEME_PAIR_ARCHIVE;
        theme.icon = "\xEF\x87\x86";
    }
    return theme;
}
