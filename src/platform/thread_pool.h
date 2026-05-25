#ifndef CC_THREAD_POOL_HBS
#define CC_THREAD_POOL_HBS
#include <stddef.h>
#include <pthread.h>

struct ConnJob { int fd; _Bool is_ssl; };

typedef void (*ConnHandler)(struct ConnJob job, void* _Nonnull ctx);

#define TP_QUEUE_CAP  256
#define TP_MAX_WORKERS 64

struct ThreadPool {
    pthread_t workers[TP_MAX_WORKERS];
    int n_workers;
    struct ConnJob queue[TP_QUEUE_CAP];
    int head;
    int tail;
    int count;
    int stop;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    ConnHandler handler;
    void* ctx;
};

_Safe int  thread_pool_start(struct ThreadPool* _Borrow tp, int n_workers, ConnHandler handler, void* _Nonnull ctx);
_Safe void thread_pool_submit(struct ThreadPool* _Borrow tp, struct ConnJob job);
_Safe void thread_pool_shutdown(struct ThreadPool* _Borrow tp);
#endif
