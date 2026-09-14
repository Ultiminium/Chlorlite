/* nettransport_test — proves the UDP transport by moving REAL datagrams over the
 * loopback interface (127.0.0.1) between a server socket and a client socket bound
 * in ONE process. Unlike the net/netcmd core tests (which pass byte buffers in
 * memory), this exercises the actual send/recv path — so it verifies the transport
 * itself, and that it correctly carries the tested cores end-to-end:
 *
 *   client --UDP--> [move command] --> server: validate (seam) --> apply
 *   server --UDP--> [snapshot]     --> client: apply --> scene converges
 *
 * Asserts: sockets bind; a command datagram arrives and is ingested through the
 * validation seam (legal applied, illegal rejected); the server learns the peer +
 * derives a stable client id; a snapshot datagram travels back and the client
 * scene converges; broadcast reaches the peer; peer id/equality work. Because UDP
 * is unreliable even on loopback in principle, recv is polled with a short spin.
 * Prints "NETTRANSPORT TEST: all checks passed" / returns 0. */
#include "cc/claudecore.h"
#include "cc/net.h"
#include "netkit/netcmd.h"
#include "netkit/nettransport.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* replicated + authoritative state: a position the server owns */
typedef struct { float x, y, z; } NetPos;
static CCComponentId POS;
static CCEntityId    g_player;
static NetPos* pos_of(CCScene* s, CCEntityId e){
    return (NetPos*)(cc_component_has(s,e,POS)?cc_component_get(s,e,POS):cc_component_add(s,e,POS));
}
static size_t write_pos(const void* comp, uint8_t* out, size_t cap, void* ud){
    (void)ud; if (cap < sizeof(NetPos)) return 0; memcpy(out, comp, sizeof(NetPos)); return sizeof(NetPos);
}
static void read_pos(void* comp, const uint8_t* in, size_t len, void* ud){
    (void)ud; if (len >= sizeof(NetPos)) memcpy(comp, in, sizeof(NetPos));
}

/* command: a small move request, validated server-side */
typedef struct { float dx, dy, dz; } MoveReq;
#define MAX_STEP 2.0f
static CCNetCmdResult validate_move(const CCNetCmdCtx* ctx){
    if (ctx->size != sizeof(MoveReq)) return CC_NETCMD_REJECT;
    const MoveReq* m = (const MoveReq*)ctx->data;
    if (m->dx*m->dx + m->dy*m->dy + m->dz*m->dz > MAX_STEP*MAX_STEP) return CC_NETCMD_REJECT;
    return CC_NETCMD_ACCEPT;
}
static void apply_move(const CCNetCmdCtx* ctx){
    const MoveReq* m = (const MoveReq*)ctx->data;
    NetPos* p = pos_of(ctx->scene, g_player);
    p->x += m->dx; p->y += m->dy; p->z += m->dz;
}

/* poll a socket for one datagram, spinning briefly (UDP loopback is effectively
 * immediate, but never assume). Returns bytes received or 0 if none within budget. */
static int recv_spin(CCNetSocket* s, uint8_t* buf, size_t cap, CCNetPeer* peer){
    for (int tries=0; tries<10000; tries++){
        int n = cc_net_socket_recv(s, buf, cap, peer);
        if (n > 0) return n;
        if (n < 0) return -1;
        struct timespec ts = {0, 100000}; /* 0.1ms */ nanosleep(&ts, NULL);
    }
    return 0;
}

int main(void){
    CCEngineConfig cfg = cc_sandbox_config(); cfg.width=64; cfg.height=64; cfg.verbose=false;
    CCEngine* e = cc_init(&cfg);
    CCScene* server_scene = cc_scene_create(e, "server");
    CCScene* client_scene = cc_scene_create(e, "client");

    POS = cc_component_register("NetPos", sizeof(NetPos));
    cc_net_reset();
    cc_net_replicate(POS, sizeof(NetPos), write_pos, read_pos, NULL);
    cc_netcmd_reset();
    CCNetCmdType MOVE = cc_netcmd_register("move", validate_move, apply_move, NULL);

    /* authoritative player entity, replicated */
    g_player = cc_entity_create(server_scene);
    *pos_of(server_scene, g_player) = (NetPos){0,0,0};
    CCNetServer*    rep_sv = cc_net_server_create(server_scene);
    CCNetClient*    rep_cl = cc_net_client_create(client_scene);
    CCNetCmdServer* cmd_sv = cc_netcmd_server_create(server_scene);
    CCNetCmdClient* cmd_cl = cc_netcmd_client_create();
    CCNetId net_player = cc_net_server_spawn(rep_sv, g_player);

    /* ── open real UDP sockets on loopback (server on any free port) ────────── */
    CCNetSocket* sv_sock = cc_net_socket_open_server(0);
    CHECK(sv_sock != NULL, "server socket bound");
    uint16_t port = cc_net_socket_local_port(sv_sock);
    CHECK(port != 0, "server got a local port");
    CCNetSocket* cl_sock = cc_net_socket_open_client("127.0.0.1", port);
    CHECK(cl_sock != NULL, "client socket opened to server");

    uint8_t buf[4096]; CCNetPeer from;

    /* ── 1. client sends a LEGAL move command OVER UDP → server validates+applies ── */
    MoveReq legal = { 1.0f, 0, 0 };
    size_t cn = cc_netcmd_client_encode(cmd_cl, MOVE, &legal, sizeof(legal), buf, sizeof(buf));
    CHECK(cn > 0, "legal command encoded");
    int sent = cc_net_socket_send(cl_sock, buf, cn);
    CHECK(sent == (int)cn, "command datagram sent over UDP");

    int got = recv_spin(sv_sock, buf, sizeof(buf), &from);
    CHECK(got == (int)cn, "server received the command datagram");
    CHECK(cc_net_socket_peer_count(sv_sock) == 1, "server learned exactly one peer");
    CCNetClientId cid = cc_net_peer_id(&from);
    CHECK(cid != 0, "stable non-zero client id derived from peer");

    uint32_t acc = cc_netcmd_server_ingest(cmd_sv, cid, buf, got);
    CHECK(acc == 1, "server accepted the move through the validation seam");
    CHECK(fabsf(pos_of(server_scene,g_player)->x - 1.0f) < 1e-6, "authoritative pos moved to 1.0");

    /* ── 2. server sends a SNAPSHOT back OVER UDP → client applies, scene converges ── */
    size_t sn = cc_net_server_snapshot(rep_sv, 0, buf, sizeof(buf));
    CHECK(sn > 0, "snapshot serialized");
    int bsent = cc_net_socket_broadcast(sv_sock, buf, sn);   /* to the learned peer */
    CHECK(bsent == 1, "snapshot broadcast reached the one peer");

    int snap_got = recv_spin(cl_sock, buf, sizeof(buf), NULL);
    CHECK(snap_got == (int)sn, "client received the snapshot datagram");
    cc_net_client_apply(rep_cl, buf, snap_got);
    CCEntityId local = cc_net_client_entity(rep_cl, net_player);
    CHECK(local != CC_ENTITY_NULL, "client mapped the net player");
    CHECK(fabsf(pos_of(client_scene, local)->x - 1.0f) < 1e-6, "client scene converged to server truth over UDP");

    /* ── 3. an ILLEGAL command over UDP is rejected; authoritative state holds ── */
    float x_before = pos_of(server_scene,g_player)->x;
    MoveReq cheat = { 500.0f, 0, 0 };
    cn = cc_netcmd_client_encode(cmd_cl, MOVE, &cheat, sizeof(cheat), buf, sizeof(buf));
    cc_net_socket_send(cl_sock, buf, cn);
    got = recv_spin(sv_sock, buf, sizeof(buf), &from);
    CHECK(got == (int)cn, "server received the illegal command datagram");
    acc = cc_netcmd_server_ingest(cmd_sv, cc_net_peer_id(&from), buf, got);
    CHECK(acc == 0, "illegal command rejected at the seam");
    CHECK(fabsf(pos_of(server_scene,g_player)->x - x_before) < 1e-6, "rejected command did not move authoritative state");

    /* ── 4. peer identity: same peer → same id, equality, no phantom peers ───── */
    CCNetPeer p2 = from;
    CHECK(cc_net_peer_equal(&from, &p2), "peer equals itself");
    CHECK(cc_net_peer_id(&from) == cc_net_peer_id(&p2), "same peer → same id");
    CHECK(cc_net_socket_peer_count(sv_sock) == 1, "still exactly one peer after more traffic");

    /* ── 5. recv on an idle socket returns 0 (non-blocking), not -1 ─────────── */
    int idle = cc_net_socket_recv(sv_sock, buf, sizeof(buf), NULL);
    CHECK(idle == 0, "non-blocking recv on idle socket returns 0");

    /* ── 6. NULL / error safety ─────────────────────────────────────────────── */
    CHECK(cc_net_socket_open_client(NULL, port) == NULL, "NULL host rejected");
    CHECK(cc_net_socket_send(NULL, buf, 4) == -1, "NULL socket send safe");
    CHECK(cc_net_socket_recv(NULL, buf, sizeof(buf), NULL) == -1, "NULL socket recv safe");
    CHECK(cc_net_peer_id(NULL) == 0, "NULL peer id is 0");

    cc_net_socket_close(sv_sock);
    cc_net_socket_close(cl_sock);
    cc_netcmd_client_destroy(cmd_cl);
    cc_netcmd_server_destroy(cmd_sv);
    cc_net_server_destroy(rep_sv);
    cc_net_client_destroy(rep_cl);

    cc_shutdown(e);   /* frees engine-owned scenes + their ECS archetypes */

    if (failures == 0)
        printf("NETTRANSPORT TEST: all checks passed (real loopback UDP: command datagram validated+applied, illegal rejected, snapshot datagram converged the client scene, peer learning + stable ids, non-blocking recv — full net stack end-to-end over an actual socket)\n");
    else
        printf("NETTRANSPORT TEST: %d FAILURES\n", failures);
    return failures ? 1 : 0;
}
