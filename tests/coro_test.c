/* coro_test — exercises the CCCoro step/protothread scheduler. Pure logic;
 * prints "CORO TEST: all checks passed" and returns 0, else aborts. */
#include "cc/coro.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* ── a sequence: yield once, wait 1s, set a flag, wait 0.5s, done ─────────── */
typedef struct { int stage; int done; int gate; int ticks; } Seq;
static CCCoroCmd seq_fn(CCCoro* co, float dt, void* ud) {
    (void)dt;
    Seq* s = (Seq*)ud;
    CC_CORO_BEGIN(co);
    s->stage = 1;
    CC_CORO_YIELD(co);              /* resume next frame */
    s->stage = 2;
    CC_CORO_WAIT(co, 1.0f);         /* pause 1 second */
    s->stage = 3;
    CC_CORO_WAIT_UNTIL(co, s->gate);/* block until gate opens */
    s->stage = 4;
    CC_CORO_WAIT(co, 0.5f);
    s->stage = 5;
    s->done = 1;
    CC_CORO_END(co);
}

/* ── counts how many frames it runs (pure yield loop of fixed length) ─────── */
typedef struct { int n; } Counter;
static CCCoroCmd counter_fn(CCCoro* co, float dt, void* ud) {
    (void)dt;
    Counter* c = (Counter*)ud;
    CC_CORO_BEGIN(co);
    while (c->n < 3) { c->n++; CC_CORO_YIELD(co); }
    CC_CORO_END(co);
}

/* ── self-cancelling coroutine ────────────────────────────────────────────── */
static CCCoroSched* g_sched;
static int g_self_runs = 0;
static CCCoroId g_self_id;
static CCCoroCmd self_cancel_fn(CCCoro* co, float dt, void* ud) {
    (void)co; (void)dt; (void)ud;
    g_self_runs++;
    cc_coro_cancel(g_sched, g_self_id);   /* cancel myself mid-update */
    return cc_coro_yield();
}

/* ── coroutine that starts another coroutine while updating ───────────────── */
static int g_child_runs = 0;
static CCCoroCmd child_fn(CCCoro* co, float dt, void* ud) {
    (void)dt; (void)ud;
    CC_CORO_BEGIN(co);
    g_child_runs++;
    CC_CORO_END(co);
}
static int g_parent_started_child = 0;
static CCCoroCmd parent_fn(CCCoro* co, float dt, void* ud) {
    (void)dt; (void)ud;
    CC_CORO_BEGIN(co);
    cc_coro_start(g_sched, child_fn, NULL);   /* child must run NEXT frame, not now */
    g_parent_started_child = 1;
    CC_CORO_END(co);
}

int main(void) {
    CCCoroSched* s = cc_coro_sched_create();
    g_sched = s;
    CHECK(s != NULL, "scheduler created");

    /* 1. full sequence with WAIT / WAIT_UNTIL timing ---------------------- */
    Seq seq = {0};
    CCCoroId h = cc_coro_start(s, seq_fn, &seq);
    CHECK(h != 0, "start returns nonzero handle");
    CHECK(cc_coro_count(s) == 1, "one coroutine live");

    cc_coro_update(s, 0.016f);           /* frame 1: runs to first YIELD, stage=1 */
    CHECK(seq.stage == 1, "stage 1 after first frame (pre-yield body ran)");

    cc_coro_update(s, 0.016f);           /* frame 2: resume past yield -> stage 2, enters WAIT(1s) */
    CHECK(seq.stage == 2, "stage 2 after yield resumes");

    /* advance ~0.9s: still waiting, stage unchanged */
    for (int i=0;i<56;i++) cc_coro_update(s, 0.016f);   /* ~0.896s */
    CHECK(seq.stage == 2, "still in WAIT before 1s elapses");
    /* cross the 1s boundary */
    for (int i=0;i<10;i++) cc_coro_update(s, 0.016f);   /* +0.16s -> ~1.056s */
    CHECK(seq.stage == 3, "stage 3 after WAIT(1s) completes");

    /* now blocked on WAIT_UNTIL(gate); stays at 3 until gate opens */
    for (int i=0;i<5;i++) cc_coro_update(s, 0.016f);
    CHECK(seq.stage == 3, "blocked on WAIT_UNTIL while gate closed");
    seq.gate = 1;
    cc_coro_update(s, 0.016f);           /* gate open -> proceeds to stage 4, WAIT(0.5s) */
    CHECK(seq.stage == 4, "stage 4 once gate opens");

    for (int i=0;i<40;i++) cc_coro_update(s, 0.016f);   /* ~0.64s > 0.5s */
    CHECK(seq.stage == 5 && seq.done == 1, "sequence completed (stage 5, done)");
    CHECK(cc_coro_count(s) == 0, "coroutine removed after CC_CORO_END");
    CHECK(!cc_coro_active(s, h), "handle inactive after completion");

    /* 2. fixed-length yield loop runs exactly N frames ------------------- */
    Counter c = {0};
    cc_coro_start(s, counter_fn, &c);
    for (int i=0;i<10;i++) cc_coro_update(s, 0.016f);
    CHECK(c.n == 3, "yield loop incremented exactly 3 times");
    CHECK(cc_coro_count(s) == 0, "counter coroutine done + removed");

    /* 3. self-cancel during update fires once then is gone --------------- */
    g_self_runs = 0;
    g_self_id = cc_coro_start(s, self_cancel_fn, NULL);
    cc_coro_update(s, 0.016f);           /* runs once, cancels self */
    cc_coro_update(s, 0.016f);           /* must NOT run again */
    CHECK(g_self_runs == 1, "self-cancel: ran exactly once");
    CHECK(cc_coro_count(s) == 0, "self-cancelled coroutine removed");

    /* 4. starting a coroutine during update defers it to next frame ------ */
    g_child_runs = 0; g_parent_started_child = 0;
    cc_coro_start(s, parent_fn, NULL);
    cc_coro_update(s, 0.016f);           /* parent runs, starts child; child must NOT run yet */
    CHECK(g_parent_started_child == 1, "parent ran and started child");
    CHECK(g_child_runs == 0, "child did NOT run same frame (deferred)");
    cc_coro_update(s, 0.016f);           /* now child runs */
    CHECK(g_child_runs == 1, "child ran next frame");

    /* 5. external cancel stops a running coroutine ---------------------- */
    Counter c2 = {0};
    CCCoroId h2 = cc_coro_start(s, counter_fn, &c2);
    cc_coro_update(s, 0.016f);           /* n -> 1 */
    CHECK(c2.n == 1, "ran one frame before cancel");
    cc_coro_cancel(s, h2);
    cc_coro_update(s, 0.016f);
    CHECK(c2.n == 1, "cancelled coroutine does not advance");
    CHECK(!cc_coro_active(s, h2), "cancelled handle inactive");

    /* 6. clear + NULL safety -------------------------------------------- */
    cc_coro_start(s, counter_fn, &c2);
    cc_coro_sched_clear(s);
    CHECK(cc_coro_count(s) == 0, "clear removed all coroutines");
    CHECK(cc_coro_start(NULL, counter_fn, NULL) == 0, "start(NULL) = 0");
    CHECK(cc_coro_update(NULL, 0.016f) == 0, "update(NULL) = 0");
    cc_coro_cancel(NULL, 1);
    cc_coro_sched_destroy(NULL);

    cc_coro_sched_destroy(s);

    if (failures == 0) {
        printf("CORO TEST: all checks passed (YIELD/WAIT/WAIT_UNTIL sequencing, "
               "done-removal, self-cancel, start-defers, external cancel, clear)\n");
        return 0;
    }
    printf("CORO TEST: %d check(s) FAILED\n", failures);
    return 1;
}
