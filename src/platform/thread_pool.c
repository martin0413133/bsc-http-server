#include "thread_pool.h"
#include <unistd.h>

// C-ABI worker (unsafe context by default). Pulls jobs and dispatches to the handler.
static void* tp_worker(void* arg) {
    struct ThreadPool* tp = (struct ThreadPool*)arg;
    for (;;) {
        pthread_mutex_lock(&tp->mtx);
        while (tp->count == 0 && !tp->stop) {
            pthread_cond_wait(&tp->not_empty, &tp->mtx);
        }
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
    return NULL;
}

_Safe int thread_pool_start(struct ThreadPool* _Nonnull tp, int n_workers, ConnHandler handler, void* _Nonnull ctx) {
    _Unsafe {
        if (n_workers > TP_MAX_WORKERS) { n_workers = TP_MAX_WORKERS; }
        if (n_workers < 1) { n_workers = 1; }
        tp->n_workers = n_workers;
        tp->head = 0;
        tp->tail = 0;
        tp->count = 0;
        tp->stop = 0;
        tp->handler = handler;
        tp->ctx = ctx;
        pthread_mutex_init(&tp->mtx, NULL);
        pthread_cond_init(&tp->not_empty, NULL);
        for (int i = 0; i < n_workers; i++) {
            if (pthread_create(&tp->workers[i], NULL, tp_worker, tp) != 0) { return -1; }
        }
        return 0;
    }
}

_Safe void thread_pool_submit(struct ThreadPool* _Nonnull tp, struct ConnJob job) {
    _Unsafe {
        pthread_mutex_lock(&tp->mtx);
        if (tp->count == TP_QUEUE_CAP) {
            pthread_mutex_unlock(&tp->mtx);
            close(job.fd);
            return;
        }
        tp->queue[tp->tail] = job;
        tp->tail = (tp->tail + 1) % TP_QUEUE_CAP;
        tp->count++;
        pthread_cond_signal(&tp->not_empty);
        pthread_mutex_unlock(&tp->mtx);
    }
}

_Safe void thread_pool_shutdown(struct ThreadPool* _Nonnull tp) {
    _Unsafe {
        pthread_mutex_lock(&tp->mtx);
        tp->stop = 1;
        pthread_cond_broadcast(&tp->not_empty);
        pthread_mutex_unlock(&tp->mtx);
        for (int i = 0; i < tp->n_workers; i++) {
            pthread_join(tp->workers[i], NULL);
        }
        pthread_mutex_destroy(&tp->mtx);
        pthread_cond_destroy(&tp->not_empty);
    }
}
