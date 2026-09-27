#include "thread.h"
#include <stdlib.h>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

struct Thread {
    HANDLE handle;
    ThreadFn fn;
    void *arg;
};

/* _beginthreadex rather than CreateThread so the CRT's per-thread state
 * (errno, stdio locks, ...) is set up for the worker -- the pipeline
 * uses stdio freely. */
static unsigned __stdcall trampoline(void *p) {
    Thread *t = (Thread *)p;
    t->fn(t->arg);
    return 0;
}

Thread *thread_start(ThreadFn fn, void *arg) {
    Thread *t = (Thread *)calloc(1, sizeof(Thread));
    t->fn = fn;
    t->arg = arg;
    uintptr_t h = _beginthreadex(NULL, 0, trampoline, t, 0, NULL);
    if (h == 0) {
        free(t);
        return NULL;
    }
    t->handle = (HANDLE)h;
    return t;
}

bool thread_is_done(Thread *t) {
    return WaitForSingleObject(t->handle, 0) == WAIT_OBJECT_0;
}

void thread_join(Thread *t) {
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    free(t);
}

#else

#include <pthread.h>

/* pthreads has no portable non-blocking join (pthread_tryjoin_np is
 * glibc-only), so completion is tracked with a mutex-guarded flag. */
struct Thread {
    pthread_t tid;
    pthread_mutex_t mu;
    bool done;
    ThreadFn fn;
    void *arg;
};

static void *trampoline(void *p) {
    Thread *t = (Thread *)p;
    t->fn(t->arg);
    pthread_mutex_lock(&t->mu);
    t->done = true;
    pthread_mutex_unlock(&t->mu);
    return NULL;
}

Thread *thread_start(ThreadFn fn, void *arg) {
    Thread *t = (Thread *)calloc(1, sizeof(Thread));
    t->fn = fn;
    t->arg = arg;
    pthread_mutex_init(&t->mu, NULL);
    if (pthread_create(&t->tid, NULL, trampoline, t) != 0) {
        pthread_mutex_destroy(&t->mu);
        free(t);
        return NULL;
    }
    return t;
}

bool thread_is_done(Thread *t) {
    pthread_mutex_lock(&t->mu);
    bool done = t->done;
    pthread_mutex_unlock(&t->mu);
    return done;
}

void thread_join(Thread *t) {
    pthread_join(t->tid, NULL);
    pthread_mutex_destroy(&t->mu);
    free(t);
}

#endif
