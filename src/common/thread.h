#ifndef CODEMAP_THREAD_H
#define CODEMAP_THREAD_H

#include <stdbool.h>

/* Minimal one-shot worker thread: start a function, poll whether it has
 * finished, join it. Exists because macOS has no C11 <threads.h> -- this
 * wraps pthreads on macOS/Linux and Win32 threads on Windows, and covers
 * exactly what the in-process project build needs (see
 * render/project_build.h), nothing more.
 *
 * Joining (or a thread_is_done() that returned true) makes everything
 * the worker wrote visible to the caller, so results can be handed back
 * through plain fields in `arg` with no extra locking. */
typedef struct Thread Thread;
typedef void (*ThreadFn)(void *arg);

/* Returns NULL if the thread couldn't be created (fn never runs). */
Thread *thread_start(ThreadFn fn, void *arg);

/* Non-blocking: true once fn has returned. */
bool thread_is_done(Thread *t);

/* Blocks until fn has returned, then frees t. */
void thread_join(Thread *t);

#endif
