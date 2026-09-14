#pragma once
/*
 * cc/input_map.h — Chlorlite action binding system
 *
 * Turns raw Qwerty key/mouse/gamepad input into named game ACTIONS that can be
 * rebound at runtime, saved/loaded, and queried by name. This is the layer a
 * game actually talks to — never raw scancodes.
 *
 *   cc_input_bind_action(im, "jump", QKEY_SPACE);
 *   cc_input_bind_action(im, "jump", QKEY_NP0);      // second binding, both work
 *   if (cc_input_action_pressed(im, "jump")) player_jump();
 *
 *   cc_input_bind_axis(im, "move_x", QKEY_D, QKEY_A); // +D / -A → -1..1
 *   float mx = cc_input_axis(im, "move_x");
 *
 * Actions have three query styles:
 *   pressed  — true only on the frame the action went down (edge)
 *   released — true only on the frame it went up (edge)
 *   held     — true every frame while down (level)
 *
 * Contexts let you swap whole binding sets (gameplay / menu / vehicle) so the
 * same key can mean different things in different states.
 */

#include "qwerty/qwerty.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_INPUT_MAX_ACTIONS      128
#define CC_INPUT_MAX_AXES         64
#define CC_INPUT_MAX_BINDINGS     4     /* bindings per action (e.g. key + gamepad) */
#define CC_INPUT_MAX_CONTEXTS     8
#define CC_INPUT_ACTION_NAME_LEN  48

typedef enum CCInputSource {
    CC_SRC_KEY = 0,
    CC_SRC_MOUSE_BUTTON,
    CC_SRC_GAMEPAD_BUTTON,
    CC_SRC_MOUSE_WHEEL,       /* wheel up/down as button-like */
} CCInputSource;

typedef struct {
    CCInputSource source;
    uint32_t      code;       /* QKey / QMouseButton / gamepad button */
} CCBinding;

typedef struct {
    char       name[CC_INPUT_ACTION_NAME_LEN];
    CCBinding  bindings[CC_INPUT_MAX_BINDINGS];
    uint32_t   binding_count;
    /* runtime edge state */
    bool       held;
    bool       held_prev;
} CCAction;

typedef struct {
    char       name[CC_INPUT_ACTION_NAME_LEN];
    /* positive / negative key bindings, and optional gamepad axis */
    CCBinding  positive[CC_INPUT_MAX_BINDINGS];
    CCBinding  negative[CC_INPUT_MAX_BINDINGS];
    uint32_t   positive_count;
    uint32_t   negative_count;
    int32_t    gamepad_axis;   /* -1 if none */
    float      value;          /* current -1..1 */
    float      smoothed;       /* eased value */
    float      smoothing;      /* 0 = instant, higher = smoother */
} CCAxis;

typedef struct CCInputMap CCInputMap;

/* ─── Lifecycle ──────────────────────────────────────────────────────── */
CCInputMap* cc_input_map_create(QContext* qwerty);
void        cc_input_map_destroy(CCInputMap* im);

/* Call once per frame BEFORE querying actions. Advances edge detection and
   pumps the Qwerty event queue into the action state. */
void        cc_input_map_update(CCInputMap* im, float dt);

/* ─── Action binding ─────────────────────────────────────────────────── */
void cc_input_bind_action(CCInputMap* im, const char* action, QKey key);
void cc_input_bind_action_mouse(CCInputMap* im, const char* action, QMouseButton btn);
void cc_input_unbind_action(CCInputMap* im, const char* action);
void cc_input_rebind_action(CCInputMap* im, const char* action, uint32_t binding_idx, QKey new_key);

/* ─── Action query ───────────────────────────────────────────────────── */
bool cc_input_action_held(CCInputMap* im, const char* action);     /* level */
bool cc_input_action_pressed(CCInputMap* im, const char* action);  /* edge down */
bool cc_input_action_released(CCInputMap* im, const char* action); /* edge up */

/* ─── Axis binding + query ───────────────────────────────────────────── */
void  cc_input_bind_axis(CCInputMap* im, const char* axis, QKey positive, QKey negative);
void  cc_input_axis_smoothing(CCInputMap* im, const char* axis, float smoothing);
float cc_input_axis(CCInputMap* im, const char* axis);          /* smoothed -1..1 */
float cc_input_axis_raw(CCInputMap* im, const char* axis);      /* instant -1..1 */

/* ─── Contexts (binding sets) ────────────────────────────────────────── */
/* Push a named context to swap active bindings; pop to restore. */
uint32_t cc_input_context_create(CCInputMap* im, const char* name);
void     cc_input_context_activate(CCInputMap* im, const char* name);
const char* cc_input_active_context(CCInputMap* im);

/* ─── Introspection (for a rebinding menu) ───────────────────────────── */
uint32_t    cc_input_action_count(CCInputMap* im);
const char* cc_input_action_name(CCInputMap* im, uint32_t idx);
const char* cc_input_action_binding_str(CCInputMap* im, const char* action); /* "SPACE, NP0" */
/* Capture the next key pressed — for "press a key to bind" UI. Returns
   QKEY_NONE until a key arrives, then that key (one-shot). */
QKey        cc_input_capture_key(CCInputMap* im);
void        cc_input_begin_capture(CCInputMap* im);

/* ─── Save / load bindings ───────────────────────────────────────────── */
bool cc_input_save_bindings(CCInputMap* im, const char* path);
bool cc_input_load_bindings(CCInputMap* im, const char* path);

#ifdef __cplusplus
}
#endif
