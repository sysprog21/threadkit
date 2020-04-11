#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "threadpool.h"
#include "threadtracer.h"

#define THREAD 4
#define SIZE 8192

threadpool_t *pool;
int left;
pthread_mutex_t lock;

int error;

void dummy_task(void *arg UNUSED)
{
    TT_ENTRY(__func__);

    usleep(100);
    TT_BEGIN(__func__);
    pthread_mutex_lock(&lock);
    left--;
    pthread_mutex_unlock(&lock);
    TT_END(__func__);
}

int main()
{
    pthread_mutex_init(&lock, NULL);

    /* Testing immediate shutdown */
    left = SIZE;
    pool = threadpool_init(THREAD);
    for (int i = 0; i < SIZE; i++)
        assert(threadpool_add(pool, &dummy_task, NULL) == 0);
    assert(threadpool_destroy(pool, false) == 0);
    assert(left > 0);
    TT_REPORT();

    /* Testing graceful shutdown */
    left = SIZE;
    pool = threadpool_init(THREAD);
    for (int i = 0; i < SIZE; i++)
        assert(threadpool_add(pool, &dummy_task, NULL) == 0);
    assert(threadpool_destroy(pool, true) == 0);
    assert(left == 0);
    TT_REPORT();

    pthread_mutex_destroy(&lock);

    return 0;
}
