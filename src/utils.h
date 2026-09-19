#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include "utils/platform.h"

void format_size(size_t size, char *buf, size_t buf_size);
const char *path_basename(const char *path);
bool is_absolute_path(const char *path);
bool utils_join_path(char *buffer, size_t size, const char *base, const char *name);

#endif // UTILS_H
