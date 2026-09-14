#include "cc/particles.h"
#include "cc/render.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    CCVec3 pos, vel;
    float  life, max_life;
    float  size_start, size_end;
    float  col_start[4], col_end[4];
    bool   alive;
} Particle;

struct CCParticles {
    Particle*     pool;
    uint32_t      cap, alive;
    CCEmitterDesc desc;
    bool          emitting;
    float         spawn_accum;   /* fractional particles carried between frames */
    uint64_t      rng;
};

/* xorshift rng → [0,1) */
static float rnd01(uint64_t* s){
    uint64_t x=*s; x^=x<<13; x^=x>>7; x^=x<<17; *s=x;
    return (float)((x>>11) & 0xFFFFFFu) / (float)0x1000000u;
}
static float rrange(uint64_t* s, float a, float b){ return a+(b-a)*rnd01(s); }

CCParticles* cc_particles_create(uint32_t capacity){
    if(capacity==0) capacity=256;
    CCParticles* p=(CCParticles*)calloc(1,sizeof(CCParticles));
    p->pool=(Particle*)calloc(capacity,sizeof(Particle));
    p->cap=capacity; p->alive=0; p->emitting=true;
    p->rng=0x9E3779B97F4A7C15ULL;
    /* a neutral default config */
    p->desc=cc_emitter_smoke();
    return p;
}
void cc_particles_destroy(CCParticles* p){ if(p){ free(p->pool); free(p); } }

void cc_particles_config(CCParticles* p, const CCEmitterDesc* d){
    if(!p||!d) return;
    p->desc=*d;
    if(d->seed) p->rng = d->seed ^ 0x9E3779B97F4A7C15ULL;
}
void cc_particles_set_position(CCParticles* p, float x,float y,float z){
    if(p){ p->desc.position.x=x; p->desc.position.y=y; p->desc.position.z=z; }
}
void cc_particles_set_emitting(CCParticles* p, bool on){ if(p) p->emitting=on; }
uint32_t cc_particles_alive(const CCParticles* p){ return p? p->alive : 0; }

static void spawn_one(CCParticles* p){
    /* find a dead slot */
    for(uint32_t i=0;i<p->cap;i++){
        if(!p->pool[i].alive){
            Particle* q=&p->pool[i];
            const CCEmitterDesc* d=&p->desc;
            q->pos.x = d->position.x + rrange(&p->rng,-d->spawn_extent.x,d->spawn_extent.x);
            q->pos.y = d->position.y + rrange(&p->rng,-d->spawn_extent.y,d->spawn_extent.y);
            q->pos.z = d->position.z + rrange(&p->rng,-d->spawn_extent.z,d->spawn_extent.z);
            q->vel.x = d->base_velocity.x + rrange(&p->rng,-d->vel_spread.x,d->vel_spread.x);
            q->vel.y = d->base_velocity.y + rrange(&p->rng,-d->vel_spread.y,d->vel_spread.y);
            q->vel.z = d->base_velocity.z + rrange(&p->rng,-d->vel_spread.z,d->vel_spread.z);
            q->max_life = rrange(&p->rng, d->life_min, d->life_max);
            if(q->max_life<=0) q->max_life=0.001f;
            q->life = q->max_life;
            float jit = 1.0f + rrange(&p->rng,-d->size_jitter,d->size_jitter);
            q->size_start = d->size_start*jit;
            q->size_end   = d->size_end*jit;
            memcpy(q->col_start,d->color_start,sizeof(float)*4);
            memcpy(q->col_end,  d->color_end,  sizeof(float)*4);
            q->alive=true; p->alive++;
            return;
        }
    }
    /* pool full: silently drop */
}

void cc_particles_burst(CCParticles* p, uint32_t count){
    if(!p) return;
    for(uint32_t i=0;i<count;i++) spawn_one(p);
}

void cc_particles_update(CCParticles* p, float dt){
    if(!p||dt<0) return;
    const CCEmitterDesc* d=&p->desc;
    /* continuous emission */
    if(p->emitting && d->rate>0){
        p->spawn_accum += d->rate*dt;
        while(p->spawn_accum>=1.0f){ spawn_one(p); p->spawn_accum-=1.0f; }
    }
    /* integrate */
    float dragf = d->drag>0 ? (1.0f - d->drag*dt) : 1.0f;
    if(dragf<0)dragf=0;
    for(uint32_t i=0;i<p->cap;i++){
        Particle* q=&p->pool[i];
        if(!q->alive) continue;
        q->vel.x += d->gravity.x*dt; q->vel.y += d->gravity.y*dt; q->vel.z += d->gravity.z*dt;
        q->vel.x *= dragf; q->vel.y *= dragf; q->vel.z *= dragf;
        q->pos.x += q->vel.x*dt; q->pos.y += q->vel.y*dt; q->pos.z += q->vel.z*dt;
        q->life -= dt;
        if(q->life<=0){ q->alive=false; if(p->alive) p->alive--; }
    }
}

void cc_particles_draw(CCParticles* p, CCEngine* eng, uint32_t tex){
    if(!p||!eng||p->alive==0) return;
    CCBillboard* bb=(CCBillboard*)malloc(sizeof(CCBillboard)*p->alive);
    if(!bb) return;
    uint32_t n=0;
    for(uint32_t i=0;i<p->cap && n<p->alive;i++){
        Particle* q=&p->pool[i];
        if(!q->alive) continue;
        float t = 1.0f - (q->life / q->max_life);   /* 0 at birth .. 1 at death */
        if(t<0)t=0; if(t>1)t=1;
        float sz = q->size_start + (q->size_end - q->size_start)*t;
        bb[n].pos[0]=q->pos.x; bb[n].pos[1]=q->pos.y; bb[n].pos[2]=q->pos.z;
        bb[n].size[0]=sz; bb[n].size[1]=sz;
        for(int c=0;c<4;c++) bb[n].color[c] = q->col_start[c] + (q->col_end[c]-q->col_start[c])*t;
        n++;
    }
    if(n>0) cc_draw_billboards(eng, tex, bb, n, CC_BILLBOARD_SPHERICAL);
    free(bb);
}

/* ─── presets ─────────────────────────────────────────────────────────────
 * All sizes/velocities in world units; tweak position after fetching. */
CCEmitterDesc cc_emitter_smoke(void){
    CCEmitterDesc d; memset(&d,0,sizeof(d));
    d.spawn_extent=(CCVec3){0.15f,0.05f,0.15f};
    d.rate=40;
    d.base_velocity=(CCVec3){0,1.2f,0}; d.vel_spread=(CCVec3){0.25f,0.3f,0.25f};
    d.gravity=(CCVec3){0,0.2f,0};   /* slight rise */
    d.drag=0.6f;
    d.life_min=1.6f; d.life_max=2.8f;
    d.size_start=0.35f; d.size_end=1.4f; d.size_jitter=0.3f;
    d.color_start[0]=0.5f;d.color_start[1]=0.5f;d.color_start[2]=0.52f;d.color_start[3]=0.5f;
    d.color_end[0]=0.35f;d.color_end[1]=0.35f;d.color_end[2]=0.38f;d.color_end[3]=0.0f;
    d.seed=1;
    return d;
}
CCEmitterDesc cc_emitter_fire(void){
    CCEmitterDesc d; memset(&d,0,sizeof(d));
    d.spawn_extent=(CCVec3){0.12f,0.02f,0.12f};
    d.rate=90;
    d.base_velocity=(CCVec3){0,2.2f,0}; d.vel_spread=(CCVec3){0.4f,0.5f,0.4f};
    d.gravity=(CCVec3){0,1.0f,0};
    d.drag=0.8f;
    d.life_min=0.5f; d.life_max=1.0f;
    d.size_start=0.5f; d.size_end=0.05f; d.size_jitter=0.35f;
    d.color_start[0]=1.6f;d.color_start[1]=0.9f;d.color_start[2]=0.25f;d.color_start[3]=1.0f; /* bright (bloom) */
    d.color_end[0]=1.2f;d.color_end[1]=0.15f;d.color_end[2]=0.05f;d.color_end[3]=0.0f;
    d.seed=2;
    return d;
}
CCEmitterDesc cc_emitter_sparks(void){
    CCEmitterDesc d; memset(&d,0,sizeof(d));
    d.spawn_extent=(CCVec3){0.02f,0.02f,0.02f};
    d.rate=0;   /* burst-oriented */
    d.base_velocity=(CCVec3){0,1.0f,0}; d.vel_spread=(CCVec3){3.0f,3.0f,3.0f};
    d.gravity=(CCVec3){0,-9.8f,0};
    d.drag=0.1f;
    d.life_min=0.4f; d.life_max=1.1f;
    d.size_start=0.08f; d.size_end=0.01f; d.size_jitter=0.5f;
    d.color_start[0]=2.0f;d.color_start[1]=1.4f;d.color_start[2]=0.5f;d.color_start[3]=1.0f;
    d.color_end[0]=1.0f;d.color_end[1]=0.3f;d.color_end[2]=0.1f;d.color_end[3]=0.0f;
    d.seed=3;
    return d;
}
CCEmitterDesc cc_emitter_rain(void){
    CCEmitterDesc d; memset(&d,0,sizeof(d));
    d.spawn_extent=(CCVec3){8.0f,0.1f,8.0f};
    d.rate=400;
    d.base_velocity=(CCVec3){0,-14.0f,0}; d.vel_spread=(CCVec3){0.5f,1.0f,0.5f};
    d.gravity=(CCVec3){0,-4.0f,0};
    d.drag=0.0f;
    d.life_min=0.8f; d.life_max=1.1f;
    d.size_start=0.03f; d.size_end=0.03f; d.size_jitter=0.2f;
    d.color_start[0]=0.6f;d.color_start[1]=0.7f;d.color_start[2]=0.9f;d.color_start[3]=0.5f;
    d.color_end[0]=0.6f;d.color_end[1]=0.7f;d.color_end[2]=0.9f;d.color_end[3]=0.3f;
    d.seed=4;
    return d;
}
CCEmitterDesc cc_emitter_magic(void){
    CCEmitterDesc d; memset(&d,0,sizeof(d));
    d.spawn_extent=(CCVec3){0.2f,0.2f,0.2f};
    d.rate=60;
    d.base_velocity=(CCVec3){0,0.6f,0}; d.vel_spread=(CCVec3){0.5f,0.5f,0.5f};
    d.gravity=(CCVec3){0,0.3f,0};
    d.drag=0.5f;
    d.life_min=1.0f; d.life_max=2.0f;
    d.size_start=0.15f; d.size_end=0.02f; d.size_jitter=0.4f;
    d.color_start[0]=0.5f;d.color_start[1]=0.9f;d.color_start[2]=1.8f;d.color_start[3]=1.0f;
    d.color_end[0]=0.8f;d.color_end[1]=0.3f;d.color_end[2]=1.5f;d.color_end[3]=0.0f;
    d.seed=5;
    return d;
}
