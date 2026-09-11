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

#define TEST_ROOT "/tmp/dot-explorer-test"

static void setup(void) {
    fs_delete_recursive(TEST_ROOT);
    assert(mkdir(TEST_ROOT, 0700) == 0);
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
    assert(mkdir(tree, 0700) == 0);
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

static void test_copy_into_itself(void) {
    char source[PATH_MAX];
    char nested[PATH_MAX];
    char error_path[PATH_MAX];
    FsCopyOptions options = { false, true, NULL, NULL, NULL };
    setup();
    make_path(source, sizeof(source), "source.txt");
    write_file(source, "original");
    assert(!fs_copy_recursive_with_options(source, source, &options,
                                           error_path, sizeof(error_path)));
    make_path(nested, sizeof(nested), "folder");
    assert(mkdir(nested, 0700) == 0);
    assert(!fs_copy_recursive_with_options(TEST_ROOT, nested, &options,
                                           error_path, sizeof(error_path)));
    assert(access(nested, F_OK) == 0);
    teardown();
}

static void test_permission_denied(void) {
#ifdef _WIN32
    return;
#else
    char source[PATH_MAX];
    char destination[PATH_MAX];
    setup();
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
    assert(fs_delete_recursive(TEST_ROOT));
#endif
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

int main(void) {
    test_delete_circular_symlinks();
    test_copy_into_itself();
    test_permission_denied();
    test_path_near_path_max();
    puts("test_fs: all tests passed");
    return 0;
}