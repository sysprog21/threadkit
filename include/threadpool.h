#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stdbool.h>

typedef struct threadpool_internal threadpool_t;

typedef enum {
    tp_invalid = -1,
    tp_lock_fail = -2,
    tp_already_shutdown = -3,
    tp_cond_broadcast = -4,
    tp_thread_fail = -5,
} threadpool_error_t;

/**
 * @brief Creates a threadpool_t object.
 * @param thread_num Number of worker threads.
 */
threadpool_t *threadpool_init(int thread_num);

/**
 * @brief add a new task in the queue of a thread pool.
 * @param pool Thread pool to which add the task.
 * @param func Pointer to the function that will perform the task.
 * @param arg Argument to be passed to the function.
 * @return 0 if all goes well, negative values in case of error (@see
 *           threadpool_error_t for codes).
 */
int threadpool_add(threadpool_t *pool, void (*func)(void *), void *arg);

/**
 * @brief Stops and destroys a thread pool.
 * @param pool Thread pool to destroy.
 * @param gracegul The thread pool does not accept any new tasks but
 *                 processes all pending tasks before shutdown.
 */
int threadpool_destroy(threadpool_t *pool, bool gracegul);

#endif
