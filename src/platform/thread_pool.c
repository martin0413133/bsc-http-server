#include "thread_pool.h"
#include <unistd.h>

// pthread functions conflict with system header declarations — only pthread calls need _Unsafe

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
    // _Unsafe: pthread API — system header raw-pointer declarations conflict with _Borrow
    _Unsafe {
        pthread_mutex_init(&tp->mtx, NULL);
        pthread_cond_init(&tp->not_empty, NULL);
        for (int i = 0; i < n_workers; i++) {
            // + void* type erasure for thread argument (_Borrow cannot convert to raw void*)
            if (pthread_create(&tp->workers[i], NULL, tp_worker, (void*)&_Mut *tp) != 0) return -1;
        }
    }
    return 0;
}

_Safe void thread_pool_submit(struct ThreadPool* _Borrow tp, struct ConnJob job) {
    _Unsafe { pthread_mutex_lock(&tp->mtx); }  // pthread: system header conflict
    if (tp->count == TP_QUEUE_CAP) {
        _Unsafe { pthread_mutex_unlock(&tp->mtx); }  // pthread: system header conflict
        close(job.fd);
        return;
    }
    tp->queue[tp->tail] = job;
    tp->tail = (tp->tail + 1) % TP_QUEUE_CAP;
    tp->count++;
    _Unsafe {  // pthread: system header conflict
        pthread_cond_signal(&tp->not_empty);
        pthread_mutex_unlock(&tp->mtx);
    }
}

_Safe void thread_pool_shutdown(struct ThreadPool* _Borrow tp) {
    _Unsafe { pthread_mutex_lock(&tp->mtx); }  // pthread: system header conflict
    tp->stop = 1;
    _Unsafe {  // pthread: system header conflict
        pthread_cond_broadcast(&tp->not_empty);
        pthread_mutex_unlock(&tp->mtx);
    }
    for (int i = 0; i < tp->n_workers; i++) {
        _Unsafe { pthread_join(tp->workers[i], NULL); }  // pthread: system header conflict
    }
    _Unsafe {  // pthread: system header conflict
        pthread_mutex_destroy(&tp->mtx);
        pthread_cond_destroy(&tp->not_empty);
    }
}
