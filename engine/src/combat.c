/*
 * combat.c — implementation of the game-feel layer (see cc/combat.h).
 *
 * The whole point: cc_combat_register_hit fires the entire feel cascade from one
 * call, and cc_combat_begin_frame makes hitstop transparent by returning a scaled
 * dt. Everything is driven by the CCFeelProfile numbers so it can be tuned.
 */
#include "cc/combat.h"
#include "cc/camera.h"
#include "cc/physics.h"
#include "cc/event.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* small fixed map victim_id -> hitstun seconds remaining */
#define CC_HITSTUN_SLOTS 64
typedef struct { uint64_t id; float remaining; bool used; } StunSlot;

struct CCCombat {
    struct CCEngine*       eng;
    struct CCCameraRig*    cam;
    struct CCPhysicsWorld* phys;
    struct CCEventBus*     bus;
    CCFeelProfile          feel;
    CCFeelEnable           en;
    CCOnHitFn              on_hit;
    void*                  on_hit_user;

    float hitstop_remaining;   /* seconds of global freeze left */
    StunSlot stun[CC_HITSTUN_SLOTS];
    float last_kb[3];          /* last knockback impulse computed */
};

CCFeelEnable cc_feel_enable_all(void){
    return (CCFeelEnable){ .hitstop=true, .screenshake=true, .knockback=true,
                           .knockback_up=true, .hitstun=true, .damage_event=true };
}
CCFeelEnable cc_feel_enable_none(void){
    return (CCFeelEnable){ 0 };
}

CCFeelProfile cc_feel_default(void) {
    CCFeelProfile p;
    p.hitstop_base           = 0.06f;
    p.hitstop_per_strength   = 0.04f;
    p.hitstop_max            = 0.20f;
    p.shake_trauma           = 0.35f;
    p.shake_per_strength     = 0.15f;
    p.knockback_base         = 6.0f;
    p.knockback_per_strength = 4.0f;
    p.knockback_up           = 1.5f;
    p.hitstun_base           = 0.18f;
    p.hitstun_per_strength   = 0.12f;
    return p;
}

CCCombat* cc_combat_create(struct CCEngine* eng) {
    CCCombat* cb = (CCCombat*)calloc(1, sizeof(CCCombat));
    if (!cb) return NULL;
    cb->eng  = eng;
    cb->feel = cc_feel_default();
    cb->en   = cc_feel_enable_all();
    return cb;
}
void cc_combat_destroy(CCCombat* cb){ free(cb); }
void cc_combat_set_profile(CCCombat* cb, const CCFeelProfile* p){ if(cb&&p) cb->feel=*p; }
CCFeelProfile* cc_combat_profile(CCCombat* cb){ return cb?&cb->feel:NULL; }
void cc_combat_set_camera(CCCombat* cb, struct CCCameraRig* r){ if(cb) cb->cam=r; }
void cc_combat_set_physics(CCCombat* cb, struct CCPhysicsWorld* w){ if(cb) cb->phys=w; }
void cc_combat_set_event_bus(CCCombat* cb, struct CCEventBus* b){ if(cb) cb->bus=b; }
void cc_combat_set_enable(CCCombat* cb, CCFeelEnable en){ if(cb) cb->en=en; }
CCFeelEnable* cc_combat_enable(CCCombat* cb){ return cb?&cb->en:NULL; }
void cc_combat_set_on_hit(CCCombat* cb, CCOnHitFn fn, void* u){ if(cb){ cb->on_hit=fn; cb->on_hit_user=u; } }

/* ─── hitstun map helpers ───────────────────────────────────────────────── */
static StunSlot* stun_find(CCCombat* cb, uint64_t id) {
    for (int i=0;i<CC_HITSTUN_SLOTS;i++)
        if (cb->stun[i].used && cb->stun[i].id==id) return &cb->stun[i];
    return NULL;
}
static StunSlot* stun_alloc(CCCombat* cb, uint64_t id) {
    StunSlot* s = stun_find(cb, id);
    if (s) return s;
    for (int i=0;i<CC_HITSTUN_SLOTS;i++)
        if (!cb->stun[i].used) { cb->stun[i].used=true; cb->stun[i].id=id; cb->stun[i].remaining=0; return &cb->stun[i]; }
    return NULL;   /* full — silently drop (64 simultaneous stunned entities is a lot) */
}

float cc_combat_begin_frame(CCCombat* cb, float dt) {
    if (!cb) return dt;
    /* hitstop: while active, freeze the sim (return 0 dt) and burn down the timer
       using REAL dt so the freeze lasts a wall-clock duration, not sim time. */
    if (cb->hitstop_remaining > 0.0f) {
        cb->hitstop_remaining -= dt;
        if (cb->hitstop_remaining < 0.0f) cb->hitstop_remaining = 0.0f;
        /* still decay hitstun during hitstop? No — hitstun should feel frozen too,
           so the freeze affects everything uniformly. Return 0 and don't decay. */
        return 0.0f;
    }
    /* decay hitstun timers with real dt */
    for (int i=0;i<CC_HITSTUN_SLOTS;i++) if (cb->stun[i].used) {
        cb->stun[i].remaining -= dt;
        if (cb->stun[i].remaining <= 0.0f) { cb->stun[i].used=false; cb->stun[i].id=0; }
    }
    return dt;
}

/* ─── mechanism primitives (genre-neutral, individually callable) ───────── */
void cc_combat_add_hitstop(CCCombat* cb, float seconds){
    if (!cb || seconds<=0) return;
    cb->hitstop_remaining += seconds;
    if (cb->hitstop_remaining > cb->feel.hitstop_max) cb->hitstop_remaining = cb->feel.hitstop_max;
}
void cc_combat_add_shake(CCCombat* cb, float trauma){
    if (!cb || !cb->cam || trauma<=0) return;
    if (trauma>1) trauma=1;
    cc_cam_add_trauma(cb->cam, trauma);
}
void cc_combat_apply_knockback(CCCombat* cb, uint32_t body, float x, float y, float z){
    if (!cb) return;
    cb->last_kb[0]=x; cb->last_kb[1]=y; cb->last_kb[2]=z;
    if (cb->phys && body){ CCVec3 imp={x,y,z}; cc_body_apply_impulse(cb->phys,(CCBodyId)body,imp); }
}
void cc_combat_set_hitstun(CCCombat* cb, uint64_t victim, float seconds){
    if (!cb || seconds<=0) return;
    StunSlot* s=stun_alloc(cb,victim);
    if (s && seconds>s->remaining) s->remaining=seconds;
}
void cc_combat_emit_damage(CCCombat* cb, uint64_t attacker, uint64_t victim, float amount){
    if (cb && cb->bus) cc_event_emit_if(cb->bus, CC_EVT_DAMAGE, attacker, (int64_t)victim, amount);
}

void cc_combat_register_hit(CCCombat* cb, const CCHit* h) {
    if (!cb || !h) return;
    const CCFeelProfile* f = &cb->feel;
    const CCFeelEnable*  en = &cb->en;
    float s = h->strength > 0 ? h->strength : 1.0f;

    /* Each reaction is gated by its enable flag — nothing is mandatory. */

    /* 1. HITSTOP */
    if (en->hitstop) {
        float stop = f->hitstop_base + f->hitstop_per_strength * (s - 1.0f);
        cc_combat_add_hitstop(cb, stop);
    }
    /* 2. SCREENSHAKE */
    if (en->screenshake && cb->cam) {
        float trauma = f->shake_trauma + f->shake_per_strength * (s - 1.0f);
        cc_combat_add_shake(cb, trauma);
    }
    /* 3. KNOCKBACK (up-pop separately toggleable) */
    if (en->knockback) {
        float kx = h->dir_x, kz = h->dir_z;
        float len = sqrtf(kx*kx + kz*kz);
        if (len > 1e-4f) { kx/=len; kz/=len; } else { kx=0; kz=0; }
        float mag = f->knockback_base + f->knockback_per_strength * (s - 1.0f);
        if (mag < 0) mag = 0;
        float up = en->knockback_up ? f->knockback_up * s : 0.0f;
        cc_combat_apply_knockback(cb, h->victim_body, kx*mag, up, kz*mag);
    }
    /* 4. HITSTUN */
    if (en->hitstun) {
        float stun = f->hitstun_base + f->hitstun_per_strength * (s - 1.0f);
        cc_combat_set_hitstun(cb, h->victim, stun);
    }
    /* 5. DAMAGE EVENT */
    if (en->damage_event)
        cc_combat_emit_damage(cb, h->attacker, h->victim, h->damage);

    /* 6. POLICY HOOK — the game's own reaction (elemental, armor pen, recoil, …).
       Runs after built-ins so it can add to or override them; with enable=none the
       built-ins do nothing and this is the ENTIRE reaction. */
    if (cb->on_hit) cb->on_hit(cb, h, cb->on_hit_user);
}

bool  cc_combat_hitstop_active(const CCCombat* cb){ return cb && cb->hitstop_remaining > 0.0f; }
float cc_combat_hitstop_remaining(const CCCombat* cb){ return cb ? cb->hitstop_remaining : 0.0f; }
bool  cc_combat_in_hitstun(const CCCombat* cb, uint64_t v){
    if (!cb) return false;
    StunSlot* s = stun_find((CCCombat*)cb, v);
    return s && s->remaining > 0.0f;
}
float cc_combat_hitstun_remaining(const CCCombat* cb, uint64_t v){
    if (!cb) return 0.0f;
    StunSlot* s = stun_find((CCCombat*)cb, v);
    return s ? s->remaining : 0.0f;
}
void cc_combat_last_knockback(const CCCombat* cb, float* x, float* y, float* z){
    if (!cb) { if(x)*x=0; if(y)*y=0; if(z)*z=0; return; }
    if (x)*x=cb->last_kb[0]; if (y)*y=cb->last_kb[1]; if (z)*z=cb->last_kb[2];
}
