/* net_test — proves replication works with NO real network: a server and
 * client(s) live in one process; snapshots are serialized to byte buffers and
 * handed to the client, and we assert the client's scene converges to the
 * server's. Covers full snapshot, delta (only-changed), spawn, despawn, position
 * sync, and a second client. Prints "NET TEST: all checks passed" / returns 0. */
#include "cc/claudecore.h"
#include "cc/net.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* a simple replicated component: 3D position */
typedef struct { float x, y, z; } NetPos;
static CCComponentId POS;

static size_t write_pos(const void* comp, uint8_t* out, size_t cap, void* ud){
    (void)ud; if (cap < sizeof(NetPos)) return 0;
    memcpy(out, comp, sizeof(NetPos)); return sizeof(NetPos);
}
static void read_pos(void* comp, const uint8_t* in, size_t len, void* ud){
    (void)ud; if (len >= sizeof(NetPos)) memcpy(comp, in, sizeof(NetPos));
}

static NetPos* pos_of(CCScene* s, CCEntityId e){
    return (NetPos*)(cc_component_has(s,e,POS)?cc_component_get(s,e,POS):cc_component_add(s,e,POS));
}

int main(void) {
    CCEngineConfig cfg = cc_sandbox_config(); cfg.width=64; cfg.height=64; cfg.verbose=false;
    CCEngine* e = cc_init(&cfg);
    CCScene* server_scene = cc_scene_create(e, "server");
    CCScene* client_scene = cc_scene_create(e, "client");

    POS = cc_component_register("NetPos", sizeof(NetPos));
    cc_net_reset();
    CCNetChannel ch = cc_net_replicate(POS, sizeof(NetPos), write_pos, read_pos, NULL);
    CHECK(ch != 0xFFFF, "position channel registered");
    CHECK(cc_net_channel_count()==1, "one replicated channel");

    CCNetServer* sv = cc_net_server_create(server_scene);
    CCNetClient* cl = cc_net_client_create(client_scene);

    /* ── spawn two entities on the server, give them positions ──────────── */
    CCEntityId a = cc_entity_create(server_scene);
    CCEntityId b = cc_entity_create(server_scene);
    *pos_of(server_scene,a) = (NetPos){1,2,3};
    *pos_of(server_scene,b) = (NetPos){4,5,6};
    CCNetId na = cc_net_server_spawn(sv, a);
    CCNetId nb = cc_net_server_spawn(sv, b);
    CHECK(na && nb && na!=nb, "two entities got distinct net ids");
    CHECK(cc_net_server_entity_count(sv)==2, "server replicating 2");

    /* ── FULL snapshot (baseline 0) → client applies ────────────────────── */
    uint8_t buf[4096];
    size_t n = cc_net_server_snapshot(sv, 0, buf, sizeof(buf));
    CHECK(n > 0, "full snapshot serialized");
    uint32_t ack = cc_net_client_apply(cl, buf, n);
    CHECK(ack == cc_net_server_tick(sv), "client acked server tick");
    CHECK(cc_net_client_entity_count(cl)==2, "client spawned 2 entities");

    /* positions converged */
    CCEntityId la = cc_net_client_entity(cl, na);
    CCEntityId lb = cc_net_client_entity(cl, nb);
    CHECK(la!=CC_ENTITY_NULL && lb!=CC_ENTITY_NULL, "client mapped both net ids");
    NetPos* pa = pos_of(client_scene, la);
    NetPos* pb = pos_of(client_scene, lb);
    CHECK(fabsf(pa->x-1)<1e-6 && fabsf(pa->y-2)<1e-6 && fabsf(pa->z-3)<1e-6, "entity A pos synced");
    CHECK(fabsf(pb->x-4)<1e-6 && fabsf(pb->z-6)<1e-6, "entity B pos synced");

    /* ── DELTA: move only A; snapshot should carry A, not unchanged B ────── */
    cc_net_server_advance(sv);
    pos_of(server_scene,a)->x = 99.0f;
    size_t n2 = cc_net_server_snapshot(sv, ack, buf, sizeof(buf));
    CHECK(n2 > 0 && n2 < n, "delta snapshot smaller than full");
    cc_net_client_apply(cl, buf, n2);
    CHECK(fabsf(pos_of(client_scene,la)->x-99.0f)<1e-6, "delta moved A on client");
    CHECK(fabsf(pos_of(client_scene,lb)->x-4.0f)<1e-6, "B unchanged after delta");

    /* ── a delta with NO changes should be tiny (header only) ───────────── */
    cc_net_server_advance(sv);
    size_t n3 = cc_net_server_snapshot(sv, cc_net_client_last_ack(cl), buf, sizeof(buf));
    CHECK(n3 > 0 && n3 <= 12, "empty delta is just a header");

    /* ── despawn B → client removes it ──────────────────────────────────── */
    cc_net_server_advance(sv);
    cc_net_server_despawn(sv, nb);
    size_t n4 = cc_net_server_snapshot(sv, cc_net_client_last_ack(cl), buf, sizeof(buf));
    cc_net_client_apply(cl, buf, n4);
    CHECK(cc_net_client_entity_count(cl)==1, "client removed despawned B");
    CHECK(cc_net_server_entity_count(sv)==1, "server down to 1");
    CHECK(cc_net_client_entity(cl, nb)==CC_ENTITY_NULL, "B net id no longer mapped");

    /* ── a fresh SECOND client gets a full snapshot and converges ───────── */
    CCScene* client2 = cc_scene_create(e, "client2");
    CCNetClient* cl2 = cc_net_client_create(client2);
    size_t nf = cc_net_server_snapshot(sv, 0, buf, sizeof(buf));   /* full for newcomer */
    cc_net_client_apply(cl2, buf, nf);
    CHECK(cc_net_client_entity_count(cl2)==1, "second client sees the 1 live entity");
    CCEntityId la2 = cc_net_client_entity(cl2, na);
    CHECK(la2!=CC_ENTITY_NULL && fabsf(pos_of(client2,la2)->x-99.0f)<1e-6,
          "second client got A's current position");

    cc_net_client_destroy(cl2);
    cc_net_client_destroy(cl);
    cc_net_server_destroy(sv);
    cc_net_reset();
    cc_scene_destroy(client2);
    cc_scene_destroy(client_scene);
    cc_scene_destroy(server_scene);
    cc_shutdown(e);

    if (failures == 0) {
        printf("NET TEST: all checks passed (full+delta snapshots, spawn/despawn, position "
               "replication, empty-delta, late-join second client — all with no real socket)\n");
        return 0;
    }
    printf("NET TEST: %d check(s) FAILED\n", failures);
    return 1;
}
