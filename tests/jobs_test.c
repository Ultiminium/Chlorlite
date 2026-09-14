/* jobs_test — verifies the job system: parallel_for correctness vs serial,
 * determinism, the inline (workers=1) path, arbitrary dispatch+wait, chunked
 * range variant, and a race stress test (atomic-free per-index writes). Pure
 * logic + threads. Prints "JOBS TEST: all checks passed" / returns 0. */
#include "cc/jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* per-index write: square each element. No sharing → deterministic + race-free. */
static void square_fn(uint64_t i, void* ud) {
    uint64_t* a = (uint64_t*)ud;
    a[i] = i * i;
}

/* chunked variant: fill a range with a constant marker */
static void fill_range(uint64_t b, uint64_t e, void* ud) {
    int* a = (int*)ud;
    for (uint64_t i = b; i < e; i++) a[i] = 7;
}

/* arbitrary task: atomically bump a counter (tests dispatch+wait) */
static void bump_task(void* ud) {
    atomic_int* c = (atomic_int*)ud;
    atomic_fetch_add(c, 1);
}

static int run_for_pool(int workers) {
    int local_fail = 0;
    CCJobPool* pool = cc_jobs_create(workers);
    if (!pool) { printf("  FAIL: pool create (workers=%d)\n", workers); return 1; }

    /* ── parallel_for correctness: squares ──────────────────────────────── */
    const uint64_t N = 100000;
    uint64_t* a = malloc(N * sizeof(uint64_t));
    memset(a, 0xFF, N * sizeof(uint64_t));
    cc_parallel_for(pool, 0, N, 0, square_fn, a);
    int ok = 1;
    for (uint64_t i = 0; i < N; i++) if (a[i] != i*i) { ok = 0; break; }
    if (!ok) { printf("  FAIL: parallel_for squares (workers=%d)\n", workers); local_fail++; }
    free(a);

    /* ── chunked range variant ──────────────────────────────────────────── */
    const uint64_t M = 5000;
    int* b = calloc(M, sizeof(int));
    cc_parallel_for_range(pool, 0, M, 128, fill_range, b);
    int filled = 1;
    for (uint64_t i = 0; i < M; i++) if (b[i] != 7) { filled = 0; break; }
    if (!filled) { printf("  FAIL: parallel_for_range fill (workers=%d)\n", workers); local_fail++; }
    free(b);

    /* ── empty / degenerate ranges do nothing, don't crash ──────────────── */
    cc_parallel_for(pool, 5, 5, 0, square_fn, NULL);   /* begin==end */
    cc_parallel_for(pool, 10, 3, 0, square_fn, NULL);  /* begin>end  */

    /* ── arbitrary dispatch + wait ──────────────────────────────────────── */
    atomic_int counter = 0;
    const int TASKS = 1000;
    for (int i = 0; i < TASKS; i++) cc_job_dispatch(pool, bump_task, &counter);
    cc_jobs_wait(pool);
    if (atomic_load(&counter) != TASKS) {
        printf("  FAIL: dispatch+wait counter=%d expected %d (workers=%d)\n",
               atomic_load(&counter), TASKS, workers); local_fail++;
    }

    cc_jobs_destroy(pool);
    return local_fail;
}

int main(void) {
    /* multi-threaded pool */
    failures += run_for_pool(0);      /* 0 = auto (one per core) */
    /* forced small pool */
    failures += run_for_pool(2);
    /* inline/serial path (must give identical results, no threads) */
    failures += run_for_pool(1);

    /* worker_count sanity */
    CCJobPool* p1 = cc_jobs_create(1);
    CHECK(cc_jobs_worker_count(p1) == 1, "workers=1 → count 1 (inline)");
    cc_jobs_destroy(p1);
    CCJobPool* pauto = cc_jobs_create(0);
    CHECK(cc_jobs_worker_count(pauto) >= 1, "auto pool has >=1 worker");
    cc_jobs_destroy(pauto);

    /* the shared default pool works and is stable across calls */
    CHECK(cc_jobs_default() != NULL, "default pool exists");
    CHECK(cc_jobs_default() == cc_jobs_default(), "default pool is stable");
    atomic_int dc = 0;
    /* default-pool correctness check with a real buffer */
    uint64_t* buf = malloc(1000*sizeof(uint64_t));
    cc_parallel_for(cc_jobs_default(), 0, 1000, 0, square_fn, buf);
    int dok=1; for (uint64_t i=0;i<1000;i++) if (buf[i]!=i*i){dok=0;break;}
    CHECK(dok, "default pool parallel_for correct");
    free(buf);
    (void)dc;

    if (failures == 0) {
        printf("JOBS TEST: all checks passed (parallel_for + range correctness across "
               "auto/2/1-worker pools, dispatch+wait, empty ranges, default pool)\n");
        return 0;
    }
    printf("JOBS TEST: %d check(s) FAILED\n", failures);
    return 1;
}
