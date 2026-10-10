#include "process.h"
#include "test_helpers.h"
#include "utils/platform.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char TEST_PROCESS_DIR[PATH_MAX];

static void setup_env(void) {
    assert(test_get_temp_dir(TEST_PROCESS_DIR, sizeof(TEST_PROCESS_DIR),
                             "dot-explorer-process-test"));
    assert(platform_mkdir(TEST_PROCESS_DIR, 0700) == 0);
}

static void teardown_env(void) {
    rmdir(TEST_PROCESS_DIR);
}

static void test_process_invalid_arguments(void) {
    assert(!process_open_file_with_editor(NULL));
    assert(!process_open_file_with_editor("/path/to/nonexistent/file/12345"));
}

static void test_process_directory_fails(void) {
    setup_env();
    errno = 0;
    assert(!process_open_file_with_editor(TEST_PROCESS_DIR));
    assert(errno == EISDIR);
    teardown_env();
}

static void test_process_open_regular_file(void) {
    setup_env();
    char filepath[PATH_MAX];
    int path_length = snprintf(filepath, sizeof(filepath), "%s%c%s",
                               TEST_PROCESS_DIR, PLATFORM_PATH_SEPARATOR,
                               "test_file.txt");
    assert(path_length >= 0 && (size_t)path_length < sizeof(filepath));

    FILE *f = fopen(filepath, "w");
    assert(f != NULL);
    fprintf(f, "hello\n");
    fclose(f);

#ifndef _WIN32
    /* Set EDITOR to true (which exits with 0 immediately) */
    setenv("EDITOR", "true", 1);
    assert(process_open_file_with_editor(filepath));
#endif

    unlink(filepath);
    teardown_env();
}

int main(void) {
    test_process_invalid_arguments();
    test_process_directory_fails();
    test_process_open_regular_file();

    printf("test_process: all tests passed\n");
    return 0;
}
