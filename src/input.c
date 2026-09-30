#include "input.h"
#include "config.h"
#include "utils.h"
#include "fs.h"

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <sys/wait.h>
#include <wordexp.h>
#include <unistd.h>
#else
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#endif

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
        if (selected_index >= state->dir_list.count) {
            selected_index = state->dir_list.count - 1;
        }
        state->selected_index = selected_index;
    } else {
        state->selected_index = 0;
    }
}

static int current_view_index(const AppState *state) {
    if (!state->filter_active) return state->selected_index;
    for (int i = 0; i < state->filtered_entries.count; ++i) {
        if (state->filtered_entries.indices[i] == state->selected_index) return i;
    }
    return -1;
}

static void move_visible_selection(AppState *state, int view_index, int list_height) {
    int visible_count = state_visible_count(state);
    int original_index;
    if (visible_count == 0) {
        state->selected_index = 0;
        state->scroll_offset = 0;
        return;
    }
    if (view_index < 0) view_index = 0;
    if (view_index >= visible_count) view_index = visible_count - 1;
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

bool open_file_with_editor(const char *path) {
    struct stat file_stat;
    bool success = false;
    int saved_errno = 0;

    if (!path || stat(path, &file_stat) != 0) {
        return false;
    }
    if (!S_ISREG(file_stat.st_mode)) {
        errno = EISDIR;
        return false;
    }
    if (access(path, R_OK) != 0) {
        return false;
    }

#ifdef _WIN32
    {
        HINSTANCE result = ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
        success = (INT_PTR)result > 32;
        if (!success) {
            errno = EIO;
        }
    }
#else
    {
        const char *editor = getenv("EDITOR");
        wordexp_t words;
        char **editor_argv = NULL;
        size_t editor_argc = 0;
        pid_t child;
        int status;

        memset(&words, 0, sizeof(words));
        if (editor && *editor && wordexp(editor, &words, WRDE_NOCMD) == 0 &&
            words.we_wordc > 0) {
            editor_argc = words.we_wordc;
            editor_argv = calloc(editor_argc + 2, sizeof(*editor_argv));
            if (editor_argv) {
                for (size_t i = 0; i < editor_argc; ++i) {
                    editor_argv[i] = words.we_wordv[i];
                }
                editor_argv[editor_argc] = (char *)path;
            }
        }

        child = fork();
        if (child == 0) {
            if (editor_argv) {
                execvp(editor_argv[0], editor_argv);
            }
            {
                char *fallback[] = { (char *)"nano", (char *)path, NULL };
                execvp(fallback[0], fallback);
            }
            {
                char *fallback[] = { (char *)"vi", (char *)path, NULL };
                execvp(fallback[0], fallback);
            }
            _exit(127);
        }
        if (child < 0) {
            saved_errno = errno;
        } else {
            do {
                success = waitpid(child, &status, 0) >= 0;
            } while (!success && errno == EINTR);
            if (success) {
                success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                if (!success) {
                    saved_errno = WIFEXITED(status) ? EIO : EINTR;
                }
            } else {
                saved_errno = errno;
            }
        }
        free(editor_argv);
        if (editor && *editor && words.we_wordc > 0) {
            wordfree(&words);
        }
    }
#endif

    if (!success) {
        errno = saved_errno ? saved_errno : EIO;
    }
    return success;
}

static bool capture_clipboard(AppState *state, Clipboard *clipboard, bool is_cut) {
    bool has_selected = false;

    if (!clipboard) return false;
    clipboard_clear(clipboard);
    for (int i = 0; i < state->dir_list.count; ++i) {
        if (state->dir_list.entries[i].is_selected) {
            has_selected = true;
            break;
        }
    }
    for (int i = 0; i < state->dir_list.count; ++i) {
        char path[PATH_MAX];
        if (has_selected && !state->dir_list.entries[i].is_selected) {
            continue;
        }
        if (!has_selected && i != state->selected_index) {
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

void input_handle(AppState *state, Clipboard *clipboard, int ch, const InputCallbacks *callbacks) {
    int list_height = cb_get_list_height(callbacks);

    switch (ch) {
        case DOT_KEY_QUIT:
            state->should_quit = true;
            break;
            
        case DOT_KEY_UP_DIR:
        case KEY_UP:
            move_visible_selection(state, current_view_index(state) - 1, list_height);
            break;

        case DOT_KEY_DOWN_DIR:
        case KEY_DOWN:
            move_visible_selection(state, current_view_index(state) + 1, list_height);
            break;

        case '/':
            filter_prompt(state, callbacks);
            break;

        case DOT_KEY_ENTER_DIR:
        case KEY_RIGHT:
            if (state->dir_list.count > 0) {
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
            break;

        case DOT_KEY_BACK_DIR:
        case KEY_LEFT:
        case KEY_BACKSPACE:
            if (!state_change_dir(state, "..")) {
                char message[PATH_MAX + 64];
                snprintf(message, sizeof(message), "..: %s", strerror(errno));
                cb_show_message(callbacks, "Navigation failed", message);
            }
            break;

        case 15:
            navigate_history(state, false);
            break;

        /* Forward history: Tab (9) is consumed by ui_handle_input for
           pane switching, so we use Ctrl+] (29) instead. */
        case 29:
            navigate_history(state, true);
            break;
            
        case DOT_KEY_REFRESH:
            if (!state_change_dir(state, ".")) {
                char message[PATH_MAX + 64];
                snprintf(message, sizeof(message), "%s", strerror(errno));
                cb_show_message(callbacks, "Refresh failed", message);
            }
            break;

        case DOT_KEY_SELECT:
            if (state->dir_list.count > 0) {
                FileEntry *entry = &state->dir_list.entries[state->selected_index];
                entry->is_selected = !entry->is_selected;
                if (state->selected_index < state->dir_list.count - 1) {
                    state->selected_index++;
                    if (state->selected_index >= state->scroll_offset + list_height) {
                        state->scroll_offset = state->selected_index - list_height + 1;
                    }
                }
            }
            break;

        case DOT_KEY_DELETE_ITEM:
            if (state->dir_list.count > 0) {
                int selected_count = 0;
                for (int i = 0; i < state->dir_list.count; ++i) {
                    if (state->dir_list.entries[i].is_selected &&
                        !is_navigation_entry(&state->dir_list.entries[i])) {
                        selected_count++;
                    }
                }

                char prompt_msg[256];
                char buf[256];

                if (selected_count > 0) {
                    snprintf(prompt_msg, sizeof(prompt_msg),
                             "Delete %d selected item%s? (y/n): ",
                             selected_count, selected_count > 1 ? "s" : "");
                    if (cb_prompt(callbacks, prompt_msg, buf, sizeof(buf)) && (buf[0] == 'y' || buf[0] == 'Y')) {
                        bool any_fail = false;
                        char fail_msg[PATH_MAX + 64] = "";
                        for (int i = 0; i < state->dir_list.count; ++i) {
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
                } else {
                    FileEntry *entry = &state->dir_list.entries[state->selected_index];
                    if (is_navigation_entry(entry)) {
                        cb_show_message(callbacks, "Delete blocked", "Navigation entries cannot be deleted.");
                        break;
                    }
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
            }
            break;

        case DOT_KEY_RENAME_ITEM:
            if (state->dir_list.count > 0) {
                FileEntry *entry = &state->dir_list.entries[state->selected_index];
                char new_name[256];
                if (is_navigation_entry(entry)) {
                    cb_show_message(callbacks, "Rename blocked", "Navigation entries cannot be renamed.");
                    break;
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
            break;

        case DOT_KEY_CREATE_FILE:
            {
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
            break;

        case DOT_KEY_CREATE_DIR:
            {
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
            break;

        case DOT_KEY_SORT_NAME:
            state->sort_type = SORT_NAME;
            fs_sort_dir_list(&state->dir_list, state->sort_type);
            break;

        case DOT_KEY_SORT_SIZE:
            state->sort_type = SORT_SIZE;
            fs_sort_dir_list(&state->dir_list, state->sort_type);
            break;

        case DOT_KEY_SORT_DATE:
            state->sort_type = SORT_DATE;
            fs_sort_dir_list(&state->dir_list, state->sort_type);
            break;

        case DOT_KEY_COPY:
            if (state->dir_list.count > 0) {
                if (!capture_clipboard(state, clipboard, false) &&
                    is_navigation_entry(&state->dir_list.entries[state->selected_index])) {
                    cb_show_message(callbacks, "Copy blocked", "Navigation entries cannot be copied.");
                }
            }
            break;

        case DOT_KEY_CUT:
            if (state->dir_list.count > 0) {
                if (!capture_clipboard(state, clipboard, true) &&
                    is_navigation_entry(&state->dir_list.entries[state->selected_index])) {
                    cb_show_message(callbacks, "Cut blocked", "Navigation entries cannot be cut.");
                }
            }
            break;

        case DOT_KEY_PASTE:
            if (clipboard && clipboard->count > 0) {
                char error_path[PATH_MAX];
                if (!clipboard_apply_operation(clipboard, state->current_path,
                                               error_path, sizeof(error_path))) {
                    cb_show_message(callbacks, "Paste failed", error_path);
                }
                state_change_dir(state, ".");
            }
            break;

        case DOT_KEY_PROPERTIES:
            if (state->dir_list.count > 0) {
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
            break;

        default:
            break;
    }
}
