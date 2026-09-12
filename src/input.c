#include "input.h"
#include "config.h"
#include "ui.h"
#include "utils.h"
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

static int escape_sequence_state;

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

static void filter_prompt(AppState *state) {
    int length = 0;
    state->filter_active = true;
    state->filter_query[0] = '\0';
    filter_entries(state->filter_query, &state->dir_list, &state->filtered_entries);
    curs_set(1);
    for (;;) {
        int ch;
        ui_render_filter_prompt(state->filter_query);
        ch = getch();
        if (ch == 27) {
            state->filter_active = false;
            state->filter_query[0] = '\0';
            entry_list_clear(&state->filtered_entries);
            break;
        }
        if (ch == '\n' || ch == KEY_ENTER) break;
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
    curs_set(0);
}
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

    def_prog_mode();
    endwin();

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

    reset_prog_mode();
    keypad(stdscr, TRUE);
    clear();
    refresh();
    if (!success) {
        errno = saved_errno ? saved_errno : EIO;
    }
    return success;
}

static bool capture_clipboard(AppState *state, bool is_cut) {
    bool has_selected = false;

    clipboard_clear(&state->clipboard);
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
            !clipboard_add_entry(&state->clipboard, path)) {
            clipboard_clear(&state->clipboard);
            return false;
        }
    }
    state->clipboard.is_cut = is_cut;
    return state->clipboard.count > 0;
}

void input_handle(AppState *state, int ch) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_x;
    int list_height = max_y - 1;

    if (escape_sequence_state == 0 && ch == 27) {
        escape_sequence_state = 1;
        return;
    }
    if (escape_sequence_state == 1) {
        if (ch == '[') {
            escape_sequence_state = 2;
            return;
        }
        escape_sequence_state = 0;
    } else if (escape_sequence_state == 2) {
        escape_sequence_state = 0;
        if (ch == 'D') {
            navigate_history(state, false);
            return;
        }
        if (ch == 'C') {
            navigate_history(state, true);
            return;
        }
    }

    switch (ch) {
        case KEY_QUIT:
            state->should_quit = true;
            break;
            
        case KEY_UP_DIR:
        case KEY_UP:
            move_visible_selection(state, current_view_index(state) - 1, list_height);
            break;

        case KEY_DOWN_DIR:
        case KEY_DOWN:
            move_visible_selection(state, current_view_index(state) + 1, list_height);
            break;

        case '/':
            filter_prompt(state);
            break;

        case KEY_ENTER_DIR:
        case KEY_RIGHT:
            if (state->dir_list.count > 0) {
                const FileEntry *entry = &state->dir_list.entries[state->selected_index];
                if (entry->is_dir) {
                    history_push(state, state->current_path, state->selected_index);
                    state_change_dir(state, entry->name);
                } else {
                    char path[PATH_MAX];
                    if (utils_join_path(path, sizeof(path), state->current_path, entry->name) &&
                        !open_file_with_editor(path)) {
                        char message[PATH_MAX + 64];
                        snprintf(message, sizeof(message), "%s: %s", path, strerror(errno));
                        ui_show_message("Open failed", message);
                    }
                }
            }
            break;

        case KEY_BACK_DIR:
        case KEY_LEFT:
        case KEY_BACKSPACE:
            state_change_dir(state, "..");
            break;

        case 15:
            navigate_history(state, false);
            break;

        case 9:
            navigate_history(state, true);
            break;
            
        case KEY_REFRESH:
            state_change_dir(state, ".");
            break;

        case KEY_SELECT:
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

        case KEY_DELETE_ITEM:
            if (state->dir_list.count > 0) {
                FileEntry *entry = &state->dir_list.entries[state->selected_index];
                char buf[256];
                if (is_navigation_entry(entry)) {
                    ui_show_message("Delete blocked", "Navigation entries cannot be deleted.");
                    break;
                }
                if (ui_prompt("Delete item? (y/n): ", buf, sizeof(buf)) && (buf[0] == 'y' || buf[0] == 'Y')) {
                    char full_path[PATH_MAX];
                    utils_join_path(full_path, sizeof(full_path), state->current_path, entry->name);
                    fs_delete(full_path);
                    state_change_dir(state, ".");
                }
            }
            break;

        case KEY_RENAME_ITEM:
            if (state->dir_list.count > 0) {
                FileEntry *entry = &state->dir_list.entries[state->selected_index];
                char new_name[256];
                if (is_navigation_entry(entry)) {
                    ui_show_message("Rename blocked", "Navigation entries cannot be renamed.");
                    break;
                }
                if (ui_prompt("New name: ", new_name, sizeof(new_name))) {
                    char old_path[PATH_MAX], new_path[PATH_MAX];
                    utils_join_path(old_path, sizeof(old_path), state->current_path, entry->name);
                    utils_join_path(new_path, sizeof(new_path), state->current_path, new_name);
                    fs_rename(old_path, new_path);
                    state_change_dir(state, ".");
                }
            }
            break;

        case KEY_CREATE_FILE:
            {
                char name[256];
                if (ui_prompt("New file name: ", name, sizeof(name))) {
                    char full_path[PATH_MAX];
                    utils_join_path(full_path, sizeof(full_path), state->current_path, name);
                    fs_create_file(full_path);
                    state_change_dir(state, ".");
                }
            }
            break;

        case KEY_CREATE_DIR:
            {
                char name[256];
                if (ui_prompt("New directory name: ", name, sizeof(name))) {
                    char full_path[PATH_MAX];
                    utils_join_path(full_path, sizeof(full_path), state->current_path, name);
                    fs_create_dir(full_path);
                    state_change_dir(state, ".");
                }
            }
            break;

        case KEY_SORT_NAME:
            state->sort_type = SORT_NAME;
            fs_sort_dir_list(&state->dir_list, state->sort_type);
            break;

        case KEY_SORT_SIZE:
            state->sort_type = SORT_SIZE;
            fs_sort_dir_list(&state->dir_list, state->sort_type);
            break;

        case KEY_SORT_DATE:
            state->sort_type = SORT_DATE;
            fs_sort_dir_list(&state->dir_list, state->sort_type);
            break;

        case KEY_COPY:
            if (state->dir_list.count > 0) {
                if (!capture_clipboard(state, false) &&
                    is_navigation_entry(&state->dir_list.entries[state->selected_index])) {
                    ui_show_message("Copy blocked", "Navigation entries cannot be copied.");
                }
            }
            break;

        case KEY_CUT:
            if (state->dir_list.count > 0) {
                if (!capture_clipboard(state, true) &&
                    is_navigation_entry(&state->dir_list.entries[state->selected_index])) {
                    ui_show_message("Cut blocked", "Navigation entries cannot be cut.");
                }
            }
            break;

        case KEY_PASTE:
            if (state->clipboard.count > 0) {
                char error_path[PATH_MAX];
                if (!clipboard_apply_operation(&state->clipboard, state->current_path,
                                               error_path, sizeof(error_path))) {
                    ui_show_message("Paste failed", error_path);
                }
                state_change_dir(state, ".");
            }
            break;

        case KEY_PROPERTIES:
            if (state->dir_list.count > 0) {
                FileEntry *entry = &state->dir_list.entries[state->selected_index];
                char size_str[64];
                format_size(entry->size, size_str, sizeof(size_str));
                
                char date_str[64];
                struct tm *tm_info = localtime(&entry->mtime);
                strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);

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
                
                ui_show_message("Properties", msg);
            }
            break;

        default:
            break;
    }
}
