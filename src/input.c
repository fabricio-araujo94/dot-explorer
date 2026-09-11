#include "input.h"
#include "config.h"
#include "ui.h"
#include "utils.h"
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

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
    int list_height = max_y - 1;

    switch (ch) {
        case KEY_QUIT:
            state->should_quit = true;
            break;
            
        case KEY_UP_DIR:
        case KEY_UP:
            if (state->selected_index > 0) {
                state->selected_index--;
                if (state->selected_index < state->scroll_offset) {
                    state->scroll_offset = state->selected_index;
                }
            }
            break;

        case KEY_DOWN_DIR:
        case KEY_DOWN:
            if (state->selected_index < state->dir_list.count - 1) {
                state->selected_index++;
                if (state->selected_index >= state->scroll_offset + list_height) {
                    state->scroll_offset = state->selected_index - list_height + 1;
                }
            }
            break;

        case KEY_ENTER_DIR:
        case KEY_RIGHT:
            if (state->dir_list.count > 0) {
                const FileEntry *entry = &state->dir_list.entries[state->selected_index];
                if (entry->is_dir) {
                    state_change_dir(state, entry->name);
                }
            }
            break;

        case KEY_BACK_DIR:
        case KEY_LEFT:
            state_change_dir(state, "..");
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
                char buf[256];
                if (ui_prompt("Delete item? (y/n): ", buf, sizeof(buf)) && (buf[0] == 'y' || buf[0] == 'Y')) {
                    FileEntry *entry = &state->dir_list.entries[state->selected_index];
                    char full_path[1024];
                    snprintf(full_path, sizeof(full_path), "%s/%s", state->current_path, entry->name);
                    fs_delete(full_path);
                    state_change_dir(state, ".");
                }
            }
            break;

        case KEY_RENAME_ITEM:
            if (state->dir_list.count > 0) {
                char new_name[256];
                if (ui_prompt("New name: ", new_name, sizeof(new_name))) {
                    FileEntry *entry = &state->dir_list.entries[state->selected_index];
                    char old_path[1024], new_path[1024];
                    snprintf(old_path, sizeof(old_path), "%s/%s", state->current_path, entry->name);
                    snprintf(new_path, sizeof(new_path), "%s/%s", state->current_path, new_name);
                    fs_rename(old_path, new_path);
                    state_change_dir(state, ".");
                }
            }
            break;

        case KEY_CREATE_FILE:
            {
                char name[256];
                if (ui_prompt("New file name: ", name, sizeof(name))) {
                    char full_path[1024];
                    snprintf(full_path, sizeof(full_path), "%s/%s", state->current_path, name);
                    fs_create_file(full_path);
                    state_change_dir(state, ".");
                }
            }
            break;

        case KEY_CREATE_DIR:
            {
                char name[256];
                if (ui_prompt("New directory name: ", name, sizeof(name))) {
                    char full_path[1024];
                    snprintf(full_path, sizeof(full_path), "%s/%s", state->current_path, name);
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
                capture_clipboard(state, false);
            }
            break;

        case KEY_CUT:
            if (state->dir_list.count > 0) {
                capture_clipboard(state, true);
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
