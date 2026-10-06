#ifndef TASK_H
#define TASK_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "utils/platform.h"

/** Lifecycle states reported by task_snapshot. */
typedef enum {
    TASK_IDLE,
    TASK_RUNNING,
    TASK_COMPLETED,
    TASK_FAILED,
    TASK_CANCELLED
} TaskStatus;

/**
 * Asynchronous copy task. Initialize with task_init before using the API.
 * Internal state is protected by mutex; use task functions rather than directly
 * reading mutable fields while the worker may be running.
 */
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    TaskStatus status;
    uint64_t bytes_copied;
    uint64_t current_file_bytes;
    uint64_t total_bytes;
    bool cancel_requested;
    bool thread_joinable;
    char source[PATH_MAX];
    char destination[PATH_MAX];
    char error_path[PATH_MAX];
} Task;

/** Initialize a task and its mutex; false indicates invalid input or mutex failure. */
bool task_init(Task *task);

/** Request cancellation, join any worker, and destroy the task mutex. */
void task_cleanup(Task *task);

/**
 * Start copying source to destination on a worker thread.
 * @return true when the worker starts; false if arguments/source are invalid,
 *         a task is already running, or thread creation fails.
 * @note Source and destination strings are copied into task-owned buffers.
 */
bool task_start_copy(Task *task, const char *source, const char *destination);

/** Request cooperative cancellation of a running copy. Safe to call across threads. */
void task_request_cancel(Task *task);

/** Return whether the task is running, taking the internal mutex. */
bool task_is_running(Task *task);

/** Block until a joinable worker exits, then mark it as reaped. */
void task_reap(Task *task);

/**
 * Copy a consistent task snapshot into any non-NULL output pointers.
 * error_path is written only when non-NULL and error_size is greater than zero.
 */
void task_snapshot(Task *task, TaskStatus *status, uint64_t *bytes_copied,
                   uint64_t *total_bytes, char *error_path, size_t error_size);

#endif // TASK_H
