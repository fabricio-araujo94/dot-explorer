#include "utils.h"
#include "utils/theme.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void test_format_size(void) {
    char buf[64];

    format_size(0, buf, sizeof(buf));
    assert(strcmp(buf, "0 B") == 0);

    format_size(500, buf, sizeof(buf));
    assert(strcmp(buf, "500 B") == 0);

    format_size(1024, buf, sizeof(buf));
    assert(strcmp(buf, "1.0 KB") == 0);

    format_size(1536, buf, sizeof(buf));
    assert(strcmp(buf, "1.5 KB") == 0);

    format_size(1024 * 1024, buf, sizeof(buf));
    assert(strcmp(buf, "1.0 MB") == 0);

    format_size(1024ULL * 1024 * 1024 * 5, buf, sizeof(buf));
    assert(strcmp(buf, "5.0 GB") == 0);
}

static void test_utils_join_path(void) {
    char result[PATH_MAX];

    /* Standard join */
    assert(utils_join_path(result, sizeof(result), "/tmp", "file.txt"));
    assert(strcmp(result, "/tmp/file.txt") == 0);

    /* Base ending with slash */
    assert(utils_join_path(result, sizeof(result), "/tmp/", "file.txt"));
    assert(strcmp(result, "/tmp/file.txt") == 0);

    /* Name starting with slash */
    assert(utils_join_path(result, sizeof(result), "/tmp", "/file.txt"));
    assert(strcmp(result, "/tmp/file.txt") == 0);

    /* Base empty */
    assert(utils_join_path(result, sizeof(result), "", "file.txt"));
    assert(strcmp(result, "file.txt") == 0);

    /* Null checks & invalid arguments */
    errno = 0;
    assert(!utils_join_path(NULL, sizeof(result), "/tmp", "file.txt"));
    assert(errno == EINVAL);
    assert(!utils_join_path(result, 0, "/tmp", "file.txt"));
    assert(errno == EINVAL);
    assert(!utils_join_path(result, sizeof(result), NULL, "file.txt"));
    assert(errno == EINVAL);
    assert(!utils_join_path(result, sizeof(result), "/tmp", NULL));
    assert(errno == EINVAL);

    /* Small buffer overflow check */
    char small_buf[8];
    errno = 0;
    assert(!utils_join_path(small_buf, sizeof(small_buf), "/tmp", "very_long_file_name.txt"));
    assert(errno == ENAMETOOLONG);
}

static void test_path_basename(void) {
    assert(strcmp(path_basename("/tmp/file.txt"), "file.txt") == 0);
    assert(strcmp(path_basename("C:\\tmp\\file.txt"), "file.txt") == 0);
    assert(strcmp(path_basename("file.txt"), "file.txt") == 0);
}

static void test_platform_realpath_and_mkdir(void) {
    char resolved[PATH_MAX];

    assert(platform_realpath(".", resolved, sizeof(resolved)));
    assert(strlen(resolved) > 0);

    /* Buffer too small */
    char small_buf[4];
    errno = 0;
    assert(!platform_realpath(".", small_buf, sizeof(small_buf)));
    assert(errno == ENAMETOOLONG);

    /* Nonexistent path */
    assert(!platform_realpath("/nonexistent_path_dot_explorer_12345", resolved, sizeof(resolved)));
}

static void test_theme_and_icons(void) {
    FileTheme theme;

    /* Directory */
    theme = get_file_color_and_icon(S_IFDIR | 0755, "");
    assert(theme.color_pair == 5); /* THEME_PAIR_DIRECTORY */
    assert(strcmp(theme.icon, "\xEF\x81\xBB") == 0);

    /* Symlink */
    theme = get_file_color_and_icon(S_IFLNK | 0777, "");
    assert(theme.color_pair == 9); /* THEME_PAIR_SYMLINK */

    /* Executable file */
    theme = get_file_color_and_icon(S_IFREG | 0755, "");
    assert(theme.color_pair == 6); /* THEME_PAIR_EXECUTABLE */

    /* Image extension */
    theme = get_file_color_and_icon(S_IFREG | 0644, ".png");
    assert(theme.color_pair == 7); /* THEME_PAIR_IMAGE */

    theme = get_file_color_and_icon(S_IFREG | 0644, ".JPG");
    assert(theme.color_pair == 7); /* Case insensitive */

    /* Archive extension */
    theme = get_file_color_and_icon(S_IFREG | 0644, ".tar.gz");
    /* .tar.gz checks ends with or extension */
    theme = get_file_color_and_icon(S_IFREG | 0644, ".zip");
    assert(theme.color_pair == 8); /* THEME_PAIR_ARCHIVE */

    /* Normal text file */
    theme = get_file_color_and_icon(S_IFREG | 0644, ".txt");
    assert(theme.color_pair == 2); /* THEME_PAIR_NORMAL */
}

int main(void) {
    test_format_size();
    test_utils_join_path();
    test_path_basename();
    test_platform_realpath_and_mkdir();
    test_theme_and_icons();
    puts("test_utils: all tests passed");
    return 0;
}
