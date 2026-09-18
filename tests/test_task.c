#include "task.h"
#include "fs.h"
#include "utils.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_TASK_ROOT "/tmp/dot-explorer-task-test"

static void setup_task_env(void) {
    fs_delete_recursive(TEST_TASK_ROOT);
    assert(mkdir(TEST_TASK_ROOT, 0700) == 0);
}

static void teardown_task_env(void) {
    assert(fs_delete_recursive(TEST_TASK_ROOT));
}

static void write_dummy_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(content, 1, strlen(content), f) == strlen(content));
    assert(fclose(f) == 0);
}

static void test_task_init_and_cleanup(void) {
    Task task;
    task_init(&task);
    assert(task.status == TASK_IDLE);
    assert(!task_is_running(&task));
    task_cleanup(&task);
}

static void test_task_copy_file_and_directory(void) {
    Task task;
    char src_file[PATH_MAX], dst_file[PATH_MAX];
    char src_dir[PATH_MAX], dst_dir[PATH_MAX];
    char file1[PATH_MAX], file2[PATH_MAX];
    TaskStatus status = TASK_IDLE;
    uint64_t copied = 0, total = 0;
    char error_path[PATH_MAX];
    int attempts = 0;

    setup_task_env();

    /* 1. Test single file async copy */
    assert(utils_join_path(src_file, sizeof(src_file), TEST_TASK_ROOT, "single_src.txt"));
    assert(utils_join_path(dst_file, sizeof(dst_file), TEST_TASK_ROOT, "single_dst.txt"));
    write_dummy_file(src_file, "Single file content to copy via background worker");

    task_init(&task);
    assert(task_start_copy(&task, src_file, dst_file));

    attempts = 0;
    do {
        task_snapshot(&task, &status, &copied, &total, error_path, sizeof(error_path));
        if (status == TASK_RUNNING) usleep(1000);
    } while (status == TASK_RUNNING && ++attempts < 5000);

    assert(status == TASK_COMPLETED);
    assert(total > 0);
    assert(copied == total);
    assert(access(dst_file, F_OK) == 0);
    task_cleanup(&task);

    /* 2. Test directory tree async copy */
    assert(utils_join_path(src_dir, sizeof(src_dir), TEST_TASK_ROOT, "source_tree"));
    assert(utils_join_path(dst_dir, sizeof(dst_dir), TEST_TASK_ROOT, "dest_tree"));
    assert(mkdir(src_dir, 0700) == 0);

    assert(utils_join_path(file1, sizeof(file1), src_dir, "f1.txt"));
    assert(utils_join_path(file2, sizeof(file2), src_dir, "f2.txt"));
    write_dummy_file(file1, "Hello World from File 1");
    write_dummy_file(file2, "Another File with Content 2");

    task_init(&task);
    assert(task_start_copy(&task, src_dir, dst_dir));
    assert(task_is_running(&task) || task.status == TASK_COMPLETED);

    /* Cannot start another copy while running */
    if (task_is_running(&task)) {
        errno = 0;
        assert(!task_start_copy(&task, src_dir, dst_dir));
        assert(errno == EBUSY);
    }

    /* Wait for completion */
    attempts = 0;
    do {
        task_snapshot(&task, &status, &copied, &total, error_path, sizeof(error_path));
        if (status == TASK_RUNNING) usleep(1000);
    } while (status == TASK_RUNNING && ++attempts < 5000);

    assert(status == TASK_COMPLETED);
    assert(total > 0);
    assert(copied == total);

    /* Verify files exist in dest_tree */
    char check1[PATH_MAX], check2[PATH_MAX];
    assert(utils_join_path(check1, sizeof(check1), dst_dir, "f1.txt"));
    assert(utils_join_path(check2, sizeof(check2), dst_dir, "f2.txt"));
    assert(access(check1, F_OK) == 0);
    assert(access(check2, F_OK) == 0);

    task_cleanup(&task);
    teardown_task_env();
}

static void test_task_copy_multifile_progress(void) {
    Task task;
    char src_dir[PATH_MAX], dst_dir[PATH_MAX];
    char f_small[PATH_MAX], f_large[PATH_MAX], f_small2[PATH_MAX], f_empty[PATH_MAX];
    char large_buf[20000];
    TaskStatus status = TASK_IDLE;
    uint64_t copied = 0, total = 0;
    char error_path[PATH_MAX];
    int attempts = 0;

    setup_task_env();
    assert(utils_join_path(src_dir, sizeof(src_dir), TEST_TASK_ROOT, "multi_src"));
    assert(utils_join_path(dst_dir, sizeof(dst_dir), TEST_TASK_ROOT, "multi_dst"));
    assert(mkdir(src_dir, 0700) == 0);

    assert(utils_join_path(f_small, sizeof(f_small), src_dir, "small.txt"));
    assert(utils_join_path(f_large, sizeof(f_large), src_dir, "large.bin"));
    assert(utils_join_path(f_small2, sizeof(f_small2), src_dir, "small2.txt"));
    assert(utils_join_path(f_empty, sizeof(f_empty), src_dir, "empty.txt"));

    write_dummy_file(f_small, "small file with 24 bytes");
    memset(large_buf, 'A', sizeof(large_buf));
    {
        FILE *f = fopen(f_large, "wb");
        assert(f != NULL);
        assert(fwrite(large_buf, 1, sizeof(large_buf), f) == sizeof(large_buf));
        assert(fclose(f) == 0);
    }
    write_dummy_file(f_small2, "another small file");
    write_dummy_file(f_empty, "");

    task_init(&task);
    assert(task_start_copy(&task, src_dir, dst_dir));

    attempts = 0;
    do {
        task_snapshot(&task, &status, &copied, &total, error_path, sizeof(error_path));
        if (status == TASK_RUNNING) usleep(1000);
    } while (status == TASK_RUNNING && ++attempts < 5000);

    assert(status == TASK_COMPLETED);
    assert(total == 24 + sizeof(large_buf) + strlen("another small file"));
    assert(copied == total);

    task_cleanup(&task);
    teardown_task_env();
}

static void test_task_cancellation(void) {
    Task task;
    char src_dir[PATH_MAX], dst_dir[PATH_MAX];
    TaskStatus status = TASK_IDLE;
    char error_path[PATH_MAX];
    int attempts = 0;

    setup_task_env();
    assert(utils_join_path(src_dir, sizeof(src_dir), TEST_TASK_ROOT, "big_source"));
    assert(utils_join_path(dst_dir, sizeof(dst_dir), TEST_TASK_ROOT, "big_dest"));
    assert(mkdir(src_dir, 0700) == 0);

    /* Create 200 files with some content to allow cancellation window */
    for (int i = 0; i < 200; ++i) {
        char fn[64], fp[PATH_MAX];
        snprintf(fn, sizeof(fn), "data_%03d.bin", i);
        assert(utils_join_path(fp, sizeof(fp), src_dir, fn));
        write_dummy_file(fp, "some dummy bytes to copy........................................");
    }

    task_init(&task);
    assert(task_start_copy(&task, src_dir, dst_dir));
    task_request_cancel(&task);

    do {
        task_snapshot(&task, &status, NULL, NULL, error_path, sizeof(error_path));
        if (status == TASK_RUNNING) usleep(1000);
    } while (status == TASK_RUNNING && ++attempts < 5000);

    assert(status == TASK_CANCELLED || status == TASK_COMPLETED);
    task_cleanup(&task);
    teardown_task_env();
}

static void test_task_invalid_inputs(void) {
    Task task;
    task_init(&task);

    /* Invalid arguments */
    errno = 0;
    assert(!task_start_copy(NULL, "source", "dest"));
    assert(errno == EINVAL);

    assert(!task_start_copy(&task, NULL, "dest"));
    assert(errno == EINVAL);

    assert(!task_start_copy(&task, "source", NULL));
    assert(errno == EINVAL);

    assert(!task_start_copy(&task, "/nonexistent_xyz_123", "/tmp/dest"));
    assert(errno == EINVAL);

    task_cleanup(&task);
}

int main(void) {
    test_task_init_and_cleanup();
    test_task_copy_file_and_directory();
    test_task_copy_multifile_progress();
    test_task_cancellation();
    test_task_invalid_inputs();
    puts("test_task: all tests passed");
    return 0;
}
