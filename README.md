# ThreadKit: A collection of lightweight threading utilities

This package `ThreadKit` contains the following threading utilities:
1. Thread pool: simple and usable thread pool.
2. Thread tracer: Lightweight inline thread profiler.
3. Skinny mutex: Low-memory-footprint mutexes for POSIX Threads.
4. Tasklet: Very lightweight thread without its own stack.

## Thread pool

Currently, this thread pool implementation
 * Works with pthreads only, but API is intentionally opaque to allow
   other implementations
 * Starts all threads on creation of the thread pool.
 * Reserves one task for signaling the queue is full.
 * Stops and joins all worker threads on destroy.

### Possible enhancements

Allow some additional options:
 * Lazy creation of threads
 * Reduce number of threads automatically
 * Unlimited queue size
 * Kill worker threads on destroy
 * Reduce locking contention

## ThreadTracer

`ThreadTracer` is a lightweight inline profiler that measures wall-time,
cpu-time and premptive context switches for threads.

### Features

ThreadTracer is an inline profiler that is special in the following ways:
* Fully supports multi threaded applications.
* Will never cause your thread to go to sleep because of profiling.
* Will not miss events.
* Will detect if threads were context-switched by scheduler, preemptively or voluntarily.
* Computes duty-cycle for each scope: not just how long it ran, but also how much of that time, it was scheduled on a core.
* Small light weight system, written in C. Just one header and one small implementation file.
* Zero dependencies.

### Limitations
* Doesn't show a live profile, but creates a report after the run, [viewable with Google Chrome](https://www.gamasutra.com/view/news/176420/Indepth_Using_Chrometracing_to_view_your_inline_profiling_data.php).
* Currently does not support asynchronous events that start on one thread, and finish on another.

### Usage

```c
#include "threadtracer.h"

// Each thread that will be generating profiling events needs to be made known to the system.
TT_ENTRY();

// C Programs need to wrap sections of code with a begin and end macro.
TT_BEGIN("simulation");
simulate( dt );
TT_END("simulation");

// When you are done profiling, typically at program end, or earlier, you can generate the profile report.
TT_REPORT();
```

### Viewing the report

Start the Google Chrome browser, and in the URL bar, type `chrome://tracing` and
then load the genererated `threadtracer*.json` file.

![screenshot](https://pbs.twimg.com/media/DNZe7tRVwAAm2_-.png)

Note that for the highlighted task, the detail view shows that the thread got
interrupted once preemptively, which causes it to run on a CPU core for only
81% of the time that the task took to complete.

The shading of the time slices shows the duty cycle: how much of the time was
spend running on a core.

### Skipping samples at launch.

To avoid recording samples right after launch, you can skip the first seconds
of recording with an environment variable. To skip the first five seconds, do:

```shell
$ THREADTRACERSKIP=5 ./foo
ThreadTracer: clock resolution: 1 nsec.
ThreadTracer: skipping the first 5 seconds before recording.
ThreadTracer: Wrote 51780 events (6 discarded) to threadtracer.json
```

### Reference

* [chrome://tracing](https://www.chromium.org/developers/how-tos/trace-event-profiling-tool)
  for their excellent in-browser visualization.

## Skinny mutex

The main kind of lock provided by the pthreads API is the mutex
(`pthread_mutex_t`).  These have a lot of features (enabled though the
attributes set in `pthread_mutexattr_t`), integrate with condition variables,
and handle contention gracefully.

But a drawback is their size.  On Linux, a `pthread_mutex_t` occupies 64 bytes
on  64-bit machines.  If the mutex is protecting a small data structure, this
can lead to unwelcome overheads in memory usage, and reduce the effectiveness
of caches.

Some pthreads implementations also have spinlocks (`pthread_spinlock_t`).
These are smaller (4 bytes on Linux).  But they don't handle contention
gracefully, so they are best used for critical sections containing small
amounts of code that can be verified to have a short bounded running time.

Hence skinny mutexes provide mutexes that occupy one pointer-sized word.
Like pthreads mutexes, they integrate with condition variables and handle
contention gracefully, so code using pthreads mutexes can be easily converted
to use skinny mutexes instead.

Skinny mutexes use atomic operations to when possible (e.g. when locking or
unlocking an uncontended skinny mutex), and fall back to the pthreads
primitives when necessary (e.g. when a lock is contended causing a thread to
block). So you will still need to compile with `-pthread`. Performance should
generally be similar to pthreads mutexes, and it might even be better in some
cases.

   Pthread                  |  Skinny mutex
----------------------------|-----------------
`pthread_mutex_t`           | `skinny_mutex_t`
`pthread_mutex_init`        | `skinny_mutex_init`
`pthread_mutex_destroy`     | `skinny_mutex_destroy`
`pthread_mutex_lock`        | `skinny_mutex_lock`
`pthread_mutex_unlock`      | `skinny_mutex_unlock`
`pthread_mutex_trylock`     | `skinny_mutex_trylock`
`pthread_cond_wait`         | `skinny_mutex_cond_wait`
`pthread_cond_timedwait`    | `skinny_mutex_cond_timedwait`
`PTHREAD_MUTEX_INITIALIZER` | `SKINNY_MUTEX_INITIALIZER`

Note that `skinny_mutex_init` does not take an attributes argument (see below
for more details).  Other than that, all the arguments of the functions
mentioned  correspond to the pthreads ones, and their specifications and
return values are intended to correspond exactly.

In particular, `skinny_mutex_lock` is not a thread cancellation point, and
`skinny_mutex_cond_wait` is.

### Limitations compared to `pthread_mutex`

Unlike pthreads mutexes, skinny mutexes do not currently support any mutex
attributes. Their behavior corresponds to the default pthread mutex
attributes (i.e. with `NULL` passed as the second argument to
`pthread_mutex_init`).

It is possible to add support for error checking corresponding to the
`PTHREAD_MUTEX_ERRORCHECK` type attribute (from `pthread_mutexattr_settype`).
This will probably be a compile-time option.

It seems feasible to add support for the protocol attribute
(`PTHREAD_PRIO_INHERIT` and `PTHREAD_PRIO_PROTECT` from
`pthread_mutexattr_setprotocol`). There might be room for improvements.

The `PTHREAD_MUTEX_RECURSIVE` type attribute will not be supported, as
it would require `skinny_mutex_t` to grow, and you can rewrite code to
avoid the need for recursive mutexes.

Support for the process-shared and priority ceiling attributes
(`pthread_mutexattr_setpshared` and
`pthread_mutexattr_setprioceiling`) is also unlikely, as they seem to
be of marginal usefulness and/or hard to implement.

## Tasklet

A tasklet is a sequential context of execution.  Like a thread, a tasklet can
wait for events (such as data arriving on a socket). Unlike a thread,
a tasklet does not have its own stack, so tasklet code has to be follow
certain idioms. But those idioms are less cumbersome than trying to write
callback-based code in C, particularly in a multithreaded context.

Tasklets are very lightweight; many millions of tasklets could fit in the
memory of a modern machine. A scalable service can schedule runnable tasklets
onto a much smaller number of threads.
