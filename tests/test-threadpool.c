#include <pthread.h>

#include "logger.h"
#include "threadpool.h"
#include "threadtracer.h"

#define THREAD_NUM 4

pthread_mutex_t lock;
size_t sum = 0;

static void sum_n(void *arg)
{
    size_t n = (size_t) arg;

    TT_ENTRY(__func__);

    TT_BEGIN(__func__);
    int rc = pthread_mutex_lock(&lock);
    check_exit(rc == 0, "pthread_mutex_lock error");

    sum += n;

    rc = pthread_mutex_unlock(&lock);
    check_exit(rc == 0, "pthread_mutex_unlock error");
    TT_END(__func__);
}

int main()
{
    check_exit(pthread_mutex_init(&lock, NULL) == 0, "lock init error");

    threadpool_t *tp = threadpool_init(THREAD_NUM);
    check_exit(tp != NULL, "threadpool_init error");

    for (size_t i = 1; i < 16; i++) {
        int rc = threadpool_add(tp, sum_n, (void *) i);
        check_exit(rc == 0, "threadpool_add error");
    }

    check_exit(threadpool_destroy(tp, 1) == 0, "threadpool_destroy error");

    check_exit(sum == 120, "sum error");

    TT_REPORT();
    return 0;
}
