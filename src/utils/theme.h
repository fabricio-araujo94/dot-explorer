#ifndef THEME_H
#define THEME_H

#include <sys/stat.h>

typedef struct {
    int color_pair;
    const char *icon;
} FileTheme;

FileTheme get_file_color_and_icon(mode_t mode, const char *ext);

#endif // THEME_H
