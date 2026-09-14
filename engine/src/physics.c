/*
 * physics.c — Chlorlite injectable rigid-body physics
 *
 * Default pipeline per step:
 *   1. accumulate forces (world gravity + user force generators)
 *   2. integrate (default: semi-implicit Euler; replaceable)
 *   3. broadphase (default: O(n²); replaceable)
 *   4. narrowphase → contacts
 *   5. resolve contacts (default: impulse w/ restitution+friction; replaceable)
 *
 * Every stage marked "replaceable" is a function pointer that falls back to the
 * built-in default when NULL. Nothing is hardcoded that a game can't override.
 */
#include "cc/physics.h"
#include "cc/event.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define CC_MAX_BODIES        4096
#define CC_MAX_FORCE_GENS    32
#define CC_MAX_CONTACTS      8192
#define CC_MAX_PAIRS         16384

typedef struct {
    CCBodyType     type;
    CCCollider     collider;
    CCBodyMaterial material;
    CCBodyState    state;
    CCVec3         force_accum;
    CCVec3         torque_accum;
    bool           sleeping;
    bool           used;
} Body;

typedef struct { CCForceGenFn fn; void* ud; bool used; } ForceGen;

struct CCPhysicsWorld {
    Body      bodies[CC_MAX_BODIES];
    uint32_t  body_count;      /* high-water mark */
    uint32_t  live_count;

    CCVec3    gravity;
    uint32_t  solver_iters;
    float     fixed_dt;        /* 0 = variable */
    float     accumulator;
    float     sleep_threshold;

    /* Hooks (NULL = use default) */
    CCIntegratorFn        integrator;      void* integrator_ud;
    CCCollisionResponseFn response;        void* response_ud;
    CCBroadphaseFn        broadphase;      void* broadphase_ud;
    CCContactEventFn      contact_event;   void* contact_event_ud;
    CCEventBus*           event_bus;       /* optional: emit CC_EVT_CONTACT (NULL=off) */
    ForceGen              force_gens[CC_MAX_FORCE_GENS];

    CCContact contacts[CC_MAX_CONTACTS];
    uint32_t  contact_count;
    uint32_t  pairs[CC_MAX_PAIRS * 2];
};

/* ─── Lifecycle ──────────────────────────────────────────────────────── */
CCPhysicsWorld* cc_physics_create(void) {
    CCPhysicsWorld* w = calloc(1, sizeof(CCPhysicsWorld));
    w->gravity = (CCVec3){0, -9.81f, 0};
    w->solver_iters = 8;
    w->fixed_dt = 1.0f / 60.0f;
    w->sleep_threshold = 0.005f;
    w->body_count = 1;   /* index 0 reserved as null */
    return w;
}
void cc_physics_destroy(CCPhysicsWorld* w) { free(w); }

CCBodyMaterial cc_body_material_default(void) {
    return (CCBodyMaterial){ .mass=1.0f, .restitution=0.3f, .friction=0.5f,
                             .linear_damping=0.01f, .angular_damping=0.05f,
                             .gravity_affected=true };
}

/* ─── Tunables ───────────────────────────────────────────────────────── */
void   cc_physics_set_gravity(CCPhysicsWorld* w, CCVec3 g) { w->gravity = g; }
CCVec3 cc_physics_get_gravity(CCPhysicsWorld* w) { return w->gravity; }
void   cc_physics_set_solver_iterations(CCPhysicsWorld* w, uint32_t i) { w->solver_iters = i ? i : 1; }
void   cc_physics_set_fixed_timestep(CCPhysicsWorld* w, float dt) { w->fixed_dt = dt; }
void   cc_physics_set_sleep_threshold(CCPhysicsWorld* w, float t) { w->sleep_threshold = t; }

/* ─── Hook injection ─────────────────────────────────────────────────── */
void cc_phys_set_integrator(CCPhysicsWorld* w, CCIntegratorFn fn, void* ud) { w->integrator=fn; w->integrator_ud=ud; }
void cc_phys_set_collision_response(CCPhysicsWorld* w, CCCollisionResponseFn fn, void* ud) { w->response=fn; w->response_ud=ud; }
void cc_phys_set_broadphase(CCPhysicsWorld* w, CCBroadphaseFn fn, void* ud) { w->broadphase=fn; w->broadphase_ud=ud; }
void cc_phys_set_contact_event(CCPhysicsWorld* w, CCContactEventFn fn, void* ud) { w->contact_event=fn; w->contact_event_ud=ud; }
void cc_phys_set_event_bus(CCPhysicsWorld* w, CCEventBus* bus) { if(w) w->event_bus=bus; }

uint32_t cc_phys_add_force_generator(CCPhysicsWorld* w, CCForceGenFn fn, void* ud) {
    for (uint32_t i=0;i<CC_MAX_FORCE_GENS;i++)
        if (!w->force_gens[i].used) { w->force_gens[i]=(ForceGen){fn,ud,true}; return i+1; }
    return 0;
}
void cc_phys_remove_force_generator(CCPhysicsWorld* w, uint32_t id) {
    if (id>0 && id<=CC_MAX_FORCE_GENS) w->force_gens[id-1].used=false;
}

/* ─── Bodies ─────────────────────────────────────────────────────────── */
CCBodyId cc_body_create(CCPhysicsWorld* w, CCBodyType type, CCCollider col,
                        CCBodyMaterial mat, CCVec3 pos) {
    uint32_t slot = 0;
    for (uint32_t i=1;i<w->body_count;i++) if (!w->bodies[i].used) { slot=i; break; }
    if (!slot) { if (w->body_count>=CC_MAX_BODIES) return CC_BODY_NULL; slot=w->body_count++; }
    Body* b = &w->bodies[slot];
    memset(b, 0, sizeof(*b));
    b->type=type; b->collider=col; b->material=mat;
    b->state.position=pos;
    b->state.orientation=(CCQuat){0,0,0,1};
    if (type!=CC_BODY_DYNAMIC) b->material.mass=0.0f; /* static/kinematic = infinite mass */
    b->used=true;
    w->live_count++;
    return slot;
}
void cc_body_destroy(CCPhysicsWorld* w, CCBodyId id) {
    if (id>0 && id<w->body_count && w->bodies[id].used) { w->bodies[id].used=false; w->live_count--; }
}

CCBodyState cc_body_get_state(CCPhysicsWorld* w, CCBodyId id) {
    if (id>0 && id<w->body_count && w->bodies[id].used) return w->bodies[id].state;
    return (CCBodyState){0};
}
void cc_body_set_state(CCPhysicsWorld* w, CCBodyId id, CCBodyState s) {
    if (id>0 && id<w->body_count && w->bodies[id].used) { w->bodies[id].state=s; w->bodies[id].sleeping=false; }
}
CCBodyMaterial cc_body_get_material(CCPhysicsWorld* w, CCBodyId id) {
    if (id>0 && id<w->body_count && w->bodies[id].used) return w->bodies[id].material;
    return cc_body_material_default();
}
void cc_body_set_material(CCPhysicsWorld* w, CCBodyId id, CCBodyMaterial m) {
    if (id>0 && id<w->body_count && w->bodies[id].used) w->bodies[id].material=m;
}
void cc_body_set_collider(CCPhysicsWorld* w, CCBodyId id, CCCollider c) {
    if (id>0 && id<w->body_count && w->bodies[id].used) w->bodies[id].collider=c;
}
void cc_body_apply_force(CCPhysicsWorld* w, CCBodyId id, CCVec3 f) {
    if (id>0 && id<w->body_count && w->bodies[id].used) { w->bodies[id].force_accum=vec3_add(w->bodies[id].force_accum,f); w->bodies[id].sleeping=false; }
}
void cc_body_apply_impulse(CCPhysicsWorld* w, CCBodyId id, CCVec3 imp) {
    if (id>0 && id<w->body_count && w->bodies[id].used && w->bodies[id].material.mass>0) {
        Body* b=&w->bodies[id];
        b->state.linear_velocity=vec3_add(b->state.linear_velocity, vec3_scale(imp, 1.0f/b->material.mass));
        b->sleeping=false;
    }
}
void cc_body_apply_torque(CCPhysicsWorld* w, CCBodyId id, CCVec3 t) {
    if (id>0 && id<w->body_count && w->bodies[id].used) w->bodies[id].torque_accum=vec3_add(w->bodies[id].torque_accum,t);
}
uint32_t cc_physics_body_count(CCPhysicsWorld* w) { return w->live_count; }

/* ─── Default integrator (semi-implicit Euler) ───────────────────────── */
static void default_integrate(CCBodyState* s, const CCBodyMaterial* m,
                              CCVec3 force, CCVec3 torque, float dt, void* ud) {
    (void)ud;
    if (m->mass <= 0.0f) return;  /* infinite mass = immovable */
    CCVec3 accel = vec3_scale(force, 1.0f/m->mass);
    s->linear_velocity = vec3_add(s->linear_velocity, vec3_scale(accel, dt));
    /* damping */
    s->linear_velocity = vec3_scale(s->linear_velocity, 1.0f/(1.0f + m->linear_damping*dt));
    s->position = vec3_add(s->position, vec3_scale(s->linear_velocity, dt));
    /* angular (simplified: torque acts on unit inertia) */
    s->angular_velocity = vec3_add(s->angular_velocity, vec3_scale(torque, dt));
    s->angular_velocity = vec3_scale(s->angular_velocity, 1.0f/(1.0f + m->angular_damping*dt));
    /* integrate orientation: q += 0.5 * w * q * dt */
    CCQuat wq = {s->angular_velocity.x, s->angular_velocity.y, s->angular_velocity.z, 0};
    CCQuat dq = quat_mul(wq, s->orientation);
    s->orientation.x += 0.5f*dq.x*dt; s->orientation.y += 0.5f*dq.y*dt;
    s->orientation.z += 0.5f*dq.z*dt; s->orientation.w += 0.5f*dq.w*dt;
    s->orientation = quat_norm(s->orientation);
}

/* ─── Narrowphase helpers ────────────────────────────────────────────── */

/* Capsule is a segment along local +Y of length `height`, centered at the
 * body position, with hemispherical caps of `radius`. Returns the two segment
 * endpoints (cap centers) in world space (ignores orientation for now — capsules
 * are treated as Y-axis-aligned, matching the character/mesh convention). */
static void capsule_segment(const Body* c, CCVec3* p0, CCVec3* p1) {
    float hh = c->collider.capsule.height * 0.5f;
    *p0 = (CCVec3){ c->state.position.x, c->state.position.y - hh, c->state.position.z };
    *p1 = (CCVec3){ c->state.position.x, c->state.position.y + hh, c->state.position.z };
}

/* Closest point on segment [a,b] to point p. */
static CCVec3 closest_on_segment(CCVec3 a, CCVec3 b, CCVec3 p) {
    CCVec3 ab = vec3_sub(b, a);
    float t = vec3_dot(vec3_sub(p, a), ab) / fmaxf(vec3_dot(ab, ab), 1e-12f);
    t = cc_clamp(t, 0.0f, 1.0f);
    return vec3_add(a, vec3_scale(ab, t));
}

/* Closest points between two segments [p1,q1] and [p2,q2]. */
static void closest_segment_segment(CCVec3 p1, CCVec3 q1, CCVec3 p2, CCVec3 q2,
                                    CCVec3* c1, CCVec3* c2) {
    CCVec3 d1 = vec3_sub(q1, p1), d2 = vec3_sub(q2, p2), r = vec3_sub(p1, p2);
    float a = vec3_dot(d1, d1), e = vec3_dot(d2, d2), f = vec3_dot(d2, r);
    float s, t;
    if (a <= 1e-12f && e <= 1e-12f) { *c1 = p1; *c2 = p2; return; }
    if (a <= 1e-12f) { s = 0.0f; t = cc_clamp(f/e, 0, 1); }
    else {
        float c = vec3_dot(d1, r);
        if (e <= 1e-12f) { t = 0.0f; s = cc_clamp(-c/a, 0, 1); }
        else {
            float b = vec3_dot(d1, d2), denom = a*e - b*b;
            s = denom > 1e-12f ? cc_clamp((b*f - c*e)/denom, 0, 1) : 0.0f;
            t = (b*s + f)/e;
            if (t < 0) { t = 0; s = cc_clamp(-c/a, 0, 1); }
            else if (t > 1) { t = 1; s = cc_clamp((b - c)/a, 0, 1); }
        }
    }
    *c1 = vec3_add(p1, vec3_scale(d1, s));
    *c2 = vec3_add(p2, vec3_scale(d2, t));
}

/* Closest point on an axis-aligned box (center bc, half-extents he) to point p. */
static CCVec3 closest_on_box(CCVec3 bc, CCVec3 he, CCVec3 p) {
    CCVec3 d = vec3_sub(p, bc);
    return (CCVec3){ bc.x + cc_clamp(d.x, -he.x, he.x),
                     bc.y + cc_clamp(d.y, -he.y, he.y),
                     bc.z + cc_clamp(d.z, -he.z, he.z) };
}

/* Emit a contact from a sphere-like (center s, radius sr) vs a nearest point
 * `cp` on the other shape, with `out->normal` pointing a→b. `a_is_sphere` tells
 * which side the sphere is so the normal orientation stays a→b. */
static bool sphere_point_contact(CCVec3 s, float sr, CCVec3 cp, bool a_is_sphere,
                                 CCContact* out) {
    CCVec3 d = vec3_sub(cp, s);
    float dist = vec3_len(d);
    if (dist >= sr) return false;
    CCVec3 n = dist > 1e-6f ? vec3_scale(d, 1.0f/dist) : (CCVec3){0,1,0}; /* s→cp */
    /* n currently points sphere→other. If sphere is body a, a→b = n; else flip. */
    out->normal = a_is_sphere ? n : vec3_neg(n);
    out->penetration = sr - dist;
    out->point = vec3_add(s, vec3_scale(n, sr - out->penetration*0.5f));
    return true;
}

/* ─── Narrowphase primitives ─────────────────────────────────────────── */
static bool collide(const Body* a, const Body* b, CCContact* out) {
    /* Sphere-sphere */
    if (a->collider.type==CC_SHAPE_SPHERE && b->collider.type==CC_SHAPE_SPHERE) {
        CCVec3 d = vec3_sub(b->state.position, a->state.position);
        float dist = vec3_len(d);
        float r = a->collider.sphere.radius + b->collider.sphere.radius;
        if (dist < r && dist > 1e-6f) {
            out->normal = vec3_scale(d, 1.0f/dist);
            out->penetration = r - dist;
            out->point = vec3_add(a->state.position, vec3_scale(out->normal, a->collider.sphere.radius));
            return true;
        }
        return false;
    }
    /* Sphere-plane (plane may be a or b) */
    const Body *sp=NULL,*pl=NULL; bool swap=false;
    if (a->collider.type==CC_SHAPE_SPHERE && b->collider.type==CC_SHAPE_PLANE) { sp=a; pl=b; }
    else if (a->collider.type==CC_SHAPE_PLANE && b->collider.type==CC_SHAPE_SPHERE) { sp=b; pl=a; swap=true; }
    if (sp && pl) {
        CCVec3 n = vec3_norm(pl->collider.plane.normal);
        float dist = vec3_dot(sp->state.position, n) - pl->collider.plane.offset;
        if (dist < sp->collider.sphere.radius) {
            /* normal points from body a(ia) to body b(ib). swap=true means a=plane,b=sphere
               → plane→sphere = +n (up). swap=false means a=sphere,b=plane → sphere→plane = -n. */
            out->normal = swap ? n : vec3_neg(n);
            out->penetration = sp->collider.sphere.radius - dist;
            out->point = vec3_sub(sp->state.position, vec3_scale(n, dist));
            return true;
        }
        return false;
    }
    /* Box-plane */
    const Body *bx=NULL; pl=NULL; swap=false;
    if (a->collider.type==CC_SHAPE_BOX && b->collider.type==CC_SHAPE_PLANE) { bx=a; pl=b; }
    else if (a->collider.type==CC_SHAPE_PLANE && b->collider.type==CC_SHAPE_BOX) { bx=b; pl=a; swap=true; }
    if (bx && pl) {
        CCVec3 n = vec3_norm(pl->collider.plane.normal);
        CCVec3 he = bx->collider.box.half_extents;
        /* projected radius of box onto normal (AABB approximation) */
        float r = fabsf(he.x*n.x) + fabsf(he.y*n.y) + fabsf(he.z*n.z);
        float dist = vec3_dot(bx->state.position, n) - pl->collider.plane.offset;
        if (dist < r) {
            out->normal = swap ? n : vec3_neg(n);
            out->penetration = r - dist;
            out->point = vec3_sub(bx->state.position, vec3_scale(n, dist));
            return true;
        }
        return false;
    }
    /* Box-box (AABB overlap) */
    if (a->collider.type==CC_SHAPE_BOX && b->collider.type==CC_SHAPE_BOX) {
        CCVec3 d = vec3_sub(b->state.position, a->state.position);
        CCVec3 ov = { a->collider.box.half_extents.x + b->collider.box.half_extents.x - fabsf(d.x),
                      a->collider.box.half_extents.y + b->collider.box.half_extents.y - fabsf(d.y),
                      a->collider.box.half_extents.z + b->collider.box.half_extents.z - fabsf(d.z) };
        if (ov.x>0 && ov.y>0 && ov.z>0) {
            /* least-penetration axis */
            if (ov.x<=ov.y && ov.x<=ov.z) { out->normal=(CCVec3){d.x<0?-1.0f:1.0f,0,0}; out->penetration=ov.x; }
            else if (ov.y<=ov.z)          { out->normal=(CCVec3){0,d.y<0?-1.0f:1.0f,0}; out->penetration=ov.y; }
            else                          { out->normal=(CCVec3){0,0,d.z<0?-1.0f:1.0f}; out->penetration=ov.z; }
            out->point = vec3_add(a->state.position, vec3_scale(d, 0.5f));
            return true;
        }
        return false;
    }
    /* Sphere-box (box may be a or b), AABB box */
    {
        const Body *s=NULL,*bx2=NULL; bool sphere_is_a=false;
        if (a->collider.type==CC_SHAPE_SPHERE && b->collider.type==CC_SHAPE_BOX) { s=a; bx2=b; sphere_is_a=true; }
        else if (a->collider.type==CC_SHAPE_BOX && b->collider.type==CC_SHAPE_SPHERE) { s=b; bx2=a; sphere_is_a=false; }
        if (s && bx2) {
            CCVec3 cp = closest_on_box(bx2->state.position, bx2->collider.box.half_extents, s->state.position);
            return sphere_point_contact(s->state.position, s->collider.sphere.radius, cp, sphere_is_a, out);
        }
    }
    /* Capsule-plane (plane may be a or b) */
    {
        const Body *cap=NULL,*pl2=NULL; bool cap_is_a=false;
        if (a->collider.type==CC_SHAPE_CAPSULE && b->collider.type==CC_SHAPE_PLANE) { cap=a; pl2=b; cap_is_a=true; }
        else if (a->collider.type==CC_SHAPE_PLANE && b->collider.type==CC_SHAPE_CAPSULE) { cap=b; pl2=a; cap_is_a=false; }
        if (cap && pl2) {
            CCVec3 p0,p1; capsule_segment(cap,&p0,&p1);
            CCVec3 n = vec3_norm(pl2->collider.plane.normal);
            float d0 = vec3_dot(p0,n) - pl2->collider.plane.offset;
            float d1 = vec3_dot(p1,n) - pl2->collider.plane.offset;
            float dist = fminf(d0,d1);                     /* nearest cap to plane */
            float r = cap->collider.capsule.radius;
            if (dist < r) {
                /* plane→capsule is +n; a→b orientation: if capsule is a, a→b = -n */
                out->normal = cap_is_a ? vec3_neg(n) : n;
                out->penetration = r - dist;
                CCVec3 lowest = (d0<d1)?p0:p1;
                out->point = vec3_sub(lowest, vec3_scale(n, dist));
                return true;
            }
            return false;
        }
    }
    /* Capsule-sphere (either order) */
    {
        const Body *cap=NULL,*s=NULL; bool cap_is_a=false;
        if (a->collider.type==CC_SHAPE_CAPSULE && b->collider.type==CC_SHAPE_SPHERE) { cap=a; s=b; cap_is_a=true; }
        else if (a->collider.type==CC_SHAPE_SPHERE && b->collider.type==CC_SHAPE_CAPSULE) { cap=b; s=a; cap_is_a=false; }
        if (cap && s) {
            CCVec3 p0,p1; capsule_segment(cap,&p0,&p1);
            CCVec3 cp = closest_on_segment(p0,p1,s->state.position);
            /* treat the capsule's nearest point as a sphere of capsule.radius */
            CCVec3 d = vec3_sub(s->state.position, cp);
            float dist = vec3_len(d);
            float rr = cap->collider.capsule.radius + s->collider.sphere.radius;
            if (dist < rr && dist > 1e-9f) {
                CCVec3 n = vec3_scale(d,1.0f/dist);   /* cap→sphere */
                out->normal = cap_is_a ? n : vec3_neg(n);
                out->penetration = rr - dist;
                out->point = vec3_add(cp, vec3_scale(n, cap->collider.capsule.radius));
                return true;
            }
            return false;
        }
    }
    /* Capsule-box (either order), AABB box */
    {
        const Body *cap=NULL,*bx2=NULL; bool cap_is_a=false;
        if (a->collider.type==CC_SHAPE_CAPSULE && b->collider.type==CC_SHAPE_BOX) { cap=a; bx2=b; cap_is_a=true; }
        else if (a->collider.type==CC_SHAPE_BOX && b->collider.type==CC_SHAPE_CAPSULE) { cap=b; bx2=a; cap_is_a=false; }
        if (cap && bx2) {
            CCVec3 p0,p1; capsule_segment(cap,&p0,&p1);
            /* approximate: closest box point to the segment midpoint, then closest
               segment point to that — one refinement step is enough for gameplay */
            CCVec3 mid = vec3_scale(vec3_add(p0,p1),0.5f);
            CCVec3 bp  = closest_on_box(bx2->state.position, bx2->collider.box.half_extents, mid);
            CCVec3 sp2 = closest_on_segment(p0,p1,bp);
            bp = closest_on_box(bx2->state.position, bx2->collider.box.half_extents, sp2);
            CCVec3 d = vec3_sub(bp, sp2);
            float dist = vec3_len(d);
            float r = cap->collider.capsule.radius;
            if (dist < r && dist > 1e-9f) {
                CCVec3 n = vec3_scale(d,1.0f/dist);   /* cap→box */
                out->normal = cap_is_a ? n : vec3_neg(n);
                out->penetration = r - dist;
                out->point = bp;
                return true;
            }
            /* segment center inside the box (dist==0): push out along min axis */
            if (dist <= 1e-9f) {
                out->normal = cap_is_a ? (CCVec3){0,-1,0} : (CCVec3){0,1,0};
                out->penetration = r;
                out->point = sp2;
                return true;
            }
            return false;
        }
    }
    /* Capsule-capsule */
    if (a->collider.type==CC_SHAPE_CAPSULE && b->collider.type==CC_SHAPE_CAPSULE) {
        CCVec3 a0,a1,b0,b1; capsule_segment(a,&a0,&a1); capsule_segment(b,&b0,&b1);
        CCVec3 ca,cb; closest_segment_segment(a0,a1,b0,b1,&ca,&cb);
        CCVec3 d = vec3_sub(cb,ca);
        float dist = vec3_len(d);
        float rr = a->collider.capsule.radius + b->collider.capsule.radius;
        if (dist < rr && dist > 1e-9f) {
            out->normal = vec3_scale(d,1.0f/dist);   /* a→b */
            out->penetration = rr - dist;
            out->point = vec3_add(ca, vec3_scale(out->normal, a->collider.capsule.radius));
            return true;
        }
        return false;
    }
    return false;
}
static void default_response(CCPhysicsWorld* w, const CCContact* c, void* ud) {
    (void)ud;
    Body* a=&w->bodies[c->a]; Body* b=&w->bodies[c->b];
    float inv_ma = a->material.mass>0 ? 1.0f/a->material.mass : 0.0f;
    float inv_mb = b->material.mass>0 ? 1.0f/b->material.mass : 0.0f;
    float inv_sum = inv_ma + inv_mb;
    if (inv_sum <= 0.0f) return;

    /* Relative velocity along normal */
    CCVec3 rv = vec3_sub(b->state.linear_velocity, a->state.linear_velocity);
    float vn = vec3_dot(rv, c->normal);
    if (vn > 0) return;   /* separating already */

    float e = fminf(a->material.restitution, b->material.restitution);
    float j = -(1.0f + e) * vn / inv_sum;
    CCVec3 impulse = vec3_scale(c->normal, j);
    a->state.linear_velocity = vec3_sub(a->state.linear_velocity, vec3_scale(impulse, inv_ma));
    b->state.linear_velocity = vec3_add(b->state.linear_velocity, vec3_scale(impulse, inv_mb));

    /* Coulomb friction (tangential) */
    rv = vec3_sub(b->state.linear_velocity, a->state.linear_velocity);
    CCVec3 tangent = vec3_sub(rv, vec3_scale(c->normal, vec3_dot(rv, c->normal)));
    float tl = vec3_len(tangent);
    if (tl > 1e-6f) {
        tangent = vec3_scale(tangent, 1.0f/tl);
        float jt = -vec3_dot(rv, tangent) / inv_sum;
        float mu = sqrtf(a->material.friction * b->material.friction);
        if (jt > j*mu) jt = j*mu; if (jt < -j*mu) jt = -j*mu;
        CCVec3 fimp = vec3_scale(tangent, jt);
        a->state.linear_velocity = vec3_sub(a->state.linear_velocity, vec3_scale(fimp, inv_ma));
        b->state.linear_velocity = vec3_add(b->state.linear_velocity, vec3_scale(fimp, inv_mb));
    }

    /* Positional correction (Baumgarte) to fix sinking */
    const float slop = 0.01f, percent = 0.4f;
    float corr_mag = fmaxf(c->penetration - slop, 0.0f) / inv_sum * percent;
    CCVec3 corr = vec3_scale(c->normal, corr_mag);
    a->state.position = vec3_sub(a->state.position, vec3_scale(corr, inv_ma));
    b->state.position = vec3_add(b->state.position, vec3_scale(corr, inv_mb));
}

/* ─── Default broadphase (O(n²)) ─────────────────────────────────────── */
static uint32_t default_broadphase(CCPhysicsWorld* w, uint32_t* out, uint32_t max, void* ud) {
    (void)ud;
    uint32_t n=0;
    for (uint32_t i=1;i<w->body_count;i++) {
        if (!w->bodies[i].used) continue;
        for (uint32_t j=i+1;j<w->body_count;j++) {
            if (!w->bodies[j].used) continue;
            /* skip static-static */
            if (w->bodies[i].material.mass<=0 && w->bodies[j].material.mass<=0) continue;
            if (n*2+1 >= max) return n;
            out[n*2]=i; out[n*2+1]=j; n++;
        }
    }
    return n;
}

/* ─── Step ───────────────────────────────────────────────────────────── */
static void step_once(CCPhysicsWorld* w, float dt) {
    CCIntegratorFn integrate = w->integrator ? w->integrator : default_integrate;
    CCCollisionResponseFn respond = w->response ? w->response : default_response;
    CCBroadphaseFn broad = w->broadphase ? w->broadphase : default_broadphase;

    /* 1. accumulate forces */
    for (uint32_t i=1;i<w->body_count;i++) {
        Body* b=&w->bodies[i];
        if (!b->used || b->type!=CC_BODY_DYNAMIC || b->sleeping) continue;
        if (b->material.gravity_affected && b->material.mass>0)
            b->force_accum = vec3_add(b->force_accum, vec3_scale(w->gravity, b->material.mass));
        for (uint32_t g=0;g<CC_MAX_FORCE_GENS;g++)
            if (w->force_gens[g].used)
                b->force_accum = vec3_add(b->force_accum,
                    w->force_gens[g].fn(i, &b->state, &b->material, dt, w->force_gens[g].ud));
    }

    /* 2. integrate */
    for (uint32_t i=1;i<w->body_count;i++) {
        Body* b=&w->bodies[i];
        if (!b->used || b->type!=CC_BODY_DYNAMIC || b->sleeping) continue;
        integrate(&b->state, &b->material, b->force_accum, b->torque_accum, dt,
                  w->integrator ? w->integrator_ud : NULL);
        b->force_accum=(CCVec3){0,0,0}; b->torque_accum=(CCVec3){0,0,0};
    }

    /* 3+4. broadphase + narrowphase → contacts */
    w->contact_count=0;
    uint32_t np = broad(w, w->pairs, CC_MAX_PAIRS, w->broadphase ? w->broadphase_ud : NULL);
    for (uint32_t p=0;p<np;p++) {
        uint32_t ia=w->pairs[p*2], ib=w->pairs[p*2+1];
        CCContact c={0}; c.a=ia; c.b=ib;
        if (collide(&w->bodies[ia], &w->bodies[ib], &c)) {
            if (w->contact_count<CC_MAX_CONTACTS) w->contacts[w->contact_count++]=c;
            if (w->contact_event) w->contact_event(&c, w->contact_event_ud);
            if (w->event_bus) {
                /* impact speed = |relative velocity · contact normal| — the
                 * quantity a game maps to damage/sfx volume. Neutral fact only;
                 * the engine does not interpret it as damage itself. */
                CCVec3 rv = vec3_sub(w->bodies[ib].state.linear_velocity,
                                     w->bodies[ia].state.linear_velocity);
                float impact = vec3_dot(rv, c.normal);
                if (impact < 0.0f) impact = -impact;
                cc_event_emit_if(w->event_bus, CC_EVT_CONTACT,
                                 (uint64_t)c.a, (int64_t)c.b, impact);
            }
        }
    }

    /* 5. resolve (iterated) */
    for (uint32_t it=0; it<w->solver_iters; it++)
        for (uint32_t c=0;c<w->contact_count;c++)
            respond(w, &w->contacts[c], w->response ? w->response_ud : NULL);

    /* sleep bodies at rest */
    for (uint32_t i=1;i<w->body_count;i++) {
        Body* b=&w->bodies[i];
        if (!b->used || b->type!=CC_BODY_DYNAMIC) continue;
        float v2 = vec3_dot(b->state.linear_velocity, b->state.linear_velocity);
        b->sleeping = (v2 < w->sleep_threshold);
    }
}

void cc_physics_step(CCPhysicsWorld* w, float dt) {
    if (w->fixed_dt > 0.0f) {
        w->accumulator += dt;
        int guard=0;
        while (w->accumulator >= w->fixed_dt && guard++ < 8) {
            step_once(w, w->fixed_dt);
            w->accumulator -= w->fixed_dt;
        }
    } else {
        step_once(w, dt);
    }
}

/* ─── Raycast (sphere/plane/box) ─────────────────────────────────────── */
bool cc_physics_raycast(CCPhysicsWorld* w, CCVec3 o, CCVec3 d, float max_dist,
                        CCBodyId* out_body, CCVec3* out_point, CCVec3* out_normal) {
    d = vec3_norm(d);
    float best = max_dist; bool hit=false; CCBodyId hb=0; CCVec3 hp={0}, hn={0};
    for (uint32_t i=1;i<w->body_count;i++) {
        Body* b=&w->bodies[i]; if (!b->used) continue;
        if (b->collider.type==CC_SHAPE_SPHERE) {
            CCVec3 m = vec3_sub(o, b->state.position);
            float bb = vec3_dot(m, d);
            float cc = vec3_dot(m,m) - b->collider.sphere.radius*b->collider.sphere.radius;
            if (cc>0 && bb>0) continue;
            float disc = bb*bb - cc;
            if (disc<0) continue;
            float t = -bb - sqrtf(disc);
            if (t<0) t=0;
            if (t<best) { best=t; hit=true; hb=i; hp=vec3_add(o,vec3_scale(d,t)); hn=vec3_norm(vec3_sub(hp,b->state.position)); }
        } else if (b->collider.type==CC_SHAPE_PLANE) {
            CCVec3 n=vec3_norm(b->collider.plane.normal);
            float dn=vec3_dot(d,n);
            if (fabsf(dn)<1e-6f) continue;
            float t=(b->collider.plane.offset - vec3_dot(o,n))/dn;
            if (t>=0 && t<best) { best=t; hit=true; hb=i; hp=vec3_add(o,vec3_scale(d,t)); hn=n; }
        } else if (b->collider.type==CC_SHAPE_BOX) {
            /* slab method against an AABB centered at body position */
            CCVec3 c=b->state.position, he=b->collider.box.half_extents;
            float tmin=0.0f, tmax=best; CCVec3 nrm={0,0,0}; bool ok=true;
            const float* dd=&d.x; const float* oo=&o.x; const float* cc2=&c.x; const float* hh=&he.x;
            for (int ax=0; ax<3; ax++) {
                float lo=cc2[ax]-hh[ax], hi=cc2[ax]+hh[ax];
                if (fabsf(dd[ax])<1e-8f) { if (oo[ax]<lo||oo[ax]>hi){ok=false;break;} continue; }
                float inv=1.0f/dd[ax];
                float t1=(lo-oo[ax])*inv, t2=(hi-oo[ax])*inv;
                float sign=-1.0f; if (t1>t2){ float tmp=t1;t1=t2;t2=tmp; sign=1.0f; }
                if (t1>tmin){ tmin=t1; nrm=(CCVec3){0,0,0}; ((float*)&nrm.x)[ax]=sign; }
                if (t2<tmax) tmax=t2;
                if (tmin>tmax){ok=false;break;}
            }
            if (ok && tmin>=0 && tmin<best) { best=tmin; hit=true; hb=i; hp=vec3_add(o,vec3_scale(d,tmin)); hn=nrm; }
        } else if (b->collider.type==CC_SHAPE_CAPSULE) {
            /* sample distance from ray to the capsule segment; refine the nearest
               approach, then solve the sphere-radius crossing there */
            CCVec3 p0,p1; capsule_segment(b,&p0,&p1);
            float r=b->collider.capsule.radius;
            /* coarse march to bracket the closest approach along the ray */
            float t_near=0; float best_d2=1e30f;
            for (int k=0;k<=64;k++){
                float t=(best)*k/64.0f;
                CCVec3 pt=vec3_add(o,vec3_scale(d,t));
                CCVec3 cp=closest_on_segment(p0,p1,pt);
                float d2=vec3_len2(vec3_sub(pt,cp));
                if (d2<best_d2){best_d2=d2;t_near=t;}
            }
            if (best_d2 <= r*r) {
                /* step back to the surface entry */
                float t=t_near;
                for (int k=0;k<24 && t>0;k--){
                    CCVec3 pt=vec3_add(o,vec3_scale(d,t));
                    CCVec3 cp=closest_on_segment(p0,p1,pt);
                    if (vec3_len2(vec3_sub(pt,cp)) > r*r) break;
                    t -= best*0.01f;
                }
                if (t<0) t=0;
                if (t<best){ best=t; hit=true; hb=i; hp=vec3_add(o,vec3_scale(d,t));
                    CCVec3 cp=closest_on_segment(p0,p1,hp); hn=vec3_norm(vec3_sub(hp,cp)); }
            }
        }
    }
    if (hit) { if(out_body)*out_body=hb; if(out_point)*out_point=hp; if(out_normal)*out_normal=hn; }
    return hit;
}
