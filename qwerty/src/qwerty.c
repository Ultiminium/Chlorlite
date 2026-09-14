#include "qwerty_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
  #include <poll.h>
#endif

/* ─── Init / Shutdown ─────────────────────────────────────────────────── */

QContext* qwerty_init(const QConfig* cfg) {
    QContext* ctx = calloc(1, sizeof(QContext));
    if (!ctx) return NULL;

    ctx->cfg = cfg ? *cfg : qwerty_default_config();

    /* Ensure power-of-2 ring buffer size */
    uint32_t cap = ctx->cfg.event_queue_size;
    if (cap < 4) cap = 4;
    /* Round up to next power of 2 */
    cap--;
    cap |= cap >> 1; cap |= cap >> 2; cap |= cap >> 4;
    cap |= cap >> 8; cap |= cap >> 16;
    cap++;

    ctx->ring.buf  = malloc(cap * sizeof(QEvent));
    if (!ctx->ring.buf) { free(ctx); return NULL; }
    ctx->ring.mask = cap - 1;
    atomic_store(&ctx->ring.head, 0);
    atomic_store(&ctx->ring.tail, 0);

    pthread_mutex_init(&ctx->callback_lock, NULL);

#ifndef _WIN32
    /* Notification pipe for qwerty_wait() (POSIX only) */
    if (pipe(ctx->notify_pipe) != 0) {
        free(ctx->ring.buf); free(ctx); return NULL;
    }
    fcntl(ctx->notify_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(ctx->notify_pipe[1], F_SETFL, O_NONBLOCK);
#endif

    atomic_store(&ctx->running, true);

    QBackend backend = ctx->cfg.backend;

#ifndef _WIN32
    /* AUTO: try evdev first, fall back to X11 (POSIX native backends) */
    if (backend == QBACKEND_AUTO || backend == QBACKEND_EVDEV) {
        if (qwerty_evdev_init(ctx)) {
            ctx->active_backend = QBACKEND_EVDEV;
            pthread_create(&ctx->input_thread, NULL, qwerty_evdev_thread, ctx);
            return ctx;
        }
    }
    if (backend == QBACKEND_AUTO || backend == QBACKEND_X11) {
        if (qwerty_x11_init(ctx)) {
            ctx->active_backend = QBACKEND_X11;
            pthread_create(&ctx->input_thread, NULL, qwerty_x11_thread, ctx);
            return ctx;
        }
    }
#endif

    /* Headless / injection backend — the default on Windows, where input is
       delivered via the window layer (GLFW callbacks → qwerty_inject_*). */
    ctx->active_backend = QBACKEND_HEADLESS;
    return ctx;
}

void qwerty_shutdown(QContext* ctx) {
    if (!ctx) return;

    atomic_store(&ctx->running, false);

#ifndef _WIN32
    /* Wake + join the input thread (POSIX native backends only) */
    char wake = 1;
    ssize_t wr = write(ctx->notify_pipe[1], &wake, 1); (void)wr;
    if (ctx->active_backend == QBACKEND_EVDEV)
        qwerty_evdev_shutdown(ctx);
    else if (ctx->active_backend == QBACKEND_X11)
        qwerty_x11_shutdown(ctx);
    if (ctx->active_backend != QBACKEND_HEADLESS)
        pthread_join(ctx->input_thread, NULL);
    close(ctx->notify_pipe[0]);
    close(ctx->notify_pipe[1]);
#endif

    free(ctx->ring.buf);
    pthread_mutex_destroy(&ctx->callback_lock);
    free(ctx);
}

/* ─── Internal dispatch (called from input thread) ────────────────────── */

void qwerty_dispatch(QContext* ctx, QEvent* ev) {
    /* Update mirrored state */
    switch (ev->type) {
        case QEVENT_KEY_DOWN:
        case QEVENT_KEY_REPEAT:
            qks_set(&ctx->key_state, ev->key.key, true);
            atomic_store_explicit(&ctx->mod_state, ev->key.mods, memory_order_release);
            break;
        case QEVENT_KEY_UP:
            qks_set(&ctx->key_state, ev->key.key, false);
            atomic_store_explicit(&ctx->mod_state, ev->key.mods, memory_order_release);
            break;
        case QEVENT_MOUSE_MOVE:
            atomic_store_explicit(&ctx->mouse_state.x, ev->mouse_move.x, memory_order_release);
            atomic_store_explicit(&ctx->mouse_state.y, ev->mouse_move.y, memory_order_release);
            break;
        case QEVENT_MOUSE_BUTTON_DOWN: {
            uint32_t old = atomic_load_explicit(&ctx->mouse_state.buttons, memory_order_relaxed);
            atomic_store_explicit(&ctx->mouse_state.buttons,
                                  old | (1u << ev->mouse_button.button), memory_order_release);
            break;
        }
        case QEVENT_MOUSE_BUTTON_UP: {
            uint32_t old = atomic_load_explicit(&ctx->mouse_state.buttons, memory_order_relaxed);
            atomic_store_explicit(&ctx->mouse_state.buttons,
                                  old & ~(1u << ev->mouse_button.button), memory_order_release);
            break;
        }
        default: break;
    }

    /* Timestamp if not set */
    if (!ev->timestamp_ns) ev->timestamp_ns = qwerty_now_ns();

    /* Push to ring buffer */
    qring_push(&ctx->ring, ev);

    /* Notify waiting consumers (POSIX qwerty_wait path) */
#ifndef _WIN32
    char wake = 1;
    ssize_t wr = write(ctx->notify_pipe[1], &wake, 1); (void)wr;
#endif

    /* Fire callback without holding ring lock */
    pthread_mutex_lock(&ctx->callback_lock);
    QEventCallback cb = ctx->callback;
    void* ud = ctx->callback_userdata;
    pthread_mutex_unlock(&ctx->callback_lock);

    if (cb) cb(ev, ud);
}


/* ─── Event injection (public) ────────────────────────────────────────── */

void qwerty_inject_event(QContext* ctx, const QEvent* ev) {
    if (!ctx || !ev) return;
    QEvent copy = *ev;
    qwerty_dispatch(ctx, &copy);
}

void qwerty_inject_key(QContext* ctx, QKey key, bool down, QMod mods) {
    if (!ctx) return;
    QEvent ev = {0};
    ev.type = down ? QEVENT_KEY_DOWN : QEVENT_KEY_UP;
    ev.key.key = key;
    ev.key.mods = mods;
    ev.key.scancode = (uint32_t)key;
    qwerty_dispatch(ctx, &ev);
}

void qwerty_inject_char(QContext* ctx, uint32_t codepoint) {
    if (!ctx) return;
    QEvent ev = {0};
    ev.type = QEVENT_CHAR;
    ev.character.codepoint = codepoint;
    qwerty_dispatch(ctx, &ev);
}

void qwerty_inject_mouse_move(QContext* ctx, int32_t x, int32_t y) {
    if (!ctx) return;
    int32_t ox=0, oy=0;
    qwerty_mouse_pos(ctx, &ox, &oy);
    QEvent ev = {0};
    ev.type = QEVENT_MOUSE_MOVE;
    ev.mouse_move.x = x; ev.mouse_move.y = y;
    ev.mouse_move.dx = x - ox; ev.mouse_move.dy = y - oy;
    qwerty_dispatch(ctx, &ev);
}

void qwerty_inject_mouse_button(QContext* ctx, QMouseButton btn, bool down, int32_t x, int32_t y) {
    if (!ctx) return;
    QEvent ev = {0};
    ev.type = down ? QEVENT_MOUSE_BUTTON_DOWN : QEVENT_MOUSE_BUTTON_UP;
    ev.mouse_button.button = btn;
    ev.mouse_button.x = x; ev.mouse_button.y = y;
    qwerty_dispatch(ctx, &ev);
}

void qwerty_inject_mouse_scroll(QContext* ctx, float dx, float dy) {
    if (!ctx) return;
    QEvent ev = {0};
    ev.type = QEVENT_MOUSE_SCROLL;
    ev.mouse_scroll.dx = dx; ev.mouse_scroll.dy = dy;
    qwerty_dispatch(ctx, &ev);
}

/* ─── Callback API ────────────────────────────────────────────────────── */

void qwerty_set_callback(QContext* ctx, QEventCallback cb, void* userdata) {
    pthread_mutex_lock(&ctx->callback_lock);
    ctx->callback = cb;
    ctx->callback_userdata = userdata;
    pthread_mutex_unlock(&ctx->callback_lock);
}

void qwerty_clear_callback(QContext* ctx) {
    qwerty_set_callback(ctx, NULL, NULL);
}

/* ─── Polling API ─────────────────────────────────────────────────────── */

uint32_t qwerty_poll(QContext* ctx, QEvent* out, uint32_t max_events) {
    return qring_drain(&ctx->ring, out, max_events);
}

uint32_t qwerty_wait(QContext* ctx, QEvent* out, uint32_t max_events,
                     uint32_t timeout_ms) {
    /* First try a non-blocking drain */
    uint32_t n = qring_drain(&ctx->ring, out, max_events);
    if (n) return n;

#ifdef _WIN32
    /* No pipe on Windows — brief sleep then re-drain. Input is injected from
       the window layer, so this path just yields until events arrive. */
    Sleep(timeout_ms ? timeout_ms : 1);
    return qring_drain(&ctx->ring, out, max_events);
#else
    /* Block on notify pipe */
    struct pollfd pfd = { .fd = ctx->notify_pipe[0], .events = POLLIN };
    int ms = timeout_ms ? (int)timeout_ms : -1;
    int r = poll(&pfd, 1, ms);
    if (r > 0) {
        char buf[64];
        ssize_t rd = read(ctx->notify_pipe[0], buf, sizeof(buf)); (void)rd;
        return qring_drain(&ctx->ring, out, max_events);
    }
    return 0;
#endif
}

/* ─── State queries ───────────────────────────────────────────────────── */

bool qwerty_key_down(const QContext* ctx, QKey key) {
    return qks_get(&ctx->key_state, key);
}

bool qwerty_mouse_down(const QContext* ctx, QMouseButton btn) {
    uint32_t buttons = atomic_load_explicit(&ctx->mouse_state.buttons, memory_order_acquire);
    return (buttons >> btn) & 1;
}

void qwerty_mouse_pos(const QContext* ctx, int32_t* x, int32_t* y) {
    if (x) *x = atomic_load_explicit(&ctx->mouse_state.x, memory_order_acquire);
    if (y) *y = atomic_load_explicit(&ctx->mouse_state.y, memory_order_acquire);
}

QMod qwerty_mods(const QContext* ctx) {
    return (QMod)atomic_load_explicit(&ctx->mod_state, memory_order_acquire);
}

/* ─── Utility ─────────────────────────────────────────────────────────── */

static const char* KEY_NAMES[QKEY_COUNT] = {
    [QKEY_NONE]      = "None",
    [QKEY_A]         = "A",  [QKEY_B] = "B",  [QKEY_C] = "C",
    [QKEY_D]         = "D",  [QKEY_E] = "E",  [QKEY_F] = "F",
    [QKEY_G]         = "G",  [QKEY_H] = "H",  [QKEY_I] = "I",
    [QKEY_J]         = "J",  [QKEY_K] = "K",  [QKEY_L] = "L",
    [QKEY_M]         = "M",  [QKEY_N] = "N",  [QKEY_O] = "O",
    [QKEY_P]         = "P",  [QKEY_Q] = "Q",  [QKEY_R] = "R",
    [QKEY_S]         = "S",  [QKEY_T] = "T",  [QKEY_U] = "U",
    [QKEY_V]         = "V",  [QKEY_W] = "W",  [QKEY_X] = "X",
    [QKEY_Y]         = "Y",  [QKEY_Z] = "Z",
    [QKEY_0]         = "0",  [QKEY_1] = "1",  [QKEY_2] = "2",
    [QKEY_3]         = "3",  [QKEY_4] = "4",  [QKEY_5] = "5",
    [QKEY_6]         = "6",  [QKEY_7] = "7",  [QKEY_8] = "8",
    [QKEY_9]         = "9",
    [QKEY_F1]        = "F1", [QKEY_F2]  = "F2",  [QKEY_F3]  = "F3",
    [QKEY_F4]        = "F4", [QKEY_F5]  = "F5",  [QKEY_F6]  = "F6",
    [QKEY_F7]        = "F7", [QKEY_F8]  = "F8",  [QKEY_F9]  = "F9",
    [QKEY_F10]       = "F10",[QKEY_F11] = "F11", [QKEY_F12] = "F12",
    [QKEY_ESCAPE]    = "Escape",   [QKEY_ENTER]     = "Enter",
    [QKEY_BACKSPACE] = "Backspace",[QKEY_TAB]       = "Tab",
    [QKEY_SPACE]     = "Space",    [QKEY_DELETE]    = "Delete",
    [QKEY_INSERT]    = "Insert",   [QKEY_HOME]      = "Home",
    [QKEY_END]       = "End",      [QKEY_PAGE_UP]   = "PageUp",
    [QKEY_PAGE_DOWN] = "PageDown",
    [QKEY_UP]        = "Up",   [QKEY_DOWN]  = "Down",
    [QKEY_LEFT]      = "Left", [QKEY_RIGHT] = "Right",
    [QKEY_LSHIFT]    = "LShift",  [QKEY_RSHIFT] = "RShift",
    [QKEY_LCTRL]     = "LCtrl",   [QKEY_RCTRL]  = "RCtrl",
    [QKEY_LALT]      = "LAlt",    [QKEY_RALT]   = "RAlt",
    [QKEY_LSUPER]    = "LSuper",  [QKEY_RSUPER] = "RSuper",
    [QKEY_CAPSLOCK]  = "CapsLock",
};

const char* qwerty_key_name(QKey key) {
    if (key < QKEY_COUNT && KEY_NAMES[key]) return KEY_NAMES[key];
    return "Unknown";
}

const char* qwerty_version_string(void) {
    return "Qwerty 0.1.0";
}

const char* qwerty_backend_name(const QContext* ctx) {
    switch (ctx->active_backend) {
        case QBACKEND_EVDEV:    return "evdev";
        case QBACKEND_X11:      return "X11";
        case QBACKEND_HEADLESS: return "headless";
        default:                return "unknown";
    }
}
