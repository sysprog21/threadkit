#include "threadpool.h"

#include <pthread.h>
#include <stdint.h>

#include "logger.h"

typedef struct task_s {
    void (*func)(void *);
    void *arg;
    struct task_s *next;
} task_t;

struct threadpool_internal {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t *threads;
    task_t *head;
    int thread_count;
    int queue_size;
    int shutdown;
    int started;
};

typedef enum { immediate_shutdown = 1, graceful_shutdown = 2 } threadpool_sd_t;

static int threadpool_free(threadpool_t *pool)
{
    if (!pool || pool->started > 0)
        return -1;

    if (pool->threads)
        free(pool->threads);

    while (pool->head->next) {
        task_t *old = pool->head->next;
        pool->head->next = pool->head->next->next;
        free(old);
    }

    return 0;
}

static void *worker(void *arg)
{
    if (!arg) {
        log_err("arg should be type threadpool_t*");
        return NULL;
    }

    threadpool_t *pool = (threadpool_t *) arg;

    while (1) {
        pthread_mutex_lock(&(pool->lock));

        /*  Wait on condition variable, check for spurious wakeups. */
        while ((pool->queue_size == 0) && !(pool->shutdown)) {
            pthread_cond_wait(&(pool->cond), &(pool->lock));
        }

        if ((pool->shutdown == immediate_shutdown) ||
            ((pool->shutdown == graceful_shutdown) && pool->queue_size == 0))
            break;

        task_t *task = pool->head->next;
        if (!task) {
            pthread_mutex_unlock(&(pool->lock));
            continue;
        }

        pool->head->next = task->next;
        pool->queue_size--;

        pthread_mutex_unlock(&(pool->lock));

        (*(task->func))(task->arg);
        /* TODO: memory pool */
        free(task);
    }

    pool->started--;
    pthread_mutex_unlock(&(pool->lock));
    pthread_exit(NULL);

    return NULL;
}
threadpool_t *threadpool_init(int thread_num)
{
    if (thread_num <= 0) {
        log_err("the arg of threadpool_init must greater than 0");
        return NULL;
    }

    threadpool_t *pool;
    if (!(pool = (threadpool_t *) malloc(sizeof(threadpool_t))))
        goto err;

    pool->thread_count = 0;
    pool->queue_size = 0;
    pool->shutdown = 0;
    pool->started = 0;
    pool->threads = (pthread_t *) malloc(sizeof(pthread_t) * thread_num);
    pool->head = (task_t *) malloc(sizeof(task_t)); /* dummy head */

    if (!pool->threads || !pool->head)
        goto err;

    pool->head->func = NULL;
    pool->head->arg = NULL;
    pool->head->next = NULL;

    if (pthread_mutex_init(&(pool->lock), NULL))
        goto err;

    if (pthread_cond_init(&(pool->cond), NULL)) {
        pthread_mutex_destroy(&(pool->lock));
        goto err;
    }

    for (int i = 0; i < thread_num; ++i) {
        if (pthread_create(&(pool->threads[i]), NULL, worker, pool)) {
            threadpool_destroy(pool, 0);
            return NULL;
        }
        log_info("thread: %08x started", (uint32_t) pool->threads[i]);

        pool->thread_count++;
        pool->started++;
    }

    return pool;

err:
    if (pool)
        threadpool_free(pool);

    return NULL;
}

int threadpool_add(threadpool_t *pool, void (*func)(void *), void *arg)
{
    int rc, err = 0;
    if (!pool || !func)
        return -1;

    if (pthread_mutex_lock(&(pool->lock)) != 0)
        return -1;

    if (pool->shutdown) {
        err = tp_already_shutdown;
        goto out;
    }

    // TODO: use a memory pool
    task_t *task = (task_t *) malloc(sizeof(task_t));
    if (!task) {
        log_err("malloc task fail");
        goto out;
    }

    // TODO: use a memory pool
    task->func = func;
    task->arg = arg;
    task->next = pool->head->next;
    pool->head->next = task;

    pool->queue_size++;

    rc = pthread_cond_signal(&(pool->cond));
    check(rc == 0, "pthread_cond_signal");

out:
    if (pthread_mutex_unlock(&pool->lock) != 0) {
        log_err("pthread_mutex_unlock");
        return -1;
    }

    return err;
}

int threadpool_destroy(threadpool_t *pool, bool graceful)
{
    int err = 0;

    if (!pool)
        return tp_invalid;

    if (pthread_mutex_lock(&(pool->lock)))
        return tp_lock_fail;

    do {
        // set the showdown flag of pool and wake up all thread
        if (pool->shutdown) {
            err = tp_already_shutdown;
            break;
        }

        pool->shutdown = (graceful) ? graceful_shutdown : immediate_shutdown;

        if (pthread_cond_broadcast(&(pool->cond))) {
            err = tp_cond_broadcast;
            break;
        }

        if (pthread_mutex_unlock(&(pool->lock))) {
            err = tp_lock_fail;
            break;
        }

        for (int i = 0; i < pool->thread_count; i++) {
            if (pthread_join(pool->threads[i], NULL))
                err = tp_thread_fail;
            log_info("thread %08x exit", (uint32_t) pool->threads[i]);
        }
    } while (0);

    if (!err) {
        pthread_mutex_destroy(&(pool->lock));
        pthread_cond_destroy(&(pool->cond));
        threadpool_free(pool);
    }

    return err;
}
