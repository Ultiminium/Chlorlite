#pragma once
/*
 * netkit/netcmd — the AUTHORITY half of game networking: a client→server COMMAND
 * channel plus a server-side VALIDATION seam.
 *
 * SCOPE: netkit is a LIBRARY that ships alongside Chlorlite but is NOT part of the
 * engine. The engine owns exactly one networking thing — cc/net.h replication —
 * because that reaches into the ECS (only the engine owns entities + components).
 * Everything else about networking (how commands are validated, how bytes cross a
 * socket, how a client predicts) is something a GAME assembles on top of the
 * engine's primitives, and it differs wildly by genre. So it lives here, in netkit,
 * not in cc/. Games opt in by including <netkit/...>; games that don't need
 * networking never touch it. This module builds ON cc/net.h (it uses CCScene /
 * CCComponentId), never the other way around.
 *
 * cc/net.h gives the server the power to BROADCAST truth (snapshots flow
 * server→client); this gives clients a way to REQUEST changes and gives the server
 * the single, enforced point at which those
 * requests are accepted or rejected. Together they make an authoritative design
 * not just POSSIBLE but STRUCTURAL: a client can only ever send a REQUEST, and
 * nothing mutates authoritative state except through an accepted apply.
 *
 * This is the seam anti-cheat lives in. CC deliberately ships the MECHANISM (the
 * enforcement point, command sequencing, reject accounting) and NOT the POLICY
 * (whether a given move is legal) — the game supplies the yes/no rule. The
 * architectural 80% of anti-cheat (an authoritative server rejecting illegal
 * requests) comes free from using this correctly; CC does not and will not claim
 * to stop wallhacks or aimbots (see cc/net.h notes).
 *
 * Like the rest of CCNet this is TRANSPORT-FREE and fully headless-testable: the
 * client serializes a command into a byte buffer, a test (or a real socket layer)
 * moves the bytes, and the server ingests them — all in one process, no sockets.
 *
 *   // shared: register a command TYPE + its server-side rule (mechanism, not policy)
 *   CCNetCmdType MOVE = cc_netcmd_register("move", validate_move, apply_move, ud);
 *
 *   // client: build a request and serialize it (does NOT touch authoritative state)
 *   MoveReq req = { .dx = 1.0f };
 *   uint8_t buf[64];
 *   size_t n = cc_netcmd_client_encode(cl, MOVE, &req, sizeof(req), buf, sizeof(buf));
 *   // ...bytes travel to the server...
 *
 *   // server: ingest. The engine decodes + calls validate; ONLY if it returns
 *   // CC_NETCMD_ACCEPT does it call apply. A rejected command never mutates state.
 *   cc_netcmd_server_ingest(sv, client_id, buf, n);
 *
 * The validate callback is the enforcement point. It receives the requesting
 * client, the decoded command, and the server scene; it returns accept/reject.
 * Reject reasons are counted per-client so a game can detect abuse.
 */
#include "cc/net.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a registered command type (index into the shared command registry) */
typedef uint16_t CCNetCmdType;
#define CC_NETCMD_NULL ((CCNetCmdType)0xFFFF)

/* identifies which client a command came from (game-assigned, e.g. connection id) */
typedef uint32_t CCNetClientId;
#define CC_NETCLIENT_NULL ((CCNetClientId)0)

/* validate result: the game's yes/no on a requested command. */
typedef enum {
    CC_NETCMD_ACCEPT = 0,   /* legal — the engine will call apply */
    CC_NETCMD_REJECT = 1    /* illegal — nothing is applied, reject counted */
} CCNetCmdResult;

/* Context handed to validate/apply for one ingested command. */
typedef struct {
    CCScene*       scene;    /* the authoritative server scene */
    CCNetClientId  client;   /* which client sent this command */
    CCNetCmdType   type;     /* the command type */
    const void*    data;     /* decoded command payload (read-only) */
    size_t         size;     /* payload size in bytes */
    uint32_t       seq;      /* per-client sequence number of this command */
    void*          ud;       /* the userdata registered with the type */
} CCNetCmdCtx;

/* The VALIDATION SEAM: the game's rule for whether a command is legal. Runs on the
 * server before any state change. Return CC_NETCMD_ACCEPT to allow, else REJECT.
 * This is the enforcement point — the engine guarantees apply runs ONLY on accept. */
typedef CCNetCmdResult (*CCNetCmdValidateFn)(const CCNetCmdCtx* ctx);
/* Applies an ACCEPTED command to authoritative state. Never called for rejects. */
typedef void           (*CCNetCmdApplyFn)(const CCNetCmdCtx* ctx);

/* ─── shared registry (same registration order on server + clients) ─────────── */
/* Register a command type with its server-side rule. `validate` may be NULL,
 * which means "accept everything" (an OPEN command — use only when the request
 * genuinely carries no authority, e.g. a chat message). `apply` may be NULL if
 * the command has no direct state effect. Returns the type handle (also its wire
 * id). Call identically on the server and all clients. */
CCNetCmdType cc_netcmd_register(const char* name,
                                CCNetCmdValidateFn validate,
                                CCNetCmdApplyFn apply, void* ud);
/* Reset the command registry (tests / new session). Independent of cc_net_reset. */
void         cc_netcmd_reset(void);
uint32_t     cc_netcmd_type_count(void);
const char*  cc_netcmd_type_name(CCNetCmdType t);

/* ─── client side: build + serialize a request ──────────────────────────────── */
typedef struct CCNetCmdClient CCNetCmdClient;
CCNetCmdClient* cc_netcmd_client_create(void);
void            cc_netcmd_client_destroy(CCNetCmdClient* c);
/* Encode a command of `type` carrying `data` (size bytes) into `out`. Stamps a
 * monotonically increasing per-client sequence number so the server can order and
 * de-duplicate. Returns bytes written (0 if it didn't fit or the type is bad).
 * This is the ONLY way a client affects the server — it produces a REQUEST, it
 * does not and cannot mutate authoritative state. */
size_t          cc_netcmd_client_encode(CCNetCmdClient* c, CCNetCmdType type,
                                        const void* data, size_t size,
                                        uint8_t* out, size_t cap);
/* The sequence number the client will stamp on its NEXT encoded command. */
uint32_t        cc_netcmd_client_next_seq(const CCNetCmdClient* c);

/* ─── server side: ingest + enforce ─────────────────────────────────────────── */
typedef struct CCNetCmdServer CCNetCmdServer;
/* The server command processor is bound to the authoritative scene it mutates. */
CCNetCmdServer* cc_netcmd_server_create(CCScene* scene);
void            cc_netcmd_server_destroy(CCNetCmdServer* s);

/* Ingest one or more encoded commands from `client`. For each: decode → look up
 * the type's rule → call validate → on ACCEPT call apply, on REJECT count it and
 * skip. Out-of-order / already-seen sequence numbers for a client are dropped as
 * stale (last-seen seq is tracked per client). Returns the number of commands
 * ACCEPTED. A malformed buffer stops parsing safely.
 *
 * A single buffer may contain multiple commands back-to-back (the encoder writes
 * one per call, but a transport may coalesce). */
uint32_t        cc_netcmd_server_ingest(CCNetCmdServer* s, CCNetClientId client,
                                        const uint8_t* buf, size_t len);

/* Per-client accounting, so a game can spot a client sending many illegal
 * requests (a strong cheat signal) and disconnect / flag it. */
uint32_t cc_netcmd_server_accepted(const CCNetCmdServer* s, CCNetClientId client);
uint32_t cc_netcmd_server_rejected(const CCNetCmdServer* s, CCNetClientId client);
/* Last sequence number accepted from a client (for input-ack / prediction). */
uint32_t cc_netcmd_server_last_seq(const CCNetCmdServer* s, CCNetClientId client);
/* Forget a client's accounting (on disconnect). */
void     cc_netcmd_server_forget(CCNetCmdServer* s, CCNetClientId client);

#ifdef __cplusplus
}
#endif
