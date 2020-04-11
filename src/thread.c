#include "thread.h"

#include <signal.h>
#include <stdlib.h>

struct thread_params {
    void (*func)(void *data);
    void *data;
};

static void *thread_trampoline(void *v_params)
{
    struct thread_params params = *(struct thread_params *) v_params;
    free(v_params);
    params.func(params.data);
    return NULL;
}

void thread_init(struct thread *thr, void (*func)(void *data), void *data)
{
    struct thread_params *params = malloc(sizeof *params);
    params->func = func;
    params->data = data;
    pthread_create(&thr->handle, NULL, thread_trampoline, params);
    thr->init = malloc(1);
}

void thread_fini(struct thread *thr)
{
    pthread_join(thr->handle, NULL);
    free(thr->init);
}

void thread_signal(thread_handle_t thr, int sig)
{
    pthread_kill(thr, sig);
}

void mutex_init(struct mutex *m)
{
    skinny_mutex_init(&m->mutex);
    m->init = malloc(1);
    m->held = false;
}

void mutex_fini(struct mutex *m)
{
    assert(!m->held);
    free(m->init);
    skinny_mutex_destroy(&m->mutex);
}

void mutex_lock(struct mutex *m)
{
    skinny_mutex_lock(&m->mutex);
    m->held = true;
}

void mutex_unlock(struct mutex *m)
{
    assert(m->held);
    m->held = false;
    skinny_mutex_unlock(&m->mutex);
}

bool mutex_transfer(struct mutex *a, struct mutex *b)
{
    assert(a->held);
    a->held = false;
    int res = skinny_mutex_transfer(&a->mutex, &b->mutex);
    if (res != EAGAIN) {
        b->held = true;
        return true;
    }
    a->held = true;
    return false;
}

void mutex_veto_transfer(struct mutex *m)
{
    assert(m->held);
    skinny_mutex_veto_transfer(&m->mutex);
}

void cond_init(struct cond *c)
{
    pthread_cond_init(&c->cond, NULL);
    c->init = malloc(1);
}

void cond_fini(struct cond *c)
{
    free(c->init);
    pthread_cond_destroy(&c->cond);
}

void cond_wait(struct cond *c, struct mutex *m)
{
    mutex_assert_held(m);
    m->held = false;
    skinny_mutex_cond_wait(&c->cond, &m->mutex);
    m->held = true;
}

void cond_signal(struct cond *c)
{
    pthread_cond_signal(&c->cond);
}

void cond_broadcast(struct cond *c)
{
    pthread_cond_broadcast(&c->cond);
}
