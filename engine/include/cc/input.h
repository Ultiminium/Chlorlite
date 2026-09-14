#pragma once

/* Chlorlite input — thin bridge over Qwerty */
#include <qwerty/qwerty.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCEngine CCEngine;

/* Direct key/mouse state — no event needed */
bool    cc_key_down(CCEngine* eng, QKey key);
bool    cc_key_pressed(CCEngine* eng, QKey key);    /* true only first frame down */
bool    cc_key_released(CCEngine* eng, QKey key);   /* true only first frame up */
bool    cc_mouse_down(CCEngine* eng, QMouseButton btn);
bool    cc_mouse_pressed(CCEngine* eng, QMouseButton btn);
bool    cc_mouse_released(CCEngine* eng, QMouseButton btn);
void    cc_mouse_pos(CCEngine* eng, int32_t* x, int32_t* y);
void    cc_mouse_delta(CCEngine* eng, int32_t* dx, int32_t* dy);
void    cc_mouse_scroll(CCEngine* eng, float* dx, float* dy);
QMod    cc_mods(CCEngine* eng);

/* Event-based access — drain the Qwerty queue */
uint32_t cc_input_poll(CCEngine* eng, QEvent* out, uint32_t max);

/* Inject synthetic input (for headless/Claude testing) */
void cc_input_inject(CCEngine* eng, const QEvent* ev);
void cc_input_inject_key(CCEngine* eng, QKey key, bool down);
void cc_input_inject_mouse_move(CCEngine* eng, int32_t x, int32_t y);
void cc_input_inject_mouse_button(CCEngine* eng, QMouseButton btn, bool down);

/* Access raw Qwerty context */
QContext* cc_input_context(CCEngine* eng);

/* ─── Gamepad ─────────────────────────────────────────────────────────────
 * Engine-side gamepad state, accumulated from Qwerty gamepad events each frame.
 * Buttons use a standard Xbox-style layout; axes are -1..1 (triggers 0..1).
 * Up to CC_MAX_GAMEPADS pads (index 0 = player 1). In headless/sandbox use, drive
 * pads via cc_gamepad_inject_* for tests and deterministic replay. */
#define CC_MAX_GAMEPADS 4

typedef enum {
    CC_GAMEPAD_A = 0, CC_GAMEPAD_B, CC_GAMEPAD_X, CC_GAMEPAD_Y,
    CC_GAMEPAD_LB, CC_GAMEPAD_RB,
    CC_GAMEPAD_BACK, CC_GAMEPAD_START,
    CC_GAMEPAD_LSTICK, CC_GAMEPAD_RSTICK,
    CC_GAMEPAD_DPAD_UP, CC_GAMEPAD_DPAD_DOWN, CC_GAMEPAD_DPAD_LEFT, CC_GAMEPAD_DPAD_RIGHT,
    CC_GAMEPAD_BUTTON_COUNT
} CCGamepadButton;

typedef enum {
    CC_GAMEPAD_AXIS_LX = 0, CC_GAMEPAD_AXIS_LY,   /* left stick  (-1..1) */
    CC_GAMEPAD_AXIS_RX, CC_GAMEPAD_AXIS_RY,       /* right stick (-1..1) */
    CC_GAMEPAD_AXIS_LT, CC_GAMEPAD_AXIS_RT,       /* triggers    ( 0..1) */
    CC_GAMEPAD_AXIS_COUNT
} CCGamepadAxis;

bool  cc_gamepad_connected(CCEngine* eng, uint32_t pad);
bool  cc_gamepad_button(CCEngine* eng, uint32_t pad, CCGamepadButton b);        /* held */
bool  cc_gamepad_button_pressed(CCEngine* eng, uint32_t pad, CCGamepadButton b);  /* this frame */
bool  cc_gamepad_button_released(CCEngine* eng, uint32_t pad, CCGamepadButton b);
float cc_gamepad_axis(CCEngine* eng, uint32_t pad, CCGamepadAxis a);            /* deadzoned */
void  cc_gamepad_set_deadzone(CCEngine* eng, float dz);                          /* default 0.15 */

/* injection for headless testing / replay */
void cc_gamepad_inject_button(CCEngine* eng, uint32_t pad, CCGamepadButton b, bool down);
void cc_gamepad_inject_axis(CCEngine* eng, uint32_t pad, CCGamepadAxis a, float value);
void cc_gamepad_inject_connected(CCEngine* eng, uint32_t pad, bool connected);

#ifdef __cplusplus
}
#endif
