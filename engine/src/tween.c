#include "cc/tween.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* handle = (index+1) | (generation<<16) so a stale handle can't hit a reused slot */
#define IDX_OF(h)  (((h)&0xFFFFu)-1u)
#define GEN_OF(h)  ((h)>>16)
#define MAKE_H(i,g) ((uint32_t)((i)+1u) | ((uint32_t)(g)<<16))

typedef struct {
    bool          used;
    uint16_t      gen;
    float*        target;      /* nullable */
    CCTweenSetFn  set;         /* used when target==NULL */
    CCTweenDoneFn done;
    void*         ud;
    float         from, to, dur, t;
    CCEaseType    ease;
} Tween;

typedef struct {
    bool       used;
    uint16_t   gen;
    CCTimerFn  fn;
    void*      ud;
    float      interval;   /* 0 = one-shot */
    float      remaining;
    bool       repeating;
} Timer;

struct CCTweens {
    Tween* tweens; uint32_t tw_cap;
    Timer* timers; uint32_t tm_cap;
};

float cc_ease(CCEaseType type, float t){
    if(t<0)t=0; if(t>1)t=1;
    switch(type){
        case CC_EASE_IN:        return t*t;
        case CC_EASE_OUT:       return t*(2.0f-t);
        case CC_EASE_IN_OUT:    return t<0.5f ? 2*t*t : -1+(4-2*t)*t;
        case CC_EASE_SMOOTH:    return t*t*(3.0f-2.0f*t);          /* smoothstep */
        case CC_EASE_CUBIC:     return t*t*t*(t*(t*6.0f-15.0f)+10.0f); /* smootherstep */
        case CC_EASE_LINEAR:
        default:                return t;
    }
}

CCTweens* cc_tweens_create(void){ return (CCTweens*)calloc(1,sizeof(CCTweens)); }
void cc_tweens_destroy(CCTweens* tw){ if(tw){ free(tw->tweens); free(tw->timers); free(tw); } }
void cc_tweens_clear(CCTweens* tw){
    if(!tw) return;
    for(uint32_t i=0;i<tw->tw_cap;i++) if(tw->tweens[i].used){ tw->tweens[i].used=false; tw->tweens[i].gen++; }
    for(uint32_t i=0;i<tw->tm_cap;i++) if(tw->timers[i].used){ tw->timers[i].used=false; tw->timers[i].gen++; }
}
uint32_t cc_tweens_active_count(const CCTweens* tw){
    if(!tw) return 0; uint32_t n=0;
    for(uint32_t i=0;i<tw->tw_cap;i++) if(tw->tweens[i].used) n++;
    for(uint32_t i=0;i<tw->tm_cap;i++) if(tw->timers[i].used) n++;
    return n;
}

static Tween* alloc_tween(CCTweens* tw, uint32_t* out_idx){
    for(uint32_t i=0;i<tw->tw_cap;i++) if(!tw->tweens[i].used){ *out_idx=i; return &tw->tweens[i]; }
    uint32_t old=tw->tw_cap; tw->tw_cap = tw->tw_cap? tw->tw_cap*2 : 16;
    tw->tweens=realloc(tw->tweens, tw->tw_cap*sizeof(Tween));
    memset(&tw->tweens[old],0,(tw->tw_cap-old)*sizeof(Tween));
    *out_idx=old; return &tw->tweens[old];
}
static Timer* alloc_timer(CCTweens* tw, uint32_t* out_idx){
    for(uint32_t i=0;i<tw->tm_cap;i++) if(!tw->timers[i].used){ *out_idx=i; return &tw->timers[i]; }
    uint32_t old=tw->tm_cap; tw->tm_cap = tw->tm_cap? tw->tm_cap*2 : 16;
    tw->timers=realloc(tw->timers, tw->tm_cap*sizeof(Timer));
    memset(&tw->timers[old],0,(tw->tm_cap-old)*sizeof(Timer));
    *out_idx=old; return &tw->timers[old];
}

static CCTweenId make_tween(CCTweens* tw, float* target, CCTweenSetFn set,
                            float from,float to,float dur,CCEaseType ease,
                            CCTweenDoneFn done,void* ud){
    if(!tw) return 0;
    uint32_t idx; Tween* t=alloc_tween(tw,&idx);
    t->used=true; t->target=target; t->set=set; t->done=done; t->ud=ud;
    t->from=from; t->to=to; t->dur=dur>0?dur:0.0001f; t->t=0; t->ease=ease;
    /* apply initial value immediately */
    if(target) *target=from; else if(set) set(from,ud);
    return MAKE_H(idx, t->gen);
}
CCTweenId cc_tween_to(CCTweens* tw, float* target, float from, float to,
                      float duration, CCEaseType ease, CCTweenDoneFn done, void* ud){
    return make_tween(tw,target,NULL,from,to,duration,ease,done,ud);
}
CCTweenId cc_tween_value(CCTweens* tw, float from, float to, float duration,
                         CCEaseType ease, CCTweenSetFn set, CCTweenDoneFn done, void* ud){
    return make_tween(tw,NULL,set,from,to,duration,ease,done,ud);
}
void cc_tween_cancel(CCTweens* tw, CCTweenId id){
    if(!tw||!id) return; uint32_t i=IDX_OF(id);
    if(i<tw->tw_cap && tw->tweens[i].used && tw->tweens[i].gen==GEN_OF(id)){
        tw->tweens[i].used=false; tw->tweens[i].gen++;
    }
}
bool cc_tween_active(const CCTweens* tw, CCTweenId id){
    if(!tw||!id) return false; uint32_t i=IDX_OF(id);
    return i<tw->tw_cap && tw->tweens[i].used && tw->tweens[i].gen==GEN_OF(id);
}

CCTimerId cc_timer_after(CCTweens* tw, float delay, CCTimerFn fn, void* ud){
    if(!tw||!fn) return 0;
    uint32_t idx; Timer* t=alloc_timer(tw,&idx);
    t->used=true; t->fn=fn; t->ud=ud; t->interval=0; t->remaining=delay>0?delay:0; t->repeating=false;
    return MAKE_H(idx,t->gen);
}
CCTimerId cc_timer_every(CCTweens* tw, float interval, CCTimerFn fn, void* ud){
    if(!tw||!fn||interval<=0) return 0;
    uint32_t idx; Timer* t=alloc_timer(tw,&idx);
    t->used=true; t->fn=fn; t->ud=ud; t->interval=interval; t->remaining=interval; t->repeating=true;
    return MAKE_H(idx,t->gen);
}
void cc_timer_cancel(CCTweens* tw, CCTimerId id){
    if(!tw||!id) return; uint32_t i=IDX_OF(id);
    if(i<tw->tm_cap && tw->timers[i].used && tw->timers[i].gen==GEN_OF(id)){
        tw->timers[i].used=false; tw->timers[i].gen++;
    }
}
bool cc_timer_active(const CCTweens* tw, CCTimerId id){
    if(!tw||!id) return false; uint32_t i=IDX_OF(id);
    return i<tw->tm_cap && tw->timers[i].used && tw->timers[i].gen==GEN_OF(id);
}

void cc_tweens_update(CCTweens* tw, float dt){
    if(!tw||dt<0) return;
    /* tweens */
    for(uint32_t i=0;i<tw->tw_cap;i++){
        Tween* t=&tw->tweens[i];
        if(!t->used) continue;
        t->t += dt;
        float p = t->t / t->dur; if(p>1.0f) p=1.0f;
        float v = t->from + (t->to - t->from) * cc_ease(t->ease, p);
        if(t->target) *t->target = v; else if(t->set) t->set(v, t->ud);
        if(p>=1.0f){
            CCTweenDoneFn done=t->done; void* ud=t->ud;
            t->used=false; t->gen++;
            if(done) done(ud);      /* after clearing, so on_done can re-add safely */
        }
    }
    /* timers */
    for(uint32_t i=0;i<tw->tm_cap;i++){
        Timer* t=&tw->timers[i];
        if(!t->used) continue;
        t->remaining -= dt;
        /* fire possibly multiple times if dt is large and repeating */
        int guard=0;
        while(t->used && t->remaining<=0 && guard++<1000){
            CCTimerFn fn=t->fn; void* ud=t->ud;
            if(t->repeating){
                t->remaining += t->interval;
                fn(ud);
            } else {
                t->used=false; t->gen++;
                fn(ud);
                break;
            }
        }
    }
}
