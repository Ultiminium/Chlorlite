/* gamepad_test — gamepad input via injection (headless). Verifies connection
 * state, button held/pressed/released edge detection across frames, axis values
 * with deadzone, and trigger axes (0..1, no symmetric deadzone). Edge detection
 * relies on cc_tick snapshotting the previous frame's buttons, so the pattern is
 * tick → inject → query. */
#include "cc/claudecore.h"
#include "cc/input.h"
#include <stdio.h>
#include <math.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)
static int feq(float a,float b){ return fabsf(a-b)<1e-3f; }

int main(void){
    CCEngineConfig cfg=cc_sandbox_config(); cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    /* initially nothing connected */
    CHECK(!cc_gamepad_connected(e,0),"pad 0 not connected initially");

    /* connect pad 0 */
    cc_gamepad_inject_connected(e,0,true);
    CHECK(cc_gamepad_connected(e,0),"pad 0 connected after inject");

    /* ── button press edge ──
       frame layout: tick latches prev=btn(old); then we inject the new state;
       queries then compare new vs latched-old. */
    cc_tick(e,0.016);                         /* latch (A currently up) */
    cc_gamepad_inject_button(e,0,CC_GAMEPAD_A,true);   /* press A this frame */
    CHECK(cc_gamepad_button(e,0,CC_GAMEPAD_A),"A held");
    CHECK(cc_gamepad_button_pressed(e,0,CC_GAMEPAD_A),"A pressed edge");
    CHECK(!cc_gamepad_button_released(e,0,CC_GAMEPAD_A),"A not released");

    /* next frame: A still held → not a fresh press */
    cc_tick(e,0.016);                         /* latch (A now down) */
    CHECK(cc_gamepad_button(e,0,CC_GAMEPAD_A),"A still held");
    CHECK(!cc_gamepad_button_pressed(e,0,CC_GAMEPAD_A),"A not pressed again while held");

    /* release A */
    cc_tick(e,0.016);                         /* latch (A down) */
    cc_gamepad_inject_button(e,0,CC_GAMEPAD_A,false);
    CHECK(!cc_gamepad_button(e,0,CC_GAMEPAD_A),"A released -> not held");
    CHECK(cc_gamepad_button_released(e,0,CC_GAMEPAD_A),"A released edge");

    /* ── axes + deadzone ──
       default deadzone 0.15; a value of 0.10 should read 0 (inside deadzone),
       0.575 should read ~0.5 after deadzone remap ((0.575-0.15)/0.85). */
    cc_gamepad_inject_axis(e,0,CC_GAMEPAD_AXIS_LX,0.10f);
    CHECK(feq(cc_gamepad_axis(e,0,CC_GAMEPAD_AXIS_LX),0.0f),"small axis inside deadzone -> 0");
    cc_gamepad_inject_axis(e,0,CC_GAMEPAD_AXIS_LX,0.575f);
    CHECK(feq(cc_gamepad_axis(e,0,CC_GAMEPAD_AXIS_LX),0.5f),"axis remapped past deadzone");
    cc_gamepad_inject_axis(e,0,CC_GAMEPAD_AXIS_LY,-1.0f);
    CHECK(feq(cc_gamepad_axis(e,0,CC_GAMEPAD_AXIS_LY),-1.0f),"full negative axis");

    /* triggers are 0..1, no symmetric deadzone applied */
    cc_gamepad_inject_axis(e,0,CC_GAMEPAD_AXIS_RT,0.10f);
    CHECK(feq(cc_gamepad_axis(e,0,CC_GAMEPAD_AXIS_RT),0.10f),"trigger not deadzoned");

    /* custom deadzone */
    cc_gamepad_set_deadzone(e,0.5f);
    cc_gamepad_inject_axis(e,0,CC_GAMEPAD_AXIS_RX,0.4f);
    CHECK(feq(cc_gamepad_axis(e,0,CC_GAMEPAD_AXIS_RX),0.0f),"0.4 inside 0.5 deadzone -> 0");

    /* a second pad is independent */
    CHECK(!cc_gamepad_connected(e,1),"pad 1 still unconnected");
    cc_gamepad_inject_button(e,1,CC_GAMEPAD_START,true);
    CHECK(cc_gamepad_connected(e,1),"pad 1 connects on input");
    CHECK(cc_gamepad_button(e,1,CC_GAMEPAD_START),"pad 1 START held");
    CHECK(!cc_gamepad_button(e,0,CC_GAMEPAD_START),"pad 0 START independent (up)");

    cc_shutdown(e);
    if(fails){ printf("GAMEPAD TEST: %d FAILURE(S)\n",fails); return 2; }
    printf("GAMEPAD TEST: all checks passed (connect, button edges, axes, deadzone, multi-pad)\n");
    return 0;
}
