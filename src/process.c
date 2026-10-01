#include "process.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>
#else
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#endif

bool process_open_file_with_editor(const char *path) {
    struct stat file_stat;
    bool success = false;
    int saved_errno = 0;

    if (!path || stat(path, &file_stat) != 0) {
        return false;
    }
    if (!S_ISREG(file_stat.st_mode)) {
        errno = EISDIR;
        return false;
    }
    if (access(path, R_OK) != 0) {
        return false;
    }

#ifdef _WIN32
    {
        HINSTANCE result = ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
        success = (INT_PTR)result > 32;
        if (!success) {
            errno = EIO;
        }
    }
#else
    {
        const char *editor = getenv("EDITOR");
        wordexp_t words;
        char **editor_argv = NULL;
        size_t editor_argc = 0;
        pid_t child;
        int status;

        memset(&words, 0, sizeof(words));
        if (editor && *editor && wordexp(editor, &words, WRDE_NOCMD) == 0 &&
            words.we_wordc > 0) {
            editor_argc = words.we_wordc;
            editor_argv = calloc(editor_argc + 2, sizeof(*editor_argv));
            if (editor_argv) {
                for (size_t i = 0; i < editor_argc; ++i) {
                    editor_argv[i] = words.we_wordv[i];
                }
                editor_argv[editor_argc] = (char *)path;
            }
        }

        child = fork();
        if (child == 0) {
            if (editor_argv) {
                execvp(editor_argv[0], editor_argv);
            }
            {
                char *fallback[] = { (char *)"nano", (char *)path, NULL };
                execvp(fallback[0], fallback);
            }
            {
                char *fallback[] = { (char *)"vi", (char *)path, NULL };
                execvp(fallback[0], fallback);
            }
            _exit(127);
        }
        if (child < 0) {
            saved_errno = errno;
        } else {
            do {
                success = waitpid(child, &status, 0) >= 0;
            } while (!success && errno == EINTR);
            if (success) {
                success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                if (!success) {
                    saved_errno = WIFEXITED(status) ? EIO : EINTR;
                }
            } else {
                saved_errno = errno;
            }
        }
        free(editor_argv);
        if (editor && *editor && words.we_wordc > 0) {
            wordfree(&words);
        }
    }
#endif

    if (!success) {
        errno = saved_errno ? saved_errno : EIO;
    }
    return success;
}

bool open_file_with_editor(const char *path) {
    return process_open_file_with_editor(path);
}
