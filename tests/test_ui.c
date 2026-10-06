#include "ui.h"
#include "input.h"
#include "fs.h"
#include "task.h"
#include "utils.h"
#include "utils/theme.h"
#include "config.h"
#include "test_helpers.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char TEST_UI_ROOT[PATH_MAX];

static void setup_env(void) {
    assert(test_get_temp_dir(TEST_UI_ROOT, sizeof(TEST_UI_ROOT), "dot-explorer-ui-test"));
    fs_delete_recursive(TEST_UI_ROOT);
    assert(platform_mkdir(TEST_UI_ROOT, 0700) == 0);
}

static void teardown_env(void) {
    fs_delete_recursive(TEST_UI_ROOT);
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(content, 1, strlen(content), f) == strlen(content));
    assert(fclose(f) == 0);
}

static void test_dual_init_null(void) {
    assert(!ui_dual_init(NULL));
}

static void test_dual_init_and_cleanup(void) {
    DualPaneUI ui;
    assert(ui_dual_init(&ui));

    assert(ui.active_pane_index == 0);
    assert(ui.task_destination_pane == -1);
    assert(!ui.task_refresh_pending);
    assert(ui.panes[0].state.current_path[0] != '\0');
    assert(ui.panes[1].state.current_path[0] != '\0');
    assert(ui.clipboard.count == 0);
    assert(!task_is_running(&ui.task));

    ui_dual_cleanup(&ui);
    assert(ui.clipboard.count == 0);
}

static void test_tab_pane_switching(void) {
    DualPaneUI ui;
    assert(ui_dual_init(&ui));

    assert(ui.active_pane_index == 0);

    /* Switch to right pane */
    ui_handle_input(&ui, '\t');
    assert(ui.active_pane_index == 1);

    /* Switch back to left pane */
    ui_handle_input(&ui, '\t');
    assert(ui.active_pane_index == 0);

    ui_dual_cleanup(&ui);
}

static void test_input_delegation_to_active_pane(void) {
    DualPaneUI ui;
    assert(ui_dual_init(&ui));

    assert(!ui.panes[0].state.should_quit);
    assert(!ui.panes[1].state.should_quit);

    /* Quit key on active pane 0 */
    ui_handle_input(&ui, DOT_KEY_QUIT);
    assert(ui.panes[0].state.should_quit);
    assert(!ui.panes[1].state.should_quit);

    /* Reset and switch to pane 1 */
    ui.panes[0].state.should_quit = false;
    ui.active_pane_index = 1;
    ui_handle_input(&ui, DOT_KEY_QUIT);
    assert(ui.panes[1].state.should_quit);
    assert(!ui.panes[0].state.should_quit);

    ui_dual_cleanup(&ui);
}

static void test_async_paste_task_creation_and_reconciliation(void) {
    DualPaneUI ui;
    char dir_left[PATH_MAX], dir_right[PATH_MAX], src_file[PATH_MAX], expected_dest[PATH_MAX];

    setup_env();
    path_join(dir_left, sizeof(dir_left), TEST_UI_ROOT, "left");
    path_join(dir_right, sizeof(dir_right), TEST_UI_ROOT, "right");
    assert(platform_mkdir(dir_left, 0700) == 0);
    assert(platform_mkdir(dir_right, 0700) == 0);

    path_join(src_file, sizeof(src_file), dir_left, "file_to_copy.txt");
    path_join(expected_dest, sizeof(expected_dest), dir_right, "file_to_copy.txt");
    write_file(src_file, "payload for copy");

    assert(ui_dual_init(&ui));
    assert(state_change_dir(&ui.panes[0].state, dir_left));
    assert(state_change_dir(&ui.panes[1].state, dir_right));

    /* Add single file to clipboard as COPY (is_cut = false) */
    assert(clipboard_add_entry(&ui.clipboard, src_file));
    ui.clipboard.is_cut = false;

    /* Switch to pane 1 (destination) */
    ui.active_pane_index = 1;

    /* Press DOT_KEY_PASTE -> triggers async background copy task */
    ui_handle_input(&ui, DOT_KEY_PASTE);

    assert(ui.task_destination_pane == 1);
    assert(ui.task_refresh_pending == true);

    /* Wait for task to finish */
    for (int i = 0; i < 500 && task_is_running(&ui.task); ++i) {
        usleep(5000);
    }

    /* Update UI: reconciles completed task and refreshes pane 1 */
    ui_update(&ui);

    assert(!ui.task_refresh_pending);
    assert(!task_is_running(&ui.task));
    assert(access(expected_dest, F_OK) == 0);

    /* Pane 1 should have refreshed and include file_to_copy.txt */
    bool found = false;
    for (size_t i = 0; i < ui.panes[1].state.dir_list.count; ++i) {
        if (strcmp(ui.panes[1].state.dir_list.entries[i].name, "file_to_copy.txt") == 0) {
            found = true;
            break;
        }
    }
    assert(found);

    ui_dual_cleanup(&ui);
    teardown_env();
}

static void test_running_task_esc_cancels(void) {
    DualPaneUI ui;
    char dir_left[PATH_MAX], dir_right[PATH_MAX], src_file[PATH_MAX], dest_file[PATH_MAX];

    setup_env();
    path_join(dir_left, sizeof(dir_left), TEST_UI_ROOT, "left_cancel");
    path_join(dir_right, sizeof(dir_right), TEST_UI_ROOT, "right_cancel");
    assert(platform_mkdir(dir_left, 0700) == 0);
    assert(platform_mkdir(dir_right, 0700) == 0);

    path_join(src_file, sizeof(src_file), dir_left, "big.txt");
    path_join(dest_file, sizeof(dest_file), dir_right, "big.txt");
    write_file(src_file, "some data");

    assert(ui_dual_init(&ui));
    assert(state_change_dir(&ui.panes[0].state, dir_left));
    assert(state_change_dir(&ui.panes[1].state, dir_right));

    /* Start copy manually */
    assert(task_start_copy(&ui.task, src_file, dest_file));

    /* If task is running, press ESC (27) */
    ui_handle_input(&ui, 27);
    TaskStatus status;
    task_snapshot(&ui.task, &status, NULL, NULL, NULL, 0);
    assert(status == TASK_CANCELLED || !task_is_running(&ui.task) || status == TASK_RUNNING);

    /* Wait and update */
    for (int i = 0; i < 500 && task_is_running(&ui.task); ++i) {
        usleep(5000);
    }
    ui_update(&ui);

    ui_dual_cleanup(&ui);
    teardown_env();
}

static void test_sync_paste_cut_operation(void) {
    DualPaneUI ui;
    char dir_left[PATH_MAX], dir_right[PATH_MAX], src_file[PATH_MAX], dest_file[PATH_MAX];

    setup_env();
    path_join(dir_left, sizeof(dir_left), TEST_UI_ROOT, "left_cut");
    path_join(dir_right, sizeof(dir_right), TEST_UI_ROOT, "right_cut");
    assert(platform_mkdir(dir_left, 0700) == 0);
    assert(platform_mkdir(dir_right, 0700) == 0);

    path_join(src_file, sizeof(src_file), dir_left, "cut_file.txt");
    path_join(dest_file, sizeof(dest_file), dir_right, "cut_file.txt");
    write_file(src_file, "cut content");

    assert(ui_dual_init(&ui));
    assert(state_change_dir(&ui.panes[0].state, dir_left));
    assert(state_change_dir(&ui.panes[1].state, dir_right));

    /* Add to clipboard as cut */
    assert(clipboard_add_entry(&ui.clipboard, src_file));
    ui.clipboard.is_cut = true;

    /* Switch to pane 1 */
    ui.active_pane_index = 1;

    /* Paste cut file */
    ui_handle_input(&ui, DOT_KEY_PASTE);

    /* Original must be removed, destination must exist */
    assert(access(src_file, F_OK) != 0);
    assert(access(dest_file, F_OK) == 0);
    assert(ui.clipboard.count == 0);

    ui_dual_cleanup(&ui);
    teardown_env();
}

static void test_theme_and_file_icons(void) {
    FileTheme theme_dir = get_file_color_and_icon(S_IFDIR | 0755, "");
    assert(theme_dir.color_pair == THEME_PAIR_DIRECTORY);
    assert(theme_dir.icon != NULL);

    FileTheme theme_img = get_file_color_and_icon(S_IFREG | 0644, ".png");
    assert(theme_img.color_pair == THEME_PAIR_IMAGE);

    FileTheme theme_archive = get_file_color_and_icon(S_IFREG | 0644, ".zip");
    assert(theme_archive.color_pair == THEME_PAIR_ARCHIVE);

    FileTheme theme_gz = get_file_color_and_icon(S_IFREG | 0644, ".gz");
    assert(theme_gz.color_pair == THEME_PAIR_ARCHIVE);

    FileTheme theme_normal = get_file_color_and_icon(S_IFREG | 0644, ".unknownext");
    assert(theme_normal.color_pair == THEME_PAIR_NORMAL);
}

int main(void) {
    test_dual_init_null();
    test_dual_init_and_cleanup();
    test_tab_pane_switching();
    test_input_delegation_to_active_pane();
    test_async_paste_task_creation_and_reconciliation();
    test_running_task_esc_cancels();
    test_sync_paste_cut_operation();
    test_theme_and_file_icons();

    printf("test_ui: all tests passed\n");
    return 0;
}
