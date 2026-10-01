#include "fs.h"
#include "state.h"
#include "task.h"
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

static char TEST_ROOT[PATH_MAX];

static void setup(void) {
    assert(test_get_temp_dir(TEST_ROOT, sizeof(TEST_ROOT), "dot-explorer-test"));
    fs_delete_recursive(TEST_ROOT);
    assert(platform_mkdir(TEST_ROOT, 0700) == 0);
}

static void teardown(void) {
    assert(fs_delete_recursive(TEST_ROOT));
}

static void make_path(char *buffer, size_t size, const char *name) {
    assert(utils_join_path(buffer, size, TEST_ROOT, name));
}

static void write_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(contents, 1, strlen(contents), file) == strlen(contents));
    assert(fclose(file) == 0);
}

static void test_delete_circular_symlinks(void) {
    char tree[PATH_MAX];
    char first[PATH_MAX];
    char second[PATH_MAX];
    setup();
    make_path(tree, sizeof(tree), "tree");
    assert(platform_mkdir(tree, 0700) == 0);
    make_path(first, sizeof(first), "tree/first");
    make_path(second, sizeof(second), "tree/second");
#ifdef _WIN32
    (void)first;
    (void)second;
#else
    assert(symlink("second", first) == 0);
    assert(symlink("first", second) == 0);
#endif
    assert(fs_delete_recursive(tree));
    assert(access(tree, F_OK) != 0);
    teardown();
}

static void test_delete_parent_path_is_blocked(void) {
    char parent[PATH_MAX];
    char child[PATH_MAX];
    char sentinel[PATH_MAX];
    char test_paths[][PATH_MAX] = {
        "",
        "",
        "",
        "",
        ""
    };

    setup();
    make_path(parent, sizeof(parent), "parent");
    assert(platform_mkdir(parent, 0700) == 0);
    make_path(child, sizeof(child), "parent/child");
    assert(platform_mkdir(child, 0700) == 0);
    make_path(sentinel, sizeof(sentinel), "parent/important.txt");
    write_file(sentinel, "keep");
    size_t parent_len = strlen(parent);
    assert(parent_len + 16 < PATH_MAX);
    memcpy(test_paths[0], parent, parent_len);
    strcpy(test_paths[0] + parent_len, "/..");
    memcpy(test_paths[1], parent, parent_len);
    strcpy(test_paths[1] + parent_len, "/../");
    memcpy(test_paths[2], parent, parent_len);
    strcpy(test_paths[2] + parent_len, "/..///");
    memcpy(test_paths[3], parent, parent_len);
    strcpy(test_paths[3] + parent_len, "/.");
    memcpy(test_paths[4], parent, parent_len);
    strcpy(test_paths[4] + parent_len, "/.//");

    for (size_t i = 0; i < sizeof(test_paths) / sizeof(test_paths[0]); i++) {
        errno = 0;
        assert(!fs_delete_recursive(test_paths[i]));
        assert(errno == EINVAL);
        errno = 0;
        assert(!fs_delete(test_paths[i]));
        assert(errno == EINVAL);
    }
    assert(access(parent, F_OK) == 0);
    assert(access(sentinel, F_OK) == 0);
    teardown();
}

static void test_copy_into_itself(void) {
    char source[PATH_MAX];
    char nested[PATH_MAX];
    char error_path[PATH_MAX];
    FsCopyOptions options = { false, true, NULL, NULL, NULL, NULL };
    setup();
    make_path(source, sizeof(source), "source.txt");
    write_file(source, "original");
    assert(!fs_copy_recursive_with_options(source, source, &options,
                                           error_path, sizeof(error_path)));
    make_path(nested, sizeof(nested), "folder");
    assert(platform_mkdir(nested, 0700) == 0);
    assert(!fs_copy_recursive_with_options(TEST_ROOT, nested, &options,
                                           error_path, sizeof(error_path)));
    assert(access(nested, F_OK) == 0);
    teardown();
}

static void test_clipboard_apply_between_directories(void) {
    Clipboard clipboard = { 0 };
    char source_dir[PATH_MAX];
    char destination_dir[PATH_MAX];
    char source_file[PATH_MAX];
    char copied_file[PATH_MAX];
    char error_path[PATH_MAX];
    FILE *file;

    setup();
    make_path(source_dir, sizeof(source_dir), "source");
    make_path(destination_dir, sizeof(destination_dir), "destination");
    assert(platform_mkdir(source_dir, 0700) == 0);
    assert(platform_mkdir(destination_dir, 0700) == 0);
    assert(utils_join_path(source_file, sizeof(source_file), source_dir, "item.txt"));
    write_file(source_file, "clipboard");
    assert(clipboard_add_entry(&clipboard, source_file));
    assert(!clipboard.is_cut);
    assert(clipboard_apply_operation(&clipboard, destination_dir,
                                     error_path, sizeof(error_path)));
    assert(utils_join_path(copied_file, sizeof(copied_file), destination_dir, "item.txt"));
    file = fopen(copied_file, "rb");
    assert(file != NULL);
    fclose(file);
    assert(access(source_file, F_OK) == 0);
    clipboard_clear(&clipboard);
    teardown();
}

static void test_clipboard_cut_same_filesystem(void) {
    Clipboard clipboard = { 0 };
    char source_dir[PATH_MAX];
    char destination_dir[PATH_MAX];
    char source_file[PATH_MAX];
    char moved_file[PATH_MAX];
    char error_path[PATH_MAX];

    setup();
    make_path(source_dir, sizeof(source_dir), "cut-source");
    make_path(destination_dir, sizeof(destination_dir), "cut-destination");
    assert(platform_mkdir(source_dir, 0700) == 0);
    assert(platform_mkdir(destination_dir, 0700) == 0);
    assert(utils_join_path(source_file, sizeof(source_file), source_dir, "item.txt"));
    write_file(source_file, "move");
    assert(clipboard_add_entry(&clipboard, source_file));
    clipboard.is_cut = true;
    assert(clipboard_apply_operation(&clipboard, destination_dir,
                                     error_path, sizeof(error_path)));
    assert(utils_join_path(moved_file, sizeof(moved_file), destination_dir, "item.txt"));
    assert(access(moved_file, F_OK) == 0);
    assert(access(source_file, F_OK) != 0);
    assert(clipboard.count == 0);
    teardown();
}

static void test_permission_denied(void) {
    char source[PATH_MAX];
    char destination[PATH_MAX];
    setup();
#ifdef _WIN32
    make_path(source, sizeof(source), "source.txt");
    make_path(destination, sizeof(destination), "missing-parent/copy.txt");
    write_file(source, "data");
    assert(!fs_copy_recursive(source, destination));
    assert(!fs_delete_recursive(destination));
#else
    if (geteuid() == 0) {
        teardown();
        return;
    }
    make_path(source, sizeof(source), "source.txt");
    make_path(destination, sizeof(destination), "copy.txt");
    write_file(source, "data");
    assert(chmod(TEST_ROOT, 0500) == 0);
    assert(!fs_copy_recursive(source, destination));
    assert(!fs_delete_recursive(source));
    assert(chmod(TEST_ROOT, 0700) == 0);
#endif
    teardown();
}

static void test_path_near_path_max(void) {
    char base[PATH_MAX];
    char result[PATH_MAX];
    size_t length = 0;
    setup();
    while (length + 2 < sizeof(base) - 2) {
        base[length++] = 'a';
        base[length++] = '/';
    }
    base[length] = '\0';
    errno = 0;
    assert(!utils_join_path(result, sizeof(result), base, "file"));
    assert(errno == ENAMETOOLONG);
    teardown();
}

static void test_async_task_lifecycle(void) {
    Task task;
    char source[PATH_MAX];
    char destination[PATH_MAX];
    TaskStatus status = TASK_IDLE;
    uint64_t copied = 0;
    uint64_t total = 0;
    char error_path[PATH_MAX];

    setup();
    make_path(source, sizeof(source), "async-source.txt");
    make_path(destination, sizeof(destination), "async-copy.txt");
    write_file(source, "background copy");
    task_init(&task);
    assert(task_start_copy(&task, source, destination));
    task_reap(&task);
    task_snapshot(&task, &status, &copied, &total, error_path, sizeof(error_path));
    assert(status == TASK_COMPLETED);
    assert(total > 0);
    assert(copied == total);
    assert(access(destination, F_OK) == 0);
    task_cleanup(&task);
    teardown();
}

static void test_copy_readonly_directory(void) {
    char src_dir[PATH_MAX];
    char src_file[PATH_MAX];
    char dst_dir[PATH_MAX];
    char dst_file[PATH_MAX];
#ifndef _WIN32
    struct stat st;
#endif

    setup();
#ifdef _WIN32
    DWORD destination_attributes;
    make_path(src_dir, sizeof(src_dir), "readonly_dir");
    make_path(dst_dir, sizeof(dst_dir), "readonly_copy");
    assert(platform_mkdir(src_dir, 0700) == 0);
    assert(utils_join_path(src_file, sizeof(src_file), src_dir, "data.txt"));
    write_file(src_file, "read-only file copy test data");
    assert(SetFileAttributesA(src_file, FILE_ATTRIBUTE_READONLY));
    assert(fs_copy_recursive(src_dir, dst_dir));
    assert(utils_join_path(dst_file, sizeof(dst_file), dst_dir, "data.txt"));
    assert(access(dst_file, F_OK) == 0);
    destination_attributes = GetFileAttributesA(dst_file);
    assert(destination_attributes != INVALID_FILE_ATTRIBUTES);
    assert(destination_attributes & FILE_ATTRIBUTE_READONLY);
    assert(SetFileAttributesA(src_file, FILE_ATTRIBUTE_NORMAL));
    assert(SetFileAttributesA(dst_file, FILE_ATTRIBUTE_NORMAL));
#else
    if (geteuid() == 0) {
        teardown();
        return;
    }
    make_path(src_dir, sizeof(src_dir), "readonly_dir");
    make_path(dst_dir, sizeof(dst_dir), "readonly_copy");
    assert(platform_mkdir(src_dir, 0700) == 0);
    assert(utils_join_path(src_file, sizeof(src_file), src_dir, "data.txt"));
    write_file(src_file, "read-only directory test data");

    /* Set source directory permissions to read-only for owner (0555) */
    assert(chmod(src_dir, 0555) == 0);

    /* Copy recursive should succeed */
    assert(fs_copy_recursive(src_dir, dst_dir));

    /* Check destination file exists and matches */
    assert(utils_join_path(dst_file, sizeof(dst_file), dst_dir, "data.txt"));
    assert(access(dst_file, F_OK) == 0);

    /* Check destination directory permissions preserved final mode (0555) */
    assert(stat(dst_dir, &st) == 0);
    assert((st.st_mode & 07777) == 0555);

    /* Restore write permissions before teardown cleanup */
    assert(chmod(src_dir, 0700) == 0);
    assert(chmod(dst_dir, 0700) == 0);
#endif
    teardown();
}

static void test_sort_dotdot_stays_first(void) {
    DirectoryList list;
    fs_init_dir_list(&list);

    /* Allocate and mock entries: ".." with small/large timestamps & sizes and several directories & files */
    list.count = 5;
    
    /* entry 0: ".." (dir, large size, old timestamp) */
    strcpy(list.entries[0].name, "..");
    list.entries[0].is_dir = true;
    list.entries[0].size = 999999;
    list.entries[0].mtime = 1000;

    /* entry 1: "alpha_dir" (dir, smaller size, newer timestamp) */
    strcpy(list.entries[1].name, "alpha_dir");
    list.entries[1].is_dir = true;
    list.entries[1].size = 10;
    list.entries[1].mtime = 5000;

    /* entry 2: "beta_dir" (dir, large size, newest timestamp) */
    strcpy(list.entries[2].name, "beta_dir");
    list.entries[2].is_dir = true;
    list.entries[2].size = 50000;
    list.entries[2].mtime = 9000;

    /* entry 3: "a_file.txt" (file, small size, newest timestamp) */
    strcpy(list.entries[3].name, "a_file.txt");
    list.entries[3].is_dir = false;
    list.entries[3].size = 5;
    list.entries[3].mtime = 9999;

    /* entry 4: "z_file.txt" (file, huge size, old timestamp) */
    strcpy(list.entries[4].name, "z_file.txt");
    list.entries[4].is_dir = false;
    list.entries[4].size = 10000000;
    list.entries[4].mtime = 100;

    /* Sort by NAME */
    fs_sort_dir_list(&list, SORT_NAME);
    assert(strcmp(list.entries[0].name, "..") == 0);

    /* Sort by SIZE */
    fs_sort_dir_list(&list, SORT_SIZE);
    assert(strcmp(list.entries[0].name, "..") == 0);

    /* Sort by DATE */
    fs_sort_dir_list(&list, SORT_DATE);
    assert(strcmp(list.entries[0].name, "..") == 0);

    fs_free_dir_list(&list);
}

static void test_refresh_preserves_selection_and_scroll(void) {
    AppState state;
    char file1[PATH_MAX];
    char file2[PATH_MAX];
    char file3[PATH_MAX];

    setup();
    make_path(file1, sizeof(file1), "a_first.txt");
    make_path(file2, sizeof(file2), "b_second.txt");
    make_path(file3, sizeof(file3), "c_third.txt");
    write_file(file1, "1");
    write_file(file2, "2");
    write_file(file3, "3");

    state_init(&state);
    state_change_dir(&state, TEST_ROOT);
    assert(state.dir_list.count >= 4); /* .., a, b, c */

    /* Find b_second.txt and select it */
    int target_idx = -1;
    for (int i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "b_second.txt") == 0) {
            target_idx = i;
            break;
        }
    }
    assert(target_idx >= 0);
    state.selected_index = target_idx;
    state.scroll_offset = 1;

    /* Refresh directory with "." */
    state_change_dir(&state, ".");

    /* Selection on b_second.txt and scroll_offset should be preserved */
    assert(strcmp(state.dir_list.entries[state.selected_index].name, "b_second.txt") == 0);
    assert(state.scroll_offset == 1);

    state_cleanup(&state);
    teardown();
}

static void test_large_dir_expansion(void) {
    char dir[PATH_MAX];
    char file_path[PATH_MAX];
    DirectoryList list;

    setup();
    make_path(dir, sizeof(dir), "large_dir");
    assert(platform_mkdir(dir, 0700) == 0);

    /* Create 300 files to force multiple reallocs from base capacity 128 */
    for (int i = 0; i < 300; ++i) {
        char filename[64];
        snprintf(filename, sizeof(filename), "file_%04d.txt", i);
        assert(utils_join_path(file_path, sizeof(file_path), dir, filename));
        write_file(file_path, "x");
    }

    fs_init_dir_list(&list);
    assert(fs_read_dir(dir, &list));
    /* Expect 300 files + 1 parent ("..") = 301 entries */
    assert(list.count == 301);
    assert(list.capacity >= 301);
    assert(list.entries != NULL);
    assert(strcmp(list.entries[0].name, "..") == 0);

    fs_free_dir_list(&list);
    assert(list.entries == NULL);
    assert(list.count == 0);
    assert(list.capacity == 0);

    teardown();
}

static void test_state_change_dir_permission_denied(void) {
    AppState state;
    char unreadable_dir[PATH_MAX];

    setup();
#ifdef _WIN32
    make_path(unreadable_dir, sizeof(unreadable_dir), "missing-directory");
    state_init(&state);
    assert(state_change_dir(&state, TEST_ROOT));
    errno = 0;
    assert(!state_change_dir(&state, unreadable_dir));
    assert(errno == ENOENT);
    assert(strcmp(state.current_path, TEST_ROOT) == 0);
    state_cleanup(&state);
#else
    if (geteuid() == 0) {
        teardown();
        return;
    }
    make_path(unreadable_dir, sizeof(unreadable_dir), "unreadable");
    assert(platform_mkdir(unreadable_dir, 0000) == 0);

    state_init(&state);
    state_change_dir(&state, TEST_ROOT);

    /* Changing into unreadable directory must fail and return false */
    errno = 0;
    assert(!state_change_dir(&state, unreadable_dir));
    assert(errno == EACCES);

    /* State path remains in previous valid directory */
    assert(strcmp(state.current_path, TEST_ROOT) == 0);

    assert(chmod(unreadable_dir, 0700) == 0);
    state_cleanup(&state);
#endif
    teardown();
}

int main(void) {
    test_delete_circular_symlinks();
    test_delete_parent_path_is_blocked();
    test_copy_into_itself();
    test_clipboard_apply_between_directories();
    test_clipboard_cut_same_filesystem();
    test_permission_denied();
    test_copy_readonly_directory();
    test_sort_dotdot_stays_first();
    test_refresh_preserves_selection_and_scroll();
    test_large_dir_expansion();
    test_state_change_dir_permission_denied();
    test_async_task_lifecycle();
    test_path_near_path_max();
    puts("test_fs: all tests passed");
    return 0;
}