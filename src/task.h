#ifndef TASK_H
#define TASK_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

typedef enum {
    TASK_IDLE,
    TASK_RUNNING,
    TASK_COMPLETED,
    TASK_FAILED,
    TASK_CANCELLED
} TaskStatus;

typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    TaskStatus status;
    uint64_t bytes_copied;
    uint64_t current_file_bytes;
    uint64_t total_bytes;
    bool cancel_requested;
    bool thread_started;
    char source[PATH_MAX];
    char destination[PATH_MAX];
    char error_path[PATH_MAX];
} Task;

void task_init(Task *task);
void task_cleanup(Task *task);
bool task_start_copy(Task *task, const char *source, const char *destination);
void task_request_cancel(Task *task);
bool task_is_running(Task *task);
void task_reap(Task *task);
void task_snapshot(Task *task, TaskStatus *status, uint64_t *bytes_copied,
                   uint64_t *total_bytes, char *error_path, size_t error_size);

#endif // TASK_H
