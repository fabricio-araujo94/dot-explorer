#include "theme.h"
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

/* Nerd Font glyphs: Font Awesome fa-file-image-o (U+F03E). */
static const char icon_image[] = "\xEF\x80\xBE";
/* Nerd Font glyph: Font Awesome fa-file-archive-o (U+F1C6). */
static const char icon_archive[] = "\xEF\x87\x86";
/* Nerd Font glyph: Font Awesome fa-link (U+F0C1). */
static const char icon_symlink[] = "\xEF\x83\x81";
/* Nerd Font glyph: Font Awesome fa-folder (U+F07B). */
static const char icon_directory[] = "\xEF\x81\xBB";
/* Nerd Font glyph: Font Awesome fa-gear (U+F013), used for executables. */
static const char icon_executable[] = "\xEF\x80\x93";

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
    { ".png",  THEME_PAIR_IMAGE,   icon_image },
    { ".jpg",  THEME_PAIR_IMAGE,   icon_image },
    { ".jpeg", THEME_PAIR_IMAGE,   icon_image },
    { ".gif",  THEME_PAIR_IMAGE,   icon_image },
    { ".bmp",  THEME_PAIR_IMAGE,   icon_image },
    { ".webp", THEME_PAIR_IMAGE,   icon_image },
    { ".svg",  THEME_PAIR_IMAGE,   icon_image },
    { ".zip",  THEME_PAIR_ARCHIVE, icon_archive },
    { ".tar",  THEME_PAIR_ARCHIVE, icon_archive },
    { ".gz",   THEME_PAIR_ARCHIVE, icon_archive },
    { ".bz2",  THEME_PAIR_ARCHIVE, icon_archive },
    { ".xz",   THEME_PAIR_ARCHIVE, icon_archive },
    { ".7z",   THEME_PAIR_ARCHIVE, icon_archive },
    { ".rar",  THEME_PAIR_ARCHIVE, icon_archive }
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
        theme.icon = icon_symlink;
    } else if (S_ISDIR(mode)) {
        theme.color_pair = THEME_PAIR_DIRECTORY;
        theme.icon = icon_directory;
    } else if ((mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) {
        theme.color_pair = THEME_PAIR_EXECUTABLE;
        theme.icon = icon_executable;
    } else {
        theme = theme_for_extension(ext);
    }
    return theme;
}
