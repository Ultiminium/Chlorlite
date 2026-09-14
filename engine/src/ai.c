/*
 * ai.c — Chlorlite AI tier: A* grid pathfinding, steering behaviors, and a
 * lightweight behavior FSM. All pure-CPU and headless-safe.
 */
#include "cc/ai.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ══════════════════════════════════════════════════════════════════════
 * 1. GRID + A*
 * ════════════════════════════════════════════════════════════════════ */

struct CCGrid {
    uint32_t w, h;
    float    cell_size;
    uint8_t* blocked;   /* w*h, 1 = blocked */
};

CCGrid* cc_grid_create(uint32_t w, uint32_t h, float cell_size) {
    if (!w || !h) return NULL;
    CCGrid* g = calloc(1, sizeof(CCGrid));
    g->w=w; g->h=h; g->cell_size=cell_size>0?cell_size:1.0f;
    g->blocked = calloc((size_t)w*h, 1);
    return g;
}
void cc_grid_destroy(CCGrid* g){ if(g){ free(g->blocked); free(g);} }
void cc_grid_set_blocked(CCGrid* g, uint32_t x, uint32_t y, bool b){
    if (g && x<g->w && y<g->h) g->blocked[(size_t)y*g->w+x] = b?1:0;
}
bool cc_grid_is_blocked(const CCGrid* g, uint32_t x, uint32_t y){
    return (g && x<g->w && y<g->h) ? g->blocked[(size_t)y*g->w+x]!=0 : true;
}
void cc_grid_clear(CCGrid* g){ if(g) memset(g->blocked,0,(size_t)g->w*g->h); }
uint32_t cc_grid_width(const CCGrid* g){ return g?g->w:0; }
uint32_t cc_grid_height(const CCGrid* g){ return g?g->h:0; }

void cc_grid_world_to_cell(const CCGrid* g, float wx, float wz, int32_t* cx, int32_t* cy){
    if (!g) { if(cx)*cx=0; if(cy)*cy=0; return; }
    float ox = g->w*0.5f*g->cell_size, oz = g->h*0.5f*g->cell_size;
    if (cx) *cx = (int32_t)floorf((wx+ox)/g->cell_size);
    if (cy) *cy = (int32_t)floorf((wz+oz)/g->cell_size);
}
void cc_grid_cell_to_world(const CCGrid* g, uint32_t cx, uint32_t cy, float* wx, float* wz){
    if (!g) { if(wx)*wx=0; if(wz)*wz=0; return; }
    float ox = g->w*0.5f*g->cell_size, oz = g->h*0.5f*g->cell_size;
    if (wx) *wx = (cx+0.5f)*g->cell_size - ox;
    if (wz) *wz = (cy+0.5f)*g->cell_size - oz;
}

/* Binary min-heap over open-set nodes keyed by f-score. */
typedef struct { int32_t idx; float f; } HeapNode;
typedef struct { HeapNode* a; uint32_t n, cap; } Heap;
static void heap_push(Heap* h, int32_t idx, float f){
    if (h->n>=h->cap){ h->cap=h->cap?h->cap*2:256; h->a=realloc(h->a,h->cap*sizeof(HeapNode)); }
    uint32_t i=h->n++; h->a[i]=(HeapNode){idx,f};
    while (i>0){ uint32_t p=(i-1)/2; if (h->a[p].f<=h->a[i].f) break;
        HeapNode t=h->a[p];h->a[p]=h->a[i];h->a[i]=t; i=p; }
}
static int32_t heap_pop(Heap* h){
    if (!h->n) return -1;
    int32_t r=h->a[0].idx; h->a[0]=h->a[--h->n];
    uint32_t i=0;
    for(;;){ uint32_t l=2*i+1,rr=2*i+2,s=i;
        if (l<h->n && h->a[l].f<h->a[s].f) s=l;
        if (rr<h->n && h->a[rr].f<h->a[s].f) s=rr;
        if (s==i) break; HeapNode t=h->a[s];h->a[s]=h->a[i];h->a[i]=t; i=s; }
    return r;
}

uint32_t cc_astar(const CCGrid* g, int32_t sx, int32_t sy, int32_t gx, int32_t gy,
                  bool diagonal, CCCell* out, uint32_t max_out){
    if (!g || !out || !max_out) return 0;
    if (sx<0||sy<0||gx<0||gy<0||(uint32_t)sx>=g->w||(uint32_t)sy>=g->h||
        (uint32_t)gx>=g->w||(uint32_t)gy>=g->h) return 0;
    if (cc_grid_is_blocked(g,sx,sy) || cc_grid_is_blocked(g,gx,gy)) return 0;

    uint32_t N=g->w*g->h;
    float*   gscore = malloc(N*sizeof(float));
    int32_t* came   = malloc(N*sizeof(int32_t));
    uint8_t* closed = calloc(N,1);
    for (uint32_t i=0;i<N;i++){ gscore[i]=1e30f; came[i]=-1; }
    Heap open={0};

    int32_t start=sy*g->w+sx, goal=gy*g->w+gx;
    gscore[start]=0;
    /* octile heuristic */
    #define HEUR(ax,ay) ({ float dx=fabsf((float)((ax)-gx)), dy=fabsf((float)((ay)-gy)); \
        (dx>dy? dx + 0.41421356f*dy : dy + 0.41421356f*dx); })
    heap_push(&open, start, HEUR(sx,sy));

    static const int nx4[4]={1,-1,0,0}, ny4[4]={0,0,1,-1};
    static const int nx8[8]={1,-1,0,0, 1,1,-1,-1}, ny8[8]={0,0,1,-1, 1,-1,1,-1};
    const int *dx = diagonal?nx8:nx4, *dy = diagonal?ny8:ny4;
    int nn = diagonal?8:4;

    bool found=false;
    while (open.n){
        int32_t cur=heap_pop(&open);
        if (cur<0) break;
        if (cur==goal){ found=true; break; }
        if (closed[cur]) continue;
        closed[cur]=1;
        int32_t cx=cur%g->w, cy=cur/g->w;
        for (int k=0;k<nn;k++){
            int32_t ax=cx+dx[k], ay=cy+dy[k];
            if (ax<0||ay<0||(uint32_t)ax>=g->w||(uint32_t)ay>=g->h) continue;
            if (cc_grid_is_blocked(g,ax,ay)) continue;
            /* forbid corner cutting: diagonal blocked if either orthogonal side is */
            if (k>=4){
                if (cc_grid_is_blocked(g,cx,ay) || cc_grid_is_blocked(g,ax,cy)) continue;
            }
            int32_t ni=ay*g->w+ax;
            if (closed[ni]) continue;
            float step = (k>=4)?1.41421356f:1.0f;
            float tentative = gscore[cur]+step;
            if (tentative < gscore[ni]){
                came[ni]=cur; gscore[ni]=tentative;
                heap_push(&open, ni, tentative + HEUR(ax,ay));
            }
        }
    }
    #undef HEUR

    uint32_t count=0;
    if (found){
        /* reconstruct backwards into a temp, then reverse into out */
        int32_t chain[4096]; uint32_t cl=0;
        for (int32_t c=goal; c!=-1 && cl<4096; c=came[c]) chain[cl++]=c;
        /* if path longer than buffer, still emit what fits from the start */
        for (uint32_t i=0;i<cl && count<max_out;i++){
            int32_t c=chain[cl-1-i];
            out[count++]=(CCCell){ c%(int32_t)g->w, c/(int32_t)g->w };
        }
    }
    free(gscore); free(came); free(closed); free(open.a);
    return count;
}

/* ══════════════════════════════════════════════════════════════════════
 * 2. STEERING (XZ plane; Y carried but not steered)
 * ════════════════════════════════════════════════════════════════════ */

static CCVec3 xz(CCVec3 v){ return (CCVec3){v.x,0,v.z}; }
static float  xzlen(CCVec3 v){ return sqrtf(v.x*v.x+v.z*v.z); }
static CCVec3 xznorm(CCVec3 v){ float l=xzlen(v); return l>1e-6f?(CCVec3){v.x/l,0,v.z/l}:(CCVec3){0,0,0}; }
static CCVec3 clamp_len(CCVec3 v, float maxlen){
    float l=xzlen(v); if (l>maxlen && l>1e-6f) return (CCVec3){v.x/l*maxlen,0,v.z/l*maxlen}; return (CCVec3){v.x,0,v.z};
}

CCAgent cc_agent_make(CCVec3 pos, float max_speed, float max_force){
    CCAgent a; memset(&a,0,sizeof a);
    a.position=pos; a.max_speed=max_speed; a.max_force=max_force; a.radius=0.5f;
    return a;
}

CCVec3 cc_steer_seek(const CCAgent* a, CCVec3 target){
    CCVec3 desired = vec3_scale(xznorm(vec3_sub(xz(target),xz(a->position))), a->max_speed);
    return vec3_sub(desired, xz(a->velocity));
}
CCVec3 cc_steer_flee(const CCAgent* a, CCVec3 threat){
    CCVec3 desired = vec3_scale(xznorm(vec3_sub(xz(a->position),xz(threat))), a->max_speed);
    return vec3_sub(desired, xz(a->velocity));
}
CCVec3 cc_steer_arrive(const CCAgent* a, CCVec3 target, float slow_radius){
    CCVec3 to = vec3_sub(xz(target), xz(a->position));
    float d = xzlen(to);
    if (d < 1e-4f) return (CCVec3){0,0,0};
    float speed = a->max_speed;
    if (d < slow_radius) speed = a->max_speed * (d/slow_radius);
    CCVec3 desired = vec3_scale(xznorm(to), speed);
    return vec3_sub(desired, xz(a->velocity));
}
CCVec3 cc_steer_wander(CCAgent* a, float jitter, float radius, float distance, float dt){
    a->_wander_angle += (((float)rand()/RAND_MAX)*2.0f-1.0f) * jitter * dt;
    CCVec3 heading = xznorm(a->velocity);
    if (xzlen(heading)<1e-4f) heading=(CCVec3){1,0,0};
    CCVec3 circle_center = vec3_add(xz(a->position), vec3_scale(heading, distance));
    /* perpendicular in XZ */
    CCVec3 perp = {-heading.z,0,heading.x};
    CCVec3 offset = vec3_add(vec3_scale(heading, radius*cosf(a->_wander_angle)),
                             vec3_scale(perp,    radius*sinf(a->_wander_angle)));
    CCVec3 target = vec3_add(circle_center, offset);
    return cc_steer_seek(a, target);
}
CCVec3 cc_steer_separation(const CCAgent* a, const CCAgent* others, uint32_t count, float sep_radius){
    CCVec3 sum={0,0,0}; int n=0;
    for (uint32_t i=0;i<count;i++){
        const CCAgent* o=&others[i];
        if (o==a) continue;
        CCVec3 diff = vec3_sub(xz(a->position), xz(o->position));
        float d = xzlen(diff);
        if (d>1e-5f && d<sep_radius){
            /* weight by inverse distance so closer = stronger */
            sum = vec3_add(sum, vec3_scale(xznorm(diff), (sep_radius-d)/sep_radius));
            n++;
        }
    }
    if (!n) return (CCVec3){0,0,0};
    CCVec3 desired = vec3_scale(xznorm(sum), a->max_speed);
    return vec3_sub(desired, xz(a->velocity));
}

CCVec3 cc_steer_path_follow(CCAgent* a, const CCGrid* g, const CCCell* path,
                            uint32_t path_len, uint32_t* wp_index,
                            float arrive_radius, bool* done){
    if (done) *done=false;
    if (!path || !path_len || !wp_index){ if(done)*done=true; return (CCVec3){0,0,0}; }
    if (*wp_index >= path_len){ if(done)*done=true; return (CCVec3){0,0,0}; }
    float wx,wz; cc_grid_cell_to_world(g, path[*wp_index].x, path[*wp_index].y, &wx,&wz);
    CCVec3 target={wx, a->position.y, wz};
    float d = xzlen(vec3_sub(target, xz(a->position)));
    if (d < arrive_radius){
        if (*wp_index+1 >= path_len){ if(done)*done=true; return cc_steer_arrive(a,target,arrive_radius); }
        (*wp_index)++;
        cc_grid_cell_to_world(g, path[*wp_index].x, path[*wp_index].y, &wx,&wz);
        target=(CCVec3){wx,a->position.y,wz};
    }
    bool last = (*wp_index+1 >= path_len);
    return last ? cc_steer_arrive(a,target,arrive_radius) : cc_steer_seek(a,target);
}

CCVec3 cc_agent_integrate(CCAgent* a, CCVec3 steering, float dt){
    steering = clamp_len(steering, a->max_force);
    a->velocity = vec3_add(xz(a->velocity), vec3_scale(steering, dt));
    a->velocity = clamp_len(a->velocity, a->max_speed);
    a->position = vec3_add(a->position, vec3_scale(a->velocity, dt));
    return a->position;
}

/* ══════════════════════════════════════════════════════════════════════
 * 3. BEHAVIOR FSM
 * ════════════════════════════════════════════════════════════════════ */

#define CC_BRAIN_MAX_STATES     64
#define CC_BRAIN_MAX_TRANS      256
#define CC_BRAIN_NAME_LEN       32

typedef struct { char name[CC_BRAIN_NAME_LEN]; CCStateFn enter,update,exit; } BState;
typedef struct { int32_t from; uint32_t to; CCGuardFn guard; } BTrans; /* from<0 = any */

struct CCBrain {
    void*   ctx;
    BState  states[CC_BRAIN_MAX_STATES];  uint32_t nstates;
    BTrans  trans[CC_BRAIN_MAX_TRANS];    uint32_t ntrans;
    int32_t current;                      /* -1 = none */
    float   time_in_state;
};

CCBrain* cc_brain_create(void* ctx){
    CCBrain* b=calloc(1,sizeof(CCBrain)); b->ctx=ctx; b->current=-1; return b;
}
void cc_brain_destroy(CCBrain* b){ free(b); }

uint32_t cc_brain_add_state(CCBrain* b, const char* name,
                            CCStateFn en, CCStateFn up, CCStateFn ex){
    if (b->nstates>=CC_BRAIN_MAX_STATES) return 0;
    uint32_t id=b->nstates++;
    BState* s=&b->states[id];
    s->enter=en; s->update=up; s->exit=ex;
    snprintf(s->name,CC_BRAIN_NAME_LEN,"%s",name?name:"");
    if (b->current<0){ b->current=(int32_t)id; b->time_in_state=0;
        if (s->enter) s->enter(b,b->ctx,0); }   /* first state auto-enters */
    return id;
}
void cc_brain_add_transition(CCBrain* b, uint32_t from, uint32_t to, CCGuardFn guard){
    if (b->ntrans>=CC_BRAIN_MAX_TRANS) return;
    b->trans[b->ntrans++]=(BTrans){(int32_t)from,to,guard};
}
void cc_brain_add_any_transition(CCBrain* b, uint32_t to, CCGuardFn guard){
    if (b->ntrans>=CC_BRAIN_MAX_TRANS) return;
    b->trans[b->ntrans++]=(BTrans){-1,to,guard};
}
void cc_brain_set_state(CCBrain* b, uint32_t state){
    if (state>=b->nstates) return;
    if (b->current>=0 && b->states[b->current].exit) b->states[b->current].exit(b,b->ctx,0);
    b->current=(int32_t)state; b->time_in_state=0;
    if (b->states[state].enter) b->states[state].enter(b,b->ctx,0);
}
uint32_t    cc_brain_current(const CCBrain* b){ return b->current<0?0:(uint32_t)b->current; }
const char* cc_brain_current_name(const CCBrain* b){ return b->current<0?"":b->states[b->current].name; }
float       cc_brain_time_in_state(const CCBrain* b){ return b->time_in_state; }

void cc_brain_tick(CCBrain* b, float dt){
    if (b->current<0) return;
    /* evaluate transitions: any-state first, then from-current; first match wins */
    for (uint32_t pass=0; pass<2; pass++){
        for (uint32_t i=0;i<b->ntrans;i++){
            BTrans* t=&b->trans[i];
            bool applies = (pass==0) ? (t->from<0) : (t->from==b->current);
            if (!applies) continue;
            if (t->to==(uint32_t)b->current) continue;
            if (t->guard && t->guard(b,b->ctx)){ cc_brain_set_state(b,t->to); goto ran; }
        }
    }
ran:
    b->time_in_state += dt;
    if (b->states[b->current].update) b->states[b->current].update(b,b->ctx,dt);
}
