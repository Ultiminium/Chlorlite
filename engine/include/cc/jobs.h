#pragma once
/*
 * CCJobs — a work-stealing thread pool for data-parallel work. The engine is
 * otherwise single-threaded; this is the foundation that lets heavy systems
 * (ECS queries, physics broadphase, particles, animation, culling) use every
 * core. Like the rest of CC it's a MECHANISM: you hand it a range or a task and
 * a function; it runs them across workers and waits. No hidden global scheduler
 * you can't see — you create a pool, dispatch to it, and it's deterministic in
 * result (though not in execution order).
 *
 *   CCJobPool* pool = cc_jobs_create(0);      // 0 = one worker per CPU core
 *   cc_parallel_for(pool, 0, n, 256, work_fn, userdata);   // splits [0,n) in chunks
 *   cc_jobs_destroy(pool);
 *
 * cc_parallel_for is the workhorse: it partitions [begin,end) into chunks of
 * ~grain items and runs work_fn(i, userdata) for every i, spread across workers,
 * and blocks until all are done. For arbitrary (non-range) tasks use
 * cc_job_dispatch + cc_jobs_wait.
 *
 * Determinism & safety: results are deterministic if your work_fn only writes to
 * per-index data (no cross-item sharing) — the standard data-parallel contract.
 * A pool of size 1 runs everything inline on the calling thread (great for tests
 * and for turning threading off). Work-stealing keeps all workers busy even when
 * chunks finish unevenly.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCJobPool CCJobPool;

/* Create a pool with `workers` threads. workers=0 → one per hardware core
 * (capped sensibly). workers=1 → fully inline/serial (no threads spawned). */
CCJobPool* cc_jobs_create(int workers);
void       cc_jobs_destroy(CCJobPool* pool);
/* How many workers the pool actually has (>=1). */
int        cc_jobs_worker_count(const CCJobPool* pool);
/* The default shared pool (created lazily, one per process, sized to cores).
 * Convenient for one-off parallel_for calls; destroyed at shutdown. */
CCJobPool* cc_jobs_default(void);

/* ─── parallel for: the workhorse ─────────────────────────────────────────── */
/* Run body(i, userdata) for every i in [begin, end), split into chunks of about
 * `grain` items across the pool's workers. Blocks until all complete. If
 * grain<=0 a reasonable grain is chosen. Safe for begin>=end (does nothing). */
typedef void (*CCParallelForFn)(uint64_t i, void* userdata);
void cc_parallel_for(CCJobPool* pool, uint64_t begin, uint64_t end,
                     uint64_t grain, CCParallelForFn body, void* userdata);

/* A chunked variant: body_range(chunk_begin, chunk_end, userdata) is called once
 * per chunk (you loop inside). Lower overhead when per-item work is tiny. */
typedef void (*CCParallelRangeFn)(uint64_t begin, uint64_t end, void* userdata);
void cc_parallel_for_range(CCJobPool* pool, uint64_t begin, uint64_t end,
                           uint64_t grain, CCParallelRangeFn body, void* userdata);

/* ─── arbitrary tasks ─────────────────────────────────────────────────────── */
/* Dispatch a one-shot task to run on some worker. Returns immediately. Use
 * cc_jobs_wait to block until all dispatched tasks on the pool have finished. */
typedef void (*CCJobFn)(void* userdata);
void cc_job_dispatch(CCJobPool* pool, CCJobFn fn, void* userdata);
/* Block until every task dispatched to this pool has completed. */
void cc_jobs_wait(CCJobPool* pool);

#ifdef __cplusplus
}
#endif
