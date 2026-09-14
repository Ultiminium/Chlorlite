#pragma once
/*
 * CCNet — snapshot REPLICATION. This is the engine's ONE networking
 * responsibility: it belongs in the engine because it reaches into the ECS
 * (creating entities, reading/writing components) and only the engine owns that
 * data. It is the SEAM that makes a game networkable — not a whole netcode stack.
 * The rest of networking (command validation, UDP transport, client prediction)
 * is genre-specific game assembly and lives in the separate `netkit` LIBRARY that
 * ships alongside the engine (see netkit/README.md), which builds ON this header.
 *
 * Built the CC way: the REPLICATION model (what state syncs, how it serializes,
 * how snapshots apply) is separate from the TRANSPORT (sockets), so the whole
 * thing is headless-testable without a network — you can drive a server and
 * clients in one process, pump packets between them as byte buffers, and assert
 * the state converged. A real UDP transport (netkit/nettransport.h) is a thin
 * plug-in under the same byte-buffer interface.
 *
 * Model: one authoritative SERVER owns the truth. CLIENTS send inputs and receive
 * SNAPSHOTS (full or delta) of the replicated entities. Each replicated entity has
 * a stable network id; each registered replicated COMPONENT is serialized by a
 * user-supplied writer/reader (mechanism, not a fixed "networked transform").
 *
 *   // shared: register what replicates
 *   CCNetChannel tf = cc_net_replicate(TRANSFORM_COMP, sizeof(Transform),
 *                                      write_tf, read_tf, NULL);
 *   // server
 *   CCNetServer* sv = cc_net_server_create();
 *   cc_net_server_spawn(sv, entity, ...);       // start replicating an entity
 *   size_t n = cc_net_server_snapshot(sv, buf, cap);  // serialize a snapshot
 *   // client
 *   CCNetClient* cl = cc_net_client_create(scene);
 *   cc_net_client_apply(cl, buf, n);            // apply snapshot → local entities
 *
 * Delta encoding: snapshots are diffed against the last acked baseline so only
 * changed components go on the wire. Clients ack; the server bases the next delta
 * on the ack. Determinism: given the same packets, clients always converge to the
 * server state.
 */
#include "cc/ecs.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* stable per-entity network id (server-assigned, unique per session) */
typedef uint32_t CCNetId;
#define CC_NET_NULL ((CCNetId)0)

/* a replicated component channel */
typedef uint16_t CCNetChannel;

/* user serializers for a replicated component: write packs `comp` into `out`
 * (return bytes written, <=cap); read unpacks `in` into `comp`. */
typedef size_t (*CCNetWriteFn)(const void* comp, uint8_t* out, size_t cap, void* ud);
typedef void   (*CCNetReadFn)(void* comp, const uint8_t* in, size_t len, void* ud);

/* Register a component for replication. `comp_id` is its ECS component id,
 * `size` its byte size. Returns a channel handle (also identifies it on the
 * wire). Call identically on server and all clients (same registration order). */
CCNetChannel cc_net_replicate(CCComponentId comp_id, uint32_t size,
                              CCNetWriteFn write, CCNetReadFn read, void* ud);
/* Reset the replication registry (tests / new session). */
void cc_net_reset(void);
uint32_t cc_net_channel_count(void);

/* ─── server ──────────────────────────────────────────────────────────────── */
typedef struct CCNetServer CCNetServer;
CCNetServer* cc_net_server_create(CCScene* scene);
void         cc_net_server_destroy(CCNetServer* sv);
/* Begin replicating an entity; returns its network id. Its registered replicated
 * components will be included in snapshots. */
CCNetId      cc_net_server_spawn(CCNetServer* sv, CCEntityId e);
/* Stop replicating (and tell clients to despawn) an entity. */
void         cc_net_server_despawn(CCNetServer* sv, CCNetId id);
/* Serialize a snapshot into buf. If `baseline_ack` names a prior snapshot the
 * client has, a DELTA is written (only changed components + spawns/despawns);
 * otherwise a FULL snapshot. Returns bytes written (0 if it didn't fit). */
size_t       cc_net_server_snapshot(CCNetServer* sv, uint32_t baseline_ack,
                                    uint8_t* buf, size_t cap);
/* The tick/sequence number of the most recent snapshot (clients ack this). */
uint32_t     cc_net_server_tick(const CCNetServer* sv);
/* Advance the server's snapshot tick (call once per network frame). */
void         cc_net_server_advance(CCNetServer* sv);
uint32_t     cc_net_server_entity_count(const CCNetServer* sv);

/* ─── client ──────────────────────────────────────────────────────────────── */
typedef struct CCNetClient CCNetClient;
CCNetClient* cc_net_client_create(CCScene* scene);
void         cc_net_client_destroy(CCNetClient* cl);
/* Apply a snapshot: spawns/despawns net entities in the client's scene and writes
 * their replicated components. Returns the snapshot tick applied (for acking). */
uint32_t     cc_net_client_apply(CCNetClient* cl, const uint8_t* buf, size_t len);
/* The last tick this client applied (send back as baseline_ack). */
uint32_t     cc_net_client_last_ack(const CCNetClient* cl);
/* Map a network id → the local entity the client spawned for it (or CC_ENTITY_NULL). */
CCEntityId   cc_net_client_entity(CCNetClient* cl, CCNetId id);
uint32_t     cc_net_client_entity_count(const CCNetClient* cl);

#ifdef __cplusplus
}
#endif
