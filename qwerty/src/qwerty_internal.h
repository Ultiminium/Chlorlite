#pragma once

#include "qwerty/qwerty.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>

/* ─── Lock-free ring buffer ───────────────────────────────────────────── */
/*
 * SPSC ring buffer: one producer (input thread) one consumer (game thread).
 * All accesses use acquire/release atomics — no mutex in hot path.
 */
typedef struct QRingBuffer {
    QEvent*           buf;
    uint32_t          mask;          /* capacity - 1, capacity is power of 2 */
    _Atomic uint32_t  head;          /* producer writes here */
    _Atomic uint32_t  tail;          /* consumer reads here */
} QRingBuffer;

static inline bool qring_push(QRingBuffer* rb, const QEvent* ev) {
    uint32_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    uint32_t next = (head + 1) & rb->mask;
    /* Check for overflow */
    if (next == atomic_load_explicit(&rb->tail, memory_order_acquire))
        return false;  /* full — drop event */
    rb->buf[head] = *ev;
    atomic_store_explicit(&rb->head, next, memory_order_release);
    return true;
}

static inline bool qring_pop(QRingBuffer* rb, QEvent* out) {
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    if (tail == atomic_load_explicit(&rb->head, memory_order_acquire))
        return false;  /* empty */
    *out = rb->buf[tail];
    atomic_store_explicit(&rb->tail, (tail + 1) & rb->mask, memory_order_release);
    return true;
}

static inline uint32_t qring_drain(QRingBuffer* rb, QEvent* out, uint32_t max) {
    uint32_t n = 0;
    while (n < max && qring_pop(rb, &out[n])) n++;
    return n;
}

/* ─── Keyboard state bitmap ───────────────────────────────────────────── */
#define QKEY_BITMAP_WORDS ((QKEY_COUNT + 63) / 64)

typedef struct QKeyState {
    _Atomic uint64_t words[QKEY_BITMAP_WORDS];
} QKeyState;

static inline void qks_set(QKeyState* ks, QKey k, bool down) {
    int w = k / 64, b = k % 64;
    uint64_t old = atomic_load_explicit(&ks->words[w], memory_order_relaxed);
    uint64_t val = down ? (old | (1ULL << b)) : (old & ~(1ULL << b));
    atomic_store_explicit(&ks->words[w], val, memory_order_release);
}

static inline bool qks_get(const QKeyState* ks, QKey k) {
    int w = k / 64, b = k % 64;
    return (atomic_load_explicit(&ks->words[w], memory_order_acquire) >> b) & 1;
}

/* ─── Mouse state ─────────────────────────────────────────────────────── */
typedef struct QMouseState {
    _Atomic int32_t  x, y;
    _Atomic uint32_t buttons;   /* bit per QMouseButton */
} QMouseState;

/* ─── Context internals ───────────────────────────────────────────────── */
struct QContext {
    QConfig         cfg;
    QBackend        active_backend;

    QRingBuffer     ring;
    QKeyState       key_state;
    QMouseState     mouse_state;
    _Atomic QMod    mod_state;

    QEventCallback  callback;
    void*           callback_userdata;
    pthread_mutex_t callback_lock;

    /* Input thread */
    pthread_t       input_thread;
    _Atomic bool    running;

    /* evdev */
    int*            evdev_fds;      /* file descriptors for all event devices */
    int             evdev_count;
    int             evdev_epoll_fd;

    /* X11 fallback */
    void*           x11_display;   /* Display* — opaque to avoid X11 header pull */
    int             x11_window;

    /* Notification pipe for wait() */
    int             notify_pipe[2];
};

/* ─── Internal event dispatch ─────────────────────────────────────────── */
void qwerty_dispatch(QContext* ctx, QEvent* ev);

/* ─── Backend init signatures ─────────────────────────────────────────── */
bool qwerty_evdev_init(QContext* ctx);
void qwerty_evdev_shutdown(QContext* ctx);
void* qwerty_evdev_thread(void* arg);

bool qwerty_x11_init(QContext* ctx);
void qwerty_x11_shutdown(QContext* ctx);
void* qwerty_x11_thread(void* arg);

/* ─── Scancode → QKey translation ─────────────────────────────────────── */
QKey qwerty_evdev_scancode_to_key(uint16_t code);
QKey qwerty_x11_keysym_to_key(uint32_t keysym);

/* ─── Monotonic timestamp ─────────────────────────────────────────────── */
#include <time.h>
static inline uint64_t qwerty_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
