#pragma once
/*
 * cc/physics.h — Chlorlite physics: injectable rigid-body dynamics
 *
 * Design principle: EVERYTHING is swappable. The default is a correct
 * semi-implicit Euler integrator with impulse-based collision resolution, but
 * every stage is a hook you can replace:
 *
 *   - Integrator:        cc_phys_set_integrator(w, my_integrate_fn)
 *   - Force generators:  cc_phys_add_force_generator(w, my_force_fn, userdata)
 *   - Collision response: cc_phys_set_collision_response(w, my_response_fn)
 *   - Broadphase:        cc_phys_set_broadphase(w, my_broadphase_fn)
 *   - Per-body material: restitution / friction / damping all per-body tunable
 *   - Global tunables:   gravity, timestep, solver iterations, sleep threshold
 *
 * If you want completely different physics, replace the integrator and the
 * collision response and the world becomes whatever you define — the built-in
 * behavior is just the default, never a black box.
 */

#include "ccmath.h"
#include "event.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCPhysicsWorld CCPhysicsWorld;
typedef uint32_t CCBodyId;
#define CC_BODY_NULL 0

typedef enum {
    CC_BODY_DYNAMIC,    /* moved by forces + collisions */
    CC_BODY_KINEMATIC,  /* moved by user, pushes dynamics, ignores forces */
    CC_BODY_STATIC,     /* never moves */
} CCBodyType;

typedef enum {
    CC_SHAPE_SPHERE,
    CC_SHAPE_BOX,
    CC_SHAPE_PLANE,     /* infinite half-space, normal = +Y by default */
    CC_SHAPE_CAPSULE,
} CCShapeType;

typedef struct {
    CCShapeType type;
    union {
        struct { float radius; } sphere;
        struct { CCVec3 half_extents; } box;
        struct { CCVec3 normal; float offset; } plane;
        struct { float radius, height; } capsule;
    };
} CCCollider;

/* Per-body material — all tunable per body. */
typedef struct {
    float mass;             /* 0 → infinite (static) */
    float restitution;      /* 0 = inelastic, 1 = perfectly elastic */
    float friction;         /* 0..1 */
    float linear_damping;   /* velocity decay per second */
    float angular_damping;
    bool  gravity_affected; /* opt out of world gravity per body */
} CCBodyMaterial;

typedef struct {
    CCVec3 position;
    CCQuat orientation;
    CCVec3 linear_velocity;
    CCVec3 angular_velocity;
} CCBodyState;

/* A collision contact passed to the (replaceable) response function. */
typedef struct {
    CCBodyId a, b;
    CCVec3   point;         /* world contact point */
    CCVec3   normal;        /* from a → b, unit */
    float    penetration;   /* overlap depth */
} CCContact;

/* ─── Injectable hook signatures ─────────────────────────────────────── */

/* Integrator: advance one body's state by dt given accumulated force/torque.
   Replace to use RK4, Verlet, or anything you want. */
typedef void (*CCIntegratorFn)(CCBodyState* state, const CCBodyMaterial* mat,
                                CCVec3 force_accum, CCVec3 torque_accum,
                                float dt, void* userdata);

/* Force generator: called each step per dynamic body; return the force to add.
   Use for gravity fields, wind, springs, drag, buoyancy — anything. */
typedef CCVec3 (*CCForceGenFn)(CCBodyId body, const CCBodyState* state,
                                const CCBodyMaterial* mat, float dt, void* userdata);

/* Collision response: resolve a contact. Replace to define custom restitution,
   friction models, or non-physical responses (e.g. platformer one-way plats). */
typedef void (*CCCollisionResponseFn)(CCPhysicsWorld* w, const CCContact* c, void* userdata);

/* Broadphase: fill out_pairs with candidate colliding body-id pairs; return count.
   Replace the default O(n²) with a grid/BVH for large scenes. */
typedef uint32_t (*CCBroadphaseFn)(CCPhysicsWorld* w, uint32_t* out_pairs,
                                    uint32_t max_pairs, void* userdata);

/* Contact event callback (observe, don't resolve) — for gameplay (damage, sfx). */
typedef void (*CCContactEventFn)(const CCContact* c, void* userdata);

/* ─── World lifecycle ────────────────────────────────────────────────── */
CCPhysicsWorld* cc_physics_create(void);
void            cc_physics_destroy(CCPhysicsWorld* w);
void            cc_physics_step(CCPhysicsWorld* w, float dt);

/* ─── Global tunables (all changeable at runtime) ────────────────────── */
void   cc_physics_set_gravity(CCPhysicsWorld* w, CCVec3 g);
CCVec3 cc_physics_get_gravity(CCPhysicsWorld* w);
void   cc_physics_set_solver_iterations(CCPhysicsWorld* w, uint32_t iters);
void   cc_physics_set_fixed_timestep(CCPhysicsWorld* w, float dt); /* 0 = variable */
void   cc_physics_set_sleep_threshold(CCPhysicsWorld* w, float linear_sq);

/* ─── Hook injection ─────────────────────────────────────────────────── */
void cc_phys_set_integrator(CCPhysicsWorld* w, CCIntegratorFn fn, void* userdata);
void cc_phys_set_collision_response(CCPhysicsWorld* w, CCCollisionResponseFn fn, void* userdata);
void cc_phys_set_broadphase(CCPhysicsWorld* w, CCBroadphaseFn fn, void* userdata);
uint32_t cc_phys_add_force_generator(CCPhysicsWorld* w, CCForceGenFn fn, void* userdata);
void cc_phys_remove_force_generator(CCPhysicsWorld* w, uint32_t gen_id);
void cc_phys_set_contact_event(CCPhysicsWorld* w, CCContactEventFn fn, void* userdata);

/* OPTIONAL: attach an event bus. When set, each detected contact ALSO publishes
 * (deferred) CC_EVT_CONTACT with sender = body a, i = body b, and f = the impact
 * speed (|relative velocity · contact normal|) — a neutral collision fact a game
 * maps to damage, sfx, or screen shake. Additive: any contact_event callback
 * still fires. Pass NULL to detach. */
void cc_phys_set_event_bus(CCPhysicsWorld* w, CCEventBus* bus);

/* Reset any hook to the built-in default by passing NULL. */

/* ─── Bodies ─────────────────────────────────────────────────────────── */
CCBodyId cc_body_create(CCPhysicsWorld* w, CCBodyType type,
                         CCCollider collider, CCBodyMaterial material,
                         CCVec3 position);
void     cc_body_destroy(CCPhysicsWorld* w, CCBodyId id);

/* Runtime state get/set — everything mutable */
CCBodyState    cc_body_get_state(CCPhysicsWorld* w, CCBodyId id);
void           cc_body_set_state(CCPhysicsWorld* w, CCBodyId id, CCBodyState s);
CCBodyMaterial cc_body_get_material(CCPhysicsWorld* w, CCBodyId id);
void           cc_body_set_material(CCPhysicsWorld* w, CCBodyId id, CCBodyMaterial m);
void           cc_body_set_collider(CCPhysicsWorld* w, CCBodyId id, CCCollider c);

/* Apply forces / impulses */
void cc_body_apply_force(CCPhysicsWorld* w, CCBodyId id, CCVec3 force);
void cc_body_apply_impulse(CCPhysicsWorld* w, CCBodyId id, CCVec3 impulse);
void cc_body_apply_torque(CCPhysicsWorld* w, CCBodyId id, CCVec3 torque);

/* Queries */
bool cc_physics_raycast(CCPhysicsWorld* w, CCVec3 origin, CCVec3 dir,
                        float max_dist, CCBodyId* out_body, CCVec3* out_point, CCVec3* out_normal);
uint32_t cc_physics_body_count(CCPhysicsWorld* w);

/* Sensible default material (mass=1, restitution=0.3, friction=0.5). */
CCBodyMaterial cc_body_material_default(void);

#ifdef __cplusplus
}
#endif
