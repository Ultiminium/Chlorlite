/* nettransport.c — CCNetTransport: thin UDP plug-in under CCNet. See
 * cc/nettransport.h. This layer only moves byte buffers; all game logic,
 * authority, and serialization live in net.c / netcmd.c. UDP, non-blocking,
 * connectionless. Loopback-testable in one process.
 *
 * Platform: POSIX sockets on Linux; Winsock2 on Windows (the two shipping
 * targets). No other platforms (per the locked design decisions — Win+Linux only).
 */
#include "netkit/nettransport.h"
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef int socklen_t;
  #define CC_INVALID_SOCK INVALID_SOCKET
  typedef SOCKET cc_sock_t;
  static int cc_close_sock(cc_sock_t s){ return closesocket(s); }
  static int cc_set_nonblock(cc_sock_t s){ u_long m=1; return ioctlsocket(s,FIONBIO,&m); }
  static bool cc_would_block(void){ return WSAGetLastError()==WSAEWOULDBLOCK; }
  /* Winsock needs process-wide init; do it once, lazily. */
  static bool g_wsa_up = false;
  static void cc_net_platform_init(void){
      if (!g_wsa_up){ WSADATA d; if (WSAStartup(MAKEWORD(2,2),&d)==0) g_wsa_up=true; }
  }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <unistd.h>
  #include <errno.h>
  typedef int cc_sock_t;
  #define CC_INVALID_SOCK (-1)
  static int cc_close_sock(cc_sock_t s){ return close(s); }
  static int cc_set_nonblock(cc_sock_t s){
      int f = fcntl(s, F_GETFL, 0); if (f<0) return -1;
      return fcntl(s, F_SETFL, f | O_NONBLOCK);
  }
  static bool cc_would_block(void){ return errno==EAGAIN || errno==EWOULDBLOCK; }
  static void cc_net_platform_init(void){}
#endif

/* learned peers on a server socket (for broadcast) */
#define CC_MAX_PEERS 256
typedef struct { struct sockaddr_in addr; bool used; } PeerSlot;

struct CCNetSocket {
    cc_sock_t          fd;
    bool               is_server;
    struct sockaddr_in dest;        /* client: fixed server addr; server: last sender */
    bool               have_dest;
    PeerSlot           peers[CC_MAX_PEERS];
    uint32_t           peer_count;
};

/* pack a sockaddr_in into the opaque CCNetPeer (addr bytes + len) */
static void peer_from_addr(CCNetPeer* p, const struct sockaddr_in* a){
    memset(p, 0, sizeof(*p));
    uint32_t n = sizeof(*a);
    if (n > sizeof(p->_addr)) n = sizeof(p->_addr);
    memcpy(p->_addr, a, n);
    p->_len = n;
}
static void addr_from_peer(struct sockaddr_in* a, const CCNetPeer* p){
    memset(a, 0, sizeof(*a));
    uint32_t n = p->_len; if (n > sizeof(*a)) n = sizeof(*a);
    memcpy(a, p->_addr, n);
}

/* remember a sender on a server socket so we can broadcast to it later */
static void remember_peer(CCNetSocket* s, const struct sockaddr_in* a){
    for (uint32_t i=0;i<CC_MAX_PEERS;i++){
        if (s->peers[i].used &&
            s->peers[i].addr.sin_addr.s_addr==a->sin_addr.s_addr &&
            s->peers[i].addr.sin_port==a->sin_port)
            return;  /* already known */
    }
    for (uint32_t i=0;i<CC_MAX_PEERS;i++){
        if (!s->peers[i].used){
            s->peers[i].addr = *a; s->peers[i].used = true; s->peer_count++;
            return;
        }
    }
    /* peer table full — silently drop the new peer from the broadcast set (recv
     * still works; only broadcast reach is capped). */
}

static CCNetSocket* alloc_sock(void){
    CCNetSocket* s = calloc(1, sizeof(CCNetSocket));
    s->fd = CC_INVALID_SOCK;
    return s;
}

CCNetSocket* cc_net_socket_open_server(uint16_t port){
    cc_net_platform_init();
    CCNetSocket* s = alloc_sock();
    s->is_server = true;
    s->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->fd == CC_INVALID_SOCK){ free(s); return NULL; }
    struct sockaddr_in a; memset(&a,0,sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(port);
    if (bind(s->fd, (struct sockaddr*)&a, sizeof(a)) != 0){ cc_close_sock(s->fd); free(s); return NULL; }
    if (cc_set_nonblock(s->fd) != 0){ cc_close_sock(s->fd); free(s); return NULL; }
    return s;
}

CCNetSocket* cc_net_socket_open_client(const char* host, uint16_t port){
    cc_net_platform_init();
    if (!host) return NULL;
    CCNetSocket* s = alloc_sock();
    s->is_server = false;
    s->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->fd == CC_INVALID_SOCK){ free(s); return NULL; }
    if (cc_set_nonblock(s->fd) != 0){ cc_close_sock(s->fd); free(s); return NULL; }
    memset(&s->dest, 0, sizeof(s->dest));
    s->dest.sin_family = AF_INET;
    s->dest.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &s->dest.sin_addr) != 1){ cc_close_sock(s->fd); free(s); return NULL; }
    s->have_dest = true;
    return s;
}

void cc_net_socket_close(CCNetSocket* s){
    if (!s) return;
    if (s->fd != CC_INVALID_SOCK) cc_close_sock(s->fd);
    free(s);
}

uint16_t cc_net_socket_local_port(const CCNetSocket* s){
    if (!s || s->fd == CC_INVALID_SOCK) return 0;
    struct sockaddr_in a; socklen_t l = sizeof(a);
    if (getsockname(s->fd, (struct sockaddr*)&a, &l) != 0) return 0;
    return ntohs(a.sin_port);
}

int cc_net_socket_send(CCNetSocket* s, const uint8_t* buf, size_t len){
    if (!s || s->fd == CC_INVALID_SOCK || !buf) return -1;
    if (!s->have_dest) return -1;   /* server with no known peer yet, or unset */
    int n = (int)sendto(s->fd, (const char*)buf, (int)len, 0,
                        (struct sockaddr*)&s->dest, sizeof(s->dest));
    return n;
}

int cc_net_socket_send_to(CCNetSocket* s, const CCNetPeer* peer,
                          const uint8_t* buf, size_t len){
    if (!s || s->fd == CC_INVALID_SOCK || !peer || !buf) return -1;
    struct sockaddr_in a; addr_from_peer(&a, peer);
    int n = (int)sendto(s->fd, (const char*)buf, (int)len, 0,
                        (struct sockaddr*)&a, sizeof(a));
    return n;
}

int cc_net_socket_broadcast(CCNetSocket* s, const uint8_t* buf, size_t len){
    if (!s || s->fd == CC_INVALID_SOCK || !buf) return -1;
    int sent_to = 0;
    for (uint32_t i=0;i<CC_MAX_PEERS;i++){
        if (!s->peers[i].used) continue;
        int n = (int)sendto(s->fd, (const char*)buf, (int)len, 0,
                            (struct sockaddr*)&s->peers[i].addr, sizeof(s->peers[i].addr));
        if (n >= 0) sent_to++;
    }
    return sent_to;
}

int cc_net_socket_recv(CCNetSocket* s, uint8_t* buf, size_t cap, CCNetPeer* out_peer){
    if (!s || s->fd == CC_INVALID_SOCK || !buf) return -1;
    struct sockaddr_in from; socklen_t fl = sizeof(from);
    int n = (int)recvfrom(s->fd, (char*)buf, (int)cap, 0,
                         (struct sockaddr*)&from, &fl);
    if (n < 0){ return cc_would_block() ? 0 : -1; }   /* 0 = nothing waiting */
    if (n == 0) return 0;
    if (s->is_server){
        remember_peer(s, &from);
        s->dest = from; s->have_dest = true;   /* convenience: reply-to-last */
    }
    if (out_peer) peer_from_addr(out_peer, &from);
    return n;
}

uint32_t cc_net_peer_id(const CCNetPeer* peer){
    if (!peer) return 0;
    /* FNV-1a over the address bytes → stable non-zero id (used as CCNetClientId). */
    uint32_t h = 2166136261u;
    for (uint32_t i=0;i<peer->_len && i<sizeof(peer->_addr);i++){
        h ^= peer->_addr[i]; h *= 16777619u;
    }
    return h ? h : 1u;   /* never 0 (CC_NETCLIENT_NULL) */
}

bool cc_net_peer_equal(const CCNetPeer* a, const CCNetPeer* b){
    if (!a || !b) return false;
    if (a->_len != b->_len) return false;
    return memcmp(a->_addr, b->_addr, a->_len) == 0;
}

uint32_t cc_net_socket_peer_count(const CCNetSocket* s){
    return s ? s->peer_count : 0;
}
