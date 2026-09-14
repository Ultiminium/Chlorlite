/* CCEventBus — deferred pub/sub message bus. See cc/event.h for the contract.
 *
 * Implementation notes:
 *  - Listeners live in a grow-only array. Removal tombstones the slot (fn=NULL)
 *    rather than compacting, so an in-progress dispatch loop never has the
 *    ground shift under it. Tombstones are reclaimed lazily by new subscribes
 *    and by a compaction pass that only runs when no dispatch is active.
 *  - Handles are (index, generation) packed into 32 bits so a stale id that
 *    happens to hit a reused slot is rejected by the generation check.
 *  - The event queue is a contiguous ring of CCEvent records plus a side heap
 *    for payload blobs. Publishing during dispatch appends to the SAME queue but
 *    past the snapshot boundary captured at update start, so those events roll
 *    to the next frame instead of being delivered re-entrantly.
 */
#include "cc/event.h"
#include <stdlib.h>
#include <string.h>

/* ── handle packing ──────────────────────────────────────────────────────
 * 20 bits index (up to ~1M listeners) | 12 bits generation. id 0 is invalid.
 * The index is stored BIASED BY +1 so that (idx=0, gen=0) does not pack to 0
 * and collide with the invalid sentinel. Decode subtracts the bias. */
#define CC_EVT_IDX_BITS   20u
#define CC_EVT_IDX_MASK   ((1u << CC_EVT_IDX_BITS) - 1u)
#define CC_EVT_GEN_MASK   ((1u << (32u - CC_EVT_IDX_BITS)) - 1u)
#define CC_EVT_MAKE_ID(idx, gen) \
    ((((gen) & CC_EVT_GEN_MASK) << CC_EVT_IDX_BITS) | (((idx) + 1u) & CC_EVT_IDX_MASK))
#define CC_EVT_ID_IDX(id) (((id) & CC_EVT_IDX_MASK) - 1u)
#define CC_EVT_ID_GEN(id) (((id) >> CC_EVT_IDX_BITS) & CC_EVT_GEN_MASK)

#define CC_EVT_ANY_CHANNEL  0xFFFFFFFFu   /* internal sentinel for subscribe_any */

typedef struct {
    CCEventFn  fn;         /* NULL = tombstoned/free slot                       */
    void*      userdata;
    uint32_t   channel;    /* CC_EVT_ANY_CHANNEL = wildcard                      */
    uint16_t   generation; /* bumped on free so stale handles miss              */
    bool       pending_del;/* marked for removal during a dispatch              */
} Listener;

/* A queued event references its payload (if any) by offset into `blobs`. */
typedef struct {
    CCEvent  ev;           /* ev.data is patched to &blobs[blob_off] at dispatch */
    uint32_t blob_off;     /* offset into blobs, or CC_NO_BLOB                   */
} QueuedEvent;
#define CC_NO_BLOB  0xFFFFFFFFu

struct CCEventBus {
    Listener*    listeners;
    uint32_t     lcount, lcap;
    uint32_t     live;        /* non-tombstoned listener count                  */

    QueuedEvent* queue;
    uint32_t     qcount, qcap;

    unsigned char* blobs;     /* packed payload bytes                           */
    uint32_t     blob_used, blob_cap;

    int          dispatch_depth; /* >0 while inside update()/emit_now()         */
    bool         need_compact;   /* a delete happened during dispatch           */
};

/* ── small growth helpers ────────────────────────────────────────────────── */
static bool ensure_listeners(CCEventBus* b, uint32_t need) {
    if (need <= b->lcap) return true;
    uint32_t cap = b->lcap ? b->lcap * 2 : 16;
    while (cap < need) cap *= 2;
    if (cap > CC_EVT_IDX_MASK) cap = CC_EVT_IDX_MASK; /* idx stored +1 biased    */
    if (cap < need) return false;              /* index space exhausted          */
    Listener* p = (Listener*)realloc(b->listeners, cap * sizeof(Listener));
    if (!p) return false;
    memset(p + b->lcap, 0, (cap - b->lcap) * sizeof(Listener));
    b->listeners = p; b->lcap = cap;
    return true;
}
static bool ensure_queue(CCEventBus* b, uint32_t need) {
    if (need <= b->qcap) return true;
    uint32_t cap = b->qcap ? b->qcap * 2 : 32;
    while (cap < need) cap *= 2;
    QueuedEvent* p = (QueuedEvent*)realloc(b->queue, cap * sizeof(QueuedEvent));
    if (!p) return false;
    b->queue = p; b->qcap = cap;
    return true;
}
static bool ensure_blobs(CCEventBus* b, uint32_t need) {
    if (need <= b->blob_cap) return true;
    uint32_t cap = b->blob_cap ? b->blob_cap * 2 : 256;
    while (cap < need) cap *= 2;
    unsigned char* p = (unsigned char*)realloc(b->blobs, cap);
    if (!p) return false;
    b->blobs = p; b->blob_cap = cap;
    return true;
}

/* ── lifecycle ───────────────────────────────────────────────────────────── */
CCEventBus* cc_event_bus_create(void) {
    return (CCEventBus*)calloc(1, sizeof(CCEventBus));
}
void cc_event_bus_destroy(CCEventBus* b) {
    if (!b) return;
    free(b->listeners);
    free(b->queue);
    free(b->blobs);
    free(b);
}
void cc_event_bus_drain(CCEventBus* b) {
    if (!b) return;
    b->qcount = 0;
    b->blob_used = 0;
}
void cc_event_bus_clear(CCEventBus* b) {
    if (!b) return;
    /* If called mid-dispatch, tombstone rather than free the array out from
     * under the loop; live listeners are zeroed so nothing else fires. */
    for (uint32_t i = 0; i < b->lcount; ++i) {
        if (b->listeners[i].fn) {
            b->listeners[i].fn = NULL;
            b->listeners[i].pending_del = false;
            b->listeners[i].generation++;
        }
    }
    b->live = 0;
    if (b->dispatch_depth == 0) b->lcount = 0;
    cc_event_bus_drain(b);
}

/* ── subscribing ─────────────────────────────────────────────────────────── */
static CCListenerId subscribe_channel(CCEventBus* b, uint32_t channel,
                                      CCEventFn fn, void* userdata) {
    if (!b || !fn) return 0;
    /* reuse a tombstoned slot if one exists (skip when dispatching to avoid
     * resurrecting a slot the active loop still holds an index to) */
    uint32_t idx = b->lcount;
    if (b->dispatch_depth == 0) {
        for (uint32_t i = 0; i < b->lcount; ++i) {
            if (!b->listeners[i].fn) { idx = i; break; }
        }
    }
    if (idx == b->lcount) {
        if (!ensure_listeners(b, b->lcount + 1)) return 0;
        b->lcount++;
    }
    Listener* L = &b->listeners[idx];
    L->fn = fn; L->userdata = userdata; L->channel = channel;
    L->pending_del = false;
    /* generation persists across reuse; it was bumped at free time */
    b->live++;
    return CC_EVT_MAKE_ID(idx, L->generation);
}
CCListenerId cc_event_subscribe(CCEventBus* b, uint32_t channel,
                                CCEventFn fn, void* userdata) {
    return subscribe_channel(b, channel, fn, userdata);
}
CCListenerId cc_event_subscribe_any(CCEventBus* b, CCEventFn fn, void* userdata) {
    return subscribe_channel(b, CC_EVT_ANY_CHANNEL, fn, userdata);
}

static void free_slot(CCEventBus* b, uint32_t idx) {
    Listener* L = &b->listeners[idx];
    if (!L->fn) return;
    L->fn = NULL; L->userdata = NULL; L->pending_del = false;
    L->generation = (uint16_t)((L->generation + 1) & CC_EVT_GEN_MASK);
    b->live--;
}
void cc_event_unsubscribe(CCEventBus* b, CCListenerId id) {
    if (!b || id == 0) return;
    uint32_t idx = CC_EVT_ID_IDX(id);
    if (idx >= b->lcount) return;
    Listener* L = &b->listeners[idx];
    if (!L->fn) return;
    if (CC_EVT_ID_GEN(id) != L->generation) return;   /* stale handle */
    if (b->dispatch_depth > 0) {
        /* defer: keep the slot live so an active loop still sees a stable set,
         * but mark it so it fires no more and gets reclaimed after dispatch. */
        L->pending_del = true;
        b->need_compact = true;
    } else {
        free_slot(b, idx);
    }
}
uint32_t cc_event_listener_count(const CCEventBus* b) { return b ? b->live : 0; }
uint32_t cc_event_queued_count  (const CCEventBus* b) { return b ? b->qcount : 0; }

/* Reclaim pending_del slots once no dispatch is active. */
static void compact(CCEventBus* b) {
    if (!b->need_compact || b->dispatch_depth > 0) return;
    for (uint32_t i = 0; i < b->lcount; ++i) {
        if (b->listeners[i].fn && b->listeners[i].pending_del) free_slot(b, i);
    }
    /* trim trailing tombstones so lcount tracks real usage */
    while (b->lcount > 0 && !b->listeners[b->lcount - 1].fn) b->lcount--;
    b->need_compact = false;
}

/* ── one delivery to all matching listeners ──────────────────────────────── */
static uint32_t deliver(CCEventBus* b, const CCEvent* e) {
    uint32_t notified = 0;
    /* snapshot the count: listeners added during this call are NOT delivered to
     * for this event (they subscribe forward-looking). */
    uint32_t n = b->lcount;
    b->dispatch_depth++;
    for (uint32_t i = 0; i < n; ++i) {
        Listener* L = &b->listeners[i];
        if (!L->fn || L->pending_del) continue;
        if (L->channel == e->channel || L->channel == CC_EVT_ANY_CHANNEL) {
            L->fn(e, L->userdata);
            notified++;
        }
    }
    b->dispatch_depth--;
    compact(b);
    return notified;
}

/* ── deferred publish ────────────────────────────────────────────────────── */
static bool enqueue(CCEventBus* b, const CCEvent* src,
                    const void* data, uint32_t data_size) {
    if (!b) return false;
    if (!ensure_queue(b, b->qcount + 1)) return false;
    uint32_t blob_off = CC_NO_BLOB;
    if (data && data_size) {
        if (!ensure_blobs(b, b->blob_used + data_size)) return false;
        blob_off = b->blob_used;
        memcpy(b->blobs + blob_off, data, data_size);
        b->blob_used += data_size;
    }
    QueuedEvent* q = &b->queue[b->qcount++];
    q->ev = *src;
    q->ev.data = NULL;              /* patched at dispatch (blobs may realloc)   */
    q->ev.data_size = (blob_off == CC_NO_BLOB) ? 0 : data_size;
    q->blob_off = blob_off;
    return true;
}

bool cc_event_emit(CCEventBus* b, const CCEvent* e) {
    if (!b || !e) return false;
    return enqueue(b, e, e->data, e->data_size);
}
bool cc_event_emit_i(CCEventBus* b, uint32_t ch, uint64_t sender, int64_t i) {
    CCEvent e = {0}; e.channel = ch; e.sender = sender; e.i = i;
    return enqueue(b, &e, NULL, 0);
}
bool cc_event_emit_f(CCEventBus* b, uint32_t ch, uint64_t sender, float f) {
    CCEvent e = {0}; e.channel = ch; e.sender = sender; e.f = f;
    return enqueue(b, &e, NULL, 0);
}
bool cc_event_emit_if(CCEventBus* b, uint32_t ch, uint64_t sender, int64_t i, float f) {
    CCEvent e = {0}; e.channel = ch; e.sender = sender; e.i = i; e.f = f;
    return enqueue(b, &e, NULL, 0);
}
bool cc_event_emit_data(CCEventBus* b, uint32_t ch, uint64_t sender,
                        int64_t i, float f, const void* data, uint32_t data_size) {
    CCEvent e = {0}; e.channel = ch; e.sender = sender; e.i = i; e.f = f;
    return enqueue(b, &e, data, data_size);
}

/* ── flush ───────────────────────────────────────────────────────────────── */
uint32_t cc_event_bus_update(CCEventBus* b) {
    if (!b || b->qcount == 0) return 0;
    /* Snapshot the boundary: only events queued BEFORE this flush are delivered
     * now. Anything a listener publishes lands at index >= boundary and waits
     * for the next update. We copy the snapshot region out first so appends
     * that realloc `queue` mid-flush can't invalidate our cursor. */
    uint32_t boundary = b->qcount;
    uint32_t delivered = 0;

    for (uint32_t idx = 0; idx < boundary; ++idx) {
        QueuedEvent q = b->queue[idx];           /* copy by value               */
        if (q.blob_off != CC_NO_BLOB)
            q.ev.data = b->blobs + q.blob_off;    /* resolve after any realloc   */
        deliver(b, &q.ev);
        delivered++;
    }

    /* Shift any events queued during dispatch (>= boundary) down to the front,
     * and rebase their blobs. Simplest correct approach: move the tail records,
     * then compact the blob heap to only the surviving payloads. */
    uint32_t tail = b->qcount - boundary;
    if (tail == 0) {
        b->qcount = 0;
        b->blob_used = 0;
    } else {
        /* Rebuild the blob heap for surviving events in-place: survivors always
         * sit at higher offsets than freed payloads, and we copy front-to-back,
         * so a single forward cursor compacts them without a scratch buffer. */
        uint32_t new_used = 0;
        for (uint32_t k = 0; k < tail; ++k) {
            QueuedEvent* q = &b->queue[boundary + k];
            if (q->blob_off != CC_NO_BLOB && q->ev.data_size) {
                memmove(b->blobs + new_used, b->blobs + q->blob_off, q->ev.data_size);
                q->blob_off = new_used;
                new_used += q->ev.data_size;
            }
            b->queue[k] = *q;
        }
        b->qcount = tail;
        b->blob_used = new_used;
    }
    return delivered;
}

uint32_t cc_event_emit_now(CCEventBus* b, const CCEvent* e) {
    if (!b || !e) return 0;
    return deliver(b, e);
}
