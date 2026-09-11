#include "state.h"
#include "utils.h"
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

void entry_list_clear(EntryList *list) {
    if (!list) return;
    free(list->indices);
    list->indices = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool fuzzy_match(const char *name, const char *query) {
    while (*name && *query) {
        if (tolower((unsigned char)*name) == tolower((unsigned char)*query)) {
            query++;
        }
        name++;
    }
    return *query == '\0';
}

bool filter_entries(const char *query, const DirectoryList *source, EntryList *list) {
    if (!query || !source || !list) {
        errno = EINVAL;
        return false;
    }
    list->count = 0;
    for (int i = 0; i < source->count; ++i) {
        if (fuzzy_match(source->entries[i].name, query)) {
            if (list->count == list->capacity) {
                int new_capacity = list->capacity == 0 ? 32 : list->capacity * 2;
                int *new_indices = realloc(list->indices,
                                            (size_t)new_capacity * sizeof(*new_indices));
                if (!new_indices) {
                    errno = ENOMEM;
                    list->count = 0;
                    return false;
                }
                list->indices = new_indices;
                list->capacity = new_capacity;
            }
            list->indices[list->count++] = i;
        }
    }
    return true;
}

int state_visible_count(const AppState *state) {
    return state->filter_active ? state->filtered_entries.count : state->dir_list.count;
}

int state_visible_index(const AppState *state, int view_index) {
    if (state->filter_active) {
        if (view_index < 0 || view_index >= state->filtered_entries.count) return -1;
        return state->filtered_entries.indices[view_index];
    }
    return view_index >= 0 && view_index < state->dir_list.count ? view_index : -1;
}

static bool set_error_path(char *error_path, size_t error_path_size, const char *path) {
    int written;
    if (!error_path || error_path_size == 0) {
        return false;
    }
    written = snprintf(error_path, error_path_size, "%s", path ? path : "");
    if (written < 0 || (size_t)written >= error_path_size) {
        error_path[error_path_size - 1] = '\0';
        errno = ENAMETOOLONG;
        return false;
    }
    return true;
}

static const char *path_basename(const char *path) {
    const char *separator = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!separator || backslash > separator)) {
        separator = backslash;
    }
#endif
    return separator ? separator + 1 : path;
}

static bool ensure_clipboard_capacity(Clipboard *clipboard) {
    size_t new_capacity;
    char **new_paths;

    if (clipboard->count < clipboard->capacity) {
        return true;
    }
    new_capacity = clipboard->capacity == 0 ? 16 : clipboard->capacity * 2;
    new_paths = realloc(clipboard->paths, new_capacity * sizeof(*new_paths));
    if (!new_paths) {
        errno = ENOMEM;
        return false;
    }
    clipboard->paths = new_paths;
    clipboard->capacity = new_capacity;
    return true;
}

bool clipboard_add_entry(Clipboard *clipboard, const char *path) {
    char absolute_path[PATH_MAX];
    char source_dir[PATH_MAX];
    const char *resolved;
    const char *separator;
    char *copy;

    if (!clipboard || !path || !*path) {
        errno = EINVAL;
        return false;
    }
    resolved = realpath(path, absolute_path);
    if (!resolved) {
        return false;
    }
    for (size_t i = 0; i < clipboard->count; ++i) {
        if (strcmp(clipboard->paths[i], absolute_path) == 0) {
            errno = EEXIST;
            return false;
        }
    }
    if (!ensure_clipboard_capacity(clipboard)) {
        return false;
    }
    copy = strdup(absolute_path);
    if (!copy) {
        errno = ENOMEM;
        return false;
    }
    clipboard->paths[clipboard->count++] = copy;

    if (clipboard->source_dir[0] == '\0') {
        separator = strrchr(absolute_path, '/');
#ifdef _WIN32
        {
            const char *backslash = strrchr(absolute_path, '\\');
            if (backslash && (!separator || backslash > separator)) {
                separator = backslash;
            }
        }
#endif
        if (!separator) {
            snprintf(source_dir, sizeof(source_dir), ".");
        } else if (separator == absolute_path) {
            snprintf(source_dir, sizeof(source_dir), "%c", separator[0]);
        } else {
            size_t length = (size_t)(separator - absolute_path);
            if (length >= sizeof(source_dir)) {
                free(copy);
                clipboard->count--;
                errno = ENAMETOOLONG;
                return false;
            }
            memcpy(source_dir, absolute_path, length);
            source_dir[length] = '\0';
        }
        snprintf(clipboard->source_dir, sizeof(clipboard->source_dir), "%s", source_dir);
    }
    return true;
}

void clipboard_clear(Clipboard *clipboard) {
    if (!clipboard) {
        return;
    }
    for (size_t i = 0; i < clipboard->count; ++i) {
        free(clipboard->paths[i]);
    }
    free(clipboard->paths);
    clipboard->paths = NULL;
    clipboard->count = 0;
    clipboard->capacity = 0;
    clipboard->is_cut = false;
    clipboard->source_dir[0] = '\0';
}

void clipboard_invert_selection(DirectoryList *list) {
    if (!list) {
        return;
    }
    for (int i = 0; i < list->count; ++i) {
        list->entries[i].is_selected = !list->entries[i].is_selected;
    }
}

bool clipboard_apply_operation(Clipboard *clipboard, const char *destination_dir,
                               char *error_path, size_t error_path_size) {
    if (!clipboard || !destination_dir || !*destination_dir ||
        clipboard->count == 0 || !error_path || error_path_size == 0) {
        errno = EINVAL;
        return false;
    }
    for (size_t i = 0; i < clipboard->count; ++i) {
        const char *source = clipboard->paths[i];
        const char *name = path_basename(source);
        char destination[PATH_MAX];

        if (!utils_join_path(destination, sizeof(destination), destination_dir, name)) {
            set_error_path(error_path, error_path_size, source);
            return false;
        }
        if (clipboard->is_cut) {
            if (rename(source, destination) != 0) {
                set_error_path(error_path, error_path_size, source);
                return false;
            }
        } else if (!fs_copy_recursive(source, destination)) {
            set_error_path(error_path, error_path_size, source);
            return false;
        }
    }
    if (clipboard->is_cut) {
        clipboard_clear(clipboard);
    }
    return true;
}

void state_init(AppState *state) {
    if (getcwd(state->current_path, sizeof(state->current_path)) == NULL) {
        strcpy(state->current_path, "/");
    }
    fs_init_dir_list(&state->dir_list);
    state->selected_index = 0;
    state->scroll_offset = 0;
    state->should_quit = false;
    state->sort_type = SORT_NAME;
    memset(&state->filtered_entries, 0, sizeof(state->filtered_entries));
    state->filter_active = false;
    state->filter_query[0] = '\0';
    memset(&state->clipboard, 0, sizeof(state->clipboard));

    fs_read_dir(state->current_path, &state->dir_list);
    fs_sort_dir_list(&state->dir_list, state->sort_type);
}

void state_cleanup(AppState *state) {
    entry_list_clear(&state->filtered_entries);
    clipboard_clear(&state->clipboard);
    fs_free_dir_list(&state->dir_list);
}
void state_change_dir(AppState *state, const char *new_path) {
    char target_path[PATH_MAX];

    if (new_path[0] == '/') {
        strncpy(target_path, new_path, sizeof(target_path) - 1);
    } else {
        if (!utils_join_path(target_path, sizeof(target_path), state->current_path, new_path)) {
            return;
        }
    }
    target_path[sizeof(target_path) - 1] = '\0';

    DirectoryList new_list;
    fs_init_dir_list(&new_list);
    if (fs_read_dir(target_path, &new_list)) {
        fs_sort_dir_list(&new_list, state->sort_type);
        fs_free_dir_list(&state->dir_list);
        state->dir_list = new_list;

        char *resolved = realpath(target_path, NULL);
        if (resolved) {
            strncpy(state->current_path, resolved, sizeof(state->current_path) - 1);
            state->current_path[sizeof(state->current_path) - 1] = '\0';
            free(resolved);
        } else {
            strncpy(state->current_path, target_path, sizeof(state->current_path) - 1);
            state->current_path[sizeof(state->current_path) - 1] = '\0';
        }

        state->selected_index = 0;
        state->scroll_offset = 0;
        state->filter_active = false;
        state->filter_query[0] = '\0';
        entry_list_clear(&state->filtered_entries);
    } else {
        fs_free_dir_list(&new_list);
    }
}
