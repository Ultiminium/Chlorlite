#pragma once
/*
 * CCCoro — a cooperative coroutine scheduler for sequenced, multi-frame game
 * logic. Domain-general control-flow infrastructure: cutscenes, scripted AI
 * ("patrol → pause → look around → resume"), tutorials, staged spawns, ability
 * combos, anything that is naturally written as "do this, wait, do the next
 * thing" but must run across many frames without blocking the main loop.
 *
 * WHY NOT REAL STACKFUL COROUTINES. C has no native yield, and stackful
 * coroutines (ucontext / custom asm) are heavy, non-portable, and awkward under
 * a headless single-threaded engine. CCCoro uses a STEP model that is fully
 * deterministic, allocation-light, and valgrind-clean: a coroutine is a plain
 * function that is called once per frame and RETURNS what it is waiting for.
 * It resumes exactly where it yielded using a tiny bit of saved state (a step
 * index you manage, usually via the CC_CORO_* helper macros below).
 *
 * A coroutine function has the signature:
 *     CCCoroCmd my_seq(CCCoro* co, float dt, void* userdata);
 * and drives itself with the macros, e.g.:
 *
 *     CCCoroCmd open_door_seq(CCCoro* co, float dt, void* ud) {
 *         Door* d = ud;
 *         CC_CORO_BEGIN(co);
 *         CC_CORO_WAIT(co, 0.5f);              // pause half a second
 *         d->opening = true;
 *         CC_CORO_WAIT_UNTIL(co, d->open_frac >= 1.0f);  // yield till open
 *         cc_event_emit_i(bus, CC_EVT_DOOR_OPENED, d->id, 1);
 *         CC_CORO_WAIT(co, 1.0f);
 *         d->locked = true;
 *         CC_CORO_END(co);                     // finished → coroutine removed
 *     }
 *
 *     // schedule it:
 *     CCCoroId h = cc_coro_start(sched, open_door_seq, door);
 *     ... each frame: cc_coro_update(sched, dt);
 *
 * The macros expand to a `switch(co->pc)` state machine (a Duff's-device style
 * protothread). RULES of the step model (standard for this technique):
 *   - Locals declared before CC_CORO_BEGIN do NOT survive across a yield (they
 *     live on the C stack, which unwinds every frame). Put anything that must
 *     persist in your `userdata` struct.
 *   - Do not yield from inside a `switch` of your own that wraps the macros, and
 *     don't put a CC_CORO_* yield inside a plain `for`/`while` you opened before
 *     BEGIN. Use CC_CORO_WAIT_UNTIL for loops instead.
 * These are the only constraints; in exchange you get straight-line sequencing.
 *
 * A coroutine can also be driven manually (without the macros) by returning a
 * CCCoroCmd built with cc_coro_wait()/cc_coro_wait_until()/cc_coro_done() — the
 * macros are just sugar over those. Handle-identified (CCCoroId, 0 = invalid)
 * so sequences can be cancelled. Pure CPU, headless-safe.
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCCoroSched CCCoroSched;
typedef struct CCCoro      CCCoro;     /* per-coroutine state (opaque-ish)      */
typedef uint32_t CCCoroId;             /* 0 = invalid                           */

/* What a coroutine step returns to tell the scheduler what to do next. */
typedef enum {
    CC_CORO_CMD_YIELD = 0, /* resume next frame                                 */
    CC_CORO_CMD_WAIT,      /* resume after `seconds` elapses                    */
    CC_CORO_CMD_DONE,      /* finished — remove the coroutine                   */
} CCCoroCmdKind;

typedef struct {
    CCCoroCmdKind kind;
    float         seconds;  /* for CC_CORO_CMD_WAIT                             */
} CCCoroCmd;

/* The coroutine function. Called once per frame while alive. `dt` is the frame
 * delta. Return a command (usually via the CC_CORO_* macros). */
typedef CCCoroCmd (*CCCoroFn)(CCCoro* co, float dt, void* userdata);

/* Per-coroutine state the macros read/write. `pc` is the resume point; `t` is a
 * scratch timer the WAIT macro uses. You normally never touch these directly. */
struct CCCoro {
    int32_t  pc;    /* program counter / resume label (0 = start)              */
    float    t;     /* scratch accumulator for CC_CORO_WAIT                     */
    CCCoroId id;    /* this coroutine's handle                                  */
};

/* ─── manual command constructors (macros are sugar over these) ───────────── */
static inline CCCoroCmd cc_coro_yield(void)         { CCCoroCmd c={CC_CORO_CMD_YIELD,0}; return c; }
static inline CCCoroCmd cc_coro_wait(float seconds) { CCCoroCmd c={CC_CORO_CMD_WAIT,seconds}; return c; }
static inline CCCoroCmd cc_coro_done(void)          { CCCoroCmd c={CC_CORO_CMD_DONE,0}; return c; }

/* ─── scheduler lifecycle ─────────────────────────────────────────────────── */
CCCoroSched* cc_coro_sched_create(void);
void         cc_coro_sched_destroy(CCCoroSched* s);
/* Advance every live coroutine by dt. Coroutines started during update run on
 * the NEXT frame (never re-entered). Returns the number still alive after. */
uint32_t     cc_coro_update(CCCoroSched* s, float dt);
void         cc_coro_sched_clear(CCCoroSched* s);   /* cancel all              */

/* ─── starting / cancelling ───────────────────────────────────────────────── */
/* Schedule `fn` to run. Returns a handle (0 only if sched is NULL/full). */
CCCoroId cc_coro_start(CCCoroSched* s, CCCoroFn fn, void* userdata);
void     cc_coro_cancel(CCCoroSched* s, CCCoroId id);
bool     cc_coro_active(const CCCoroSched* s, CCCoroId id);
uint32_t cc_coro_count(const CCCoroSched* s);       /* live coroutines         */

#ifdef __cplusplus
}
#endif

/* ─── the protothread macros ──────────────────────────────────────────────────
 * These expand into a switch on co->pc. Each yield records __LINE__ as the
 * resume label and returns; next frame the switch jumps back to it. Modeled on
 * the well-known "protothreads" / local-continuation technique.
 *
 *   CC_CORO_BEGIN(co)              — open the state machine (required, first)
 *   CC_CORO_YIELD(co)             — resume next frame
 *   CC_CORO_WAIT(co, secs)        — resume after `secs` seconds
 *   CC_CORO_WAIT_UNTIL(co, cond)  — yield each frame until `cond` is true
 *   CC_CORO_END(co)               — finish (coroutine removed); required, last
 */
#define CC_CORO_BEGIN(co)          switch((co)->pc){ case 0:
#define CC_CORO_YIELD(co)          do{ (co)->pc=__LINE__; return cc_coro_yield(); \
                                       case __LINE__:; }while(0)
#define CC_CORO_WAIT(co, secs)     do{ (co)->pc=__LINE__; (co)->t=0; \
                                       return cc_coro_wait(secs); \
                                       case __LINE__:; }while(0)
#define CC_CORO_WAIT_UNTIL(co, cond) do{ (co)->pc=__LINE__; \
                                       case __LINE__: if(!(cond)) return cc_coro_yield(); \
                                       }while(0)
#define CC_CORO_END(co)            } (co)->pc=0; return cc_coro_done()
