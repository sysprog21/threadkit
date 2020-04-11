#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "threadpool.h"
#include "threadtracer.h"

#define THREAD 4
#define SIZE 8192

static threadpool_t *pool[1];
static int tasks[SIZE], left;
static pthread_mutex_t lock;

static void dummy_task(void *arg)
{
    int *pi = (int *) arg;
    *pi += 1;

    TT_ENTRY(__func__);

    TT_BEGIN(__func__);
    if (*pi < 1) {
        assert(threadpool_add(pool[*pi], &dummy_task, arg) == 0);
    } else {
        pthread_mutex_lock(&lock);
        left--;
        pthread_mutex_unlock(&lock);
    }
    TT_END(__func__);
}

int main()
{
    left = SIZE;
    pthread_mutex_init(&lock, NULL);

    pool[0] = threadpool_init(THREAD);
    assert(pool[0] != NULL);

    usleep(10);

    for (int i = 0; i < SIZE; i++) {
        tasks[i] = 0;
        assert(threadpool_add(pool[0], &dummy_task, &(tasks[i])) == 0);
    }

    int copy = 1;
    while (copy > 0) {
        usleep(10);
        pthread_mutex_lock(&lock);
        copy = left;
        pthread_mutex_unlock(&lock);
    }

    assert(threadpool_destroy(pool[0], 0) == 0);

    pthread_mutex_destroy(&lock);

    TT_REPORT();
    return 0;
}
