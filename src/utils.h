#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include "utils/platform.h"

void utils_format_size(size_t size, char *buf, size_t buf_size);
bool set_error_path(char *error_path, size_t error_path_size, const char *path);
const char *path_find_last_separator(const char *path);
const char *path_basename(const char *path);
bool path_is_absolute(const char *path);
bool path_join(char *buffer, size_t size, const char *base, const char *name);

#endif // UTILS_H
