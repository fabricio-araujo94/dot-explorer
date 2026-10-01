#include "state.h"
#include "fs.h"
#include "utils.h"
#include "test_helpers.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char TEST_STATE_ROOT[PATH_MAX];

static void setup_state_env(void) {
    assert(test_get_temp_dir(TEST_STATE_ROOT, sizeof(TEST_STATE_ROOT),
                             "dot-explorer-state-test"));
    fs_delete_recursive(TEST_STATE_ROOT);
    assert(platform_mkdir(TEST_STATE_ROOT, 0700) == 0);
}

static void teardown_state_env(void) {
    assert(fs_delete_recursive(TEST_STATE_ROOT));
}

static void write_dummy_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(content, 1, strlen(content), f) == strlen(content));
    assert(fclose(f) == 0);
}

static void test_state_init_and_cleanup(void) {
    AppState state;
    state_init(&state);
    assert(state.dir_list.entries != NULL || state.dir_list.count == 0);
    assert(state.selected_index == 0);
    assert(state.scroll_offset == 0);
    assert(state.should_quit == false);
    assert(state.sort_type == SORT_NAME);
    assert(state.filter_active == false);
    assert(state.history.back_count == 0);
    assert(state.history.forward_count == 0);

    state_cleanup(&state);
    assert(state.dir_list.entries == NULL);
    assert(state.dir_list.count == 0);
}

static void test_fuzzy_filtering(void) {
    AppState state;
    char file1[PATH_MAX], file2[PATH_MAX], file3[PATH_MAX];

    setup_state_env();
    assert(utils_join_path(file1, sizeof(file1), TEST_STATE_ROOT, "config_prod.json"));
    assert(utils_join_path(file2, sizeof(file2), TEST_STATE_ROOT, "config_dev.json"));
    assert(utils_join_path(file3, sizeof(file3), TEST_STATE_ROOT, "document.pdf"));
    write_dummy_file(file1, "{}");
    write_dummy_file(file2, "{}");
    write_dummy_file(file3, "%PDF");

    state_init(&state);
    assert(state_change_dir(&state, TEST_STATE_ROOT));

    /* Test filter with "cfg" -> matches config_prod.json and config_dev.json */
    assert(filter_entries("cfg", &state.dir_list, &state.filtered_entries));
    assert(state.filtered_entries.count == 2);

    state.filter_active = true;
    assert(state_visible_count(&state) == 2);
    int idx0 = state_visible_index(&state, 0);
    int idx1 = state_visible_index(&state, 1);
    assert(idx0 >= 0 && idx1 >= 0);
    assert(strstr(state.dir_list.entries[idx0].name, "config_") != NULL);
    assert(strstr(state.dir_list.entries[idx1].name, "config_") != NULL);

    /* Test invalid view_index */
    assert(state_visible_index(&state, -1) == -1);
    assert(state_visible_index(&state, 99) == -1);

    /* Test filter with query matching nothing */
    assert(filter_entries("nonexistent", &state.dir_list, &state.filtered_entries));
    assert(state.filtered_entries.count == 0);
    assert(state_visible_count(&state) == 0);

    state.filter_active = false;
    assert(state_visible_count(&state) == state.dir_list.count);

    state_cleanup(&state);
    teardown_state_env();
}

static void test_navigation_history_stack(void) {
    AppState state;
    char path[PATH_MAX];
    int sel_idx = -1;

    setup_state_env();
    state_init(&state);

    /* Initial history is empty */
    assert(!history_pop_back(&state, path, sizeof(path), &sel_idx));
    assert(!history_pop_forward(&state, path, sizeof(path), &sel_idx));

    /* Push history locations */
    assert(history_push(&state, "/dir1", 2));
    assert(state.history.back_count == 1);
    assert(state.history.forward_count == 0);

    assert(history_push(&state, "/dir2", 5));
    assert(state.history.back_count == 2);

    /* Simulate current path as /dir3, pop back */
    snprintf(state.current_path, sizeof(state.current_path), "/dir3");
    state.selected_index = 0;

    assert(history_pop_back(&state, path, sizeof(path), &sel_idx));
    assert(strcmp(path, "/dir2") == 0);
    assert(sel_idx == 5);
    assert(state.history.back_count == 1);
    assert(state.history.forward_count == 1);

    /* Pop back again to /dir1 */
    snprintf(state.current_path, sizeof(state.current_path), "/dir2");
    state.selected_index = 5;

    assert(history_pop_back(&state, path, sizeof(path), &sel_idx));
    assert(strcmp(path, "/dir1") == 0);
    assert(sel_idx == 2);
    assert(state.history.back_count == 0);
    assert(state.history.forward_count == 2);

    /* Pop forward towards /dir2 */
    snprintf(state.current_path, sizeof(state.current_path), "/dir1");
    state.selected_index = 2;

    assert(history_pop_forward(&state, path, sizeof(path), &sel_idx));
    assert(strcmp(path, "/dir2") == 0);
    assert(sel_idx == 5);
    assert(state.history.back_count == 1);
    assert(state.history.forward_count == 1);

    /* Pushing new navigation clears forward history */
    assert(history_push(&state, "/dir_new", 0));
    assert(state.history.forward_count == 0);
    assert(state.history.back_count == 2);

    state_cleanup(&state);
    teardown_state_env();
}

static void test_clipboard_lifecycle_and_errors(void) {
    Clipboard cb = { 0 };
    char file1[PATH_MAX];
    char error_path[PATH_MAX];

    setup_state_env();
    assert(utils_join_path(file1, sizeof(file1), TEST_STATE_ROOT, "clip_test.txt"));
    write_dummy_file(file1, "sample");

    /* Add entry */
    assert(clipboard_add_entry(&cb, file1));
    assert(cb.count == 1);
    assert(cb.paths != NULL);
    assert(strlen(cb.source_dir) > 0);

    /* Adding duplicate entry fails with EEXIST */
    errno = 0;
    assert(!clipboard_add_entry(&cb, file1));
    assert(errno == EEXIST);

    /* Invalid arguments */
    errno = 0;
    assert(!clipboard_add_entry(NULL, file1));
    assert(errno == EINVAL);
    assert(!clipboard_add_entry(&cb, NULL));
    assert(errno == EINVAL);

    /* Clear clipboard */
    clipboard_clear(&cb);
    assert(cb.count == 0);
    assert(cb.capacity == 0);
    assert(cb.paths == NULL);
    assert(cb.source_dir[0] == '\0');

    /* Apply operation on empty clipboard fails */
    errno = 0;
    assert(!clipboard_apply_operation(&cb, TEST_STATE_ROOT, error_path, sizeof(error_path)));
    assert(errno == EINVAL);

    teardown_state_env();
}

static void test_clipboard_cut_partial_failure(void) {
    Clipboard cb = { 0 };
    char first[PATH_MAX];
    char second[PATH_MAX];
    char destination_dir[PATH_MAX];
    char moved_path[PATH_MAX];
    char error_path[PATH_MAX];
    char missing_path[PATH_MAX];

    setup_state_env();
    assert(utils_join_path(first, sizeof(first), TEST_STATE_ROOT, "first.txt"));
    assert(utils_join_path(second, sizeof(second), TEST_STATE_ROOT, "second.txt"));
    assert(utils_join_path(destination_dir, sizeof(destination_dir), TEST_STATE_ROOT, "destination"));
    assert(utils_join_path(moved_path, sizeof(moved_path), destination_dir, "first.txt"));
    write_dummy_file(first, "first");
    write_dummy_file(second, "second");
    assert(platform_mkdir(destination_dir, 0700) == 0);
    assert(clipboard_add_entry(&cb, first));
    assert(clipboard_add_entry(&cb, second));
    assert(utils_join_path(missing_path, sizeof(missing_path), TEST_STATE_ROOT,
                           "missing.txt"));
    free(cb.paths[1]);
    cb.paths[1] = strdup(missing_path);
    assert(cb.paths[1] != NULL);
    cb.is_cut = true;

    assert(!clipboard_apply_operation(&cb, destination_dir,
                                      error_path, sizeof(error_path)));
    assert(strcmp(error_path, missing_path) == 0);
    assert(cb.count == 1);
    assert(strcmp(cb.paths[0], missing_path) == 0);
    assert(access(first, F_OK) != 0);
    assert(access(moved_path, F_OK) == 0);

    clipboard_clear(&cb);
    teardown_state_env();
}

int main(void) {
    test_state_init_and_cleanup();
    test_fuzzy_filtering();
    test_navigation_history_stack();
    test_clipboard_lifecycle_and_errors();
    test_clipboard_cut_partial_failure();
    puts("test_state: all tests passed");
    return 0;
}
