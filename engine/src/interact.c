#include "cc/interact.h"
#include "cc/event.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

struct CCInteractable {
    CCActor        actor;
    CCInteractType type;
    float          range;
    int            on;          /* open / on / collected */
    int            locked;
    char           verb[32];
    CCInteractFn   cb;
    void*          cb_ud;
    /* door swing animation */
    float          anim_t;      /* 0..1 progress, -1 = idle */
    float          base_yaw;    /* closed yaw (deg) */
    float          target_yaw;  /* open yaw (deg) */
    /* ── generic mechanism (custom interactables) ── */
    void*              state;        /* user data pointer                       */
    float              cooldown;     /* min seconds between triggers            */
    float              cd_timer;     /* remaining cooldown                      */
    int                enabled;      /* 1 = participates in query/trigger       */
    int                focused;      /* engine focus tracking for on_focus      */
    CCInteractActionFn on_interact;  /* custom behavior                         */
    CCInteractFocusFn  on_focus;
    CCInteractFocusFn  on_unfocus;
    CCInteractUpdateFn on_update;
};

/* per-world registry, keyed by world pointer (mirrors the actor build-cache) */
typedef struct {
    CCScene*         world;
    CCInteractable** items;
    uint32_t         count, cap;
    CCEventBus*      bus;   /* optional: publish CC_EVT_* on trigger (NULL=off) */
} InteractRegistry;

#define CC_MAX_INTERACT_WORLDS 16
static InteractRegistry g_reg[CC_MAX_INTERACT_WORLDS];

static InteractRegistry* reg_for(CCScene* world){
    for(int i=0;i<CC_MAX_INTERACT_WORLDS;i++) if(g_reg[i].world==world) return &g_reg[i];
    for(int i=0;i<CC_MAX_INTERACT_WORLDS;i++) if(g_reg[i].world==NULL){ g_reg[i].world=world; return &g_reg[i]; }
    return NULL;
}

static const char* default_verb(CCInteractType t){
    switch(t){
        case CC_INTERACT_DOOR:   return "Open";
        case CC_INTERACT_SWITCH: return "Toggle";
        case CC_INTERACT_LEVER:  return "Pull";
        case CC_INTERACT_PICKUP: return "Take";
        default:                 return "Use";
    }
}

CCInteractable* cc_interactable_register(CCScene* world, CCActor actor,
                                         CCInteractType type, float range){
    InteractRegistry* r=reg_for(world);
    if(!r) return NULL;
    CCInteractable* it=(CCInteractable*)calloc(1,sizeof(CCInteractable));
    it->actor=actor; it->type=type; it->range=range>0?range:2.0f;
    it->on=0; it->locked=0; it->anim_t=-1.0f; it->enabled=1;
    snprintf(it->verb,sizeof(it->verb),"%s",default_verb(type));
    /* record the door's closed/open yaw from the actor's current rotation */
    CCTransform3D xf;
    if(cc_actor_get_transform(actor,&xf)){
        /* recover yaw from quaternion (Y-axis) */
        float yaw = 2.0f*atan2f(xf.rot[1],xf.rot[3]) * 180.0f/3.14159265f;
        it->base_yaw=yaw; it->target_yaw=yaw+90.0f;
    }
    if(r->count>=r->cap){ r->cap=r->cap?r->cap*2:16; r->items=realloc(r->items,r->cap*sizeof(CCInteractable*)); }
    r->items[r->count++]=it;
    return it;
}

void cc_interactable_set_callback(CCInteractable* it, CCInteractFn fn, void* ud){
    if(it){ it->cb=fn; it->cb_ud=ud; }
}

CCInteractable* cc_interactable_create(CCScene* world, CCActor actor, const CCInteractDesc* d){
    InteractRegistry* r=reg_for(world);
    if(!r) return NULL;
    CCInteractable* it=(CCInteractable*)calloc(1,sizeof(CCInteractable));
    it->actor=actor;
    it->type=CC_INTERACT_CUSTOM;      /* no built-in behavior; callbacks are it */
    it->range=(d && d->range>0)? d->range : 2.0f;
    it->anim_t=-1.0f; it->enabled=1;
    snprintf(it->verb,sizeof(it->verb),"%s",(d && d->prompt)? d->prompt : "Use");
    if(d){
        it->state=d->state;
        it->cooldown=d->cooldown>0?d->cooldown:0.0f;
        it->on_interact=d->on_interact;
        it->on_focus=d->on_focus;
        it->on_unfocus=d->on_unfocus;
        it->on_update=d->on_update;
    }
    if(r->count>=r->cap){ r->cap=r->cap?r->cap*2:16; r->items=realloc(r->items,r->cap*sizeof(CCInteractable*)); }
    r->items[r->count++]=it;
    return it;
}

void* cc_interactable_state(const CCInteractable* it){ return it? it->state : NULL; }
void  cc_interactable_set_state(CCInteractable* it, void* s){ if(it) it->state=s; }
void  cc_interactable_set_enabled(CCInteractable* it, bool en){ if(it) it->enabled=en?1:0; }
bool  cc_interactable_enabled(const CCInteractable* it){ return it? it->enabled!=0 : false; }
void  cc_interactable_set_on(CCInteractable* it, bool on){ if(it) it->on=on?1:0; }
void cc_interactable_set_prompt(CCInteractable* it, const char* verb){
    if(it&&verb) snprintf(it->verb,sizeof(it->verb),"%s",verb);
}
void cc_interactable_set_locked(CCInteractable* it, bool locked){
    if(it) it->locked=locked?1:0;
}
void cc_interactable_set_event_bus(CCScene* world, CCEventBus* bus){
    InteractRegistry* r=reg_for(world);
    if(r) r->bus=bus;
}

CCInteractable* cc_interactable_query(CCScene* world, float px,float py,float pz,
                                      float fx,float fz){
    InteractRegistry* r=reg_for(world);
    if(!r||r->count==0) return NULL;
    /* normalize facing if provided */
    float flen=sqrtf(fx*fx+fz*fz);
    int have_facing = flen>1e-4f;
    if(have_facing){ fx/=flen; fz/=flen; }
    CCInteractable* best=NULL; float best_score=1e30f;
    for(uint32_t i=0;i<r->count;i++){
        CCInteractable* it=r->items[i];
        if(!it->enabled) continue;
        if(!cc_actor_valid(it->actor)) continue;
        if(it->type==CC_INTERACT_PICKUP && it->on) continue;   /* already taken */
        float ax,ay,az; cc_actor_get_position(it->actor,&ax,&ay,&az);
        float dx=ax-px, dy=ay-py, dz=az-pz;
        float dist=sqrtf(dx*dx+dy*dy+dz*dz);
        if(dist>it->range) continue;
        float score=dist;
        if(have_facing && dist>1e-3f){
            /* prefer targets in front: subtract a bonus for alignment */
            float ndx=dx/dist, ndz=dz/dist;
            float dot=ndx*fx+ndz*fz;      /* 1 = directly ahead */
            if(dot<0.0f) continue;         /* behind the player: skip */
            score = dist*(1.5f-0.5f*dot);  /* closer + more-ahead scores lower */
        }
        if(score<best_score){ best_score=score; best=it; }
    }
    /* focus transitions: fire on_focus for the new best, on_unfocus for others */
    for(uint32_t i=0;i<r->count;i++){
        CCInteractable* it=r->items[i];
        int now_focused = (it==best);
        if(now_focused && !it->focused){
            it->focused=1; if(it->on_focus) it->on_focus(it, it->state);
        } else if(!now_focused && it->focused){
            it->focused=0; if(it->on_unfocus) it->on_unfocus(it, it->state);
        }
    }
    return best;
}

bool cc_interactable_trigger(CCScene* world, CCInteractable* it){
    if(!it || !it->enabled) return false;
    InteractRegistry* r=reg_for(world);
    CCEventBus* bus = r ? r->bus : NULL;
    uint64_t sender = it->actor.id;

    if(it->locked){
        if(bus) cc_event_emit_i(bus, CC_EVT_INTERACT_LOCKED, sender, (int64_t)it->type);
        return false;
    }
    if(it->cd_timer>0.0f) return false;  /* still cooling down */
    if(it->anim_t>=0.0f) return false;   /* mid-animation */

    /* CUSTOM: the developer's callback IS the behavior. Engine only manages the
     * toggle bit + cooldown + optional generic event, then calls on_interact. */
    if(it->type==CC_INTERACT_CUSTOM){
        it->on = !it->on;                     /* convenience toggle they may use */
        if(it->cooldown>0.0f) it->cd_timer=it->cooldown;
        if(it->on_interact) it->on_interact(it, it->state);
        if(it->cb) it->cb(it, it->on!=0, it->cb_ud);  /* legacy cb still honored  */
        if(bus) cc_event_emit_i(bus, CC_EVT_INTERACT_USED, sender, it->on?1:0);
        return true;
    }

    switch(it->type){
        case CC_INTERACT_DOOR:
            it->on = !it->on;
            it->anim_t = 0.0f;   /* begin swing; update() drives it */
            break;
        case CC_INTERACT_SWITCH:
            it->on = !it->on;
            break;
        case CC_INTERACT_LEVER:
            if(it->on) return false;  /* latching: once pulled, stays */
            it->on = 1;
            break;
        case CC_INTERACT_PICKUP:
            if(it->on) return false;  /* already collected */
            it->on = 1;
            cc_actor_set_visible(it->actor, false);
            break;
        case CC_INTERACT_USE:
        default:
            break;
    }
    if(it->cb) it->cb(it, it->on!=0, it->cb_ud);

    /* Additive: publish on the reserved engine channel (no-op if no bus). */
    if(bus){
        switch(it->type){
            case CC_INTERACT_DOOR:
                cc_event_emit_i(bus, it->on ? CC_EVT_DOOR_OPENED : CC_EVT_DOOR_CLOSED,
                                sender, it->on ? 1 : 0);
                break;
            case CC_INTERACT_SWITCH:
                cc_event_emit_i(bus, CC_EVT_SWITCH_TOGGLED, sender, it->on ? 1 : 0);
                break;
            case CC_INTERACT_LEVER:
                cc_event_emit_i(bus, CC_EVT_LEVER_PULLED, sender, 1);
                break;
            case CC_INTERACT_PICKUP:
                cc_event_emit_i(bus, CC_EVT_ITEM_PICKED_UP, sender, 0);
                break;
            case CC_INTERACT_USE:
            default:
                break;
        }
    }
    return true;
}

void cc_interactable_update(CCScene* world, float dt){
    InteractRegistry* r=reg_for(world);
    if(!r) return;
    for(uint32_t i=0;i<r->count;i++){
        CCInteractable* it=r->items[i];
        /* tick cooldown + run the developer's per-frame callback */
        if(it->cd_timer>0.0f){ it->cd_timer-=dt; if(it->cd_timer<0.0f) it->cd_timer=0.0f; }
        if(it->enabled && it->on_update) it->on_update(it, dt, it->state);
        /* built-in door swing animation */
        if(it->anim_t<0.0f) continue;
        it->anim_t += dt*3.0f;   /* ~1/3s swing */
        float t=it->anim_t; if(t>1.0f){ t=1.0f; it->anim_t=-1.0f; }
        /* ease in-out */
        float e = t<0.5f ? 2*t*t : -1+(4-2*t)*t;
        float from = it->on ? it->base_yaw : it->target_yaw;   /* opening vs closing */
        float to   = it->on ? it->target_yaw : it->base_yaw;
        float yaw = from + (to-from)*e;
        if(cc_actor_valid(it->actor)) cc_actor_set_rotation_y(it->actor, yaw);
    }
}

bool cc_interactable_is_on(const CCInteractable* it){ return it? it->on!=0 : false; }
bool cc_interactable_is_locked(const CCInteractable* it){ return it? it->locked!=0 : false; }
CCActor cc_interactable_actor(const CCInteractable* it){ return it? it->actor : CC_ACTOR_NULL; }
const char* cc_interactable_prompt(const CCInteractable* it){ return it? it->verb : ""; }
CCInteractType cc_interactable_type(const CCInteractable* it){ return it? it->type : CC_INTERACT_USE; }

void cc_interactable_clear(CCScene* world){
    InteractRegistry* r=reg_for(world);
    if(!r) return;
    for(uint32_t i=0;i<r->count;i++) free(r->items[i]);
    free(r->items);
    r->items=NULL; r->count=r->cap=0; r->world=NULL;
}
