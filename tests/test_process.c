#include "process.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_PROCESS_DIR "/tmp/dot-explorer-process-test"

static void setup_env(void) {
    mkdir(TEST_PROCESS_DIR, 0700);
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
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/test_file.txt", TEST_PROCESS_DIR);

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
