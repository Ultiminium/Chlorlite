#pragma once
/*
 * cc/player.h — Chlorlite character controller (Player/input tier)
 *
 * A kinematic capsule character: consumes a movement intent (a 2D wish-dir in
 * the camera's ground plane + jump) and integrates it into grounded/airborne
 * motion with gravity, acceleration, friction, air control, and a ground query
 * callback. It does NOT use the rigid-body physics solver — this is the
 * responsive, game-feel-first controller most third/first-person games want.
 *
 * Typical loop:
 *   CCCharacter pc; cc_character_init(&pc, cc_character_default());
 *   pc.position = (CCVec3){0, spawn_h, 0};
 *   ...
 *   CCCharInput in = {0};
 *   in.move_x = cc_input_axis(im,"move_x");    // strafe  (-1..1)
 *   in.move_z = cc_input_axis(im,"move_z");    // fwd/back(-1..1)
 *   in.jump   = cc_input_action_pressed(im,"jump");
 *   in.sprint = cc_input_action_held(im,"sprint");
 *   in.yaw_deg = camera_yaw;                    // move relative to camera facing
 *   cc_character_update(&pc, &in, ground_height_fn, user, dt);
 *   // then: draw at pc.position, or feed pc.position to a follow-camera.
 *
 * The ground query lets the controller work over any world: return the ground
 * height (world Y of the surface) directly under (x,z). Return -INFINITY for a
 * bottomless pit. Pass NULL for a flat ground at y=0.
 */

#include "cc/ccmath.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-frame movement intent, all in "input space". */
typedef struct CCCharInput {
    float move_x;    /* strafe, -1 (left) .. +1 (right)   */
    float move_z;    /* forward/back, -1 (back) .. +1 (fwd)*/
    float yaw_deg;   /* facing yaw the move is relative to (usually camera yaw) */
    bool  jump;      /* edge: trigger a jump this frame if grounded/coyote */
    bool  sprint;    /* hold: apply sprint multiplier */
    bool  crouch;    /* hold: apply crouch height/speed  */
} CCCharInput;

/* Tunable movement parameters (game feel). */
typedef struct CCCharConfig {
    float radius;            /* capsule radius */
    float height;            /* standing capsule height (feet→head) */
    float crouch_height;     /* height while crouching */

    float walk_speed;        /* target ground speed */
    float sprint_mult;       /* × walk_speed while sprinting */
    float crouch_mult;       /* × walk_speed while crouching */

    float ground_accel;      /* how fast we reach target vel on ground (1/s) */
    float air_accel;         /* acceleration authority in air (1/s) */
    float friction;          /* ground deceleration when no input (1/s) */

    float gravity;           /* downward accel, units/s^2 (positive number) */
    float jump_height;       /* apex height of a jump, world units */
    float max_fall_speed;    /* terminal velocity clamp */

    float step_offset;       /* max height auto-stepped up without jumping */
    float coyote_time;       /* grace window after leaving ground (s) */
    float jump_buffer;       /* pre-press window before landing (s) */
} CCCharConfig;

/* Full controller state. Position is at the capsule's FEET (bottom sphere
 * center minus radius → the point that rests on the ground). */
typedef struct CCCharacter {
    CCVec3 position;         /* feet position (what rests on ground) */
    CCVec3 velocity;         /* world-space velocity */
    float  facing_yaw;       /* smoothed body yaw (degrees), toward move dir */

    bool   grounded;
    bool   was_grounded;
    float  height;           /* current (interpolated crouch) height */

    /* internal timers */
    float  _coyote;          /* time since grounded */
    float  _jump_buf;        /* time since jump pressed */

    CCCharConfig cfg;
} CCCharacter;

/* Ground height query: return the surface world-Y under (x,z), or -INFINITY
 * for a pit. `user` is passed through from cc_character_update. */
typedef float (*CCGroundFn)(float x, float z, void* user);

/* Sensible defaults for a human-scale character (meters-ish units). */
CCCharConfig cc_character_default(void);

/* Initialize a character with a config (zeroes state, spawns at origin). */
void cc_character_init(CCCharacter* c, CCCharConfig cfg);

/* Integrate one frame. `ground` may be NULL (flat ground at y=0). */
void cc_character_update(CCCharacter* c, const CCCharInput* in,
                         CCGroundFn ground, void* user, float dt);

/* Convenience: build a CCTransform3D-compatible pos/rot for drawing. Writes a
 * quaternion (xyzw) for the body's facing_yaw into out_rot4, and the capsule
 * CENTER position (feet + height/2) into out_pos3 so a capsule mesh sits right.
 * Either pointer may be NULL. */
void cc_character_transform(const CCCharacter* c, float out_pos3[3], float out_rot4[4]);

#ifdef __cplusplus
}
#endif
