#include "cc/trail.h"
#include "cc/debug.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { float x,y,z; float age; } TrailPoint;

/* Points are kept NEWEST-FIRST in a contiguous array (index 0 = newest). This
   keeps push/update/draw simple and unambiguous; capacities are small so the
   O(n) shift on push is negligible. */
struct CCTrail {
    TrailPoint* pts;
    uint32_t    cap;
    uint32_t    count;
    float       lifetime;
};

CCTrail* cc_trail_create(uint32_t max_points, float lifetime){
    if(max_points<2) max_points=2;
    CCTrail* t=(CCTrail*)calloc(1,sizeof(CCTrail));
    t->pts=(TrailPoint*)calloc(max_points,sizeof(TrailPoint));
    t->cap=max_points; t->count=0;
    t->lifetime=lifetime>0?lifetime:0.5f;
    return t;
}
void cc_trail_destroy(CCTrail* t){ if(t){ free(t->pts); free(t); } }
void cc_trail_clear(CCTrail* t){ if(t) t->count=0; }
uint32_t cc_trail_point_count(const CCTrail* t){ return t? t->count : 0; }

void cc_trail_push(CCTrail* t, float x, float y, float z){
    if(!t) return;
    if(t->count>0){
        TrailPoint* newest=&t->pts[0];
        float dx=x-newest->x, dy=y-newest->y, dz=z-newest->z;
        if(dx*dx+dy*dy+dz*dz < 1e-6f) return;   /* ignore near-duplicate */
    }
    uint32_t keep = (t->count<t->cap) ? t->count : t->cap-1;
    memmove(&t->pts[1], &t->pts[0], keep*sizeof(TrailPoint));
    t->pts[0].x=x; t->pts[0].y=y; t->pts[0].z=z; t->pts[0].age=0.0f;
    t->count = keep+1;
}

void cc_trail_update(CCTrail* t, float dt){
    if(!t||dt<0) return;
    for(uint32_t i=0;i<t->count;i++) t->pts[i].age += dt;
    while(t->count>0 && t->pts[t->count-1].age > t->lifetime) t->count--;
}

void cc_trail_draw(CCEngine* eng, const CCTrail* t, float r, float g, float b){
    if(!eng||!t||t->count<2) return;
    for(uint32_t i=0;i+1<t->count;i++){
        const TrailPoint* a=&t->pts[i];
        const TrailPoint* c=&t->pts[i+1];
        float fade = 1.0f - (a->age / t->lifetime); if(fade<0)fade=0; if(fade>1)fade=1;
        CCVec3 pa={a->x,a->y,a->z}, pc={c->x,c->y,c->z};
        CCVec3 col={r*fade, g*fade, b*fade};
        cc_gizmo_line(eng, pa, pc, col);
    }
}
