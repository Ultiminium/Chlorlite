#pragma once

/*
 * Qwerty — High-performance input handling library
 * Linux evdev/X11 backend, Chlorlite integration layer
 *
 * Design goals:
 *   - Zero-copy event path from kernel to callback
 *   - Sub-millisecond latency via dedicated input thread
 *   - No heap allocations in the hot path
 *   - Both polling and callback (push) API
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Version ─────────────────────────────────────────────────────────── */
#define QWERTY_VERSION_MAJOR 0
#define QWERTY_VERSION_MINOR 1
#define QWERTY_VERSION_PATCH 0

/* ─── Key codes (evdev-aligned, portable subset) ──────────────────────── */
typedef enum QKey {
    QKEY_NONE = 0,

    /* Letters */
    QKEY_A = 1, QKEY_B, QKEY_C, QKEY_D, QKEY_E, QKEY_F, QKEY_G,
    QKEY_H, QKEY_I, QKEY_J, QKEY_K, QKEY_L, QKEY_M, QKEY_N,
    QKEY_O, QKEY_P, QKEY_Q, QKEY_R, QKEY_S, QKEY_T, QKEY_U,
    QKEY_V, QKEY_W, QKEY_X, QKEY_Y, QKEY_Z,

    /* Digits */
    QKEY_0 = 30, QKEY_1, QKEY_2, QKEY_3, QKEY_4,
    QKEY_5, QKEY_6, QKEY_7, QKEY_8, QKEY_9,

    /* Function keys */
    QKEY_F1 = 40, QKEY_F2, QKEY_F3, QKEY_F4, QKEY_F5, QKEY_F6,
    QKEY_F7, QKEY_F8, QKEY_F9, QKEY_F10, QKEY_F11, QKEY_F12,

    /* Control */
    QKEY_ESCAPE = 60, QKEY_ENTER, QKEY_BACKSPACE, QKEY_TAB,
    QKEY_SPACE, QKEY_DELETE, QKEY_INSERT, QKEY_HOME, QKEY_END,
    QKEY_PAGE_UP, QKEY_PAGE_DOWN,

    /* Arrows */
    QKEY_UP = 80, QKEY_DOWN, QKEY_LEFT, QKEY_RIGHT,

    /* Modifiers */
    QKEY_LSHIFT = 90, QKEY_RSHIFT, QKEY_LCTRL, QKEY_RCTRL,
    QKEY_LALT, QKEY_RALT, QKEY_LSUPER, QKEY_RSUPER, QKEY_CAPSLOCK,

    /* Punctuation */
    QKEY_MINUS = 110, QKEY_EQUALS, QKEY_LBRACKET, QKEY_RBRACKET,
    QKEY_BACKSLASH, QKEY_SEMICOLON, QKEY_APOSTROPHE, QKEY_GRAVE,
    QKEY_COMMA, QKEY_PERIOD, QKEY_SLASH,

    /* Numpad */
    QKEY_NP0 = 130, QKEY_NP1, QKEY_NP2, QKEY_NP3, QKEY_NP4,
    QKEY_NP5, QKEY_NP6, QKEY_NP7, QKEY_NP8, QKEY_NP9,
    QKEY_NP_ENTER, QKEY_NP_PLUS, QKEY_NP_MINUS, QKEY_NP_MUL,
    QKEY_NP_DIV, QKEY_NP_DOT,

    QKEY_COUNT
} QKey;

/* ─── Mouse buttons ───────────────────────────────────────────────────── */
typedef enum QMouseButton {
    QMOUSE_LEFT   = 0,
    QMOUSE_RIGHT  = 1,
    QMOUSE_MIDDLE = 2,
    QMOUSE_X1     = 3,
    QMOUSE_X2     = 4,
    QMOUSE_COUNT
} QMouseButton;

/* ─── Event types ─────────────────────────────────────────────────────── */
typedef enum QEventType {
    QEVENT_NONE = 0,
    QEVENT_KEY_DOWN,
    QEVENT_KEY_UP,
    QEVENT_KEY_REPEAT,
    QEVENT_MOUSE_MOVE,
    QEVENT_MOUSE_BUTTON_DOWN,
    QEVENT_MOUSE_BUTTON_UP,
    QEVENT_MOUSE_SCROLL,
    QEVENT_CHAR,            /* UTF-32 character input */
    QEVENT_FOCUS_GAINED,
    QEVENT_FOCUS_LOST,
    QEVENT_GAMEPAD_BUTTON_DOWN,
    QEVENT_GAMEPAD_BUTTON_UP,
    QEVENT_GAMEPAD_AXIS,
} QEventType;

/* ─── Modifier flags ──────────────────────────────────────────────────── */
typedef enum QMod {
    QMOD_NONE   = 0,
    QMOD_SHIFT  = 1 << 0,
    QMOD_CTRL   = 1 << 1,
    QMOD_ALT    = 1 << 2,
    QMOD_SUPER  = 1 << 3,
    QMOD_CAPS   = 1 << 4,
} QMod;

/* ─── Event structs ───────────────────────────────────────────────────── */
typedef struct QKeyEvent {
    QKey      key;
    QMod      mods;
    uint32_t  scancode;     /* raw hardware scancode */
} QKeyEvent;

typedef struct QMouseMoveEvent {
    int32_t   x, y;         /* absolute position */
    int32_t   dx, dy;       /* delta since last event */
} QMouseMoveEvent;

typedef struct QMouseButtonEvent {
    QMouseButton button;
    int32_t      x, y;
    QMod         mods;
} QMouseButtonEvent;

typedef struct QMouseScrollEvent {
    float    dx, dy;        /* horizontal, vertical — in lines */
    int32_t  x, y;
} QMouseScrollEvent;

typedef struct QCharEvent {
    uint32_t codepoint;     /* UTF-32 */
} QCharEvent;

typedef struct QGamepadAxisEvent {
    uint8_t  gamepad_id;
    uint8_t  axis;
    float    value;         /* -1.0 .. 1.0 */
} QGamepadAxisEvent;

typedef struct QGamepadButtonEvent {
    uint8_t  gamepad_id;
    uint8_t  button;
} QGamepadButtonEvent;

typedef struct QEvent {
    QEventType type;
    uint64_t   timestamp_ns;  /* monotonic nanoseconds */
    union {
        QKeyEvent           key;
        QMouseMoveEvent     mouse_move;
        QMouseButtonEvent   mouse_button;
        QMouseScrollEvent   mouse_scroll;
        QCharEvent          character;
        QGamepadAxisEvent   gamepad_axis;
        QGamepadButtonEvent gamepad_button;
    };
} QEvent;

/* ─── Callback type ───────────────────────────────────────────────────── */
typedef void (*QEventCallback)(const QEvent* event, void* userdata);

/* ─── Context ─────────────────────────────────────────────────────────── */
typedef struct QContext QContext;

typedef enum QBackend {
    QBACKEND_AUTO = 0,  /* evdev preferred, X11 fallback */
    QBACKEND_EVDEV,
    QBACKEND_X11,
    QBACKEND_HEADLESS,  /* for Chlorlite sandbox — no display required */
} QBackend;

typedef struct QConfig {
    QBackend backend;
    bool     grab_keyboard;   /* exclusive evdev grab */
    bool     grab_mouse;      /* exclusive evdev grab */
    bool     enable_gamepad;
    uint32_t event_queue_size; /* ring buffer size, must be power of 2 */
} QConfig;

/* Default config — safe for Chlorlite integration */
static inline QConfig qwerty_default_config(void) {
    return (QConfig){
        .backend          = QBACKEND_AUTO,
        .grab_keyboard    = false,
        .grab_mouse       = false,
        .enable_gamepad   = true,
        .event_queue_size = 1024,
    };
}

/* ─── Lifecycle ───────────────────────────────────────────────────────── */
QContext* qwerty_init(const QConfig* cfg);
void      qwerty_shutdown(QContext* ctx);

/* ─── Callback API (push — recommended for games) ─────────────────────── */
void qwerty_set_callback(QContext* ctx, QEventCallback cb, void* userdata);
void qwerty_clear_callback(QContext* ctx);

/* ─── Polling API (pull — for integration into existing loops) ────────── */

/*
 * Drain up to `max_events` events from the ring buffer into `out`.
 * Returns the number of events written.
 * Non-blocking — returns 0 if queue is empty.
 */
uint32_t qwerty_poll(QContext* ctx, QEvent* out, uint32_t max_events);

/*
 * Block until at least one event arrives or timeout_ms elapses.
 * Returns events drained. Pass timeout_ms=0 for infinite wait.
 */
uint32_t qwerty_wait(QContext* ctx, QEvent* out, uint32_t max_events,
                     uint32_t timeout_ms);

/* ─── Event injection (headless / testing / synthetic input) ──────────── */
/* These feed events through the exact same path as real hardware input:
   they update instant key/mouse state, push to the poll ring buffer, and
   fire registered callbacks. Works on any backend, essential for the
   Chlorlite headless sandbox and for automated input testing. */
void qwerty_inject_key(QContext* ctx, QKey key, bool down, QMod mods);
void qwerty_inject_char(QContext* ctx, uint32_t codepoint);
void qwerty_inject_mouse_move(QContext* ctx, int32_t x, int32_t y);
void qwerty_inject_mouse_button(QContext* ctx, QMouseButton btn, bool down, int32_t x, int32_t y);
void qwerty_inject_mouse_scroll(QContext* ctx, float dx, float dy);
void qwerty_inject_event(QContext* ctx, const QEvent* ev);

/* ─── Instant key/mouse state (no event needed) ──────────────────────── */
bool    qwerty_key_down(const QContext* ctx, QKey key);
bool    qwerty_mouse_down(const QContext* ctx, QMouseButton btn);
void    qwerty_mouse_pos(const QContext* ctx, int32_t* x, int32_t* y);
QMod    qwerty_mods(const QContext* ctx);

/* ─── Utility ─────────────────────────────────────────────────────────── */
const char* qwerty_key_name(QKey key);
const char* qwerty_version_string(void);
const char* qwerty_backend_name(const QContext* ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif
