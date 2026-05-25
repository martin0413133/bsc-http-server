#ifndef CC_THREAD_POOL_HBS
#define CC_THREAD_POOL_HBS
#include <stddef.h>
#include <pthread.h>

// A connection job: fd + SSL flag (worker does TLS handshake when set).
struct ConnJob { int fd; _Bool is_ssl; };

// Connection handler invoked on a worker thread. ctx is the opaque server context.
typedef void (*ConnHandler)(struct ConnJob job, void* _Nonnull ctx);

#define TP_QUEUE_CAP  256
#define TP_MAX_WORKERS 64

// Plain struct (POD + pthread primitives + opaque ctx); only ConnJobs cross threads.
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

// Adapter: _Safe interface, _Unsafe (pthread + raw struct access) inside.
_Safe int  thread_pool_start(struct ThreadPool* _Nonnull tp, int n_workers, ConnHandler handler, void* _Nonnull ctx);
_Safe void thread_pool_submit(struct ThreadPool* _Nonnull tp, struct ConnJob job);
_Safe void thread_pool_shutdown(struct ThreadPool* _Nonnull tp);
#endif
