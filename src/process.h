#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>

/**
 * Opens a regular file using the system or user-configured editor.
 * On POSIX systems, uses $EDITOR with fallbacks (nano, vi).
 * On Windows, uses ShellExecuteA with the "open" verb.
 *
 * @param path Path to the file to open.
 * @return true if the file was opened and the editor exited successfully, false on error.
 */
bool process_open_file_with_editor(const char *path);

/* Alias for compatibility */
bool open_file_with_editor(const char *path);

#endif // PROCESS_H
