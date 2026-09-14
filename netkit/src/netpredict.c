/* netpredict.c — CCNetPredict: client-side prediction + reconciliation. See
 * cc/netpredict.h. Pure client-side bookkeeping over opaque state + input blobs;
 * no sockets, no serialization — the game moves the bytes, the engine owns the
 * in-flight input ring and the replay loop (mechanism), the game supplies the
 * simulate rule (policy).
 */
#include "netkit/netpredict.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t seq;
    /* input bytes follow inline in the ring's flat buffer (input_size each) */
} InputHdr;

struct CCNetPredict {
    uint32_t       state_size;
    uint32_t       input_size;
    CCPredictSimFn simulate;
    void*          ud;

    /* ring of unacked inputs: parallel arrays for seq + input bytes. Stored oldest
     * → newest logically via head/count (contiguous replay is simplest + fast). */
    uint32_t*      seqs;      /* capacity entries */
    uint8_t*       inputs;    /* capacity * input_size bytes */
    uint32_t       capacity;
    uint32_t       head;      /* index of oldest pending input */
    uint32_t       count;     /* number pending */

    uint32_t       next_seq;  /* seq for the next apply */
    uint32_t       last_acked;
};

#define CC_PREDICT_DEFAULT_CAP 256

static CCNetPredict* create_common(uint32_t state_size, uint32_t input_size,
                                   CCPredictSimFn simulate, void* ud, uint32_t cap) {
    if (input_size == 0 || cap == 0) return NULL;
    CCNetPredict* p = calloc(1, sizeof(CCNetPredict));
    if (!p) return NULL;
    p->state_size = state_size;
    p->input_size = input_size;
    p->simulate   = simulate;
    p->ud         = ud;
    p->capacity   = cap;
    p->seqs       = calloc(cap, sizeof(uint32_t));
    p->inputs     = calloc((size_t)cap, input_size);
    p->next_seq   = 1;
    if (!p->seqs || !p->inputs) { free(p->seqs); free(p->inputs); free(p); return NULL; }
    return p;
}

CCNetPredict* cc_predict_create(uint32_t state_size, uint32_t input_size,
                                CCPredictSimFn simulate, void* ud) {
    return create_common(state_size, input_size, simulate, ud, CC_PREDICT_DEFAULT_CAP);
}
CCNetPredict* cc_predict_create_ex(uint32_t state_size, uint32_t input_size,
                                   CCPredictSimFn simulate, void* ud, uint32_t capacity) {
    return create_common(state_size, input_size, simulate, ud, capacity);
}
void cc_predict_destroy(CCNetPredict* p) {
    if (!p) return;
    free(p->seqs); free(p->inputs); free(p);
}

/* physical index of the k-th pending input (0 = oldest) */
static uint32_t ring_index(const CCNetPredict* p, uint32_t k) {
    return (p->head + k) % p->capacity;
}
static uint8_t* input_slot(CCNetPredict* p, uint32_t phys) {
    return p->inputs + (size_t)phys * p->input_size;
}

uint32_t cc_predict_apply(CCNetPredict* p, void* state, const void* input) {
    if (!p || !input) return 0;
    uint32_t seq = p->next_seq++;

    /* record in the ring; if full, drop the oldest (it's older than anything the
     * server could still ack — a very-behind client). */
    if (p->count == p->capacity) {
        p->head = (p->head + 1) % p->capacity;
        p->count--;
    }
    uint32_t phys = ring_index(p, p->count);
    p->seqs[phys] = seq;
    memcpy(input_slot(p, phys), input, p->input_size);
    p->count++;

    /* predict now: advance local state by this input */
    if (p->simulate && state) p->simulate(state, input, p->ud);
    return seq;
}

uint32_t cc_predict_reconcile(CCNetPredict* p, void* state,
                              const void* authoritative, uint32_t acked_seq) {
    if (!p || !state) return 0;
    p->last_acked = acked_seq;

    /* 1. snap to authoritative truth */
    if (authoritative && p->state_size) memcpy(state, authoritative, p->state_size);

    /* 2. drop acked inputs (seq <= acked_seq) from the front of the ring. Seqs are
     * monotonic in insertion order, so acked ones are a prefix. */
    while (p->count > 0) {
        uint32_t phys = ring_index(p, 0);
        if (p->seqs[phys] > acked_seq) break;
        p->head = (p->head + 1) % p->capacity;
        p->count--;
    }

    /* 3. replay the still-unacked inputs on top of authoritative state */
    uint32_t replayed = 0;
    if (p->simulate) {
        for (uint32_t k = 0; k < p->count; k++) {
            uint8_t* in = input_slot(p, ring_index(p, k));
            p->simulate(state, in, p->ud);
            replayed++;
        }
    }
    return replayed;
}

uint32_t cc_predict_pending(const CCNetPredict* p)  { return p ? p->count : 0; }
uint32_t cc_predict_next_seq(const CCNetPredict* p) { return p ? p->next_seq : 0; }
uint32_t cc_predict_last_acked(const CCNetPredict* p){ return p ? p->last_acked : 0; }

void cc_predict_reset(CCNetPredict* p) {
    if (!p) return;
    p->head = p->count = 0;
    p->next_seq = 1;
    p->last_acked = 0;
}
