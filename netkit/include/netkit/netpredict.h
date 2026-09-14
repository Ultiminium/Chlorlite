#pragma once
/*
 * netkit/netpredict — client-side PREDICTION + server RECONCILIATION. Part of the
 * netkit LIBRARY (ships alongside Chlorlite, not part of the engine — see
 * netkit/netcmd.h for the scope rationale). This is what makes
 * the authoritative model (net.h + netcmd.h) actually PLAYABLE instead of laggy.
 *
 * The problem it solves: with a strict authoritative server, a client that waits
 * for the server to confirm every input before showing it feels sluggish — one
 * full round-trip of input lag on every move. Prediction fixes this: the client
 * applies its own input IMMEDIATELY (predicts), and later RECONCILES against the
 * authoritative snapshot when it arrives.
 *
 * The loop:
 *   1. Each frame the client produces an input, stamps it with a monotonic seq,
 *      sends it to the server (via netcmd), AND applies it locally right now.
 *   2. Unacked inputs are kept in a ring buffer (they might need replaying).
 *   3. When an authoritative snapshot arrives, it carries the last input seq the
 *      server has processed for this client (the game reads this from
 *      cc_netcmd_server_last_seq on the server and puts it on the wire). The client:
 *        a. SNAPS its predicted state to the authoritative state (server is truth),
 *        b. DISCARDS inputs the server has already applied (seq <= acked),
 *        c. REPLAYS the still-unacked inputs on top of the authoritative state.
 *      Result: the local entity sits at (authoritative state + inputs in flight) —
 *      correct AND responsive. If the server agreed with the client's prediction,
 *      the replay reproduces exactly what was on screen (no visible correction);
 *      if it disagreed (a rejected/adjusted input), the client smoothly ends up at
 *      the corrected position — this is the anti-cheat correction made visible.
 *
 * MECHANISM, NOT POLICY (the CC way): the engine owns the input RING BUFFER and the
 * REPLAY loop. It cannot know how an input mutates your game state — that's your
 * movement code — so YOU supply a `simulate` callback: "advance this state by this
 * one input." The engine calls it to predict (once, live) and to replay (N times,
 * during reconciliation). Your state + input types are opaque blobs to the engine.
 *
 * TRANSPORT-FREE / headless-testable like the rest of CCNet: no sockets here at
 * all — this is pure client-side bookkeeping over your state + input bytes. A test
 * drives predict/reconcile directly and asserts convergence.
 *
 *   // your types
 *   typedef struct { float x, vx; } State;   // the predicted entity's local state
 *   typedef struct { float ax; }     Input;   // one frame of input
 *   void sim(void* s, const void* in, void* ud){ State*st=s; const Input*i=in;
 *       st->vx += i->ax; st->x += st->vx; }   // advance state by one input
 *
 *   CCNetPredict* p = cc_predict_create(sizeof(State), sizeof(Input), sim, NULL);
 *   // each frame:
 *   uint32_t seq = cc_predict_apply(p, &state, &input);   // predict now + record
 *   send_to_server(seq, &input);
 *   // when a snapshot arrives with authoritative State + the server's acked seq:
 *   cc_predict_reconcile(p, &state, &authoritative_state, acked_seq);
 *   // `state` now = authoritative + replayed unacked inputs.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Advance `state` in place by applying one `input`. Called by the engine to
 * predict (live) and to replay (during reconcile). `ud` is the userdata registered
 * with the predictor. This is the game's movement/simulation rule — the seam. */
typedef void (*CCPredictSimFn)(void* state, const void* input, void* ud);

typedef struct CCNetPredict CCNetPredict;

/* Create a predictor. `state_size` = bytes of the predicted local state (the thing
 * `simulate` mutates); `input_size` = bytes of one input. `simulate` is the game's
 * "advance by one input" rule. The engine keeps a ring of unacked inputs internally
 * (default capacity is generous; see cc_predict_create_ex to size it). */
CCNetPredict* cc_predict_create(uint32_t state_size, uint32_t input_size,
                                CCPredictSimFn simulate, void* ud);
/* As above but with an explicit max in-flight input capacity (ring size). If more
 * than `capacity` inputs go unacked, the oldest are dropped (they'd be older than
 * anything the server could still ack). */
CCNetPredict* cc_predict_create_ex(uint32_t state_size, uint32_t input_size,
                                   CCPredictSimFn simulate, void* ud,
                                   uint32_t capacity);
void          cc_predict_destroy(CCNetPredict* p);

/* Predict one input: stamps it with the next monotonic seq, stores a copy in the
 * unacked ring, and applies it to `state` immediately via `simulate`. Returns the
 * seq assigned (send this to the server alongside the input). `state` and `input`
 * point to buffers of the sizes given at create. */
uint32_t cc_predict_apply(CCNetPredict* p, void* state, const void* input);

/* Reconcile against an authoritative snapshot. `state` is the client's local
 * predicted state (overwritten by this call); `authoritative` is the server's truth
 * for this entity (state_size bytes); `acked_seq` is the last input seq the server
 * had processed when it produced that snapshot. The engine copies authoritative →
 * state, drops inputs with seq <= acked_seq from the ring, then replays the
 * remaining (still-in-flight) inputs on top via `simulate`. After this, `state` =
 * authoritative + all inputs the server hasn't seen yet. Returns the number of
 * inputs replayed (0 = server was fully caught up; a large number = client is far
 * ahead / high latency). */
uint32_t cc_predict_reconcile(CCNetPredict* p, void* state,
                              const void* authoritative, uint32_t acked_seq);

/* ─── introspection (for HUDs / tuning / tests) ─────────────────────────────── */
/* How many inputs are currently in flight (predicted but not yet acked). */
uint32_t cc_predict_pending(const CCNetPredict* p);
/* The seq that will be assigned to the NEXT cc_predict_apply. */
uint32_t cc_predict_next_seq(const CCNetPredict* p);
/* The most recent acked_seq passed to reconcile (0 if none yet). */
uint32_t cc_predict_last_acked(const CCNetPredict* p);
/* Drop all pending inputs and reset seqs (e.g. on respawn / hard resync). */
void     cc_predict_reset(CCNetPredict* p);

#ifdef __cplusplus
}
#endif
