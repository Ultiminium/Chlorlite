/*
 * player.c — Chlorlite kinematic character controller.
 * See cc/player.h for the design contract.
 */
#include "cc/player.h"
#include <math.h>
#include <string.h>
#include <float.h>

CCCharConfig cc_character_default(void) {
    CCCharConfig c;
    memset(&c,0,sizeof c);
    c.radius        = 0.4f;
    c.height        = 1.8f;
    c.crouch_height = 1.0f;
    c.walk_speed    = 5.0f;
    c.sprint_mult   = 1.8f;
    c.crouch_mult   = 0.5f;
    c.ground_accel  = 12.0f;
    c.air_accel     = 3.0f;
    c.friction      = 10.0f;
    c.gravity       = 20.0f;   /* snappier than 9.8 — typical for games */
    c.jump_height   = 1.3f;
    c.max_fall_speed= 40.0f;
    c.step_offset   = 0.35f;
    c.coyote_time   = 0.12f;
    c.jump_buffer   = 0.12f;
    return c;
}

void cc_character_init(CCCharacter* c, CCCharConfig cfg) {
    if (!c) return;
    memset(c,0,sizeof *c);
    c->cfg      = cfg;
    c->height   = cfg.height;
    c->grounded = false;
    c->_coyote  = 1e9f;
    c->_jump_buf= 1e9f;
}

/* flat-ground fallback */
static float flat_ground(float x, float z, void* u){ (void)x;(void)z;(void)u; return 0.0f; }

/* Move `cur` toward `target` at rate `accel` (exponential-ish, dt-correct). */
static float approach(float cur, float target, float accel, float dt) {
    float t = 1.0f - expf(-accel * dt);   /* framerate-independent lerp factor */
    return cur + (target - cur) * t;
}

void cc_character_update(CCCharacter* c, const CCCharInput* in,
                         CCGroundFn ground, void* user, float dt) {
    if (!c || dt <= 0.0f) return;
    if (!ground) ground = flat_ground;
    const CCCharConfig* cfg = &c->cfg;
    CCCharInput z = {0}; if (!in) in = &z;

    /* ── interpolate crouch height ─────────────────────────────────────── */
    float target_h = in->crouch ? cfg->crouch_height : cfg->height;
    c->height = approach(c->height, target_h, 12.0f, dt);

    /* ── build world-space wish direction from input + yaw ─────────────── */
    float yaw = in->yaw_deg * CC_DEG2RAD;
    float sinY = sinf(yaw), cosY = cosf(yaw);
    /* forward = (sin,0,-cos) in a Y-up, -Z-forward convention (matches camera) */
    CCVec3 fwd   = { sinY, 0.0f, -cosY };
    CCVec3 right = { cosY, 0.0f,  sinY };
    CCVec3 wish = vec3_add(vec3_scale(right, in->move_x),
                           vec3_scale(fwd,   in->move_z));
    float wish_len = vec3_len(wish);
    if (wish_len > 1.0f) wish = vec3_scale(wish, 1.0f/wish_len); /* no diagonal boost */
    bool has_input = wish_len > 1e-3f;

    /* target horizontal speed */
    float speed = cfg->walk_speed;
    if (in->sprint) speed *= cfg->sprint_mult;
    if (in->crouch) speed *= cfg->crouch_mult;

    /* ── ground detection (from current feet position) ─────────────────── */
    float gy = ground(c->position.x, c->position.z, user);
    bool  has_ground = (gy > -1e30f);
    float dist_to_ground = c->position.y - gy;
    bool  grounded = has_ground && dist_to_ground <= 0.05f && c->velocity.y <= 0.01f;

    c->was_grounded = c->grounded;
    c->grounded = grounded;
    c->_coyote  = grounded ? 0.0f : c->_coyote + dt;

    /* ── jump buffering ────────────────────────────────────────────────── */
    if (in->jump) c->_jump_buf = 0.0f; else c->_jump_buf += dt;
    bool can_coyote = c->_coyote <= cfg->coyote_time;
    bool want_jump  = c->_jump_buf <= cfg->jump_buffer;
    if (want_jump && can_coyote) {
        /* v = sqrt(2 g h) for a given apex height */
        c->velocity.y = sqrtf(2.0f * cfg->gravity * cfg->jump_height);
        c->grounded = false;
        c->_coyote  = 1e9f;   /* consume */
        c->_jump_buf= 1e9f;
    }

    /* ── horizontal acceleration (ground vs air) ───────────────────────── */
    CCVec3 hvel = { c->velocity.x, 0.0f, c->velocity.z };
    if (c->grounded) {
        if (has_input) {
            CCVec3 target = vec3_scale(wish, speed);
            hvel.x = approach(hvel.x, target.x, cfg->ground_accel, dt);
            hvel.z = approach(hvel.z, target.z, cfg->ground_accel, dt);
        } else {
            /* friction toward zero */
            hvel.x = approach(hvel.x, 0.0f, cfg->friction, dt);
            hvel.z = approach(hvel.z, 0.0f, cfg->friction, dt);
        }
    } else {
        /* limited air control: nudge toward wish but keep momentum */
        if (has_input) {
            CCVec3 target = vec3_scale(wish, speed);
            hvel.x = approach(hvel.x, target.x, cfg->air_accel, dt);
            hvel.z = approach(hvel.z, target.z, cfg->air_accel, dt);
        }
    }
    c->velocity.x = hvel.x;
    c->velocity.z = hvel.z;

    /* ── gravity ───────────────────────────────────────────────────────── */
    if (!c->grounded) {
        c->velocity.y -= cfg->gravity * dt;
        if (c->velocity.y < -cfg->max_fall_speed) c->velocity.y = -cfg->max_fall_speed;
    } else if (c->velocity.y < 0.0f) {
        c->velocity.y = 0.0f;
    }

    /* ── integrate position ────────────────────────────────────────────── */
    c->position.x += c->velocity.x * dt;
    c->position.y += c->velocity.y * dt;
    c->position.z += c->velocity.z * dt;

    /* ── resolve against ground at the NEW xz (handles walking onto slopes
     *    and auto-stepping up small ledges) ───────────────────────────── */
    float ngy = ground(c->position.x, c->position.z, user);
    if (ngy > -1e30f) {
        if (c->position.y <= ngy) {
            /* below/at ground: snap up, land */
            c->position.y = ngy;
            if (c->velocity.y < 0.0f) c->velocity.y = 0.0f;
            c->grounded = true;
            c->_coyote = 0.0f;
        } else if (c->grounded && c->position.y - ngy <= cfg->step_offset) {
            /* small step up while grounded: stick to surface */
            c->position.y = ngy;
        } else if (c->position.y - ngy <= 0.05f) {
            c->grounded = true;
        }
    }

    /* ── body facing: smoothly rotate toward horizontal move direction ─── */
    float hspeed = sqrtf(c->velocity.x*c->velocity.x + c->velocity.z*c->velocity.z);
    if (hspeed > 0.1f) {
        float target_yaw = atan2f(c->velocity.x, -c->velocity.z) * CC_RAD2DEG;
        /* shortest-arc interpolation */
        float d = target_yaw - c->facing_yaw;
        while (d >  180.0f) d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        c->facing_yaw += d * (1.0f - expf(-12.0f * dt));
    }
}

void cc_character_transform(const CCCharacter* c, float out_pos3[3], float out_rot4[4]) {
    if (!c) return;
    if (out_pos3) {
        out_pos3[0] = c->position.x;
        out_pos3[1] = c->position.y + c->height * 0.5f; /* feet → capsule center */
        out_pos3[2] = c->position.z;
    }
    if (out_rot4) {
        CCQuat q = quat_from_axis_angle((CCVec3){0,1,0}, c->facing_yaw * CC_DEG2RAD);
        out_rot4[0]=q.x; out_rot4[1]=q.y; out_rot4[2]=q.z; out_rot4[3]=q.w;
    }
}
