#include "input.h"
#include "fs.h"
#include "utils.h"
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

static char TEST_INPUT_ROOT[PATH_MAX];

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
    assert(test_get_temp_dir(TEST_INPUT_ROOT, sizeof(TEST_INPUT_ROOT), "dot-explorer-input-test"));
    fs_delete_recursive(TEST_INPUT_ROOT);
    assert(platform_mkdir(TEST_INPUT_ROOT, 0700) == 0);
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
    path_join(f1, sizeof(f1), TEST_INPUT_ROOT, "alpha.txt");
    path_join(f2, sizeof(f2), TEST_INPUT_ROOT, "beta.txt");
    path_join(f3, sizeof(f3), TEST_INPUT_ROOT, "gamma.txt");
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

    /* Toggle again */
    state.selected_index = 0;
    input_handle(&state, &cb, input_key_event(DOT_KEY_SELECT), &icb);
    assert(!state.dir_list.entries[0].is_selected);

    state_cleanup(&state);
    teardown_env();
}

static void test_selection_advances_within_filter(void) {
    AppState state;
    Clipboard clipboard;
    MockUIContext context;
    InputCallbacks callbacks;
    char apple_path[PATH_MAX];
    char banana_path[PATH_MAX];
    char grape_path[PATH_MAX];

    setup_env();
    assert(path_join(apple_path, sizeof(apple_path), TEST_INPUT_ROOT, "apple.txt"));
    assert(path_join(banana_path, sizeof(banana_path), TEST_INPUT_ROOT, "banana.txt"));
    assert(path_join(grape_path, sizeof(grape_path), TEST_INPUT_ROOT, "grape.txt"));
    write_file(apple_path, "apple");
    write_file(banana_path, "banana");
    write_file(grape_path, "grape");

    assert(state_init(&state));
    assert(state_change_dir(&state, TEST_INPUT_ROOT));
    memset(&clipboard, 0, sizeof(clipboard));
    init_mock_context(&context, &callbacks);
    assert(filter_entries("ap", &state.dir_list, &state.filtered_entries));
    assert(state.filtered_entries.count == 2);
    assert(state.filtered_entries.indices[1] > state.filtered_entries.indices[0] + 1);
    state.filter_active = true;
    strcpy(state.filter_query, "ap");
    state.selected_index = state.filtered_entries.indices[0];

    input_handle(&state, &clipboard, input_key_event(DOT_KEY_SELECT), &callbacks);

    assert(state.dir_list.entries[state.filtered_entries.indices[0]].is_selected);
    assert(state.selected_index == state.filtered_entries.indices[1]);

    state_cleanup(&state);
    teardown_env();
}

static void test_enter_open_and_back(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char subdir[PATH_MAX], f1[PATH_MAX];

    setup_env();
    path_join(subdir, sizeof(subdir), TEST_INPUT_ROOT, "subfolder");
    path_join(f1, sizeof(f1), TEST_INPUT_ROOT, "test.txt");
    assert(platform_mkdir(subdir, 0700) == 0);
    write_file(f1, "hello");

    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    /* Select subfolder and press enter */
    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "subfolder") == 0) {
            state.selected_index = (int)i;
            break;
        }
    }
    input_handle(&state, &cb, input_key_event(DOT_KEY_ENTER_DIR), &icb);
    assert(strstr(state.current_path, "subfolder") != NULL);
    assert(state.history.back_count == 1);

    /* Press back (DOT_KEY_BACK_DIR / KEY_LEFT / KEY_BACKSPACE) */
    input_handle(&state, &cb, input_key_event(DOT_KEY_BACK_DIR), &icb);
    assert(strcmp(path_basename(state.current_path), "dot-explorer-input-test") == 0 ||
           strstr(state.current_path, "dot-explorer-input-test") != NULL);

    /* Select test.txt and press KEY_RIGHT to open file */
    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "test.txt") == 0) {
            state.selected_index = (int)i;
            break;
        }
    }
    input_handle(&state, &cb, input_key_event(KEY_RIGHT), &icb);
    assert(ctx.open_file_count == 1);
    assert(strstr(ctx.last_opened_file, "test.txt") != NULL);

    state_cleanup(&state);
    teardown_env();
}

static void test_history_navigation_keys(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char subdir1[PATH_MAX], subdir2[PATH_MAX];

    setup_env();
    path_join(subdir1, sizeof(subdir1), TEST_INPUT_ROOT, "dir1");
    path_join(subdir2, sizeof(subdir2), TEST_INPUT_ROOT, "dir2");
    assert(platform_mkdir(subdir1, 0700) == 0);
    assert(platform_mkdir(subdir2, 0700) == 0);

    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    /* Enter dir1 */
    state_change_dir(&state, subdir1);
    history_push(&state, TEST_INPUT_ROOT, 0);

    /* Enter dir2 */
    state_change_dir(&state, subdir2);
    history_push(&state, subdir1, 0);

    /* History back */
    input_handle(&state, &cb, input_key_event(DOT_KEY_HISTORY_BACK), &icb);
    assert(strstr(state.current_path, "dir1") != NULL);

    /* History forward */
    input_handle(&state, &cb, input_key_event(DOT_KEY_HISTORY_FORWARD), &icb);
    assert(strstr(state.current_path, "dir2") != NULL);

    state_cleanup(&state);
    teardown_env();
}

static void test_history_navigation_failure_preserves_selection(void) {
    AppState state;
    Clipboard clipboard;
    MockUIContext context;
    InputCallbacks callbacks;
    char current_file[PATH_MAX];
    char deleted_directory[PATH_MAX];
    int file_index = -1;

    setup_env();
    path_join(current_file, sizeof(current_file), TEST_INPUT_ROOT, "current.txt");
    path_join(deleted_directory, sizeof(deleted_directory), TEST_INPUT_ROOT, "deleted");
    write_file(current_file, "current");
    assert(platform_mkdir(deleted_directory, 0700) == 0);
    assert(state_init(&state));
    assert(state_change_dir(&state, TEST_INPUT_ROOT));
    memset(&clipboard, 0, sizeof(clipboard));
    init_mock_context(&context, &callbacks);

    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "current.txt") == 0) {
            file_index = (int)i;
            break;
        }
    }
    assert(file_index >= 0);
    state.selected_index = file_index;
    assert(history_push(&state, deleted_directory, 0));
    assert(fs_delete_recursive(deleted_directory));

    input_handle(&state, &clipboard,
                 input_key_event(DOT_KEY_HISTORY_BACK), &callbacks);

    assert(strcmp(state.current_path, TEST_INPUT_ROOT) == 0);
    assert(state.selected_index == file_index);
    assert(context.message_count == 1);
    assert(strcmp(context.last_message_title, "Navigation failed") == 0);
    assert(strstr(context.last_message_body, deleted_directory) != NULL);

    state_cleanup(&state);
    teardown_env();
}

static void test_refresh_key(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char f1[PATH_MAX];

    setup_env();
    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    size_t count_before = state.dir_list.count;

    path_join(f1, sizeof(f1), TEST_INPUT_ROOT, "added_externally.txt");
    write_file(f1, "content");

    input_handle(&state, &cb, input_key_event(DOT_KEY_REFRESH), &icb);
    assert(state.dir_list.count == count_before + 1);

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

static void test_clipboard_copy_cut_paste_multi(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char f1[PATH_MAX], f2[PATH_MAX], dest_dir[PATH_MAX];

    setup_env();
    path_join(f1, sizeof(f1), TEST_INPUT_ROOT, "f1.txt");
    path_join(f2, sizeof(f2), TEST_INPUT_ROOT, "f2.txt");
    path_join(dest_dir, sizeof(dest_dir), TEST_INPUT_ROOT, "destination");
    write_file(f1, "111");
    write_file(f2, "222");
    assert(platform_mkdir(dest_dir, 0700) == 0);

    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    /* Mark f1.txt and f2.txt as selected */
    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "f1.txt") == 0 ||
            strcmp(state.dir_list.entries[i].name, "f2.txt") == 0) {
            state.dir_list.entries[i].is_selected = true;
        }
    }

    /* Copy multi selected */
    input_handle(&state, &cb, input_key_event(DOT_KEY_COPY), &icb);
    assert(cb.count == 2);
    assert(!cb.is_cut);

    /* Navigate to dest_dir and Paste */
    state_change_dir(&state, dest_dir);
    input_handle(&state, &cb, input_key_event(DOT_KEY_PASTE), &icb);
    assert(state.dir_list.count >= 2);

    /* Test Cut multi selected */
    state_change_dir(&state, TEST_INPUT_ROOT);
    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "f1.txt") == 0 ||
            strcmp(state.dir_list.entries[i].name, "f2.txt") == 0) {
            state.dir_list.entries[i].is_selected = true;
        }
    }
    input_handle(&state, &cb, input_key_event(DOT_KEY_CUT), &icb);
    assert(cb.count == 2);
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
    path_join(expected_file, sizeof(expected_file), TEST_INPUT_ROOT, "newfile.txt");
    assert(access(expected_file, F_OK) == 0);

    /* Create directory */
    snprintf(ctx.prompt_response, sizeof(ctx.prompt_response), "newdir");
    input_handle(&state, &cb, input_key_event(DOT_KEY_CREATE_DIR), &icb);
    path_join(expected_dir, sizeof(expected_dir), TEST_INPUT_ROOT, "newdir");
    assert(access(expected_dir, F_OK) == 0);

    /* When prompt is cancelled (returns false) */
    ctx.prompt_return_value = false;
    input_handle(&state, &cb, input_key_event(DOT_KEY_CREATE_FILE), &icb);
    input_handle(&state, &cb, input_key_event(DOT_KEY_CREATE_DIR), &icb);

    state_cleanup(&state);
    teardown_env();
}

static void test_rename_and_delete_multi(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;
    char initial_path[PATH_MAX], renamed_path[PATH_MAX], f2[PATH_MAX];

    setup_env();
    path_join(initial_path, sizeof(initial_path), TEST_INPUT_ROOT, "orig.txt");
    path_join(renamed_path, sizeof(renamed_path), TEST_INPUT_ROOT, "renamed.txt");
    path_join(f2, sizeof(f2), TEST_INPUT_ROOT, "other.txt");
    write_file(initial_path, "test");
    write_file(f2, "test2");

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

    /* Select both renamed.txt and other.txt for multi delete */
    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "renamed.txt") == 0 ||
            strcmp(state.dir_list.entries[i].name, "other.txt") == 0) {
            state.dir_list.entries[i].is_selected = true;
        }
    }

    /* Delete with 'n' response */
    snprintf(ctx.prompt_response, sizeof(ctx.prompt_response), "n");
    input_handle(&state, &cb, input_key_event(DOT_KEY_DELETE_ITEM), &icb);
    assert(access(renamed_path, F_OK) == 0);
    assert(access(f2, F_OK) == 0);

    /* Delete with 'y' response */
    snprintf(ctx.prompt_response, sizeof(ctx.prompt_response), "y");
    input_handle(&state, &cb, input_key_event(DOT_KEY_DELETE_ITEM), &icb);
    assert(access(renamed_path, F_OK) != 0);
    assert(access(f2, F_OK) != 0);

    state_cleanup(&state);
    teardown_env();
}

static void set_unjoinable_current_path(AppState *state) {
    memset(state->current_path, 'x', sizeof(state->current_path) - 1);
    state->current_path[sizeof(state->current_path) - 1] = '\0';
}

static void test_path_join_failures_block_mutations(void) {
    AppState state;
    Clipboard clipboard;
    MockUIContext context;
    InputCallbacks callbacks;
    char original_path[PATH_MAX];
    char multi_path[PATH_MAX];
    char created_file[PATH_MAX];
    char created_directory[PATH_MAX];
    char renamed_path[PATH_MAX];
    int original_index = -1;
    int multi_index = -1;

    setup_env();
    assert(path_join(original_path, sizeof(original_path), TEST_INPUT_ROOT, "original.txt"));
    assert(path_join(multi_path, sizeof(multi_path), TEST_INPUT_ROOT, "multi.txt"));
    assert(path_join(created_file, sizeof(created_file), TEST_INPUT_ROOT, "created.txt"));
    assert(path_join(created_directory, sizeof(created_directory), TEST_INPUT_ROOT, "created-dir"));
    assert(path_join(renamed_path, sizeof(renamed_path), TEST_INPUT_ROOT, "renamed.txt"));
    write_file(original_path, "keep original");
    write_file(multi_path, "keep multi");

    assert(state_init(&state));
    assert(state_change_dir(&state, TEST_INPUT_ROOT));
    memset(&clipboard, 0, sizeof(clipboard));
    init_mock_context(&context, &callbacks);
    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, "original.txt") == 0) {
            original_index = (int)i;
        } else if (strcmp(state.dir_list.entries[i].name, "multi.txt") == 0) {
            multi_index = (int)i;
        }
    }
    assert(original_index >= 0);
    assert(multi_index >= 0);

    snprintf(context.prompt_response, sizeof(context.prompt_response), "created.txt");
    set_unjoinable_current_path(&state);
    input_handle(&state, &clipboard, input_key_event(DOT_KEY_CREATE_FILE), &callbacks);
    assert(strcmp(context.last_message_title, "Create file failed") == 0);
    assert(access(created_file, F_OK) != 0);

    context.message_count = 0;
    snprintf(context.prompt_response, sizeof(context.prompt_response), "created-dir");
    set_unjoinable_current_path(&state);
    input_handle(&state, &clipboard, input_key_event(DOT_KEY_CREATE_DIR), &callbacks);
    assert(strcmp(context.last_message_title, "Create directory failed") == 0);
    assert(access(created_directory, F_OK) != 0);

    context.message_count = 0;
    snprintf(context.prompt_response, sizeof(context.prompt_response), "renamed.txt");
    state.selected_index = original_index;
    set_unjoinable_current_path(&state);
    input_handle(&state, &clipboard, input_key_event(DOT_KEY_RENAME_ITEM), &callbacks);
    assert(strcmp(context.last_message_title, "Rename failed") == 0);
    assert(access(original_path, F_OK) == 0);
    assert(access(renamed_path, F_OK) != 0);

    context.message_count = 0;
    context.prompt_response[0] = 'y';
    context.prompt_response[1] = '\0';
    state.selected_index = original_index;
    set_unjoinable_current_path(&state);
    input_handle(&state, &clipboard, input_key_event(DOT_KEY_DELETE_ITEM), &callbacks);
    assert(strcmp(context.last_message_title, "Delete failed") == 0);
    assert(access(original_path, F_OK) == 0);

    context.message_count = 0;
    state.dir_list.entries[multi_index].is_selected = true;
    set_unjoinable_current_path(&state);
    input_handle(&state, &clipboard, input_key_event(DOT_KEY_DELETE_ITEM), &callbacks);
    assert(strcmp(context.last_message_title, "Delete failed") == 0);
    assert(access(multi_path, F_OK) == 0);

    state_cleanup(&state);
    teardown_env();
}

static void test_navigation_entries_blocked_actions(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;

    setup_env();
    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    /* Select '.' or '..' entry */
    int nav_idx = -1;
    for (size_t i = 0; i < state.dir_list.count; ++i) {
        if (strcmp(state.dir_list.entries[i].name, ".") == 0 ||
            strcmp(state.dir_list.entries[i].name, "..") == 0) {
            nav_idx = (int)i;
            break;
        }
    }
    if (nav_idx >= 0) {
        state.selected_index = nav_idx;

        /* Rename blocked */
        ctx.message_count = 0;
        input_handle(&state, &cb, input_key_event(DOT_KEY_RENAME_ITEM), &icb);
        assert(ctx.message_count > 0);
        assert(strcmp(ctx.last_message_title, "Rename blocked") == 0);

        /* Delete blocked */
        ctx.message_count = 0;
        input_handle(&state, &cb, input_key_event(DOT_KEY_DELETE_ITEM), &icb);
        assert(ctx.message_count > 0);
        assert(strcmp(ctx.last_message_title, "Delete blocked") == 0);

        /* Copy blocked */
        ctx.message_count = 0;
        input_handle(&state, &cb, input_key_event(DOT_KEY_COPY), &icb);
        assert(ctx.message_count > 0);
        assert(strcmp(ctx.last_message_title, "Copy blocked") == 0);

        /* Cut blocked */
        ctx.message_count = 0;
        input_handle(&state, &cb, input_key_event(DOT_KEY_CUT), &icb);
        assert(ctx.message_count > 0);
        assert(strcmp(ctx.last_message_title, "Cut blocked") == 0);
    }

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
    path_join(f1, sizeof(f1), TEST_INPUT_ROOT, "props.txt");
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
    path_join(f1, sizeof(f1), TEST_INPUT_ROOT, "apple.txt");
    path_join(f2, sizeof(f2), TEST_INPUT_ROOT, "banana.txt");
    write_file(f1, "a");
    write_file(f2, "b");

    state_init(&state);
    state_change_dir(&state, TEST_INPUT_ROOT);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);

    /* A timed-out read is ignored while typing continues. */
    int keys[] = { -1, 'a', 'p', '\n' };
    ctx.key_stream = keys;
    ctx.key_stream_len = sizeof(keys) / sizeof(keys[0]);
    ctx.key_stream_pos = 0;

    input_handle(&state, &cb, input_key_event('/'), &icb);
    assert(state.filter_active);
    assert(strcmp(state.filter_query, "ap") == 0);

    /* Simulate backspace: 'a', 'p', '\b', '\n' */
    int bs_keys[] = { 'a', 'p', '\b', '\n' };
    ctx.key_stream = bs_keys;
    ctx.key_stream_len = sizeof(bs_keys) / sizeof(bs_keys[0]);
    ctx.key_stream_pos = 0;

    input_handle(&state, &cb, input_key_event('/'), &icb);
    assert(state.filter_active);
    assert(strcmp(state.filter_query, "a") == 0);

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

static void test_edge_cases(void) {
    AppState state;
    Clipboard cb;
    MockUIContext ctx;
    InputCallbacks icb;

    /* NULL state */
    input_handle(NULL, NULL, input_key_event('q'), NULL);

    /* Unbound key */
    state_init(&state);
    memset(&cb, 0, sizeof(cb));
    init_mock_context(&ctx, &icb);
    input_handle(&state, &cb, input_key_event(9999), &icb);
    assert(!state.should_quit);

    state_cleanup(&state);
}

int main(void) {
    test_quit_command();
    test_navigation_and_selection();
    test_selection_advances_within_filter();
    test_enter_open_and_back();
    test_history_navigation_keys();
    test_history_navigation_failure_preserves_selection();
    test_refresh_key();
    test_sorting_commands();
    test_clipboard_copy_cut_paste_multi();
    test_create_file_and_dir();
    test_rename_and_delete_multi();
    test_navigation_entries_blocked_actions();
    test_properties();
    test_filter_prompt_interactive();
    test_path_join_failures_block_mutations();
    test_edge_cases();

    printf("test_input: all tests passed\n");
    return 0;
}
