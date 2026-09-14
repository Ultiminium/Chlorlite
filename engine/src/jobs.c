/* jobs.c — CCJobs: a pthread work pool with parallel_for. See cc/jobs.h.
 *
 * Design: a single shared task queue (ring buffer) protected by a mutex, with a
 * condvar to wake idle workers. Not a per-thread work-stealing deque — a shared
 * queue is simpler, correct, and plenty for the engine's chunk sizes (workers
 * pull the next chunk when free, which gives the same load-balancing benefit as
 * stealing for parallel_for). A pending-counter + condvar implements wait().
 *
 * workers==1 runs everything inline (no threads) so tests are deterministic and
 * threading can be switched off trivially.
 */
#include "cc/jobs.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#if defined(_WIN32)
  #include <windows.h>
#else
  #include <unistd.h>
#endif

/* Portable logical-core count (0/negative → caller falls back). Windows uses
 * GetSystemInfo; POSIX uses sysconf. Keeps the job pool auto-sizing on both
 * shipping targets without a POSIX-only dependency. */
static long cc_jobs_hw_cores(void) {
#if defined(_WIN32)
    SYSTEM_INFO si; GetSystemInfo(&si);
    return (long)si.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

typedef struct { CCJobFn fn; void* ud; } Task;

#define JOBQ_CAP 4096

struct CCJobPool {
    pthread_t*      threads;
    int             nthreads;         /* actual worker threads (0 if inline)   */
    Task            q[JOBQ_CAP];
    int             head, tail;       /* ring buffer indices                    */
    int             count;            /* tasks queued                           */
    long            pending;          /* dispatched-but-not-finished tasks      */
    bool            stopping;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;        /* signaled when a task is enqueued       */
    pthread_cond_t  done;             /* signaled when pending hits 0           */
};

/* push a task (caller holds lock). Returns false if the queue is full. */
static bool q_push(CCJobPool* p, Task t) {
    if (p->count >= JOBQ_CAP) return false;
    p->q[p->tail] = t;
    p->tail = (p->tail + 1) % JOBQ_CAP;
    p->count++;
    return true;
}
static bool q_pop(CCJobPool* p, Task* out) {
    if (p->count == 0) return false;
    *out = p->q[p->head];
    p->head = (p->head + 1) % JOBQ_CAP;
    p->count--;
    return true;
}

static void* worker_main(void* arg) {
    CCJobPool* p = (CCJobPool*)arg;
    pthread_mutex_lock(&p->lock);
    for (;;) {
        Task t;
        while (p->count == 0 && !p->stopping)
            pthread_cond_wait(&p->not_empty, &p->lock);
        if (p->stopping && p->count == 0) break;
        if (!q_pop(p, &t)) continue;
        pthread_mutex_unlock(&p->lock);
        t.fn(t.ud);                       /* run outside the lock */
        pthread_mutex_lock(&p->lock);
        p->pending--;
        if (p->pending == 0) pthread_cond_broadcast(&p->done);
    }
    pthread_mutex_unlock(&p->lock);
    return NULL;
}

int cc_jobs_worker_count(const CCJobPool* p) {
    if (!p) return 1;
    return p->nthreads < 1 ? 1 : p->nthreads;
}

CCJobPool* cc_jobs_create(int workers) {
    CCJobPool* p = calloc(1, sizeof(CCJobPool));
    if (!p) return NULL;
    if (workers <= 0) {
        long n = cc_jobs_hw_cores();
        workers = (n > 0) ? (int)n : 4;
        if (workers > 64) workers = 64;      /* sane cap */
    }
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->not_empty, NULL);
    pthread_cond_init(&p->done, NULL);
    p->head = p->tail = p->count = 0;
    p->pending = 0;
    p->stopping = false;

    if (workers <= 1) {
        p->nthreads = 0;                     /* inline/serial mode */
        return p;
    }
    p->nthreads = workers;
    p->threads = calloc((size_t)workers, sizeof(pthread_t));
    for (int i = 0; i < workers; i++)
        pthread_create(&p->threads[i], NULL, worker_main, p);
    return p;
}

void cc_jobs_destroy(CCJobPool* p) {
    if (!p) return;
    if (p->nthreads > 0) {
        pthread_mutex_lock(&p->lock);
        p->stopping = true;
        pthread_cond_broadcast(&p->not_empty);
        pthread_mutex_unlock(&p->lock);
        for (int i = 0; i < p->nthreads; i++)
            pthread_join(p->threads[i], NULL);
        free(p->threads);
    }
    pthread_mutex_destroy(&p->lock);
    pthread_cond_destroy(&p->not_empty);
    pthread_cond_destroy(&p->done);
    free(p);
}

void cc_job_dispatch(CCJobPool* p, CCJobFn fn, void* ud) {
    if (!p || !fn) return;
    if (p->nthreads == 0) { fn(ud); return; }    /* inline */
    pthread_mutex_lock(&p->lock);
    /* if the queue is momentarily full, run inline to avoid deadlock/loss */
    Task t = { fn, ud };
    p->pending++;
    if (!q_push(p, t)) {
        p->pending--;
        pthread_mutex_unlock(&p->lock);
        fn(ud);
        return;
    }
    pthread_cond_signal(&p->not_empty);
    pthread_mutex_unlock(&p->lock);
}

void cc_jobs_wait(CCJobPool* p) {
    if (!p || p->nthreads == 0) return;
    pthread_mutex_lock(&p->lock);
    while (p->pending > 0)
        pthread_cond_wait(&p->done, &p->lock);
    pthread_mutex_unlock(&p->lock);
}

/* ─── parallel_for ────────────────────────────────────────────────────────── */
/* Each chunk becomes a task. We dispatch all chunk-tasks then wait. */
typedef struct {
    CCParallelRangeFn body;
    void*             ud;
    uint64_t          begin, end;
} RangeChunk;

static void range_chunk_run(void* arg) {
    RangeChunk* c = (RangeChunk*)arg;
    c->body(c->begin, c->end, c->ud);
}

/* wrapper so cc_parallel_for (per-item) reuses the range machinery */
typedef struct { CCParallelForFn body; void* ud; } PerItemCtx;
static void per_item_range(uint64_t b, uint64_t e, void* ud) {
    PerItemCtx* c = (PerItemCtx*)ud;
    for (uint64_t i = b; i < e; i++) c->body(i, c->ud);
}

void cc_parallel_for_range(CCJobPool* p, uint64_t begin, uint64_t end,
                           uint64_t grain, CCParallelRangeFn body, void* ud) {
    if (!body || begin >= end) return;
    uint64_t total = end - begin;
    if (grain == 0) {
        int w = cc_jobs_worker_count(p);
        /* aim for ~4 chunks per worker for load balance */
        uint64_t target = (uint64_t)w * 4;
        grain = (total + target - 1) / (target ? target : 1);
        if (grain == 0) grain = 1;
    }
    /* inline/serial pool → just run the whole range here */
    if (!p || p->nthreads == 0) { body(begin, end, ud); return; }

    /* number of chunks */
    uint64_t nchunks = (total + grain - 1) / grain;
    RangeChunk* chunks = malloc((size_t)nchunks * sizeof(RangeChunk));
    if (!chunks) { body(begin, end, ud); return; }   /* fallback */
    uint64_t ci = 0;
    for (uint64_t s = begin; s < end; s += grain) {
        uint64_t e = s + grain; if (e > end) e = end;
        chunks[ci].body = body; chunks[ci].ud = ud;
        chunks[ci].begin = s;   chunks[ci].end = e;
        ci++;
    }
    for (uint64_t i = 0; i < nchunks; i++)
        cc_job_dispatch(p, range_chunk_run, &chunks[i]);
    cc_jobs_wait(p);
    free(chunks);
}

void cc_parallel_for(CCJobPool* p, uint64_t begin, uint64_t end,
                     uint64_t grain, CCParallelForFn body, void* ud) {
    if (!body || begin >= end) return;
    PerItemCtx ctx = { body, ud };
    cc_parallel_for_range(p, begin, end, grain, per_item_range, &ctx);
}

/* ─── default shared pool ─────────────────────────────────────────────────── */
static CCJobPool* g_default = NULL;
static pthread_once_t g_default_once = PTHREAD_ONCE_INIT;
static void make_default(void) { g_default = cc_jobs_create(0); }
CCJobPool* cc_jobs_default(void) {
    pthread_once(&g_default_once, make_default);
    return g_default;
}
