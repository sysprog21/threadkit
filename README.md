# A lightweight thread pool implementation with inline profiler

Currently, this thread pool implementation
 * Works with pthreads only, but API is intentionally opaque to allow
   other implementations
 * Starts all threads on creation of the thread pool.
 * Reserves one task for signaling the queue is full.
 * Stops and joins all worker threads on destroy.

## Possible enhancements

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

Add threadtracer.c to your project, and compile your sources with
`-D_GNU_SOURCE` flag so that `RUSAGE_THREAD` support is available for
the `getrusage()` call. Thus, it is only valid to Linux and FreeBSD.

### Viewing the report

Start the Google Chrome browser, and in the URL bar, type ```chrome://tracing``` and then load the genererated threadtracer.json file.

![screenshot](https://pbs.twimg.com/media/DNZe7tRVwAAm2_-.png)

Note that for the highlighted task, the detail view shows that the thread got interrupted once preemptively, which causes it to run on a CPU core for only 81% of the time that the task took to complete.

The shading of the time slices shows the duty cycle: how much of the time was spend running on a core.

### Skipping samples at launch.

To avoid recording samples right after launch, you can skip the first seconds of recording with an environment variable. To skip the first five seconds, do:

```
$ THREADTRACERSKIP=5 ./foo
ThreadTracer: clock resolution: 1 nsec.
ThreadTracer: skipping the first 5 seconds before recording.
ThreadTracer: Wrote 51780 events (6 discarded) to threadtracer.json
```

## Acknowledgements

* [chrome://tracing](https://www.chromium.org/developers/how-tos/trace-event-profiling-tool) for their excellent in-browser visualization.
