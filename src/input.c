#include "input.h"
#include "config.h"
#include "utils.h"
#include "fs.h"
#include "process.h"

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

static inline void cb_show_message(const InputCallbacks *cb, const char *title, const char *msg) {
    if (cb && cb->show_message) {
        cb->show_message(title, msg, cb->userdata);
    }
}

static inline bool cb_prompt(const InputCallbacks *cb, const char *prompt, char *buf, size_t size) {
    if (cb && cb->prompt) {
        return cb->prompt(prompt, buf, size, cb->userdata);
    }
    return false;
}

static inline int cb_get_list_height(const InputCallbacks *cb) {
    if (cb && cb->get_list_height) {
        int h = cb->get_list_height(cb->userdata);
        if (h > 0) return h;
    }
    return 24;
}

static inline bool cb_open_file(const InputCallbacks *cb, const char *path) {
    if (cb && cb->open_file) {
        return cb->open_file(path, cb->userdata);
    }
    return open_file_with_editor(path);
}

static bool is_navigation_entry(const FileEntry *entry) {
    return entry && (strcmp(entry->name, ".") == 0 ||
                     strcmp(entry->name, "..") == 0);
}

static void navigate_history(AppState *state, bool forward) {
    char path[PATH_MAX];
    int selected_index;
    bool restored;

    if (forward) {
        restored = history_pop_forward(state, path, sizeof(path), &selected_index);
    } else {
        restored = history_pop_back(state, path, sizeof(path), &selected_index);
    }
    if (!restored) {
        return;
    }
    state_change_dir(state, path);
    if (state->dir_list.count > 0) {
        if (selected_index < 0) selected_index = 0;
        if ((size_t)selected_index >= state->dir_list.count) {
            size_t last_index = state->dir_list.count - 1;
            selected_index = last_index > (size_t)INT_MAX ?
                             INT_MAX : (int)last_index;
        }
        state->selected_index = selected_index;
    } else {
        state->selected_index = 0;
    }
}

static int current_view_index(const AppState *state) {
    if (!state->filter_active) return state->selected_index;
    for (size_t i = 0; i < state->filtered_entries.count; ++i) {
        if (state->filtered_entries.indices[i] == state->selected_index) {
            return i <= (size_t)INT_MAX ? (int)i : -1;
        }
    }
    return -1;
}

static void move_visible_selection(AppState *state, int view_index, int list_height) {
    size_t visible_count = state_visible_count(state);
    int original_index;
    if (visible_count == 0) {
        state->selected_index = 0;
        state->scroll_offset = 0;
        return;
    }
    if (view_index < 0) view_index = 0;
    if (view_index >= 0 && (size_t)view_index >= visible_count) {
        view_index = (int)(visible_count - 1);
    }
    original_index = state_visible_index(state, view_index);
    if (original_index >= 0) state->selected_index = original_index;
    if (view_index < state->scroll_offset) state->scroll_offset = view_index;
    if (view_index >= state->scroll_offset + list_height) {
        state->scroll_offset = view_index - list_height + 1;
    }
}

static void filter_prompt(AppState *state, const InputCallbacks *cb) {
    if (!cb || !cb->get_char) {
        return;
    }
    int length = 0;
    state->filter_active = true;
    state->filter_query[0] = '\0';
    filter_entries(state->filter_query, &state->dir_list, &state->filtered_entries);
    if (cb->set_cursor) cb->set_cursor(1, cb->userdata);

    for (;;) {
        int ch;
        if (cb->render_filter_prompt) {
            cb->render_filter_prompt(state->filter_query, cb->userdata);
        }
        ch = cb->get_char(cb->userdata);
        if (ch == 27 || ch == -1) { /* 27 = ESC */
            state->filter_active = false;
            state->filter_query[0] = '\0';
            entry_list_clear(&state->filtered_entries);
            break;
        }
        if (ch == '\n' || ch == 10 || ch == 13 || ch == KEY_ENTER) break;
        if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
            if (length > 0) state->filter_query[--length] = '\0';
        } else if (isprint((unsigned char)ch) &&
                   length < (int)sizeof(state->filter_query) - 1) {
            state->filter_query[length++] = (char)ch;
            state->filter_query[length] = '\0';
        } else {
            continue;
        }
        filter_entries(state->filter_query, &state->dir_list, &state->filtered_entries);
        if (state->filtered_entries.count > 0 && current_view_index(state) < 0) {
            state->selected_index = state->filtered_entries.indices[0];
            state->scroll_offset = 0;
        }
    }
    if (state->filter_query[0] == '\0') {
        state->filter_active = false;
        entry_list_clear(&state->filtered_entries);
    }
    if (cb->set_cursor) cb->set_cursor(0, cb->userdata);
}

static bool capture_clipboard(AppState *state, Clipboard *clipboard, bool is_cut) {
    bool has_selected = false;

    if (!clipboard) return false;
    clipboard_clear(clipboard);
    for (size_t i = 0; i < state->dir_list.count; ++i) {
        if (state->dir_list.entries[i].is_selected) {
            has_selected = true;
            break;
        }
    }
    for (size_t i = 0; i < state->dir_list.count; ++i) {
        char path[PATH_MAX];
        if (has_selected && !state->dir_list.entries[i].is_selected) {
            continue;
        }
        if (!has_selected && (state->selected_index < 0 ||
                      i != (size_t)state->selected_index)) {
            continue;
        }
        if (is_navigation_entry(&state->dir_list.entries[i])) {
            continue;
        }
        if (!utils_join_path(path, sizeof(path), state->current_path,
                             state->dir_list.entries[i].name) ||
            !clipboard_add_entry(clipboard, path)) {
            clipboard_clear(clipboard);
            return false;
        }
    }
    clipboard->is_cut = is_cut;
    return clipboard->count > 0;
}

/* Action Handlers */

typedef void (*KeyActionHandler)(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch);

static void handle_quit_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)callbacks;
    (void)ch;
    state->should_quit = true;
}

static void handle_move_up_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    int list_height = cb_get_list_height(callbacks);
    move_visible_selection(state, current_view_index(state) - 1, list_height);
}

static void handle_move_down_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    int list_height = cb_get_list_height(callbacks);
    move_visible_selection(state, current_view_index(state) + 1, list_height);
}

static void handle_filter_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    filter_prompt(state, callbacks);
}

static void handle_enter_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    if (state->dir_list.count == 0) {
        return;
    }
    const FileEntry *entry = &state->dir_list.entries[state->selected_index];
    if (entry->is_dir) {
        char previous_path[PATH_MAX];
        int previous_selected_index = state->selected_index;
        snprintf(previous_path, sizeof(previous_path), "%s", state->current_path);
        if (state_change_dir(state, entry->name)) {
            history_push(state, previous_path, previous_selected_index);
        } else {
            char message[PATH_MAX + 64];
            snprintf(message, sizeof(message), "%s: %s", entry->name, strerror(errno));
            cb_show_message(callbacks, "Navigation failed", message);
        }
    } else {
        char path[PATH_MAX];
        if (utils_join_path(path, sizeof(path), state->current_path, entry->name) &&
            !cb_open_file(callbacks, path)) {
            char message[PATH_MAX + 64];
            snprintf(message, sizeof(message), "%s: %s", path, strerror(errno));
            cb_show_message(callbacks, "Open failed", message);
        }
    }
}

static void handle_back_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    if (!state_change_dir(state, "..")) {
        char message[PATH_MAX + 64];
        snprintf(message, sizeof(message), "..: %s", strerror(errno));
        cb_show_message(callbacks, "Navigation failed", message);
    }
}

static void handle_history_back_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)callbacks;
    (void)ch;
    navigate_history(state, false);
}

static void handle_history_forward_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)callbacks;
    (void)ch;
    navigate_history(state, true);
}

static void handle_refresh_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    if (!state_change_dir(state, ".")) {
        char message[PATH_MAX + 64];
        snprintf(message, sizeof(message), "%s", strerror(errno));
        cb_show_message(callbacks, "Refresh failed", message);
    }
}

static void handle_select_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    if (state->dir_list.count == 0) {
        return;
    }
    int list_height = cb_get_list_height(callbacks);
    FileEntry *entry = &state->dir_list.entries[state->selected_index];
    entry->is_selected = !entry->is_selected;
    if (state->selected_index >= 0 &&
        (size_t)state->selected_index < state->dir_list.count - 1) {
        state->selected_index++;
        if (state->selected_index >= state->scroll_offset + list_height) {
            state->scroll_offset = state->selected_index - list_height + 1;
        }
    }
}

static void handle_delete_selected_items(AppState *state, const InputCallbacks *callbacks,
                                         size_t selected_count) {
    char prompt_msg[256];
    char buf[256];
    snprintf(prompt_msg, sizeof(prompt_msg),
             "Delete %zu selected item%s? (y/n): ",
             selected_count, selected_count > 1 ? "s" : "");
    if (cb_prompt(callbacks, prompt_msg, buf, sizeof(buf)) && (buf[0] == 'y' || buf[0] == 'Y')) {
        bool any_fail = false;
        char fail_msg[PATH_MAX + 64] = "";
        for (size_t i = 0; i < state->dir_list.count; ++i) {
            FileEntry *entry = &state->dir_list.entries[i];
            if (entry->is_selected && !is_navigation_entry(entry)) {
                char full_path[PATH_MAX];
                utils_join_path(full_path, sizeof(full_path), state->current_path, entry->name);
                if (!fs_delete(full_path)) {
                    any_fail = true;
                    snprintf(fail_msg, sizeof(fail_msg), "%s: %s", entry->name, strerror(errno));
                }
            }
        }
        if (any_fail) {
            cb_show_message(callbacks, "Delete failed", fail_msg);
        }
        state_change_dir(state, ".");
    }
}

static void handle_delete_single_item(AppState *state, const InputCallbacks *callbacks) {
    FileEntry *entry = &state->dir_list.entries[state->selected_index];
    if (is_navigation_entry(entry)) {
        cb_show_message(callbacks, "Delete blocked", "Navigation entries cannot be deleted.");
        return;
    }
    char buf[256];
    if (cb_prompt(callbacks, "Delete item? (y/n): ", buf, sizeof(buf)) && (buf[0] == 'y' || buf[0] == 'Y')) {
        char full_path[PATH_MAX];
        utils_join_path(full_path, sizeof(full_path), state->current_path, entry->name);
        if (!fs_delete(full_path)) {
            char msg[PATH_MAX + 64];
            snprintf(msg, sizeof(msg), "%s: %s", entry->name, strerror(errno));
            cb_show_message(callbacks, "Delete failed", msg);
        }
        state_change_dir(state, ".");
    }
}

static void handle_delete_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    if (state->dir_list.count == 0) {
        return;
    }
    size_t selected_count = 0;
    for (size_t i = 0; i < state->dir_list.count; ++i) {
        if (state->dir_list.entries[i].is_selected &&
            !is_navigation_entry(&state->dir_list.entries[i])) {
            selected_count++;
        }
    }
    if (selected_count > 0) {
        handle_delete_selected_items(state, callbacks, selected_count);
    } else {
        handle_delete_single_item(state, callbacks);
    }
}

static void handle_rename_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    if (state->dir_list.count == 0) {
        return;
    }
    FileEntry *entry = &state->dir_list.entries[state->selected_index];
    char new_name[256];
    if (is_navigation_entry(entry)) {
        cb_show_message(callbacks, "Rename blocked", "Navigation entries cannot be renamed.");
        return;
    }
    if (cb_prompt(callbacks, "New name: ", new_name, sizeof(new_name))) {
        char old_path[PATH_MAX], new_path[PATH_MAX];
        utils_join_path(old_path, sizeof(old_path), state->current_path, entry->name);
        utils_join_path(new_path, sizeof(new_path), state->current_path, new_name);
        if (!fs_rename(old_path, new_path)) {
            char msg[PATH_MAX + 64];
            snprintf(msg, sizeof(msg), "%s: %s", new_name, strerror(errno));
            cb_show_message(callbacks, "Rename failed", msg);
        }
        state_change_dir(state, ".");
    }
}

static void handle_create_file_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    char name[256];
    if (cb_prompt(callbacks, "New file name: ", name, sizeof(name))) {
        char full_path[PATH_MAX];
        utils_join_path(full_path, sizeof(full_path), state->current_path, name);
        if (!fs_create_file(full_path)) {
            char msg[PATH_MAX + 64];
            snprintf(msg, sizeof(msg), "%s: %s", name, strerror(errno));
            cb_show_message(callbacks, "Create file failed", msg);
        }
        state_change_dir(state, ".");
    }
}

static void handle_create_dir_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    char name[256];
    if (cb_prompt(callbacks, "New directory name: ", name, sizeof(name))) {
        char full_path[PATH_MAX];
        utils_join_path(full_path, sizeof(full_path), state->current_path, name);
        if (!fs_create_dir(full_path)) {
            char msg[PATH_MAX + 64];
            snprintf(msg, sizeof(msg), "%s: %s", name, strerror(errno));
            cb_show_message(callbacks, "Create directory failed", msg);
        }
        state_change_dir(state, ".");
    }
}

static void handle_sort_name_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)callbacks;
    (void)ch;
    state->sort_type = SORT_NAME;
    fs_sort_dir_list(&state->dir_list, state->sort_type);
}

static void handle_sort_size_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)callbacks;
    (void)ch;
    state->sort_type = SORT_SIZE;
    fs_sort_dir_list(&state->dir_list, state->sort_type);
}

static void handle_sort_date_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)callbacks;
    (void)ch;
    state->sort_type = SORT_DATE;
    fs_sort_dir_list(&state->dir_list, state->sort_type);
}

static void handle_copy_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)ch;
    if (state->dir_list.count > 0) {
        if (!capture_clipboard(state, clipboard, false) &&
            is_navigation_entry(&state->dir_list.entries[state->selected_index])) {
            cb_show_message(callbacks, "Copy blocked", "Navigation entries cannot be copied.");
        }
    }
}

static void handle_cut_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)ch;
    if (state->dir_list.count > 0) {
        if (!capture_clipboard(state, clipboard, true) &&
            is_navigation_entry(&state->dir_list.entries[state->selected_index])) {
            cb_show_message(callbacks, "Cut blocked", "Navigation entries cannot be cut.");
        }
    }
}

static void handle_paste_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)ch;
    if (clipboard && clipboard->count > 0) {
        char error_path[PATH_MAX];
        if (!clipboard_apply_operation(clipboard, state->current_path,
                                       error_path, sizeof(error_path))) {
            cb_show_message(callbacks, "Paste failed", error_path);
        }
        state_change_dir(state, ".");
    }
}

static void handle_properties_action(AppState *state, Clipboard *clipboard, const InputCallbacks *callbacks, int ch) {
    (void)clipboard;
    (void)ch;
    if (state->dir_list.count == 0) {
        return;
    }
    FileEntry *entry = &state->dir_list.entries[state->selected_index];
    char size_str[64];
    format_size(entry->size, size_str, sizeof(size_str));

    char date_str[64];
    struct tm *tm_info = localtime(&entry->mtime);
    if (tm_info) {
        strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        snprintf(date_str, sizeof(date_str), "Unknown");
    }

    char msg[512];
    snprintf(msg, sizeof(msg),
             "Name: %s\n"
             "Type: %s\n"
             "Size: %s\n"
             "Modified: %s",
             entry->name,
             entry->is_dir ? "Directory" : "File",
             entry->is_dir ? "-" : size_str,
             date_str);

    cb_show_message(callbacks, "Properties", msg);
}

/* Dispatch Table */

typedef struct {
    int key;
    KeyActionHandler handler;
} KeyBinding;

static const KeyBinding key_bindings[] = {
    { DOT_KEY_QUIT, handle_quit_action },
    { DOT_KEY_UP_DIR, handle_move_up_action },
    { KEY_UP, handle_move_up_action },
    { DOT_KEY_DOWN_DIR, handle_move_down_action },
    { KEY_DOWN, handle_move_down_action },
    { '/', handle_filter_action },
    { DOT_KEY_ENTER_DIR, handle_enter_action },
    { KEY_RIGHT, handle_enter_action },
    { DOT_KEY_BACK_DIR, handle_back_action },
    { KEY_LEFT, handle_back_action },
    { KEY_BACKSPACE, handle_back_action },
    { 15, handle_history_back_action },
    { 29, handle_history_forward_action },
    { DOT_KEY_REFRESH, handle_refresh_action },
    { DOT_KEY_SELECT, handle_select_action },
    { DOT_KEY_DELETE_ITEM, handle_delete_action },
    { DOT_KEY_RENAME_ITEM, handle_rename_action },
    { DOT_KEY_CREATE_FILE, handle_create_file_action },
    { DOT_KEY_CREATE_DIR, handle_create_dir_action },
    { DOT_KEY_SORT_NAME, handle_sort_name_action },
    { DOT_KEY_SORT_SIZE, handle_sort_size_action },
    { DOT_KEY_SORT_DATE, handle_sort_date_action },
    { DOT_KEY_COPY, handle_copy_action },
    { DOT_KEY_CUT, handle_cut_action },
    { DOT_KEY_PASTE, handle_paste_action },
    { DOT_KEY_PROPERTIES, handle_properties_action },
};

void input_handle(AppState *state, Clipboard *clipboard, int ch, const InputCallbacks *callbacks) {
    if (!state) return;

    for (size_t i = 0; i < sizeof(key_bindings) / sizeof(key_bindings[0]); ++i) {
        if (key_bindings[i].key == ch) {
            key_bindings[i].handler(state, clipboard, callbacks, ch);
            return;
        }
    }
}
