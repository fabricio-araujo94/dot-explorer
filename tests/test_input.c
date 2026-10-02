#include "input.h"
#include "fs.h"
#include "utils.h"
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_INPUT_ROOT "/tmp/dot-explorer-input-test"

typedef struct {
    char last_message_title[128];
    char last_message_body[512];
    int message_count;
    char prompt_response[256];
    bool prompt_return_value;
    int prompt_count;
    char last_opened_file[PATH_MAX];
    bool open_file_return_value;
    int open_file_count;
    int list_height;

    const int *key_stream;
    size_t key_stream_len;
    size_t key_stream_pos;

    char last_rendered_filter[256];
    int cursor_visibility;
} MockUIContext;

static bool mock_prompt(const char *prompt, char *buffer, size_t buf_size, void *userdata) {
    (void)prompt;
    MockUIContext *ctx = (MockUIContext *)userdata;
    ctx->prompt_count++;
    if (!ctx->prompt_return_value) return false;
    snprintf(buffer, buf_size, "%s", ctx->prompt_response);
    return true;
}

static void mock_show_message(const char *title, const char *message, void *userdata) {
    MockUIContext *ctx = (MockUIContext *)userdata;
    ctx->message_count++;
    snprintf(ctx->last_message_title, sizeof(ctx->last_message_title), "%s", title ? title : "");
    snprintf(ctx->last_message_body, sizeof(ctx->last_message_body), "%s", message ? message : "");
}

static int mock_get_list_height(void *userdata) {
    MockUIContext *ctx = (MockUIContext *)userdata;
    return ctx->list_height > 0 ? ctx->list_height : 10;
}

static bool mock_open_file(const char *path, void *userdata) {
    MockUIContext *ctx = (MockUIContext *)userdata;
    ctx->open_file_count++;
    snprintf(ctx->last_opened_file, sizeof(ctx->last_opened_file), "%s", path ? path : "");
    return ctx->open_file_return_value;
}

static void mock_render_filter_prompt(const char *query, void *userdata) {
    MockUIContext *ctx = (MockUIContext *)userdata;
    snprintf(ctx->last_rendered_filter, sizeof(ctx->last_rendered_filter), "%s", query ? query : "");
}

static int mock_get_char(void *userdata) {
    MockUIContext *ctx = (MockUIContext *)userdata;
    if (ctx->key_stream && ctx->key_stream_pos < ctx->key_stream_len) {
        return ctx->key_stream[ctx->key_stream_pos++];
    }
    return 27; /* ESC */
}

static void mock_set_cursor(int visibility, void *userdata) {
    MockUIContext *ctx = (MockUIContext *)userdata;
    ctx->cursor_visibility = visibility;
}

static void init_mock_context(MockUIContext *ctx, InputCallbacks *cb) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->list_height = 10;
    ctx->prompt_return_value = true;
    ctx->open_file_return_value = true;

    cb->prompt = mock_prompt;
    cb->show_message = mock_show_message;
    cb->get_list_height = mock_get_list_height;
    cb->open_file = mock_open_file;
    cb->render_filter_prompt = mock_render_filter_prompt;
    cb->get_char = mock_get_char;
    cb->set_cursor = mock_set_cursor;
    cb->userdata = ctx;
}

static void setup_env(void) {
    fs_delete_recursive(TEST_INPUT_ROOT);
    assert(mkdir(TEST_INPUT_ROOT, 0700) == 0);
}

static void teardown_env(void) {
    fs_delete_recursive(TEST_INPUT_ROOT);
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(content, 1, strlen(content), f) == strlen(content));
    assert(fclose(f) == 0);
}

static void test_quit_command(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;

    state_init(&state);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    assert(!state.should_quit);
    input_handle(&state, &cb, input_key_event(DOT_KEY_QUIT), &icb);
    assert(state.should_quit);

    state_cleanup(&state);
}

static void test_navigation_and_selection(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char f1[PATH_MAX], f2[PATH_MAX], f3[PATH_MAX];

    setup_env();
    utils_join_path(f1, sizeof(f1), TEST_INPUT_ROOT, "alpha.txt");
    utils_join_path(f2, sizeof(f2), TEST_INPUT_ROOT, "beta.txt");
    utils_join_path(f3, sizeof(f3), TEST_INPUT_ROOT, "gamma.txt");
    write_file(f1, "1");
    write_file(f2, "22");
    write_file(f3, "333");

    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    assert(state.dir_list.count >= 3);
    assert(state.selected_index == 0);

    /* Move down */
    input_handle(&state, &cb, input_key_event(KEY_DOWN), &icb);
    assert(state.selected_index == 1);
    input_handle(&state, &cb, input_key_event(DOT_KEY_DOWN_DIR), &icb);
    assert(state.selected_index == 2);

    /* Move up */
    input_handle(&state, &cb, input_key_event(KEY_UP), &icb);
    assert(state.selected_index == 1);
    input_handle(&state, &cb, input_key_event(DOT_KEY_UP_DIR), &icb);
    assert(state.selected_index == 0);

    /* Selection toggle */
    assert(!state.dir_list.entries[0].is_selected);
    input_handle(&state, &cb, input_key_event(DOT_KEY_SELECT), &icb);
    assert(state.dir_list.entries[0].is_selected);
    assert(state.selected_index == 1);

    state_cleanup(&state);
    teardown_env();
}

static void test_sorting_commands(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;

    state_init(&state);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    input_handle(&state, &cb, input_key_event(DOT_KEY_SORT_SIZE), &icb);
    assert(state.sort_type == SORT_SIZE);

    input_handle(&state, &cb, input_key_event(DOT_KEY_SORT_DATE), &icb);
    assert(state.sort_type == SORT_DATE);

    input_handle(&state, &cb, input_key_event(DOT_KEY_SORT_NAME), &icb);
    assert(state.sort_type == SORT_NAME);

    state_cleanup(&state);
}

static void test_clipboard_copy_and_cut(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char f1[PATH_MAX];

    setup_env();
    utils_join_path(f1, sizeof(f1), TEST_INPUT_ROOT, "sample.txt");
    write_file(f1, "data");

    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    /* Find sample.txt index */
    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "sample.txt") == 0) {
            state.selected_index = (int)i;
            break;
        }
    }

    /* Copy */
    input_handle(&state, &cb, input_key_event(DOT_KEY_COPY), &icb);
    assert(cb.count == 1);
    assert(!cb.is_cut);
    assert(strstr(cb.paths[0], "sample.txt") != NULL);

    /* Cut */
    input_handle(&state, &cb, input_key_event(DOT_KEY_CUT), &icb);
    assert(cb.count == 1);
    assert(cb.is_cut);

    clipboard_clear(&cb);
    state_cleanup(&state);
    teardown_env();
}

static void test_create_file_and_dir(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char expected_file[PATH_MAX], expected_dir[PATH_MAX];

    setup_env();
    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    /* Create file */
    snprintf(ctx.prompt_response, sizeof(ctx.prompt_response), "newfile.txt");
    input_handle(&state, &cb, input_key_event(DOT_KEY_CREATE_FILE), &icb);
    utils_join_path(expected_file, sizeof(expected_file), TEST_INPUT_ROOT, "newfile.txt");
    assert(access(expected_file, F_OK) == 0);

    /* Create directory */
    snprintf(ctx.prompt_response, sizeof(ctx.prompt_response), "newdir");
    input_handle(&state, &cb, input_key_event(DOT_KEY_CREATE_DIR), &icb);
    utils_join_path(expected_dir, sizeof(expected_dir), TEST_INPUT_ROOT, "newdir");
    assert(access(expected_dir, F_OK) == 0);

    state_cleanup(&state);
    teardown_env();
}

static void test_rename_and_delete(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char initial_path[PATH_MAX], renamed_path[PATH_MAX];

    setup_env();
    utils_join_path(initial_path, sizeof(initial_path), TEST_INPUT_ROOT, "orig.txt");
    utils_join_path(renamed_path, sizeof(renamed_path), TEST_INPUT_ROOT, "renamed.txt");
    write_file(initial_path, "test");

    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    /* Select orig.txt */
    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "orig.txt") == 0) {
            state.selected_index = (int)i;
            break;
        }
    }

    /* Rename */
    snprintf(ctx.prompt_response, sizeof(ctx.prompt_response), "renamed.txt");
    input_handle(&state, &cb, input_key_event(DOT_KEY_RENAME_ITEM), &icb);
    assert(access(initial_path, F_OK) != 0);
    assert(access(renamed_path, F_OK) == 0);

    /* Select renamed.txt */
    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "renamed.txt") == 0) {
            state.selected_index = (int)i;
            break;
        }
    }

    /* Delete with 'n' response */
    snprintf(ctx.prompt_response, sizeof(ctx.prompt_response), "n");
    input_handle(&state, &cb, input_key_event(DOT_KEY_DELETE_ITEM), &icb);
    assert(access(renamed_path, F_OK) == 0);

    /* Delete with 'y' response */
    snprintf(ctx.prompt_response, sizeof(ctx.prompt_response), "y");
    input_handle(&state, &cb, input_key_event(DOT_KEY_DELETE_ITEM), &icb);
    assert(access(renamed_path, F_OK) != 0);

    state_cleanup(&state);
    teardown_env();
}

static void test_properties(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char f1[PATH_MAX];

    setup_env();
    utils_join_path(f1, sizeof(f1), TEST_INPUT_ROOT, "props.txt");
    write_file(f1, "hello world");

    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "props.txt") == 0) {
            state.selected_index = (int)i;
            break;
        }
    }

    input_handle(&state, &cb, input_key_event(DOT_KEY_PROPERTIES), &icb);
    assert(ctx.message_count == 1);
    assert(strcmp(ctx.last_message_title, "Properties") == 0);
    assert(strstr(ctx.last_message_body, "props.txt") != NULL);

    state_cleanup(&state);
    teardown_env();
}

static void test_filter_prompt_interactive(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char f1[PATH_MAX], f2[PATH_MAX];

    setup_env();
    utils_join_path(f1, sizeof(f1), TEST_INPUT_ROOT, "apple.txt");
    utils_join_path(f2, sizeof(f2), TEST_INPUT_ROOT, "banana.txt");
    write_file(f1, "a");
    write_file(f2, "b");

    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    /* Simulate typing 'a', 'p', '\n' */
    int keys[] = { 'a', 'p', '\n' };
    ctx.key_stream = keys;
    ctx.key_stream_len = sizeof(keys) / sizeof(keys[0]);
    ctx.key_stream_pos = 0;

    input_handle(&state, &cb, input_key_event('/'), &icb);
    assert(state.filter_active);
    assert(strcmp(state.filter_query, "ap") == 0);

    /* Simulate ESC */
    int esc_keys[] = { 27 };
    ctx.key_stream = esc_keys;
    ctx.key_stream_len = 1;
    ctx.key_stream_pos = 0;

    input_handle(&state, &cb, input_key_event('/'), &icb);
    assert(!state.filter_active);
    assert(state.filter_query[0] == '\0');

    state_cleanup(&state);
    teardown_env();
}

int main(void) {
    test_quit_command();
    test_navigation_and_selection();
    test_sorting_commands();
    test_clipboard_copy_and_cut();
    test_create_file_and_dir();
    test_rename_and_delete();
    test_properties();
    test_filter_prompt_interactive();

    printf("test_input: all tests passed\n");
    return 0;
}
