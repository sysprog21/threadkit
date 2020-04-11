#ifndef TASKLET_H
#define TASKLET_H

#include <stdbool.h>

#include "thread.h"

struct tasklet {
    struct mutex *mutex;

    void (*handler)(void *);
    void *data;

    struct mutex wait_mutex;
    struct wait_list *wait; /* Covered by wait_mutex */
    int unwaiting;          /* Covered by wait_mutex */
    bool waited;
    struct tasklet *wait_next; /* Covered by wait's mutex */
    struct tasklet *wait_prev; /* Ditto */

    struct run_queue *runq;    /* Set using atomic ops */
    struct tasklet *runq_next; /* Covered by runq's mutex */
    struct tasklet *runq_prev; /* Ditto */
};

struct wait_list {
    struct mutex mutex;
    void *head;
    int unwaiting;
    int up_count;
};

struct run_queue *run_queue_create(void);

/* Set the preferred run queue for this thread. */
void run_queue_target(struct run_queue *runq);

/* Serve a run queue.  Returns once the run queue is drained.
 * If 'wait' is set, waits until tasklets arrive.
 */
void run_queue_run(struct run_queue *runq, int wait);

void tasklet_init(struct tasklet *tasklet, struct mutex *mutex, void *data);
void tasklet_fini(struct tasklet *t);
void tasklet_stop(struct tasklet *t);
void tasklet_run(struct tasklet *t);

static inline void tasklet_set_handler(struct tasklet *t,
                                       void (*handler)(void *))
{
    mutex_assert_held(t->mutex);
    t->handler = handler;
}

static inline void tasklet_goto(struct tasklet *t, void (*handler)(void *))
{
    tasklet_set_handler(t, handler);
    handler(t->data);
}

static inline void tasklet_later(struct tasklet *t, void (*handler)(void *))
{
    t->handler = handler;
    tasklet_run(t);
}

void wait_list_init(struct wait_list *w, int up_count);
void wait_list_fini(struct wait_list *w);
void wait_list_up(struct wait_list *w, int n);
bool wait_list_down(struct wait_list *w, int n, struct tasklet *t);
void wait_list_set(struct wait_list *w, int n, bool broadcast);
bool wait_list_nonempty(struct wait_list *w);

void wait_list_wait(struct wait_list *w, struct tasklet *t);
void wait_list_broadcast(struct wait_list *w);

#endif
