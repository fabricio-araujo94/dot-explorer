#include "state.h"
#include "utils.h"
#include <errno.h>
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

static void history_clear_stack(HistoryEntry **entries, size_t *count,
                                size_t *capacity) {
    free(*entries);
    *entries = NULL;
    *count = 0;
    *capacity = 0;
}

static bool history_stack_push(HistoryEntry **entries, size_t *count,
                               size_t *capacity, const char *path,
                               int selected_index) {
    HistoryEntry *new_entries;
    size_t new_capacity;
    if (*count == *capacity) {
        new_capacity = *capacity == 0 ? 16 : *capacity * 2;
        new_entries = realloc(*entries, new_capacity * sizeof(*new_entries));
        if (!new_entries) {
            errno = ENOMEM;
            return false;
        }
        *entries = new_entries;
        *capacity = new_capacity;
    }
    if (snprintf((*entries)[*count].path, PATH_MAX, "%s", path) >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return false;
    }
    (*entries)[*count].selected_index = selected_index;
    (*count)++;
    return true;
}

static bool history_pop_stack(HistoryEntry *entries, size_t *count,
                              char *path, size_t path_size, int *selected_index) {
    HistoryEntry *entry;
    if (!entries || !count || *count == 0 || !path || path_size == 0 ||
        !selected_index) {
        errno = ENOENT;
        return false;
    }
    entry = &entries[*count - 1];
    if (snprintf(path, path_size, "%s", entry->path) >= (int)path_size) {
        errno = ENAMETOOLONG;
        return false;
    }
    *selected_index = entry->selected_index;
    (*count)--;
    return true;
}

bool history_push(AppState *state, const char *path, int selected_index) {
    if (!state || !path || !*path) {
        errno = EINVAL;
        return false;
    }
    if (!history_stack_push(&state->history.back, &state->history.back_count,
                            &state->history.back_capacity, path, selected_index)) {
        return false;
    }
    history_clear_stack(&state->history.forward, &state->history.forward_count,
                        &state->history.forward_capacity);
    return true;
}

bool history_pop_back(AppState *state, char *path, size_t path_size,
                      int *selected_index) {
    if (!state || state->history.back_count == 0) {
        errno = ENOENT;
        return false;
    }
    if (!history_stack_push(&state->history.forward, &state->history.forward_count,
                            &state->history.forward_capacity, state->current_path,
                            state->selected_index)) {
        return false;
    }
    return history_pop_stack(state->history.back, &state->history.back_count,
                             path, path_size, selected_index);
}

bool history_pop_forward(AppState *state, char *path, size_t path_size,
                         int *selected_index) {
    if (!state || state->history.forward_count == 0) {
        errno = ENOENT;
        return false;
    }
    if (!history_stack_push(&state->history.back, &state->history.back_count,
                            &state->history.back_capacity, state->current_path,
                            state->selected_index)) {
        return false;
    }
    return history_pop_stack(state->history.forward, &state->history.forward_count,
                             path, path_size, selected_index);
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
    const char *separator;
    char *copy;

    if (!clipboard || !path || !*path) {
        errno = EINVAL;
        return false;
    }
    if (!platform_realpath(path, absolute_path, sizeof(absolute_path))) {
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
                int rename_errno = errno;
                if (rename_errno == EXDEV) {
                    if (!fs_copy_recursive(source, destination)) {
                        set_error_path(error_path, error_path_size, source);
                        return false;
                    }
                    if (!fs_delete_recursive(source)) {
                        set_error_path(error_path, error_path_size, source);
                        return false;
                    }
                    continue;
                }
                errno = rename_errno;
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
    memset(&state->history, 0, sizeof(state->history));

    fs_read_dir(state->current_path, &state->dir_list);
    fs_sort_dir_list(&state->dir_list, state->sort_type);
}

void state_cleanup(AppState *state) {
    entry_list_clear(&state->filtered_entries);
    history_clear_stack(&state->history.back, &state->history.back_count,
                        &state->history.back_capacity);
    history_clear_stack(&state->history.forward, &state->history.forward_count,
                        &state->history.forward_capacity);
    fs_free_dir_list(&state->dir_list);
}
void state_change_dir(AppState *state, const char *new_path) {
    char target_path[PATH_MAX];
    bool is_refresh = (strcmp(new_path, ".") == 0);
    char saved_selected_name[256] = "";
    int old_selected_index = state->selected_index;
    int old_scroll_offset = state->scroll_offset;

    if (is_refresh && state->dir_list.count > 0 &&
        state->selected_index >= 0 && state->selected_index < state->dir_list.count) {
        strncpy(saved_selected_name, state->dir_list.entries[state->selected_index].name,
                sizeof(saved_selected_name) - 1);
        saved_selected_name[sizeof(saved_selected_name) - 1] = '\0';
    }

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

        char resolved[PATH_MAX];
        if (platform_realpath(target_path, resolved, sizeof(resolved))) {
            strncpy(state->current_path, resolved, sizeof(state->current_path) - 1);
            state->current_path[sizeof(state->current_path) - 1] = '\0';
        } else {
            strncpy(state->current_path, target_path, sizeof(state->current_path) - 1);
            state->current_path[sizeof(state->current_path) - 1] = '\0';
        }

        if (is_refresh && state->dir_list.count > 0) {
            int restored_index = -1;
            if (saved_selected_name[0] != '\0') {
                for (int i = 0; i < state->dir_list.count; ++i) {
                    if (strcmp(state->dir_list.entries[i].name, saved_selected_name) == 0) {
                        restored_index = i;
                        break;
                    }
                }
            }
            if (restored_index >= 0) {
                state->selected_index = restored_index;
            } else {
                state->selected_index = old_selected_index < state->dir_list.count ?
                                        old_selected_index : state->dir_list.count - 1;
            }
            state->scroll_offset = old_scroll_offset <= state->selected_index ?
                                   old_scroll_offset : state->selected_index;
        } else {
            state->selected_index = 0;
            state->scroll_offset = 0;
        }
        state->filter_active = false;
        state->filter_query[0] = '\0';
        entry_list_clear(&state->filtered_entries);
    } else {
        fs_free_dir_list(&new_list);
    }
}
