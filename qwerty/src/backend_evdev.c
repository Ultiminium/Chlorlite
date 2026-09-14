#include "qwerty_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define EVDEV_DIR    "/dev/input"
#define MAX_DEVICES  32
#define MAX_EVENTS   64

/* ─── Scancode → QKey table (evdev KEY_* codes) ───────────────────────── */
static const QKey EVDEV_KEY_TABLE[KEY_CNT] = {
    [KEY_A]          = QKEY_A,    [KEY_B]          = QKEY_B,
    [KEY_C]          = QKEY_C,    [KEY_D]          = QKEY_D,
    [KEY_E]          = QKEY_E,    [KEY_F]          = QKEY_F,
    [KEY_G]          = QKEY_G,    [KEY_H]          = QKEY_H,
    [KEY_I]          = QKEY_I,    [KEY_J]          = QKEY_J,
    [KEY_K]          = QKEY_K,    [KEY_L]          = QKEY_L,
    [KEY_M]          = QKEY_M,    [KEY_N]          = QKEY_N,
    [KEY_O]          = QKEY_O,    [KEY_P]          = QKEY_P,
    [KEY_Q]          = QKEY_Q,    [KEY_R]          = QKEY_R,
    [KEY_S]          = QKEY_S,    [KEY_T]          = QKEY_T,
    [KEY_U]          = QKEY_U,    [KEY_V]          = QKEY_V,
    [KEY_W]          = QKEY_W,    [KEY_X]          = QKEY_X,
    [KEY_Y]          = QKEY_Y,    [KEY_Z]          = QKEY_Z,
    [KEY_0]          = QKEY_0,    [KEY_1]          = QKEY_1,
    [KEY_2]          = QKEY_2,    [KEY_3]          = QKEY_3,
    [KEY_4]          = QKEY_4,    [KEY_5]          = QKEY_5,
    [KEY_6]          = QKEY_6,    [KEY_7]          = QKEY_7,
    [KEY_8]          = QKEY_8,    [KEY_9]          = QKEY_9,
    [KEY_F1]         = QKEY_F1,   [KEY_F2]         = QKEY_F2,
    [KEY_F3]         = QKEY_F3,   [KEY_F4]         = QKEY_F4,
    [KEY_F5]         = QKEY_F5,   [KEY_F6]         = QKEY_F6,
    [KEY_F7]         = QKEY_F7,   [KEY_F8]         = QKEY_F8,
    [KEY_F9]         = QKEY_F9,   [KEY_F10]        = QKEY_F10,
    [KEY_F11]        = QKEY_F11,  [KEY_F12]        = QKEY_F12,
    [KEY_ESC]        = QKEY_ESCAPE,
    [KEY_ENTER]      = QKEY_ENTER,
    [KEY_BACKSPACE]  = QKEY_BACKSPACE,
    [KEY_TAB]        = QKEY_TAB,
    [KEY_SPACE]      = QKEY_SPACE,
    [KEY_DELETE]     = QKEY_DELETE,
    [KEY_INSERT]     = QKEY_INSERT,
    [KEY_HOME]       = QKEY_HOME,
    [KEY_END]        = QKEY_END,
    [KEY_PAGEUP]     = QKEY_PAGE_UP,
    [KEY_PAGEDOWN]   = QKEY_PAGE_DOWN,
    [KEY_UP]         = QKEY_UP,
    [KEY_DOWN]       = QKEY_DOWN,
    [KEY_LEFT]       = QKEY_LEFT,
    [KEY_RIGHT]      = QKEY_RIGHT,
    [KEY_LEFTSHIFT]  = QKEY_LSHIFT,
    [KEY_RIGHTSHIFT] = QKEY_RSHIFT,
    [KEY_LEFTCTRL]   = QKEY_LCTRL,
    [KEY_RIGHTCTRL]  = QKEY_RCTRL,
    [KEY_LEFTALT]    = QKEY_LALT,
    [KEY_RIGHTALT]   = QKEY_RALT,
    [KEY_LEFTMETA]   = QKEY_LSUPER,
    [KEY_RIGHTMETA]  = QKEY_RSUPER,
    [KEY_CAPSLOCK]   = QKEY_CAPSLOCK,
    [KEY_MINUS]      = QKEY_MINUS,
    [KEY_EQUAL]      = QKEY_EQUALS,
    [KEY_LEFTBRACE]  = QKEY_LBRACKET,
    [KEY_RIGHTBRACE] = QKEY_RBRACKET,
    [KEY_BACKSLASH]  = QKEY_BACKSLASH,
    [KEY_SEMICOLON]  = QKEY_SEMICOLON,
    [KEY_APOSTROPHE] = QKEY_APOSTROPHE,
    [KEY_GRAVE]      = QKEY_GRAVE,
    [KEY_COMMA]      = QKEY_COMMA,
    [KEY_DOT]        = QKEY_PERIOD,
    [KEY_SLASH]      = QKEY_SLASH,
    [KEY_KP0]        = QKEY_NP0,  [KEY_KP1] = QKEY_NP1,
    [KEY_KP2]        = QKEY_NP2,  [KEY_KP3] = QKEY_NP3,
    [KEY_KP4]        = QKEY_NP4,  [KEY_KP5] = QKEY_NP5,
    [KEY_KP6]        = QKEY_NP6,  [KEY_KP7] = QKEY_NP7,
    [KEY_KP8]        = QKEY_NP8,  [KEY_KP9] = QKEY_NP9,
    [KEY_KPENTER]    = QKEY_NP_ENTER,
    [KEY_KPPLUS]     = QKEY_NP_PLUS,
    [KEY_KPMINUS]    = QKEY_NP_MINUS,
    [KEY_KPASTERISK] = QKEY_NP_MUL,
    [KEY_KPSLASH]    = QKEY_NP_DIV,
    [KEY_KPDOT]      = QKEY_NP_DOT,
};

QKey qwerty_evdev_scancode_to_key(uint16_t code) {
    if (code < KEY_CNT) return EVDEV_KEY_TABLE[code];
    return QKEY_NONE;
}

/* ─── Device discovery ─────────────────────────────────────────────────── */
static bool is_event_device(const char* name) {
    return strncmp(name, "event", 5) == 0;
}

static bool has_keys(int fd) {
    unsigned long evbit = 0;
    ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit);
    return (evbit & (1 << EV_KEY)) != 0;
}

bool qwerty_evdev_init(QContext* ctx) {
    ctx->evdev_epoll_fd = epoll_create1(0);
    if (ctx->evdev_epoll_fd < 0) return false;

    ctx->evdev_fds = calloc(MAX_DEVICES, sizeof(int));
    if (!ctx->evdev_fds) { close(ctx->evdev_epoll_fd); return false; }
    ctx->evdev_count = 0;

    DIR* dir = opendir(EVDEV_DIR);
    if (!dir) {
        /* No /dev/input — probably headless sandbox */
        free(ctx->evdev_fds);
        close(ctx->evdev_epoll_fd);
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) && ctx->evdev_count < MAX_DEVICES) {
        if (!is_event_device(entry->d_name)) continue;

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", EVDEV_DIR, entry->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        if (!has_keys(fd)) { close(fd); continue; }

        /* Optionally grab exclusive access */
        if (ctx->cfg.grab_keyboard || ctx->cfg.grab_mouse)
            ioctl(fd, EVIOCGRAB, 1);

        struct epoll_event epev = {
            .events = EPOLLIN,
            .data.fd = fd,
        };
        epoll_ctl(ctx->evdev_epoll_fd, EPOLL_CTL_ADD, fd, &epev);
        ctx->evdev_fds[ctx->evdev_count++] = fd;
    }
    closedir(dir);

    if (ctx->evdev_count == 0) {
        free(ctx->evdev_fds);
        close(ctx->evdev_epoll_fd);
        return false;
    }

    return true;
}

void qwerty_evdev_shutdown(QContext* ctx) {
    for (int i = 0; i < ctx->evdev_count; i++) {
        if (ctx->cfg.grab_keyboard || ctx->cfg.grab_mouse)
            ioctl(ctx->evdev_fds[i], EVIOCGRAB, 0);
        close(ctx->evdev_fds[i]);
    }
    free(ctx->evdev_fds);
    ctx->evdev_fds = NULL;
    ctx->evdev_count = 0;
    close(ctx->evdev_epoll_fd);
}

/* ─── Build modifier state from key state ─────────────────────────────── */
static QMod build_mods(QContext* ctx) {
    QMod m = QMOD_NONE;
    if (qks_get(&ctx->key_state, QKEY_LSHIFT) || qks_get(&ctx->key_state, QKEY_RSHIFT)) m |= QMOD_SHIFT;
    if (qks_get(&ctx->key_state, QKEY_LCTRL)  || qks_get(&ctx->key_state, QKEY_RCTRL))  m |= QMOD_CTRL;
    if (qks_get(&ctx->key_state, QKEY_LALT)   || qks_get(&ctx->key_state, QKEY_RALT))   m |= QMOD_ALT;
    if (qks_get(&ctx->key_state, QKEY_LSUPER) || qks_get(&ctx->key_state, QKEY_RSUPER)) m |= QMOD_SUPER;
    if (qks_get(&ctx->key_state, QKEY_CAPSLOCK)) m |= QMOD_CAPS;
    return m;
}

/* ─── Input thread ────────────────────────────────────────────────────── */
void* qwerty_evdev_thread(void* arg) {
    QContext* ctx = arg;
    struct epoll_event events[MAX_EVENTS];
    struct input_event iev[MAX_EVENTS];

    /* Also watch notify pipe for shutdown */
    struct epoll_event pipe_ev = {
        .events  = EPOLLIN,
        .data.fd = ctx->notify_pipe[0],
    };
    epoll_ctl(ctx->evdev_epoll_fd, EPOLL_CTL_ADD, ctx->notify_pipe[0], &pipe_ev);

    /* Accumulated mouse deltas — flushed on SYN_REPORT */
    int32_t acc_dx = 0, acc_dy = 0;
    bool has_mouse_delta = false;

    while (atomic_load(&ctx->running)) {
        int n = epoll_wait(ctx->evdev_epoll_fd, events, MAX_EVENTS, 100);
        if (n < 0) break;

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == ctx->notify_pipe[0]) goto done; /* shutdown */

            ssize_t bytes = read(fd, iev, sizeof(iev));
            if (bytes < 0) continue;

            int count = (int)(bytes / sizeof(struct input_event));
            for (int j = 0; j < count; j++) {
                struct input_event* ie = &iev[j];

                if (ie->type == EV_KEY) {
                    QKey key = qwerty_evdev_scancode_to_key(ie->code);
                    if (key == QKEY_NONE) continue;

                    /* Update key state BEFORE building mods */
                    qks_set(&ctx->key_state, key, ie->value != 0);
                    QMod mods = build_mods(ctx);

                    QEvent ev = {
                        .type = (ie->value == 0) ? QEVENT_KEY_UP
                              : (ie->value == 2) ? QEVENT_KEY_REPEAT
                              :                    QEVENT_KEY_DOWN,
                        .timestamp_ns = (uint64_t)ie->input_event_sec * 1000000000ULL
                                      + (uint64_t)ie->input_event_usec * 1000ULL,
                        .key = { .key = key, .mods = mods, .scancode = ie->code },
                    };
                    qwerty_dispatch(ctx, &ev);
                }

                else if (ie->type == EV_REL) {
                    if (ie->code == REL_X) { acc_dx += ie->value; has_mouse_delta = true; }
                    else if (ie->code == REL_Y) { acc_dy += ie->value; has_mouse_delta = true; }
                    else if (ie->code == REL_WHEEL) {
                        QEvent ev = {
                            .type = QEVENT_MOUSE_SCROLL,
                            .mouse_scroll = { .dy = (float)ie->value },
                        };
                        qwerty_dispatch(ctx, &ev);
                    }
                    else if (ie->code == REL_HWHEEL) {
                        QEvent ev = {
                            .type = QEVENT_MOUSE_SCROLL,
                            .mouse_scroll = { .dx = (float)ie->value },
                        };
                        qwerty_dispatch(ctx, &ev);
                    }
                }

                else if (ie->type == EV_KEY && ie->code >= BTN_MOUSE && ie->code < BTN_JOYSTICK) {
                    QMouseButton btn = QMOUSE_LEFT;
                    if      (ie->code == BTN_LEFT)   btn = QMOUSE_LEFT;
                    else if (ie->code == BTN_RIGHT)  btn = QMOUSE_RIGHT;
                    else if (ie->code == BTN_MIDDLE) btn = QMOUSE_MIDDLE;
                    else if (ie->code == BTN_SIDE)   btn = QMOUSE_X1;
                    else if (ie->code == BTN_EXTRA)  btn = QMOUSE_X2;
                    else continue;

                    int32_t mx, my;
                    qwerty_mouse_pos(ctx, &mx, &my);
                    QEvent ev = {
                        .type = ie->value ? QEVENT_MOUSE_BUTTON_DOWN : QEVENT_MOUSE_BUTTON_UP,
                        .mouse_button = { .button = btn, .x = mx, .y = my },
                    };
                    qwerty_dispatch(ctx, &ev);
                }

                else if (ie->type == EV_SYN && ie->code == SYN_REPORT) {
                    if (has_mouse_delta) {
                        int32_t mx = atomic_load_explicit(&ctx->mouse_state.x, memory_order_relaxed);
                        int32_t my = atomic_load_explicit(&ctx->mouse_state.y, memory_order_relaxed);
                        mx += acc_dx; my += acc_dy;
                        QEvent ev = {
                            .type = QEVENT_MOUSE_MOVE,
                            .mouse_move = { .x = mx, .y = my, .dx = acc_dx, .dy = acc_dy },
                        };
                        qwerty_dispatch(ctx, &ev);
                        acc_dx = acc_dy = 0;
                        has_mouse_delta = false;
                    }
                }
            }
        }
    }
done:
    return NULL;
}
