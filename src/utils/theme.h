#ifndef THEME_H
#define THEME_H

#include <sys/stat.h>

typedef enum ColorPair {
    THEME_PAIR_CYAN = 1,
    THEME_PAIR_NORMAL = 2,
    THEME_PAIR_ACTIVE = 3,
    THEME_PAIR_PROMPT = 4,
    THEME_PAIR_DIRECTORY = 5,
    THEME_PAIR_EXECUTABLE = 6,
    THEME_PAIR_IMAGE = 7,
    THEME_PAIR_ARCHIVE = 8,
    THEME_PAIR_SYMLINK = 9
} ColorPair;

typedef struct {
    ColorPair color_pair;
    const char *icon;
} FileTheme;

FileTheme get_file_color_and_icon(mode_t mode, const char *ext);

#endif // THEME_H
