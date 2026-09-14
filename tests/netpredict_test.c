/* netpredict_test — proves client-side PREDICTION + server RECONCILIATION with no
 * network at all (pure client-side bookkeeping over state + input blobs). Asserts:
 * prediction applies immediately (responsive, no round-trip lag); reconcile snaps
 * to authoritative truth then replays only the still-unacked inputs so the local
 * state = server truth + inputs in flight; when the server AGREES with the client's
 * prediction the reconcile is invisible (state unchanged); when the server DISAGREES
 * (a mispredict — e.g. the server clamped an illegal move) the client ends up at the
 * corrected position; ring-buffer overflow drops oldest; introspection + reset.
 * Prints "NETPREDICT TEST: all checks passed" / returns 0. */
#include "cc/claudecore.h"
#include "netkit/netpredict.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* the predicted local state and one frame of input */
typedef struct { float x, vx; } State;
typedef struct { float ax; }    Input;

/* the game's movement rule: integrate acceleration → velocity → position. Called
 * by the engine to predict (live) and to replay (during reconcile). */
static void sim(void* s, const void* in, void* ud){
    (void)ud;
    State* st = (State*)s; const Input* i = (const Input*)in;
    st->vx += i->ax;
    st->x  += st->vx;
}

/* reference integrator so the test can compute expected values independently */
static State run_from(State s, const Input* inputs, int n){
    for (int k=0;k<n;k++){ s.vx += inputs[k].ax; s.x += s.vx; }
    return s;
}

int main(void){
    /* prediction is engine-independent, but init the engine to match the suite's
     * harness (and to prove it links cleanly through the umbrella). */
    CCEngineConfig cfg = cc_sandbox_config(); cfg.width=64; cfg.height=64; cfg.verbose=false;
    CCEngine* e = cc_init(&cfg);

    CCNetPredict* p = cc_predict_create(sizeof(State), sizeof(Input), sim, NULL);
    CHECK(p != NULL, "predictor created");
    CHECK(cc_predict_next_seq(p) == 1, "first seq is 1");
    CHECK(cc_predict_pending(p) == 0, "no pending inputs initially");

    /* ── 1. PREDICTION IS IMMEDIATE ─────────────────────────────────────────── */
    State st = {0,0};
    Input i1 = { 1.0f };
    uint32_t s1 = cc_predict_apply(p, &st, &i1);
    CHECK(s1 == 1, "apply returned seq 1");
    /* after one input: vx=1, x=1 — applied RIGHT NOW, no waiting for the server */
    CHECK(fabsf(st.vx-1.0f)<1e-6 && fabsf(st.x-1.0f)<1e-6, "prediction applied immediately");
    CHECK(cc_predict_pending(p) == 1, "one input in flight");

    Input i2 = { 1.0f };
    uint32_t s2 = cc_predict_apply(p, &st, &i2);
    CHECK(s2 == 2, "second seq is 2");
    /* vx=2, x=3 */
    CHECK(fabsf(st.vx-2.0f)<1e-6 && fabsf(st.x-3.0f)<1e-6, "second prediction stacked");
    CHECK(cc_predict_pending(p) == 2, "two inputs in flight");

    /* ── 2. RECONCILE, SERVER AGREES → invisible correction ─────────────────── */
    /* The server processed input #1 and produced authoritative state matching what
     * the client predicted after input #1: vx=1, x=1. It acks seq 1. The client
     * should snap to that, drop input #1, and REPLAY input #2 → back to vx=2, x=3.
     * Because the server agreed, the reconciled state equals the pre-reconcile
     * state: no visible pop. */
    State pre = st;
    State auth_after_1 = {1.0f, 1.0f};
    uint32_t replayed = cc_predict_reconcile(p, &st, &auth_after_1, 1);
    CHECK(replayed == 1, "replayed exactly the 1 still-unacked input");
    CHECK(cc_predict_pending(p) == 1, "input #1 dropped as acked, #2 still pending");
    CHECK(fabsf(st.x-pre.x)<1e-6 && fabsf(st.vx-pre.vx)<1e-6, "server agreed → reconcile invisible (state unchanged)");
    CHECK(cc_predict_last_acked(p) == 1, "last acked recorded");

    /* ── 3. RECONCILE, SERVER DISAGREES → client corrects ───────────────────── */
    /* Now the server processed input #2 but CLAMPED it (say anti-cheat capped the
     * move): authoritative after #2 is x=2.5 (not the predicted 3), vx=1.5. It acks
     * seq 2. No inputs remain unacked, so the client snaps straight to the
     * correction. This is the mispredict path — the correction becomes visible. */
    State auth_after_2 = {2.5f, 1.5f};
    replayed = cc_predict_reconcile(p, &st, &auth_after_2, 2);
    CHECK(replayed == 0, "nothing to replay — server fully caught up");
    CHECK(cc_predict_pending(p) == 0, "no inputs in flight after full ack");
    CHECK(fabsf(st.x-2.5f)<1e-6 && fabsf(st.vx-1.5f)<1e-6, "mispredict corrected to authoritative state");

    /* ── 4. REPLAY MATH: local = authoritative + all in-flight inputs ────────── */
    /* Fire several inputs ahead of the server, then reconcile with an old ack and
     * assert the reconciled state equals (authoritative, then replay of unacked). */
    cc_predict_reset(p);
    CHECK(cc_predict_pending(p)==0 && cc_predict_next_seq(p)==1, "reset cleared ring + seqs");
    State st2 = {0,0};
    Input seq_inputs[6] = {{1},{1},{1},{1},{1},{1}};
    for (int k=0;k<6;k++) cc_predict_apply(p, &st2, &seq_inputs[k]);   /* seqs 1..6 */
    CHECK(cc_predict_pending(p)==6, "six inputs in flight");
    /* server acked through seq 3 and its authoritative state after 3 inputs is the
     * clean integration of the first 3; client should replay inputs 4,5,6 on top. */
    State auth_after_3 = run_from((State){0,0}, seq_inputs, 3);   /* vx=3, x=6 */
    replayed = cc_predict_reconcile(p, &st2, &auth_after_3, 3);
    CHECK(replayed == 3, "replayed the 3 unacked inputs (4,5,6)");
    State expected = run_from((State){0,0}, seq_inputs, 6);       /* full 6 = vx6, x21 */
    CHECK(fabsf(st2.x-expected.x)<1e-4 && fabsf(st2.vx-expected.vx)<1e-4,
          "reconciled state == authoritative + replayed in-flight inputs");

    /* ── 5. RING OVERFLOW drops oldest, newest survive ──────────────────────── */
    CCNetPredict* q = cc_predict_create_ex(sizeof(State), sizeof(Input), sim, NULL, 4);
    State st3 = {0,0};
    Input ov[7] = {{1},{2},{3},{4},{5},{6},{7}};
    for (int k=0;k<7;k++) cc_predict_apply(q, &st3, &ov[k]);   /* 7 into a ring of 4 */
    CHECK(cc_predict_pending(q) == 4, "ring capped at capacity (oldest dropped)");
    /* acking below the surviving window replays all 4 survivors (inputs 4..7) */
    State base = {0,0};
    uint32_t r = cc_predict_reconcile(q, &st3, &base, 0);
    CHECK(r == 4, "only the 4 surviving inputs replay after overflow");
    cc_predict_destroy(q);

    /* ── 6. NULL-safety ─────────────────────────────────────────────────────── */
    CHECK(cc_predict_apply(NULL, &st, &i1)==0, "NULL predictor apply safe");
    CHECK(cc_predict_reconcile(NULL, &st, &auth_after_2, 1)==0, "NULL predictor reconcile safe");
    CHECK(cc_predict_create(sizeof(State), 0, sim, NULL)==NULL, "zero input_size rejected");

    cc_predict_destroy(p);
    cc_shutdown(e);

    if (failures == 0)
        printf("NETPREDICT TEST: all checks passed (immediate prediction, reconcile replays only unacked inputs, server-agree is invisible, mispredict corrects, replay math = authoritative + in-flight, ring overflow, reset — no network)\n");
    else
        printf("NETPREDICT TEST: %d FAILURES\n", failures);
    return failures ? 1 : 0;
}
