/* netcmd_test — proves the AUTHORITY half of CCNet with NO real network. A client
 * builds command REQUESTS and serializes them to bytes; the server ingests the
 * bytes, runs the game's VALIDATE rule, and applies ONLY accepted commands to its
 * authoritative scene. Asserts: legal command applied; illegal command rejected
 * AND state untouched; replay/stale sequence dropped; open (NULL-validate) command
 * accepted; reject accounting; multiple commands coalesced in one buffer; and the
 * core guarantee — a client can ONLY request, it cannot write authority directly.
 * Prints "NETCMD TEST: all checks passed" / returns 0. */
#include "cc/claudecore.h"
#include "cc/net.h"
#include "netkit/netcmd.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* authoritative game state: each entity has a position + a "health" the server owns */
typedef struct { float x, y, z; } Pos;
static CCComponentId POS;
static Pos* pos_of(CCScene* s, CCEntityId e){
    return (Pos*)(cc_component_has(s,e,POS)?cc_component_get(s,e,POS):cc_component_add(s,e,POS));
}

/* the player entity the commands act on (in a real game, resolved from ctx->client) */
static CCEntityId g_player;

/* ── command payloads (what a client REQUESTS) ──────────────────────────────── */
typedef struct { float dx, dy, dz; } MoveReq;    /* "please move me by this delta" */
typedef struct { char text[32]; }    ChatReq;    /* open command, no authority */

/* server-side RULE for MOVE: only allow small steps. A cheating client asking to
 * teleport across the map is REJECTED — the enforcement point. */
#define MAX_STEP 2.0f
static CCNetCmdResult validate_move(const CCNetCmdCtx* ctx) {
    if (ctx->size != sizeof(MoveReq)) return CC_NETCMD_REJECT;
    const MoveReq* m = (const MoveReq*)ctx->data;
    float d2 = m->dx*m->dx + m->dy*m->dy + m->dz*m->dz;
    if (d2 > MAX_STEP*MAX_STEP) return CC_NETCMD_REJECT;   /* illegal: too far */
    return CC_NETCMD_ACCEPT;
}
static void apply_move(const CCNetCmdCtx* ctx) {
    const MoveReq* m = (const MoveReq*)ctx->data;
    Pos* p = pos_of(ctx->scene, g_player);
    p->x += m->dx; p->y += m->dy; p->z += m->dz;
}

static int g_chat_count = 0;
static void apply_chat(const CCNetCmdCtx* ctx) {
    (void)ctx; g_chat_count++;   /* open command: no validate, just a side effect */
}

int main(void) {
    CCEngineConfig cfg = cc_sandbox_config(); cfg.width=64; cfg.height=64; cfg.verbose=false;
    CCEngine* e = cc_init(&cfg);
    CCScene* server_scene = cc_scene_create(e, "server");

    POS = cc_component_register("Pos", sizeof(Pos));
    g_player = cc_entity_create(server_scene);
    *pos_of(server_scene, g_player) = (Pos){0,0,0};

    /* register command types + their server-side rules (mechanism vs policy) */
    cc_netcmd_reset();
    CCNetCmdType MOVE = cc_netcmd_register("move", validate_move, apply_move, NULL);
    CCNetCmdType CHAT = cc_netcmd_register("chat", NULL,          apply_chat, NULL); /* open */
    CHECK(MOVE != CC_NETCMD_NULL && CHAT != CC_NETCMD_NULL, "command types registered");
    CHECK(cc_netcmd_type_count()==2, "two command types");
    CHECK(strcmp(cc_netcmd_type_name(MOVE),"move")==0, "type name introspectable");

    CCNetCmdClient* cl = cc_netcmd_client_create();
    CCNetCmdServer* sv = cc_netcmd_server_create(server_scene);
    const CCNetClientId CLIENT_A = 1001;

    uint8_t buf[256];

    /* ── 1. a LEGAL move request → accepted + applied ───────────────────────── */
    MoveReq legal = { 1.0f, 0.0f, 0.0f };
    size_t n = cc_netcmd_client_encode(cl, MOVE, &legal, sizeof(legal), buf, sizeof(buf));
    CHECK(n > 0, "legal move encoded");
    uint32_t acc = cc_netcmd_server_ingest(sv, CLIENT_A, buf, n);
    CHECK(acc == 1, "server accepted the legal move");
    CHECK(fabsf(pos_of(server_scene,g_player)->x - 1.0f) < 1e-6, "authoritative pos moved by the accepted command");

    /* ── 2. an ILLEGAL move (teleport) → rejected + state UNTOUCHED ──────────── */
    float x_before = pos_of(server_scene,g_player)->x;
    MoveReq cheat = { 500.0f, 0.0f, 0.0f };   /* way past MAX_STEP */
    n = cc_netcmd_client_encode(cl, MOVE, &cheat, sizeof(cheat), buf, sizeof(buf));
    acc = cc_netcmd_server_ingest(sv, CLIENT_A, buf, n);
    CHECK(acc == 0, "server rejected the illegal teleport");
    CHECK(fabsf(pos_of(server_scene,g_player)->x - x_before) < 1e-6, "rejected command did NOT mutate authoritative state");
    CHECK(cc_netcmd_server_rejected(sv, CLIENT_A)==1, "reject counted for the client");
    CHECK(cc_netcmd_server_accepted(sv, CLIENT_A)==1, "accepted count still 1");

    /* ── 3. REPLAY / stale sequence is dropped ──────────────────────────────── */
    /* re-encode a fresh legal move, ingest it, then ingest the SAME bytes again;
     * the second must be dropped as stale (seq already seen) — no double-apply. */
    MoveReq step = { 0.5f, 0.0f, 0.0f };
    n = cc_netcmd_client_encode(cl, MOVE, &step, sizeof(step), buf, sizeof(buf));
    float x_pre = pos_of(server_scene,g_player)->x;
    cc_netcmd_server_ingest(sv, CLIENT_A, buf, n);    /* first time: applies */
    float x_mid = pos_of(server_scene,g_player)->x;
    CHECK(fabsf(x_mid - (x_pre+0.5f)) < 1e-6, "fresh move applied once");
    uint32_t acc_replay = cc_netcmd_server_ingest(sv, CLIENT_A, buf, n);  /* replay */
    CHECK(acc_replay == 0, "replayed command dropped as stale");
    CHECK(fabsf(pos_of(server_scene,g_player)->x - x_mid) < 1e-6, "replay did not move state again");

    /* ── 4. OPEN command (NULL validate) is accepted ────────────────────────── */
    ChatReq chat; memset(&chat,0,sizeof(chat)); strcpy(chat.text, "hello");
    n = cc_netcmd_client_encode(cl, CHAT, &chat, sizeof(chat), buf, sizeof(buf));
    acc = cc_netcmd_server_ingest(sv, CLIENT_A, buf, n);
    CHECK(acc == 1 && g_chat_count == 1, "open command accepted + applied");

    /* ── 5. MULTIPLE commands coalesced into one buffer ─────────────────────── */
    /* a transport may pack several encoded commands together; ingest must process
     * all of them in order. */
    MoveReq a = {0.1f,0,0}, b = {0.2f,0,0};
    uint8_t multi[256]; size_t off = 0;
    off += cc_netcmd_client_encode(cl, MOVE, &a, sizeof(a), multi+off, sizeof(multi)-off);
    off += cc_netcmd_client_encode(cl, MOVE, &b, sizeof(b), multi+off, sizeof(multi)-off);
    float x_batch = pos_of(server_scene,g_player)->x;
    acc = cc_netcmd_server_ingest(sv, CLIENT_A, multi, off);
    CHECK(acc == 2, "both coalesced commands accepted");
    CHECK(fabsf(pos_of(server_scene,g_player)->x - (x_batch+0.3f)) < 1e-6, "both coalesced moves applied in order");

    /* ── 6. THE STRUCTURAL GUARANTEE: a client cannot write authority directly ── */
    /* There is no client-side API that touches server_scene. The ONLY influence a
     * client has is cc_netcmd_client_encode → bytes → server ingest → validate.
     * Prove it by having a second, hostile client blast illegal requests: none of
     * them move the authoritative state. */
    CCNetCmdClient* hostile = cc_netcmd_client_create();
    const CCNetClientId CLIENT_B = 2002;
    float x_locked = pos_of(server_scene,g_player)->x;
    for (int i=0;i<10;i++) {
        MoveReq tp = { 999.0f + i, 0, 0 };
        size_t hn = cc_netcmd_client_encode(hostile, MOVE, &tp, sizeof(tp), buf, sizeof(buf));
        cc_netcmd_server_ingest(sv, CLIENT_B, buf, hn);
    }
    CHECK(fabsf(pos_of(server_scene,g_player)->x - x_locked) < 1e-6, "hostile client could not write authoritative state");
    CHECK(cc_netcmd_server_rejected(sv, CLIENT_B)==10, "all 10 hostile requests rejected + counted");
    CHECK(cc_netcmd_server_accepted(sv, CLIENT_B)==0, "hostile client accepted nothing");

    /* ── 7. last-seq accounting (for input-ack / prediction) + forget ───────── */
    CHECK(cc_netcmd_server_last_seq(sv, CLIENT_A) > 0, "server tracks last accepted seq per client");
    cc_netcmd_server_forget(sv, CLIENT_A);
    CHECK(cc_netcmd_server_accepted(sv, CLIENT_A)==0, "forgotten client accounting cleared");

    /* ── 8. NULL-safety ─────────────────────────────────────────────────────── */
    CHECK(cc_netcmd_client_encode(NULL, MOVE, &legal, sizeof(legal), buf, sizeof(buf))==0, "NULL client encode safe");
    CHECK(cc_netcmd_server_ingest(NULL, CLIENT_A, buf, 4)==0, "NULL server ingest safe");
    CHECK(cc_netcmd_client_encode(cl, 9999, &legal, sizeof(legal), buf, sizeof(buf))==0, "unknown type encode rejected");

    cc_netcmd_client_destroy(cl);
    cc_netcmd_client_destroy(hostile);
    cc_netcmd_server_destroy(sv);

    if (failures == 0)
        printf("NETCMD TEST: all checks passed (command channel + validation seam: legal applied, illegal rejected + state untouched, replay dropped, open command, coalesced batch, hostile client blocked, accounting — all with no real socket)\n");
    else
        printf("NETCMD TEST: %d FAILURES\n", failures);
    return failures ? 1 : 0;
}
