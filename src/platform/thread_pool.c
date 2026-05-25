#include "thread_pool.h"
#include <unistd.h>

// _Safe declarations with _Borrow for pthread functions.
// pthread_create kept in _Unsafe: _Borrow→void* type erasure is forbidden.
_Safe int pthread_mutex_init(pthread_mutex_t* _Borrow mutex, const pthread_mutexattr_t* _Nullable attr);
_Safe int pthread_mutex_lock(pthread_mutex_t* _Borrow mutex);
_Safe int pthread_mutex_unlock(pthread_mutex_t* _Borrow mutex);
_Safe int pthread_mutex_destroy(pthread_mutex_t* _Borrow mutex);
_Safe int pthread_cond_init(pthread_cond_t* _Borrow cond, const pthread_condattr_t* _Nullable attr);
_Safe int pthread_cond_signal(pthread_cond_t* _Borrow cond);
_Safe int pthread_cond_broadcast(pthread_cond_t* _Borrow cond);
_Safe int pthread_cond_destroy(pthread_cond_t* _Borrow cond);
_Safe int pthread_join(pthread_t thread, void** _Nullable retval);
_Safe int close(int fd);

// C-ABI worker callback — runs in raw pthread context.
static void* tp_worker(void* arg) {
    struct ThreadPool* tp = (struct ThreadPool*)arg;
    for (;;) {
        pthread_mutex_lock(&tp->mtx);
        while (tp->count == 0 && !tp->stop)
            pthread_cond_wait(&tp->not_empty, &tp->mtx);
        if (tp->stop && tp->count == 0) {
            pthread_mutex_unlock(&tp->mtx);
            break;
        }
        struct ConnJob job = tp->queue[tp->head];
        tp->head = (tp->head + 1) % TP_QUEUE_CAP;
        tp->count--;
        pthread_mutex_unlock(&tp->mtx);
        tp->handler(job, tp->ctx);
    }
    return 0;
}

_Safe int thread_pool_start(struct ThreadPool* _Borrow tp, int n_workers, ConnHandler handler, void* _Nonnull ctx) {
    if (n_workers > TP_MAX_WORKERS) { n_workers = TP_MAX_WORKERS; }
    if (n_workers < 1) { n_workers = 1; }
    tp->n_workers = n_workers;
    tp->head = 0;
    tp->tail = 0;
    tp->count = 0;
    tp->stop = 0;
    tp->handler = handler;
    tp->ctx = ctx;
    pthread_mutex_init(&_Mut tp->mtx, nullptr);
    pthread_cond_init(&_Mut tp->not_empty, nullptr);
    // _Unsafe: pthread_create — _Borrow→void* type erasure (ThreadPool is non-trivial)
    // Also arg1 expects pthread_t* _Borrow but &tp->workers[i] is raw pthread_t*
    _Unsafe {
        for (int i = 0; i < n_workers; i++)
            if (pthread_create(&tp->workers[i], NULL, tp_worker, (void*)(struct ThreadPool*)tp) != 0) return -1;
    }
    return 0;
}

_Safe void thread_pool_submit(struct ThreadPool* _Borrow tp, struct ConnJob job) {
    pthread_mutex_lock(&_Mut tp->mtx);
    if (tp->count == TP_QUEUE_CAP) {
        pthread_mutex_unlock(&_Mut tp->mtx);
        close(job.fd);
        return;
    }
    tp->queue[tp->tail] = job;
    tp->tail = (tp->tail + 1) % TP_QUEUE_CAP;
    tp->count++;
    pthread_cond_signal(&_Mut tp->not_empty);
    pthread_mutex_unlock(&_Mut tp->mtx);
}

_Safe void thread_pool_shutdown(struct ThreadPool* _Borrow tp) {
    pthread_mutex_lock(&_Mut tp->mtx);
    tp->stop = 1;
    pthread_cond_broadcast(&_Mut tp->not_empty);
    pthread_mutex_unlock(&_Mut tp->mtx);
    for (int i = 0; i < tp->n_workers; i++)
        pthread_join(tp->workers[i], nullptr);
    pthread_mutex_destroy(&_Mut tp->mtx);
    pthread_cond_destroy(&_Mut tp->not_empty);
}
