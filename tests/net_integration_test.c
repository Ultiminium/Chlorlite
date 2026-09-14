/* net_integration_test — the whole CCNet stack working together in one process,
 * no sockets: client PREDICTION (netpredict) + the authoritative COMMAND channel
 * and VALIDATION SEAM (netcmd) + snapshot REPLICATION (net). Demonstrates the
 * end-to-end payoff and the key property: a client feels instant (prediction), the
 * server stays authoritative (validation), and when the two disagree — e.g. the
 * server CLAMPS an over-fast move the way an anti-cheat rule would — reconciliation
 * pulls the client back to truth. Prints "NET INTEGRATION TEST: all checks passed".
 *
 * Scenario: a 1-D runner. The client pushes "move right by dx" each tick. The server
 * enforces a max speed: it clamps dx to MAX_DX before applying. When the client asks
 * for a legal dx, prediction and server agree (no correction). When the client asks
 * for an illegal-fast dx (a speed hack), the server clamps it, and the client — which
 * optimistically predicted the full move — gets corrected down on reconcile. */
#include "cc/claudecore.h"
#include "cc/net.h"
#include "netkit/netcmd.h"
#include "netkit/netpredict.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* shared local prediction state + input */
typedef struct { float x; } RunState;
typedef struct { float dx; } RunInput;

#define MAX_DX 1.0f   /* the server's authoritative speed cap (the anti-cheat rule) */

/* client-side prediction: optimistic — apply the full requested dx locally */
static void predict_sim(void* s, const void* in, void* ud){
    (void)ud; ((RunState*)s)->x += ((const RunInput*)in)->dx;
}

/* server-side authoritative movement, reachable from the netcmd apply. It clamps. */
static CCComponentId POS;           /* authoritative position component */
static CCEntityId    g_srv_player;
static float* srv_x(CCScene* sc){
    return (float*)(cc_component_has(sc,g_srv_player,POS)?cc_component_get(sc,g_srv_player,POS)
                                                         :cc_component_add(sc,g_srv_player,POS));
}
static CCNetCmdResult validate_move(const CCNetCmdCtx* ctx){
    /* the seam accepts the command but the apply clamps — modelling a server that
     * corrects rather than rejects (both are valid anti-cheat responses). We accept
     * any well-formed move and enforce the cap in apply. */
    return ctx->size == sizeof(RunInput) ? CC_NETCMD_ACCEPT : CC_NETCMD_REJECT;
}
static void apply_move(const CCNetCmdCtx* ctx){
    const RunInput* in = (const RunInput*)ctx->data;
    float dx = in->dx;
    if (dx >  MAX_DX) dx =  MAX_DX;      /* clamp: authoritative speed cap */
    if (dx < -MAX_DX) dx = -MAX_DX;
    *srv_x(ctx->scene) += dx;
}

int main(void){
    CCEngineConfig cfg = cc_sandbox_config(); cfg.width=64; cfg.height=64; cfg.verbose=false;
    CCEngine* e = cc_init(&cfg);
    CCScene* server_scene = cc_scene_create(e, "server");

    POS = cc_component_register("Pos1D", sizeof(float));
    g_srv_player = cc_entity_create(server_scene);
    *srv_x(server_scene) = 0.0f;

    cc_netcmd_reset();
    CCNetCmdType MOVE = cc_netcmd_register("move", validate_move, apply_move, NULL);
    CCNetCmdServer* cmd_sv = cc_netcmd_server_create(server_scene);
    CCNetCmdClient* cmd_cl = cc_netcmd_client_create();
    const CCNetClientId CID = 7;

    CCNetPredict* pred = cc_predict_create(sizeof(RunState), sizeof(RunInput), predict_sim, NULL);
    RunState local = { 0.0f };

    uint8_t buf[128];

    /* ── round 1: a LEGAL move (dx = 1.0 == cap) — client and server agree ───── */
    RunInput legal = { 1.0f };
    /* client predicts locally (instant) + records for reconcile */
    uint32_t seq1 = cc_predict_apply(pred, &local, &legal);
    CHECK(fabsf(local.x - 1.0f) < 1e-6, "client predicted the legal move instantly");
    /* client sends the command; server validates + applies (clamp doesn't bite) */
    size_t n = cc_netcmd_client_encode(cmd_cl, MOVE, &legal, sizeof(legal), buf, sizeof(buf));
    cc_netcmd_server_ingest(cmd_sv, CID, buf, n);
    CHECK(fabsf(*srv_x(server_scene) - 1.0f) < 1e-6, "server applied the legal move");
    /* server reports authoritative x + the seq it has processed; client reconciles */
    uint32_t acked1 = cc_netcmd_server_last_seq(cmd_sv, CID);
    CHECK(acked1 == seq1, "server acked the client's input seq");
    RunState auth1 = { *srv_x(server_scene) };
    uint32_t replayed1 = cc_predict_reconcile(pred, &local, &auth1, acked1);
    CHECK(replayed1 == 0, "nothing in flight after ack");
    CHECK(fabsf(local.x - 1.0f) < 1e-6, "agree case: reconcile leaves client at 1.0 (no visible pop)");

    /* ── round 2: a SPEED HACK (dx = 5.0) — server clamps, client gets corrected ─ */
    RunInput hack = { 5.0f };
    uint32_t seq2 = cc_predict_apply(pred, &local, &hack);
    /* client optimistically predicts the full 5.0 → local x = 1 + 5 = 6.0 */
    CHECK(fabsf(local.x - 6.0f) < 1e-6, "client optimistically predicted the fast (illegal) move");
    n = cc_netcmd_client_encode(cmd_cl, MOVE, &hack, sizeof(hack), buf, sizeof(buf));
    cc_netcmd_server_ingest(cmd_sv, CID, buf, n);
    /* server clamped 5.0 → 1.0, so authoritative x = 1 + 1 = 2.0, NOT 6.0 */
    CHECK(fabsf(*srv_x(server_scene) - 2.0f) < 1e-6, "server clamped the speed hack to the cap");
    uint32_t acked2 = cc_netcmd_server_last_seq(cmd_sv, CID);
    CHECK(acked2 == seq2, "server acked the second input");
    RunState auth2 = { *srv_x(server_scene) };
    uint32_t replayed2 = cc_predict_reconcile(pred, &local, &auth2, acked2);
    CHECK(replayed2 == 0, "no in-flight inputs to replay");
    /* the mispredict is corrected: client snaps from its optimistic 6.0 to truth 2.0 */
    CHECK(fabsf(local.x - 2.0f) < 1e-6, "mispredict corrected: client pulled back to authoritative 2.0");

    /* ── round 3: prediction stays responsive WHILE inputs are in flight ─────── */
    /* fire two legal moves before any ack; both show locally immediately, and a
     * later reconcile that acks only the first still replays the second. */
    RunInput a = {1.0f}, b = {1.0f};
    uint32_t seqA = cc_predict_apply(pred, &local, &a);   /* local 2 → 3 */
    uint32_t seqB = cc_predict_apply(pred, &local, &b);   /* local 3 → 4 */
    (void)seqB;
    CHECK(fabsf(local.x - 4.0f) < 1e-6, "two in-flight moves both shown locally (responsive)");
    CHECK(cc_predict_pending(pred) == 2, "two inputs in flight");
    /* server applies only A so far (x: 2 → 3), acks seqA */
    n = cc_netcmd_client_encode(cmd_cl, MOVE, &a, sizeof(a), buf, sizeof(buf));
    cc_netcmd_server_ingest(cmd_sv, CID, buf, n);
    RunState auth3 = { *srv_x(server_scene) };   /* 3.0 */
    uint32_t replayed3 = cc_predict_reconcile(pred, &local, &auth3, seqA);
    CHECK(replayed3 == 1, "only input B replays (A was acked)");
    CHECK(fabsf(local.x - 4.0f) < 1e-6, "reconcile keeps client at 4.0: authoritative 3.0 + in-flight B");

    cc_predict_destroy(pred);
    cc_netcmd_client_destroy(cmd_cl);
    cc_netcmd_server_destroy(cmd_sv);
    cc_shutdown(e);

    if (failures == 0)
        printf("NET INTEGRATION TEST: all checks passed (predict + netcmd validate/clamp + reconcile: legal move agrees invisibly, speed-hack is clamped server-side and corrected on the client, prediction stays responsive with inputs in flight — full stack, no sockets)\n");
    else
        printf("NET INTEGRATION TEST: %d FAILURES\n", failures);
    return failures ? 1 : 0;
}
