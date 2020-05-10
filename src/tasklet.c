#include "tasklet.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define pointer_bits(p) ((uintptr_t)(p) &3)
#define pointer_clear_bits(p) ((void *) ((uintptr_t)(p) & -4))
#define pointer_set_bits(p, bits) ((void *) ((uintptr_t)(p) | (bits)))

void tasklet_init(struct tasklet *tasklet, struct mutex *mutex, void *data)
{
    tasklet->mutex = mutex;
    tasklet->handler = NULL;
    tasklet->data = data;
    mutex_init(&tasklet->wait_mutex);
    tasklet->wait = NULL;
    tasklet->unwaiting = 0;
    tasklet->runq = NULL;
}

struct run_queue {
    /* Linked list of all run queues. */
    struct run_queue *next;

    struct mutex mutex;
    struct tasklet *head;
    struct tasklet *current;
    enum { CURRENT_STARTED, CURRENT_STOPPED, CURRENT_REQUEUE } current_state;

    bool stop_waiting;
    bool worker_waiting;
    thread_handle_t thread;
    struct cond cond;
};

/* Threads can hold a run_queue reference without holding any locks.
   So run_queues cannot simply be freed.  At some stage it might be
   worth introducing RCU-like grace periods to determine when
   run_queues can be freed.  But for now, we don't expect many of them
   to get allocated, so we simply clean them up on exit to keep
   valgrind happy. */

static struct run_queue *run_queues;

static void run_queue_destroy(struct run_queue *runq)
{
    assert(!runq->head);
    assert(!runq->current);
    mutex_fini(&runq->mutex);
    cond_fini(&runq->cond);
    free(runq);
}

static void cleanup_run_queues(void)
{
    struct run_queue *runq = run_queues;

    while (runq) {
        struct run_queue *next = runq->next;
        run_queue_destroy(runq);
        runq = next;
    }
}

static struct run_queue *run_queue_create_unlinked(void)
{
    struct run_queue *runq = malloc(sizeof *runq);

    mutex_init(&runq->mutex);
    runq->head = runq->current = NULL;
    runq->stop_waiting = false;
    runq->worker_waiting = false;
    cond_init(&runq->cond);

    return runq;
}

static void add_to_run_queues(struct run_queue *runq)
{
    do {
        runq->next = run_queues;
    } while (!__sync_bool_compare_and_swap(&run_queues, runq->next, runq));

    if (!runq->next)
        atexit(cleanup_run_queues);
}

struct run_queue *run_queue_create(void)
{
    struct run_queue *runq = run_queue_create_unlinked();
    add_to_run_queues(runq);
    return runq;
}

/* A dedicated run_queue worker thread */
struct worker {
    struct run_queue *runq;
    struct thread thread;

    /* Only used when we are stopping the worker */
    struct mutex mutex;
    struct tasklet tasklet;
};

static void worker_thread(void *v_worker);

static struct worker *worker_create(struct run_queue *runq)
{
    struct worker *w = malloc(sizeof *w);
    w->runq = runq;
    thread_init(&w->thread, worker_thread, w);
    return w;
}

static void stop_worker(void *v_worker)
{
    struct worker *w = v_worker;
    w->runq = NULL;
    tasklet_stop(&w->tasklet);
}

static void worker_destroy(struct worker *w)
{
    mutex_init(&w->mutex);
    tasklet_init(&w->tasklet, &w->mutex, w);
    tasklet_later(&w->tasklet, stop_worker);
    thread_fini(&w->thread);
    mutex_lock(&w->mutex);
    tasklet_fini(&w->tasklet);
    mutex_unlock_fini(&w->mutex);
    free(w);
}

static struct run_queue *default_run_queue;
static struct worker *default_worker;

static void cleanup_default_worker(void)
{
    worker_destroy(default_worker);
}

TLS_VAR_DECLARE_STATIC(tls_run_queue);

void run_queue_target(struct run_queue *runq)
{
    TLS_VAR_SET(tls_run_queue, runq);
}

static struct run_queue *thread_run_queue(void)
{
    struct run_queue *runq = TLS_VAR_GET(tls_run_queue);
    if (runq)
        return runq;

    for (;;) {
        runq = default_run_queue;
        if (runq)
            return runq;

        runq = run_queue_create_unlinked();
        if (__sync_bool_compare_and_swap(&default_run_queue, NULL, runq))
            break;

        run_queue_destroy(runq);
    }

    /* The order is important here - we want cleanup_run_queues to
       come after cleanup_default_worker. */
    add_to_run_queues(runq);

    default_worker = worker_create(runq);
    atexit(cleanup_default_worker);

    return runq;
}

static void run_queue_enqueue(struct run_queue *runq, struct tasklet *t)
{
    struct tasklet *head;

    mutex_assert_held(&runq->mutex);
    assert(t->runq == runq);

    head = runq->head;
    if (!head) {
        runq->head = t->runq_next = t->runq_prev = t;

        if (runq->worker_waiting)
            cond_signal(&runq->cond);
    } else {
        struct tasklet *prev = head->runq_prev;
        t->runq_next = head;
        t->runq_prev = prev;
        prev->runq_next = head->runq_prev = t;
    }
}

static void run_queue_remove(struct run_queue *runq, struct tasklet *t)
{
    struct tasklet *next, *prev;

    mutex_assert_held(&runq->mutex);
    assert(t->runq == runq);

    next = t->runq_next;
    prev = t->runq_prev;
    next->runq_prev = prev;
    prev->runq_next = next;

    if (runq->head == t)
        runq->head = (next == t ? NULL : next);
}

/* The tasklet lock does not need to be held for this. */
void tasklet_run(struct tasklet *t)
{
    bool done = false;

    do {
        struct run_queue *runq = t->runq;
        if (!runq) {
            runq = thread_run_queue();
            mutex_lock(&runq->mutex);

            if (__sync_bool_compare_and_swap(&t->runq, NULL, runq)) {
                run_queue_enqueue(runq, t);
                done = true;
            }
        } else {
            mutex_lock(&runq->mutex);

            if (t->runq == runq) {
                if (runq->current == t)
                    runq->current_state = CURRENT_REQUEUE;

                done = true;
            }
        }

        mutex_unlock(&runq->mutex);
    } while (!done);
}


void wait_list_init(struct wait_list *w, int up_count)
{
    mutex_init(&w->mutex);
    w->head = NULL;
    w->unwaiting = 0;
    w->up_count = up_count;
}

void wait_list_fini(struct wait_list *w)
{
    struct tasklet *head;

    mutex_lock(&w->mutex);

    head = w->head;
    if (head) {
        struct tasklet *t = head;

        /* Remove all tasklets from the linked list */
        do {
            struct tasklet *next = t->wait_next;

            tasklet_run(t);

            mutex_lock(&t->wait_mutex);
            w->unwaiting += t->unwaiting;
            t->wait = NULL;
            t->unwaiting = 0;
            mutex_unlock(&t->wait_mutex);

            t = next;
        } while (t != head);

        /* If other threads are waiting on the wait_list mutex
           to remove themselves, allow them to proceed and
           wait until they are done. */
        if (w->unwaiting) {
            struct cond cond;

            cond_init(&cond);
            w->head = pointer_set_bits(&cond, 1);

            do {
                cond_wait(&cond, &w->mutex);
            } while (w->unwaiting);

            cond_fini(&cond);
        }

        w->head = NULL;
    }

    mutex_unlock_fini(&w->mutex);
}

static void tasklet_unwait(struct tasklet *t)
{
    struct wait_list *w;
    struct tasklet *next;

    mutex_lock(&t->wait_mutex);

    for (;;) {
        w = t->wait;
        if (!w) {
            /* Tasklet is not on a wait_list */
            mutex_unlock(&t->wait_mutex);
            return;
        }

        t->unwaiting++;
        mutex_unlock(&t->wait_mutex);
        mutex_lock(&w->mutex);
        mutex_lock(&t->wait_mutex);

        /* The tasklet could have been removed from the
           waitlist, or even be on a different waitlist by
           now. If so, we need to start again. */
        if (t->wait == w)
            break;

        if (!--w->unwaiting && pointer_bits(w->head)) {
            /* Dropping the last reference to the
               wait_list, so wake up the wait_list_fini
               caller. */
            struct cond *cond = pointer_clear_bits(w->head);
            cond_signal(cond);
        }

        mutex_unlock(&w->mutex);
    }

    /* Remove t from the waitlist */
    t->wait = NULL;

    /* Other threads may be accounted for in t->unwaiting.
       We need to record them in the wait_list. */
    w->unwaiting += t->unwaiting - 1;
    t->unwaiting = 0;

    next = t->wait_next;
    t->wait_prev->wait_next = next;
    next->wait_prev = t->wait_prev;

    if (w->head == t) {
        if (next == t) {
            w->head = NULL;
        } else {
            w->head = next;
            if (w->up_count)
                tasklet_run(next);
        }
    }

    mutex_unlock(&t->wait_mutex);
    mutex_unlock(&w->mutex);
}

static void wait_list_broadcast_locked(struct wait_list *w)
{
    struct tasklet *head;

    mutex_assert_held(&w->mutex);

    head = w->head;
    if (head) {
        struct tasklet *t = head;
        do {
            tasklet_run(t);
            t = t->wait_next;
        } while (t != head);
    }
}

void wait_list_broadcast(struct wait_list *w)
{
    mutex_lock(&w->mutex);
    wait_list_broadcast_locked(w);
    mutex_unlock(&w->mutex);
}

void wait_list_set(struct wait_list *w, int n, bool broadcast)
{
    mutex_lock(&w->mutex);
    w->up_count = n;
    if (broadcast)
        wait_list_broadcast_locked(w);
    mutex_unlock(&w->mutex);
}

static void wait_list_add(struct wait_list *w, struct tasklet *t)
{
    t->wait = w;

    if (!w->head) {
        w->head = t->wait_next = t->wait_prev = t;
        if (w->up_count)
            tasklet_run(t);
    } else {
        struct tasklet *head = w->head;
        struct tasklet *prev = head->wait_prev;
        t->wait_next = head;
        t->wait_prev = prev;
        head->wait_prev = prev->wait_next = t;
    }
}

void wait_list_wait(struct wait_list *w, struct tasklet *t)
{
    for (;;) {
        int done = false;

        mutex_lock(&w->mutex);
        mutex_lock(&t->wait_mutex);

        if (!t->wait) {
            wait_list_add(w, t);
            done = true;
        } else if (t->wait == w) {
            done = true;
        }

        mutex_unlock(&t->wait_mutex);
        mutex_unlock(&w->mutex);

        if (done)
            break;

        tasklet_unwait(t);
    }

    t->waited = true;
}

bool wait_list_down(struct wait_list *w, int n, struct tasklet *t)
{
    int res;

    for (;;) {
        int done = false;

        mutex_lock(&w->mutex);
        mutex_lock(&t->wait_mutex);

        if (!t->wait || t->wait == w) {
            if (w->up_count >= n) {
                w->up_count -= n;
                res = true;
            } else {
                if (t->wait != w)
                    wait_list_add(w, t);

                t->waited = true;
                res = false;
            }

            done = true;
        }

        mutex_unlock(&t->wait_mutex);
        mutex_unlock(&w->mutex);

        if (done)
            break;

        tasklet_unwait(t);
    }

    return res;
}

void wait_list_up(struct wait_list *w, int n)
{
    mutex_lock(&w->mutex);

    w->up_count += n;
    if (w->head)
        tasklet_run(w->head);

    mutex_unlock(&w->mutex);
}

bool wait_list_nonempty(struct wait_list *w)
{
    return !!w->head;
}

void run_queue_run(struct run_queue *runq, int wait)
{
    struct tasklet *t;

    mutex_lock(&runq->mutex);

    t = runq->head;
    if (!t) {
        if (!wait)
            goto out;

        runq->worker_waiting = true;
        do {
            cond_wait(&runq->cond, &runq->mutex);
            t = runq->head;
        } while (!t);
        runq->worker_waiting = false;
    }

    runq->thread = thread_handle_current();

    do {
        run_queue_remove(runq, t);
        runq->current = t;
        t->waited = false;

        for (;;) {
            runq->current_state = CURRENT_STARTED;
            if (mutex_transfer(&runq->mutex, t->mutex))
                break;

            /* mutex_transfer can fail because of a veto
               aimed at a tasket on another run queue but
               using the same mutex.  So we have to check
               that the current tasklet was really
               stopped. */
            if (runq->current_state == CURRENT_STOPPED)
                goto next;
        }

        t->handler(t->data);

        mutex_lock(&runq->mutex);
        if (runq->current != t)
            /* tasklet was destroyed */
            goto next;

        switch (runq->current_state) {
        case CURRENT_STARTED:
            /* Detect dangling tasklets that are not on a
               waitlist and were not explicitly
               stopped. */
            assert(t->waited);
            assert(t->wait);

            /* fall through */
        case CURRENT_STOPPED:
            t->runq = NULL;
            break;

        case CURRENT_REQUEUE:
            run_queue_enqueue(runq, t);
        }

        mutex_unlock(t->mutex);

    next:
        if (runq->stop_waiting) {
            runq->stop_waiting = false;
            cond_broadcast(&runq->cond);
        }

        t = runq->head;
    } while (t);

    runq->current = NULL;

out:
    mutex_unlock(&runq->mutex);
}

void tasklet_stop(struct tasklet *t)
{
    mutex_assert_held(t->mutex);
    tasklet_unwait(t);

    for (;;) {
        struct run_queue *runq = t->runq;
        if (!runq)
            break;

        mutex_lock(&runq->mutex);

        if (t->runq == runq) {
            if (runq->current != t) {
                run_queue_remove(runq, t);
                t->runq = NULL;
            } else {
                runq->current_state = CURRENT_STOPPED;

                if (thread_handle_current() != runq->thread) {
                    mutex_veto_transfer(t->mutex);

                    /* Wait until the tasklet is done */
                    runq->stop_waiting = true;

                    do
                        cond_wait(&runq->cond, &runq->mutex);
                    while (runq->current == t);
                }
            }

            mutex_unlock(&runq->mutex);
            break;
        }

        mutex_unlock(&runq->mutex);
    }
}

void tasklet_fini(struct tasklet *t)
{
    mutex_assert_held(t->mutex);
    tasklet_unwait(t);

    for (;;) {
        struct run_queue *runq = t->runq;
        if (!runq)
            break;

        mutex_lock(&runq->mutex);

        if (t->runq == runq) {
            if (runq->current != t) {
                run_queue_remove(runq, t);
                t->runq = NULL;
            } else {
                runq->current_state = CURRENT_STOPPED;

                if (thread_handle_current() == runq->thread) {
                    runq->current = NULL;
                } else {
                    mutex_veto_transfer(t->mutex);

                    /* Wait until the tasklet is done */
                    runq->stop_waiting = true;

                    do
                        cond_wait(&runq->cond, &runq->mutex);
                    while (runq->current == t);
                }
            }

            mutex_unlock(&runq->mutex);
            break;
        }

        mutex_unlock(&runq->mutex);
    }

    t->mutex = NULL;
    t->handler = NULL;
    t->data = NULL;
    mutex_fini(&t->wait_mutex);
}

static void worker_thread(void *v_worker)
{
    struct worker *w = v_worker;

    for (;;) {
        struct run_queue *runq = w->runq;
        if (!runq)
            break;

        run_queue_run(runq, true);
    }
}
