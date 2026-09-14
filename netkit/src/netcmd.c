/* netcmd.c — CCNetCmd: client→server command channel + server-side validation
 * seam. See cc/netcmd.h. This is the authority half of CCNet: cc/net.h lets the
 * server broadcast truth; this lets clients request changes and gives the server
 * the single enforced point where requests are accepted or rejected.
 *
 * Wire format (little-endian, one command; a buffer may hold several back-to-back):
 *   [u8 type=CMD][u16 cmd_type][u32 seq][u16 len][len bytes payload]
 *
 * Transport-free: the client encodes bytes, the server ingests bytes; a test or a
 * real socket layer moves them. The engine owns decode/sequencing/dispatch/reject
 * accounting (mechanism); the game owns validate/apply (policy).
 */
#include "netkit/netcmd.h"
#include <stdlib.h>
#include <string.h>

/* ─── shared command registry (process-global; same order both ends) ────────── */
typedef struct {
    char                name[48];
    CCNetCmdValidateFn  validate;
    CCNetCmdApplyFn     apply;
    void*               ud;
    bool                used;
} CmdType;

#define NETCMD_MAX_TYPES 128
static CmdType  g_types[NETCMD_MAX_TYPES];
static uint32_t g_type_count = 0;

CCNetCmdType cc_netcmd_register(const char* name, CCNetCmdValidateFn validate,
                                CCNetCmdApplyFn apply, void* ud) {
    if (g_type_count >= NETCMD_MAX_TYPES) return CC_NETCMD_NULL;
    uint32_t i = g_type_count++;
    CmdType* t = &g_types[i];
    memset(t, 0, sizeof(*t));
    if (name) { strncpy(t->name, name, sizeof(t->name)-1); }
    t->validate = validate; t->apply = apply; t->ud = ud; t->used = true;
    return (CCNetCmdType)i;
}
void        cc_netcmd_reset(void) { g_type_count = 0; memset(g_types, 0, sizeof(g_types)); }
uint32_t    cc_netcmd_type_count(void) { return g_type_count; }
const char* cc_netcmd_type_name(CCNetCmdType t) {
    if (t >= g_type_count) return NULL;
    return g_types[t].name;
}

/* ─── little-endian buffer helpers (same style as net.c) ────────────────────── */
typedef struct { uint8_t* p; size_t cap, len; bool overflow; } WBuf;
static void w_u8 (WBuf* b, uint8_t v){ if(b->len+1>b->cap){b->overflow=true;return;} b->p[b->len++]=v; }
static void w_u16(WBuf* b, uint16_t v){ if(b->len+2>b->cap){b->overflow=true;return;} b->p[b->len++]=v&0xFF; b->p[b->len++]=(v>>8)&0xFF; }
static void w_u32(WBuf* b, uint32_t v){ if(b->len+4>b->cap){b->overflow=true;return;} for(int i=0;i<4;i++) b->p[b->len++]=(v>>(8*i))&0xFF; }
static void w_bytes(WBuf* b, const uint8_t* s, size_t n){ if(n && b->len+n>b->cap){b->overflow=true;return;} if(n) memcpy(b->p+b->len,s,n); b->len+=n; }

typedef struct { const uint8_t* p; size_t len, pos; bool bad; } RBuf;
static uint8_t  r_u8 (RBuf* b){ if(b->pos+1>b->len){b->bad=true;return 0;} return b->p[b->pos++]; }
static uint16_t r_u16(RBuf* b){ if(b->pos+2>b->len){b->bad=true;return 0;} uint16_t v=b->p[b->pos]|(b->p[b->pos+1]<<8); b->pos+=2; return v; }
static uint32_t r_u32(RBuf* b){ if(b->pos+4>b->len){b->bad=true;return 0;} uint32_t v=0; for(int i=0;i<4;i++) v|=((uint32_t)b->p[b->pos+i])<<(8*i); b->pos+=4; return v; }

enum { MSG_CMD = 2 };   /* distinct from net.c's MSG_SNAP=1, in case a transport shares a stream */

/* ─── client ────────────────────────────────────────────────────────────────── */
struct CCNetCmdClient { uint32_t next_seq; };

CCNetCmdClient* cc_netcmd_client_create(void) {
    CCNetCmdClient* c = calloc(1, sizeof(CCNetCmdClient));
    c->next_seq = 1;
    return c;
}
void cc_netcmd_client_destroy(CCNetCmdClient* c){ free(c); }
uint32_t cc_netcmd_client_next_seq(const CCNetCmdClient* c){ return c?c->next_seq:0; }

size_t cc_netcmd_client_encode(CCNetCmdClient* c, CCNetCmdType type,
                               const void* data, size_t size,
                               uint8_t* out, size_t cap) {
    if (!c || !out) return 0;
    if (type >= g_type_count) return 0;         /* unknown type — reject at the source */
    if (size > 0xFFFF) return 0;                /* payload length is a u16 on the wire */
    WBuf wb = { out, cap, 0, false };
    w_u8 (&wb, MSG_CMD);
    w_u16(&wb, (uint16_t)type);
    w_u32(&wb, c->next_seq);
    w_u16(&wb, (uint16_t)size);
    w_bytes(&wb, (const uint8_t*)data, size);
    if (wb.overflow) return 0;
    c->next_seq++;                              /* consume the sequence number */
    return wb.len;
}

/* ─── server ────────────────────────────────────────────────────────────────── */
typedef struct {
    CCNetClientId id;
    uint32_t      accepted, rejected;
    uint32_t      last_seq;      /* highest seq seen from this client (stale-drop) */
    bool          used;
} CmdClient;

struct CCNetCmdServer {
    CCScene*    scene;
    CmdClient*  clients;
    uint32_t    count, cap;
};

CCNetCmdServer* cc_netcmd_server_create(CCScene* scene) {
    CCNetCmdServer* s = calloc(1, sizeof(CCNetCmdServer));
    s->scene = scene;
    s->cap = 8; s->clients = calloc(s->cap, sizeof(CmdClient));
    return s;
}
void cc_netcmd_server_destroy(CCNetCmdServer* s){
    if (!s) return;
    free(s->clients); free(s);
}
static CmdClient* srv_client(CCNetCmdServer* s, CCNetClientId id, bool create) {
    for (uint32_t i=0;i<s->count;i++) if (s->clients[i].used && s->clients[i].id==id) return &s->clients[i];
    if (!create) return NULL;
    /* reuse a freed slot if any */
    for (uint32_t i=0;i<s->count;i++) if (!s->clients[i].used) {
        CmdClient* c=&s->clients[i]; memset(c,0,sizeof(*c)); c->id=id; c->used=true; return c;
    }
    if (s->count >= s->cap) {
        s->cap *= 2; s->clients = realloc(s->clients, s->cap*sizeof(CmdClient));
        memset(&s->clients[s->count], 0, (s->cap-s->count)*sizeof(CmdClient));
    }
    CmdClient* c = &s->clients[s->count++];
    memset(c, 0, sizeof(*c)); c->id = id; c->used = true;
    return c;
}

uint32_t cc_netcmd_server_ingest(CCNetCmdServer* s, CCNetClientId client,
                                 const uint8_t* buf, size_t len) {
    if (!s || !buf) return 0;
    CmdClient* cc = srv_client(s, client, true);
    RBuf rb = { buf, len, 0, false };
    uint32_t accepted = 0;

    while (rb.pos < rb.len) {
        uint8_t msg = r_u8(&rb);
        if (rb.bad || msg != MSG_CMD) break;    /* not a command stream — stop safely */
        uint16_t type = r_u16(&rb);
        uint32_t seq  = r_u32(&rb);
        uint16_t plen = r_u16(&rb);
        if (rb.bad) break;
        if (rb.pos + plen > rb.len) break;       /* truncated payload — stop */
        const uint8_t* payload = buf + rb.pos;
        rb.pos += plen;

        /* stale / duplicate: a client's sequence numbers only move forward. This
         * drops replays and out-of-order arrivals (a cheap correctness + anti-
         * replay guard; a real UDP layer can reorder). */
        if (seq <= cc->last_seq) continue;
        cc->last_seq = seq;

        if (type >= g_type_count || !g_types[type].used) { cc->rejected++; continue; }
        CmdType* t = &g_types[type];

        CCNetCmdCtx ctx = {
            .scene = s->scene, .client = client, .type = (CCNetCmdType)type,
            .data = payload, .size = plen, .seq = seq, .ud = t->ud,
        };

        /* THE ENFORCEMENT POINT: validate first. NULL validate = open command
         * (accept). apply runs ONLY on accept — a rejected command can never
         * mutate authoritative state. */
        CCNetCmdResult res = t->validate ? t->validate(&ctx) : CC_NETCMD_ACCEPT;
        if (res == CC_NETCMD_ACCEPT) {
            if (t->apply) t->apply(&ctx);
            cc->accepted++; accepted++;
        } else {
            cc->rejected++;
        }
    }
    return accepted;
}

uint32_t cc_netcmd_server_accepted(const CCNetCmdServer* s, CCNetClientId client) {
    if (!s) return 0;
    for (uint32_t i=0;i<s->count;i++) if (s->clients[i].used && s->clients[i].id==client) return s->clients[i].accepted;
    return 0;
}
uint32_t cc_netcmd_server_rejected(const CCNetCmdServer* s, CCNetClientId client) {
    if (!s) return 0;
    for (uint32_t i=0;i<s->count;i++) if (s->clients[i].used && s->clients[i].id==client) return s->clients[i].rejected;
    return 0;
}
uint32_t cc_netcmd_server_last_seq(const CCNetCmdServer* s, CCNetClientId client) {
    if (!s) return 0;
    for (uint32_t i=0;i<s->count;i++) if (s->clients[i].used && s->clients[i].id==client) return s->clients[i].last_seq;
    return 0;
}
void cc_netcmd_server_forget(CCNetCmdServer* s, CCNetClientId client) {
    if (!s) return;
    for (uint32_t i=0;i<s->count;i++) if (s->clients[i].used && s->clients[i].id==client) { s->clients[i].used=false; return; }
}
