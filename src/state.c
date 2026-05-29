#include "state.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void state_init(AppState *state) {
    if (getcwd(state->current_path, sizeof(state->current_path)) == NULL) {
        strcpy(state->current_path, "/");
    }
    fs_init_dir_list(&state->dir_list);
    state->selected_index = 0;
    state->scroll_offset = 0;
    state->should_quit = false;
    state->sort_type = SORT_NAME;
    state->clipboard_op = CLIPBOARD_NONE;
    state->clipboard_path[0] = '\0';

    fs_read_dir(state->current_path, &state->dir_list);
    fs_sort_dir_list(&state->dir_list, state->sort_type);
}

void state_cleanup(AppState *state) {
    fs_free_dir_list(&state->dir_list);
}
void state_change_dir(AppState *state, const char *new_path) {
    char target_path[PATH_MAX];

    if (new_path[0] == '/') {
        strncpy(target_path, new_path, sizeof(target_path) - 1);
    } else {
        snprintf(target_path, sizeof(target_path), "%s/%s", state->current_path, new_path);
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
    } else {
        fs_free_dir_list(&new_list);
    }
}
