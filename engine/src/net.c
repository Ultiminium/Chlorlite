/* net.c — CCNet: authoritative server + snapshot replication. See cc/net.h.
 *
 * Wire format (little-endian, versionless for now — same build both ends):
 *   snapshot header:  [u8 type=SNAP][u32 tick][u32 baseline][u16 nrec]
 *   per entity record:[u32 netid][u8 flags][u16 nchan]
 *                       flags: bit0=spawn bit1=despawn
 *                     then per changed channel: [u16 chan][u16 len][bytes]
 * A FULL snapshot marks every entity as spawn + writes all channels. A DELTA
 * writes only entities/channels whose serialized bytes changed vs the baseline,
 * plus spawns/despawns since the baseline.
 *
 * Transport-free: the server produces byte buffers and the client consumes them.
 * A test (or a real socket layer) moves the bytes. Fully headless.
 */
#include "cc/net.h"
#include <stdlib.h>
#include <string.h>

/* ─── replication registry (shared, process-global; same order both ends) ──── */
typedef struct {
    CCComponentId comp;
    uint32_t      size;
    CCNetWriteFn  write;
    CCNetReadFn   read;
    void*         ud;
} NetChan;

#define NET_MAX_CHAN 64
static NetChan  g_chan[NET_MAX_CHAN];
static uint32_t g_chan_count = 0;

CCNetChannel cc_net_replicate(CCComponentId comp, uint32_t size,
                              CCNetWriteFn w, CCNetReadFn r, void* ud) {
    if (g_chan_count >= NET_MAX_CHAN) return 0xFFFF;
    uint32_t i = g_chan_count++;
    g_chan[i].comp = comp; g_chan[i].size = size;
    g_chan[i].write = w;   g_chan[i].read = r; g_chan[i].ud = ud;
    return (CCNetChannel)i;
}
void     cc_net_reset(void) { g_chan_count = 0; }
uint32_t cc_net_channel_count(void) { return g_chan_count; }

/* ─── little-endian buffer helpers ────────────────────────────────────────── */
typedef struct { uint8_t* p; size_t cap, len; bool overflow; } WBuf;
static void w_u8 (WBuf* b, uint8_t v){ if(b->len+1>b->cap){b->overflow=true;return;} b->p[b->len++]=v; }
static void w_u16(WBuf* b, uint16_t v){ if(b->len+2>b->cap){b->overflow=true;return;} b->p[b->len++]=v&0xFF; b->p[b->len++]=(v>>8)&0xFF; }
static void w_u32(WBuf* b, uint32_t v){ if(b->len+4>b->cap){b->overflow=true;return;} for(int i=0;i<4;i++) b->p[b->len++]=(v>>(8*i))&0xFF; }
static void w_bytes(WBuf* b, const uint8_t* s, size_t n){ if(b->len+n>b->cap){b->overflow=true;return;} memcpy(b->p+b->len,s,n); b->len+=n; }

typedef struct { const uint8_t* p; size_t len, pos; bool bad; } RBuf;
static uint8_t  r_u8 (RBuf* b){ if(b->pos+1>b->len){b->bad=true;return 0;} return b->p[b->pos++]; }
static uint16_t r_u16(RBuf* b){ if(b->pos+2>b->len){b->bad=true;return 0;} uint16_t v=b->p[b->pos]|(b->p[b->pos+1]<<8); b->pos+=2; return v; }
static uint32_t r_u32(RBuf* b){ if(b->pos+4>b->len){b->bad=true;return 0;} uint32_t v=0; for(int i=0;i<4;i++) v|=((uint32_t)b->p[b->pos+i])<<(8*i); b->pos+=4; return v; }

enum { MSG_SNAP = 1 };
enum { FLAG_SPAWN = 1, FLAG_DESPAWN = 2 };

/* ─── server ──────────────────────────────────────────────────────────────── */
/* per replicated entity, we cache the last-serialized bytes per channel so a
 * delta can detect changes. */
typedef struct {
    CCNetId    netid;
    CCEntityId entity;
    bool       alive;
    bool       spawned_since_baseline;
    bool       despawn_pending;
    uint8_t*   cache[NET_MAX_CHAN];   /* last serialized bytes per channel */
    uint16_t   cache_len[NET_MAX_CHAN];
} SvEnt;

struct CCNetServer {
    CCScene*  scene;
    SvEnt*    ents;
    uint32_t  count, cap;
    CCNetId   next_id;
    uint32_t  tick;
};

CCNetServer* cc_net_server_create(CCScene* scene) {
    CCNetServer* sv = calloc(1, sizeof(CCNetServer));
    sv->scene = scene; sv->next_id = 1; sv->tick = 1;
    sv->cap = 16; sv->ents = calloc(sv->cap, sizeof(SvEnt));
    return sv;
}
void cc_net_server_destroy(CCNetServer* sv) {
    if (!sv) return;
    for (uint32_t i=0;i<sv->count;i++)
        for (uint32_t c=0;c<NET_MAX_CHAN;c++) free(sv->ents[i].cache[c]);
    free(sv->ents); free(sv);
}
static SvEnt* sv_find(CCNetServer* sv, CCNetId id){
    for(uint32_t i=0;i<sv->count;i++) if(sv->ents[i].netid==id) return &sv->ents[i];
    return NULL;
}
CCNetId cc_net_server_spawn(CCNetServer* sv, CCEntityId e) {
    if (!sv) return CC_NET_NULL;
    if (sv->count >= sv->cap) {
        sv->cap*=2; sv->ents = realloc(sv->ents, sv->cap*sizeof(SvEnt));
        memset(&sv->ents[sv->count], 0, (sv->cap-sv->count)*sizeof(SvEnt));
    }
    SvEnt* se = &sv->ents[sv->count++];
    memset(se, 0, sizeof(*se));
    se->netid = sv->next_id++;
    se->entity = e; se->alive = true; se->spawned_since_baseline = true;
    return se->netid;
}
void cc_net_server_despawn(CCNetServer* sv, CCNetId id) {
    SvEnt* se = sv_find(sv, id);
    if (se) { se->alive = false; se->despawn_pending = true; }
}
uint32_t cc_net_server_tick(const CCNetServer* sv){ return sv?sv->tick:0; }
void cc_net_server_advance(CCNetServer* sv){ if(sv) sv->tick++; }
uint32_t cc_net_server_entity_count(const CCNetServer* sv){
    if(!sv) return 0; uint32_t n=0; for(uint32_t i=0;i<sv->count;i++) if(sv->ents[i].alive) n++; return n;
}

/* serialize one entity's changed channels into wb; returns true if anything (or a
 * spawn/despawn) was written for it. full=true forces all channels + spawn flag. */
static bool sv_write_entity(CCNetServer* sv, SvEnt* se, WBuf* wb, bool full) {
    uint8_t flags = 0;
    if (full || se->spawned_since_baseline) flags |= FLAG_SPAWN;
    if (se->despawn_pending) flags |= FLAG_DESPAWN;

    /* gather changed channels into a temp list first (need count up front) */
    uint16_t chans[NET_MAX_CHAN]; uint16_t lens[NET_MAX_CHAN];
    uint8_t  scratch[NET_MAX_CHAN][256]; uint16_t nchan = 0;

    if (!se->despawn_pending) {
        for (uint32_t c=0;c<g_chan_count;c++) {
            if (!cc_component_has(sv->scene, se->entity, g_chan[c].comp)) continue;
            void* comp = cc_component_get(sv->scene, se->entity, g_chan[c].comp);
            if (!comp) continue;
            uint8_t tmp[256];
            size_t n = g_chan[c].write ? g_chan[c].write(comp, tmp, sizeof(tmp), g_chan[c].ud) : 0;
            if (n == 0 || n > sizeof(tmp)) continue;
            bool changed = full || se->spawned_since_baseline
                        || se->cache_len[c] != n
                        || memcmp(se->cache[c] ? se->cache[c] : (uint8_t*)"", tmp, n) != 0;
            if (changed) {
                chans[nchan] = (uint16_t)c; lens[nchan] = (uint16_t)n;
                memcpy(scratch[nchan], tmp, n); nchan++;
                /* update cache */
                se->cache[c] = realloc(se->cache[c], n);
                memcpy(se->cache[c], tmp, n); se->cache_len[c] = (uint16_t)n;
            }
        }
    }
    if (!flags && nchan == 0) return false;   /* nothing to send for this entity */

    w_u32(wb, se->netid);
    w_u8 (wb, flags);
    w_u16(wb, nchan);
    for (uint16_t k=0;k<nchan;k++) {
        w_u16(wb, chans[k]); w_u16(wb, lens[k]); w_bytes(wb, scratch[k], lens[k]);
    }
    return true;
}

size_t cc_net_server_snapshot(CCNetServer* sv, uint32_t baseline_ack,
                              uint8_t* buf, size_t cap) {
    if (!sv || !buf) return 0;
    bool full = (baseline_ack == 0);
    WBuf wb = { buf, cap, 0, false };
    w_u8 (&wb, MSG_SNAP);
    w_u32(&wb, sv->tick);
    w_u32(&wb, baseline_ack);
    size_t nrec_pos = wb.len; w_u16(&wb, 0);   /* patched after */

    uint16_t nrec = 0;
    for (uint32_t i=0;i<sv->count;i++) {
        SvEnt* se = &sv->ents[i];
        if (!se->alive && !se->despawn_pending) continue;
        if (sv_write_entity(sv, se, &wb, full)) nrec++;
    }
    if (wb.overflow) return 0;
    /* patch nrec */
    buf[nrec_pos] = nrec & 0xFF; buf[nrec_pos+1] = (nrec>>8)&0xFF;

    /* after a snapshot: clear spawn flags; drop despawned entities */
    for (uint32_t i=0;i<sv->count;) {
        SvEnt* se=&sv->ents[i];
        se->spawned_since_baseline = false;
        if (se->despawn_pending) {
            for (uint32_t c=0;c<NET_MAX_CHAN;c++) free(se->cache[c]);
            *se = sv->ents[sv->count-1]; sv->count--; continue;
        }
        i++;
    }
    return wb.len;
}

/* ─── client ──────────────────────────────────────────────────────────────── */
typedef struct { CCNetId netid; CCEntityId local; bool alive; } ClEnt;
struct CCNetClient {
    CCScene* scene;
    ClEnt*   ents; uint32_t count, cap;
    uint32_t last_ack;
};
CCNetClient* cc_net_client_create(CCScene* scene) {
    CCNetClient* cl = calloc(1, sizeof(CCNetClient));
    cl->scene = scene; cl->cap = 16; cl->ents = calloc(cl->cap, sizeof(ClEnt));
    return cl;
}
void cc_net_client_destroy(CCNetClient* cl){ if(!cl)return; free(cl->ents); free(cl); }
static ClEnt* cl_find(CCNetClient* cl, CCNetId id){
    for(uint32_t i=0;i<cl->count;i++) if(cl->ents[i].netid==id) return &cl->ents[i];
    return NULL;
}
static ClEnt* cl_add(CCNetClient* cl, CCNetId id, CCEntityId local){
    if (cl->count>=cl->cap){ cl->cap*=2; cl->ents=realloc(cl->ents,cl->cap*sizeof(ClEnt)); }
    ClEnt* e=&cl->ents[cl->count++]; e->netid=id; e->local=local; e->alive=true; return e;
}
CCEntityId cc_net_client_entity(CCNetClient* cl, CCNetId id){
    ClEnt* e=cl_find(cl,id); return e?e->local:CC_ENTITY_NULL;
}
uint32_t cc_net_client_last_ack(const CCNetClient* cl){ return cl?cl->last_ack:0; }
uint32_t cc_net_client_entity_count(const CCNetClient* cl){
    if(!cl) return 0; uint32_t n=0; for(uint32_t i=0;i<cl->count;i++) if(cl->ents[i].alive) n++; return n;
}

uint32_t cc_net_client_apply(CCNetClient* cl, const uint8_t* buf, size_t len) {
    if (!cl || !buf) return 0;
    RBuf rb = { buf, len, 0, false };
    uint8_t type = r_u8(&rb);
    if (type != MSG_SNAP) return 0;
    uint32_t tick = r_u32(&rb);
    (void)r_u32(&rb);                 /* baseline (informational to client) */
    uint16_t nrec = r_u16(&rb);
    for (uint16_t i=0;i<nrec;i++) {
        CCNetId id = r_u32(&rb);
        uint8_t flags = r_u8(&rb);
        uint16_t nchan = r_u16(&rb);
        ClEnt* ce = cl_find(cl, id);
        if (flags & FLAG_SPAWN) {
            if (!ce) { CCEntityId e = cc_entity_create(cl->scene); ce = cl_add(cl, id, e); }
        }
        if (!ce && !(flags & FLAG_DESPAWN)) {
            /* unknown entity in a delta without spawn — create defensively */
            CCEntityId e = cc_entity_create(cl->scene); ce = cl_add(cl, id, e);
        }
        for (uint16_t k=0;k<nchan;k++) {
            uint16_t chan = r_u16(&rb);
            uint16_t clen = r_u16(&rb);
            if (rb.pos + clen > rb.len) { rb.bad=true; break; }
            const uint8_t* data = buf + rb.pos; rb.pos += clen;
            if (ce && chan < g_chan_count) {
                NetChan* nc = &g_chan[chan];
                void* comp = cc_component_has(cl->scene, ce->local, nc->comp)
                           ? cc_component_get(cl->scene, ce->local, nc->comp)
                           : cc_component_add(cl->scene, ce->local, nc->comp);
                if (comp && nc->read) nc->read(comp, data, clen, nc->ud);
            }
        }
        if (flags & FLAG_DESPAWN) {
            if (ce) { cc_entity_destroy(cl->scene, ce->local); ce->alive=false;
                      *ce = cl->ents[cl->count-1]; cl->count--; }
        }
        if (rb.bad) break;
    }
    if (!rb.bad) cl->last_ack = tick;
    return cl->last_ack;
}
