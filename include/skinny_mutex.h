#ifndef SKINNY_MUTEX_H
#define SKINNY_MUTEX_H

#include <errno.h>
#include <pthread.h>
#include <time.h>

typedef struct {
    void *val;
} skinny_mutex_t;

static inline int skinny_mutex_init(skinny_mutex_t *m)
{
    m->val = 0;
    return 0;
}

static inline int skinny_mutex_destroy(skinny_mutex_t *m)
{
    return !m->val ? 0 : EBUSY;
}

#define SKINNY_MUTEX_INITIALIZER \
    {                            \
        (void *) 0               \
    }

int skinny_mutex_lock_slow(skinny_mutex_t *m);

static inline int skinny_mutex_lock(skinny_mutex_t *m)
{
    if (__builtin_expect(
            __sync_bool_compare_and_swap(&m->val, (void *) 0, (void *) 1), 1))
        return 0;
    return skinny_mutex_lock_slow(m);
}

int skinny_mutex_unlock_slow(skinny_mutex_t *m);

static inline int skinny_mutex_unlock(skinny_mutex_t *m)
{
    if (__builtin_expect(
            __sync_bool_compare_and_swap(&m->val, (void *) 1, (void *) 0), 1))
        return 0;
    return skinny_mutex_unlock_slow(m);
}

int skinny_mutex_trylock(skinny_mutex_t *m);
int skinny_mutex_cond_wait(pthread_cond_t *cond, skinny_mutex_t *m);
int skinny_mutex_cond_timedwait(pthread_cond_t *cond,
                                skinny_mutex_t *m,
                                const struct timespec *abstime);

int skinny_mutex_transfer(skinny_mutex_t *a, skinny_mutex_t *b);
int skinny_mutex_veto_transfer(skinny_mutex_t *m);

#endif /* SKINNY_MUTEX_H */
