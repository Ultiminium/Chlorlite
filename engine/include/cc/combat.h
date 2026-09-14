#pragma once
/*
 * cc/combat.h — the GAME-FEEL layer for combat.
 *
 * The renderer, physics, animation, and event systems already give you the
 * FUNCTIONAL half of combat (a hit is detected, damage is a number). This module
 * is the other half — the part that makes a hit FEEL good: the tiny, deliberate,
 * heavily-tunable reactions that fire the instant an attack lands.
 *
 *   - HITSTOP    a brief global (or per-entity) freeze on impact — the single most
 *                important "juice" primitive; it sells weight. Measured in seconds.
 *   - SCREENSHAKE trauma added to the camera rig (cc_cam_add_trauma) — scaled by hit
 *                strength. (The shake model itself lives in cc/camera.h; combat just
 *                drives it.)
 *   - KNOCKBACK  an impulse applied away from the attacker, tunable direction + rise.
 *   - HITSTUN    a duration the victim can't act — the window that makes combos and
 *                reactions readable.
 *   - HITLAG per-entity: attacker and victim both freeze their own animation for a
 *                few frames so the contact reads as a real collision, not a pass-through.
 *
 * EVERYTHING is a number you tune. That is the entire design: this module owns the
 * *structure* of good feel (what fires, in what order, driven off one hit event),
 * and exposes the *values* (how many frames, how much trauma, how hard the knock)
 * so they can be dialed in against how it actually feels to play. "AI can't make
 * combat feel good" assumes the feel is ineffable; it isn't — it's this struct.
 *
 * Usage:
 *   CCCombat* cb = cc_combat_create(engine);
 *   cc_combat_set_camera(cb, rig);              // where screenshake goes
 *   ... each frame, BEFORE your sim step:
 *   float scaled_dt = cc_combat_begin_frame(cb, dt);   // applies hitstop
 *   ... run your sim with scaled_dt ...
 *   ... when an attack connects:
 *   CCHit h = { .attacker=a, .victim=v, .damage=12, .strength=1.0f,
 *               .dir_x=kx, .dir_z=kz };
 *   cc_combat_register_hit(cb, &h);             // fires the whole feel cascade
 *   ... query victim reaction:
 *   if (cc_combat_in_hitstun(cb, v)) { ... victim can't act ... }
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CCEngine;
struct CCCameraRig;
struct CCPhysicsWorld;
struct CCEventBus;

typedef struct CCCombat CCCombat;

/* Which built-in reactions fire on a hit. Every reaction is independently
   toggleable so NO behavior is mandatory — a realistic sim can disable hitstop and
   the upward knockback pop; an ability game can keep them; a game with its own
   reaction logic can disable all of them and use on_hit (below). This is the
   "mechanism, not policy" line: the engine provides the reactions, the game picks
   which (if any) apply. */
typedef struct CCFeelEnable {
    bool hitstop;      /* freeze sim time briefly on hit         (default on)  */
    bool screenshake;  /* add camera trauma                      (default on)  */
    bool knockback;    /* compute + (optionally) apply impulse   (default on)  */
    bool knockback_up; /* the upward pop (melee readability)     (default on)  */
    bool hitstun;      /* victim can't-act window                (default on)  */
    bool damage_event; /* emit CC_EVT_DAMAGE                     (default on)  */
} CCFeelEnable;
CCFeelEnable cc_feel_enable_all(void);   /* everything on (the melee/action default) */
CCFeelEnable cc_feel_enable_none(void);  /* everything off (build your own via on_hit) */

/* Tunable feel profile — the knobs. One set of sensible defaults, override freely.
 * These are the numbers you iterate on with a human until it feels right. */
typedef struct CCFeelProfile {
    /* HITSTOP: global time freeze on a hit, seconds, scaled by hit strength.
       ~0.05-0.12s reads as a solid hit; too much feels laggy. */
    float hitstop_base;          /* seconds at strength 1.0 (default 0.06) */
    float hitstop_per_strength;  /* extra seconds per strength unit (default 0.04) */
    float hitstop_max;           /* clamp (default 0.20) */

    /* SCREENSHAKE: trauma (0..1) added per hit, scaled by strength. */
    float shake_trauma;          /* default 0.35 at strength 1.0 */
    float shake_per_strength;    /* default 0.15 */

    /* KNOCKBACK: impulse magnitude away from attacker, scaled by strength. */
    float knockback_base;        /* default 6.0 */
    float knockback_per_strength;/* default 4.0 */
    float knockback_up;          /* small upward pop for readability (default 1.5) */

    /* HITSTUN: seconds the victim cannot act, scaled by strength. */
    float hitstun_base;          /* default 0.18 */
    float hitstun_per_strength;  /* default 0.12 */
} CCFeelProfile;

/* Sensible, good-feeling defaults (tuned to the comments above). */
CCFeelProfile cc_feel_default(void);

/* A single landed hit — the one fact that drives the whole cascade. */
typedef struct CCHit {
    uint64_t attacker;     /* caller entity/actor id (for events; 0 ok) */
    uint64_t victim;       /* caller entity/actor id */
    float    damage;       /* emitted as CC_EVT_DAMAGE */
    float    strength;     /* 0..N multiplier for all feel (1.0 = a normal hit) */
    float    dir_x, dir_z; /* knockback direction (need not be normalized; 0 = auto) */
    uint32_t victim_body;  /* optional physics body id for knockback impulse (0 = none) */
} CCHit;

/* ─── lifecycle ─────────────────────────────────────────────────────────── */
CCCombat* cc_combat_create(struct CCEngine* eng);
void      cc_combat_destroy(CCCombat* cb);
void      cc_combat_set_profile(CCCombat* cb, const CCFeelProfile* p);
CCFeelProfile* cc_combat_profile(CCCombat* cb);   /* mutable — tweak live */
void      cc_combat_set_camera(CCCombat* cb, struct CCCameraRig* rig);   /* shake target */
void      cc_combat_set_physics(CCCombat* cb, struct CCPhysicsWorld* w); /* knockback target */
/* Optional: where to emit CC_EVT_DAMAGE. The engine doesn't own a bus — games
   create their own (cc_event_bus_create); hand it here to get damage events. If
   unset, register_hit still does all the FEEL, just emits no event. */
void      cc_combat_set_event_bus(CCCombat* cb, struct CCEventBus* bus);
/* Choose which built-in reactions fire (default: all on). */
void      cc_combat_set_enable(CCCombat* cb, CCFeelEnable en);
CCFeelEnable* cc_combat_enable(CCCombat* cb);   /* mutable */

/* POLICY HOOK: if set, this runs on every register_hit AFTER the (enabled) built-in
   reactions, receiving the hit + the combat context so a game can add or replace
   behavior — elemental damage, armor-penetration math, a custom stagger, a recoil
   model, whatever the genre needs. Set enable=none to make the built-ins do nothing
   and drive EVERYTHING from here using the mechanism primitives below. This is how
   CC stays genre-neutral: melee/ability games use the built-ins; a realistic sim or
   an exotic ability system supplies its own policy without fighting the engine. */
typedef void (*CCOnHitFn)(CCCombat* cb, const struct CCHit* hit, void* user);
void      cc_combat_set_on_hit(CCCombat* cb, CCOnHitFn fn, void* user);

/* ── mechanism primitives (genre-neutral building blocks) ─────────────────
   These are the individual reactions as standalone calls, so a custom on_hit (or
   any game code) can compose its own feel without the built-in cascade. */
void  cc_combat_add_hitstop(CCCombat* cb, float seconds);           /* freeze sim */
void  cc_combat_add_shake(CCCombat* cb, float trauma);              /* camera trauma */
void  cc_combat_apply_knockback(CCCombat* cb, uint32_t body,
                                float x, float y, float z);         /* impulse (if phys) */
void  cc_combat_set_hitstun(CCCombat* cb, uint64_t victim, float seconds);
void  cc_combat_emit_damage(CCCombat* cb, uint64_t attacker, uint64_t victim, float amount);

/* ─── per-frame ─────────────────────────────────────────────────────────── */
/* Call at the START of each sim step. Returns the dt your sim should actually use:
   normal dt, or 0 (frozen) while hitstop is active. Also decays hitstun timers.
   This is what makes hitstop "just work" — feed the returned dt to your logic. */
float cc_combat_begin_frame(CCCombat* cb, float dt);

/* ─── the hit ───────────────────────────────────────────────────────────── */
/* Register a landed hit: applies hitstop, adds screenshake trauma, applies the
   knockback impulse (if a body/physics world is set), starts the victim's hitstun,
   and emits CC_EVT_DAMAGE on the engine's event bus. One call, whole feel. */
void cc_combat_register_hit(CCCombat* cb, const CCHit* hit);

/* ─── queries ───────────────────────────────────────────────────────────── */
bool  cc_combat_hitstop_active(const CCCombat* cb);
float cc_combat_hitstop_remaining(const CCCombat* cb);
bool  cc_combat_in_hitstun(const CCCombat* cb, uint64_t victim);
float cc_combat_hitstun_remaining(const CCCombat* cb, uint64_t victim);
/* Last knockback impulse computed (for games that apply it themselves to a
   non-physics character controller). Valid immediately after register_hit. */
void  cc_combat_last_knockback(const CCCombat* cb, float* out_x, float* out_y, float* out_z);

#ifdef __cplusplus
}
#endif
