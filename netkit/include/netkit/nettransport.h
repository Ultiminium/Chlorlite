#pragma once
/*
 * netkit/nettransport — a thin UDP transport. Part of the netkit LIBRARY (ships
 * alongside Chlorlite, not part of the engine — see netkit/netcmd.h for the scope
 * rationale). This is the "thin
 * plug-in" the net.h / netcmd.h headers promise: the replication core (net.h) and
 * the authority core (netcmd.h) are transport-FREE — they produce and consume byte
 * buffers. This layer just MOVES those buffers over UDP. It adds no game logic, no
 * authority, no policy: send a buffer, receive a buffer, know which peer it came
 * from. That is the whole job.
 *
 * WHY UDP: games want per-packet delivery without head-of-line blocking; snapshots
 * are idempotent (a dropped one is superseded by the next) and commands carry their
 * own sequence numbers (netcmd de-dupes/orders), so reliable-ordered TCP is the
 * wrong default. This layer is intentionally UNRELIABLE + connectionless, matching
 * how net.h/netcmd.h were designed (they already tolerate loss/reorder).
 *
 * WHAT CC STILL DOES NOT DO (per the locked design decisions): CC does not run or
 * host a server, does not match-make, and does not implement an anti-cheat. This is
 * plumbing you link into YOUR server process (which is CC compiled headless) and
 * YOUR client. The server process lifecycle is the developer's; this just gives it
 * a socket.
 *
 * HEADLESS-TESTABLE: both endpoints can bind to 127.0.0.1 in ONE process, so the
 * real send/recv path is exercised with no external network — a test binds a server
 * socket + a client socket to loopback, pumps actual datagrams between them, and
 * asserts the bytes arrive. (This is real socket I/O, not the in-proc buffer pass
 * the core tests use — it verifies the transport itself.)
 *
 *   // server (inside your headless CC server process)
 *   CCNetSocket* s = cc_net_socket_open_server(9000);
 *   // ...each net frame:
 *   uint8_t snap[4096];
 *   size_t n = cc_net_server_snapshot(sv, ack, snap, sizeof(snap));
 *   cc_net_socket_broadcast(s, snap, n);              // to all known peers
 *   CCNetPeer from; uint8_t in[2048];
 *   int len;
 *   while ((len = cc_net_socket_recv(s, in, sizeof(in), &from)) > 0)
 *       cc_netcmd_server_ingest(cmdsv, cc_net_peer_id(&from), in, len);
 *
 *   // client
 *   CCNetSocket* c = cc_net_socket_open_client("127.0.0.1", 9000);
 *   cc_net_socket_send(c, cmd_bytes, cmd_len);        // to the server
 *   int len = cc_net_socket_recv(c, in, sizeof(in), NULL);   // a snapshot
 *   if (len > 0) cc_net_client_apply(cl, in, len);
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque UDP endpoint. A "server" socket is bound to a port and tracks the peers
 * that have sent to it (so it can broadcast); a "client" socket has one fixed
 * destination (the server) it sends to. Both send + recv datagrams the same way. */
typedef struct CCNetSocket CCNetSocket;

/* Identifies a peer by its source address, opaquely. Compare with cc_net_peer_id
 * (a stable per-address hash usable as a CCNetClientId for cc_netcmd_server_ingest)
 * or cc_net_peer_equal. The raw address bytes are not exposed. */
typedef struct { uint8_t _addr[16]; uint32_t _len; } CCNetPeer;

/* ─── open / close ──────────────────────────────────────────────────────────── */
/* Bind a server socket to `port` on all interfaces. Non-blocking. Returns NULL on
 * failure (port in use, etc). */
CCNetSocket* cc_net_socket_open_server(uint16_t port);
/* Open a client socket whose fixed destination is host:port. `host` is a dotted
 * IPv4 string (e.g. "127.0.0.1"). Binds an ephemeral local port. Non-blocking.
 * Returns NULL on failure. */
CCNetSocket* cc_net_socket_open_client(const char* host, uint16_t port);
void         cc_net_socket_close(CCNetSocket* s);
/* The actual local port a socket is bound to (useful when a server was opened on
 * port 0 = "any free port", e.g. in tests). 0 on error. */
uint16_t     cc_net_socket_local_port(const CCNetSocket* s);

/* ─── send ──────────────────────────────────────────────────────────────────── */
/* Client: send a datagram to the fixed server destination. Server: send to the
 * peer it most recently received from (convenience; prefer send_to/broadcast on a
 * server). Returns bytes sent, or -1 on error. A single call = one datagram. */
int  cc_net_socket_send(CCNetSocket* s, const uint8_t* buf, size_t len);
/* Send a datagram to a specific peer (as returned by recv). Returns bytes sent
 * or -1. */
int  cc_net_socket_send_to(CCNetSocket* s, const CCNetPeer* peer,
                           const uint8_t* buf, size_t len);
/* Server: send a datagram to EVERY peer that has sent to this socket (the client
 * set the transport has learned). Returns the number of peers it was sent to. */
int  cc_net_socket_broadcast(CCNetSocket* s, const uint8_t* buf, size_t len);

/* ─── recv ──────────────────────────────────────────────────────────────────── */
/* Receive one datagram (non-blocking). Writes up to `cap` bytes into buf and, if
 * `out_peer` is non-NULL, the sender's address. On a server socket, the sender is
 * also remembered for broadcast + send. Returns bytes received (>0), 0 if no
 * datagram is waiting, or -1 on error. Call in a loop to drain the socket. */
int  cc_net_socket_recv(CCNetSocket* s, uint8_t* buf, size_t cap, CCNetPeer* out_peer);

/* ─── peer identity ─────────────────────────────────────────────────────────── */
/* A stable non-zero id derived from a peer's address:port — usable directly as the
 * CCNetClientId argument to cc_netcmd_server_ingest so the validation seam's per-
 * client accounting works. Same peer → same id for the session. */
uint32_t cc_net_peer_id(const CCNetPeer* peer);
bool     cc_net_peer_equal(const CCNetPeer* a, const CCNetPeer* b);
/* Number of distinct peers a server socket has learned (has received from). */
uint32_t cc_net_socket_peer_count(const CCNetSocket* s);

#ifdef __cplusplus
}
#endif
