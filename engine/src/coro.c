/* CCCoro — cooperative step-based coroutine scheduler. See cc/coro.h.
 *
 * Each coroutine is a slot holding its CCCoro state (pc/t/id), its function +
 * userdata, and a wait countdown. update() walks the slots; a coroutine that
 * returned CC_CORO_CMD_WAIT is not re-invoked until its countdown elapses.
 *
 * Concurrency, matching the event bus's discipline: coroutines started during
 * update() get a slot but are not run until the NEXT frame (snapshot the count
 * at update start). Cancellation during update tombstones the slot rather than
 * shifting the array, and reclamation happens after the walk. Handles pack
 * (index+1, generation) so a stale id can't hit a reused slot.
 */
#include "cc/coro.h"
#include <stdlib.h>
#include <string.h>

#define CC_CORO_IDX_BITS 20u
#define CC_CORO_IDX_MASK ((1u<<CC_CORO_IDX_BITS)-1u)
#define CC_CORO_GEN_MASK ((1u<<(32u-CC_CORO_IDX_BITS))-1u)
#define CC_CORO_MAKE_ID(idx,gen) ((((gen)&CC_CORO_GEN_MASK)<<CC_CORO_IDX_BITS)|(((idx)+1u)&CC_CORO_IDX_MASK))
#define CC_CORO_ID_IDX(id) (((id)&CC_CORO_IDX_MASK)-1u)
#define CC_CORO_ID_GEN(id) (((id)>>CC_CORO_IDX_BITS)&CC_CORO_GEN_MASK)

typedef struct {
    CCCoro     co;          /* pc / t / id — the resumable state                */
    CCCoroFn   fn;          /* NULL = free/tombstoned slot                      */
    void*      ud;
    float      wait_left;   /* seconds remaining on a WAIT (0 = ready this frame)*/
    uint16_t   generation;
    bool       pending_del;
} Slot;

struct CCCoroSched {
    Slot*    slots;
    uint32_t count, cap;
    uint32_t live;
    int      updating;      /* >0 while inside update()                         */
    bool     need_compact;
};

static bool ensure_cap(CCCoroSched* s, uint32_t need) {
    if (need <= s->cap) return true;
    uint32_t cap = s->cap ? s->cap*2 : 16;
    while (cap < need) cap *= 2;
    if (cap > CC_CORO_IDX_MASK) cap = CC_CORO_IDX_MASK;
    if (cap < need) return false;
    Slot* p = (Slot*)realloc(s->slots, cap*sizeof(Slot));
    if (!p) return false;
    memset(p + s->cap, 0, (cap - s->cap)*sizeof(Slot));
    s->slots = p; s->cap = cap;
    return true;
}

CCCoroSched* cc_coro_sched_create(void) {
    return (CCCoroSched*)calloc(1, sizeof(CCCoroSched));
}
void cc_coro_sched_destroy(CCCoroSched* s) {
    if (!s) return;
    free(s->slots);
    free(s);
}

static void free_slot(CCCoroSched* s, uint32_t idx) {
    Slot* sl = &s->slots[idx];
    if (!sl->fn) return;
    sl->fn = NULL; sl->ud = NULL; sl->pending_del = false;
    sl->generation = (uint16_t)((sl->generation + 1) & CC_CORO_GEN_MASK);
    s->live--;
}
static void compact(CCCoroSched* s) {
    if (!s->need_compact || s->updating > 0) return;
    for (uint32_t i=0;i<s->count;i++)
        if (s->slots[i].fn && s->slots[i].pending_del) free_slot(s, i);
    while (s->count > 0 && !s->slots[s->count-1].fn) s->count--;
    s->need_compact = false;
}

CCCoroId cc_coro_start(CCCoroSched* s, CCCoroFn fn, void* ud) {
    if (!s || !fn) return 0;
    uint32_t idx = s->count;
    if (s->updating == 0) {
        for (uint32_t i=0;i<s->count;i++) if (!s->slots[i].fn) { idx=i; break; }
    }
    if (idx == s->count) {
        if (!ensure_cap(s, s->count+1)) return 0;
        s->count++;
    }
    Slot* sl = &s->slots[idx];
    sl->fn = fn; sl->ud = ud; sl->pending_del = false;
    sl->wait_left = 0.0f;
    sl->co.pc = 0; sl->co.t = 0.0f;
    CCCoroId id = CC_CORO_MAKE_ID(idx, sl->generation);
    sl->co.id = id;
    s->live++;
    return id;
}

void cc_coro_cancel(CCCoroSched* s, CCCoroId id) {
    if (!s || id == 0) return;
    uint32_t idx = CC_CORO_ID_IDX(id);
    if (idx >= s->count) return;
    Slot* sl = &s->slots[idx];
    if (!sl->fn || CC_CORO_ID_GEN(id) != sl->generation) return;
    if (s->updating > 0) { sl->pending_del = true; s->need_compact = true; }
    else                   free_slot(s, idx);
}
bool cc_coro_active(const CCCoroSched* s, CCCoroId id) {
    if (!s || id == 0) return false;
    uint32_t idx = CC_CORO_ID_IDX(id);
    if (idx >= s->count) return false;
    const Slot* sl = &s->slots[idx];
    return sl->fn && !sl->pending_del && CC_CORO_ID_GEN(id) == sl->generation;
}
uint32_t cc_coro_count(const CCCoroSched* s) { return s ? s->live : 0; }

void cc_coro_sched_clear(CCCoroSched* s) {
    if (!s) return;
    for (uint32_t i=0;i<s->count;i++) {
        if (s->slots[i].fn) {
            if (s->updating > 0) { s->slots[i].pending_del = true; s->need_compact = true; }
            else free_slot(s, i);
        }
    }
}

uint32_t cc_coro_update(CCCoroSched* s, float dt) {
    if (!s) return 0;
    uint32_t n = s->count;      /* snapshot: coroutines started now run next frame */
    s->updating++;
    for (uint32_t i=0;i<n;i++) {
        Slot* sl = &s->slots[i];
        if (!sl->fn || sl->pending_del) continue;

        if (sl->wait_left > 0.0f) {
            sl->wait_left -= dt;
            if (sl->wait_left > 0.0f) continue;   /* still waiting */
            sl->wait_left = 0.0f;
        }
        CCCoroCmd cmd = sl->fn(&sl->co, dt, sl->ud);
        /* the fn may have cancelled itself; if so, skip acting on its return */
        if (sl->pending_del) continue;
        switch (cmd.kind) {
            case CC_CORO_CMD_WAIT:
                sl->wait_left = cmd.seconds > 0.0f ? cmd.seconds : 0.0f;
                break;
            case CC_CORO_CMD_DONE:
                sl->pending_del = true; s->need_compact = true;
                break;
            case CC_CORO_CMD_YIELD:
            default:
                break;   /* run again next frame */
        }
    }
    s->updating--;
    compact(s);
    return s->live;
}
