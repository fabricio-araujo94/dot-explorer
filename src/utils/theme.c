#include "theme.h"
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

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

typedef struct {
    const char *extension;
    ColorPair color_pair;
    const char *icon;
} ExtensionTheme;

static const ExtensionTheme extension_themes[] = {
    { ".png",  THEME_PAIR_IMAGE,   "\xEF\x80\xBE" },
    { ".jpg",  THEME_PAIR_IMAGE,   "\xEF\x80\xBE" },
    { ".jpeg", THEME_PAIR_IMAGE,   "\xEF\x80\xBE" },
    { ".gif",  THEME_PAIR_IMAGE,   "\xEF\x80\xBE" },
    { ".bmp",  THEME_PAIR_IMAGE,   "\xEF\x80\xBE" },
    { ".webp", THEME_PAIR_IMAGE,   "\xEF\x80\xBE" },
    { ".svg",  THEME_PAIR_IMAGE,   "\xEF\x80\xBE" },
    { ".zip",  THEME_PAIR_ARCHIVE, "\xEF\x87\x86" },
    { ".tar",  THEME_PAIR_ARCHIVE, "\xEF\x87\x86" },
    { ".gz",   THEME_PAIR_ARCHIVE, "\xEF\x87\x86" },
    { ".bz2",  THEME_PAIR_ARCHIVE, "\xEF\x87\x86" },
    { ".xz",   THEME_PAIR_ARCHIVE, "\xEF\x87\x86" },
    { ".7z",   THEME_PAIR_ARCHIVE, "\xEF\x87\x86" },
    { ".rar",  THEME_PAIR_ARCHIVE, "\xEF\x87\x86" }
};

static FileTheme theme_for_extension(const char *ext) {
    for (size_t i = 0; i < sizeof(extension_themes) / sizeof(extension_themes[0]); ++i) {
        if (extension_is(ext, &extension_themes[i].extension, 1)) {
            FileTheme theme = {
                extension_themes[i].color_pair,
                extension_themes[i].icon
            };
            return theme;
        }
    }
    return (FileTheme){ THEME_PAIR_NORMAL, " " };
}

FileTheme get_file_color_and_icon(mode_t mode, const char *ext) {
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
    } else {
        theme = theme_for_extension(ext);
    }
    return theme;
}
