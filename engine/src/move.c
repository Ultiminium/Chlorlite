#include "cc/move.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef enum { COL_BOX, COL_PLANE } ColKind;
typedef struct {
    ColKind kind;
    float c[3], h[3];      /* box: center, half-extents */
    float n[3], d;         /* plane: normal, offset */
} Collider;

struct CCMoveWorld {
    Collider* items;
    uint32_t  count, cap;
};

CCMoveWorld* cc_move_world_create(void){ return (CCMoveWorld*)calloc(1,sizeof(CCMoveWorld)); }
void cc_move_world_destroy(CCMoveWorld* w){ if(w){ free(w->items); free(w); } }
void cc_move_clear(CCMoveWorld* w){ if(w) w->count=0; }
uint32_t cc_move_collider_count(const CCMoveWorld* w){ return w? w->count : 0; }

static Collider* add(CCMoveWorld* w){
    if(w->count>=w->cap){ w->cap=w->cap?w->cap*2:16; w->items=realloc(w->items,w->cap*sizeof(Collider)); }
    return &w->items[w->count++];
}
void cc_move_add_box(CCMoveWorld* w, float cx,float cy,float cz, float hx,float hy,float hz){
    if(!w) return; Collider* c=add(w); c->kind=COL_BOX;
    c->c[0]=cx;c->c[1]=cy;c->c[2]=cz; c->h[0]=hx;c->h[1]=hy;c->h[2]=hz;
}
void cc_move_add_plane(CCMoveWorld* w, float nx,float ny,float nz, float d){
    if(!w) return; float l=sqrtf(nx*nx+ny*ny+nz*nz); if(l<1e-6f)l=1;
    Collider* c=add(w); c->kind=COL_PLANE; c->n[0]=nx/l;c->n[1]=ny/l;c->n[2]=nz/l; c->d=d/l;
}

/* nearest point on an AABB to point p */
static void closest_on_box(const Collider* b, const float p[3], float out[3]){
    for(int i=0;i<3;i++){
        float lo=b->c[i]-b->h[i], hi=b->c[i]+b->h[i];
        out[i] = p[i]<lo?lo : (p[i]>hi?hi:p[i]);
    }
}

/* Test a sphere at `p` (radius r) against one collider. If penetrating, writes
   the contact normal (unit, pointing from surface toward the sphere center) and
   penetration depth, returns 1. */
static int sphere_collide(const Collider* col, const float p[3], float r,
                          float out_n[3], float* out_depth){
    if(col->kind==COL_BOX){
        float q[3]; closest_on_box(col,p,q);
        float dx=p[0]-q[0], dy=p[1]-q[1], dz=p[2]-q[2];
        float d2=dx*dx+dy*dy+dz*dz;
        if(d2 > r*r) return 0;
        float d=sqrtf(d2);
        if(d>1e-6f){ out_n[0]=dx/d; out_n[1]=dy/d; out_n[2]=dz/d; *out_depth=r-d; }
        else {
            /* center inside the box: push out along the least-penetrated axis */
            float best=1e30f; int axis=0; float sign=1;
            for(int i=0;i<3;i++){
                float pen = col->h[i] - fabsf(p[i]-col->c[i]);
                if(pen<best){ best=pen; axis=i; sign=(p[i]>=col->c[i])?1.0f:-1.0f; }
            }
            out_n[0]=out_n[1]=out_n[2]=0; out_n[axis]=sign; *out_depth=r+best;
        }
        return 1;
    } else { /* plane */
        float dist = p[0]*col->n[0]+p[1]*col->n[1]+p[2]*col->n[2] - col->d;
        if(dist > r) return 0;
        out_n[0]=col->n[0]; out_n[1]=col->n[1]; out_n[2]=col->n[2];
        *out_depth = r - dist;   /* may be >r if behind the plane */
        return 1;
    }
}

CCMoveResult cc_move_slide(const CCMoveWorld* w, const float pos[3], float radius,
                           const float disp[3], float out_pos[3]){
    CCMoveResult res={0};
    res.ground_y = pos[1];
    float p[3]={pos[0], pos[1], pos[2]};
    if(!w||w->count==0){ out_pos[0]=pos[0]+disp[0];out_pos[1]=pos[1]+disp[1];out_pos[2]=pos[2]+disp[2]; return res; }

    /* Substep the move so the sphere never advances more than ~half its radius
       per step — this prevents tunneling through thin/near collider faces, so
       the closest-point depenetration always resolves against the entered face
       (producing a correct stop + slide) rather than ejecting out the far side. */
    float len = sqrtf(disp[0]*disp[0]+disp[1]*disp[1]+disp[2]*disp[2]);
    float step_max = radius*0.5f; if(step_max<1e-4f) step_max=1e-4f;
    int steps = (int)ceilf(len/step_max); if(steps<1) steps=1; if(steps>512) steps=512;
    float inv=1.0f/(float)steps;
    float d[3]={disp[0]*inv, disp[1]*inv, disp[2]*inv};

    for(int s=0; s<steps; s++){
        p[0]+=d[0]; p[1]+=d[1]; p[2]+=d[2];
        /* resolve penetrations at this sub-position (a few passes for corners) */
        for(int it=0; it<4; it++){
            float best_depth=0, best_n[3]={0,0,0}; int any=0;
            for(uint32_t i=0;i<w->count;i++){
                float n[3], depth;
                if(sphere_collide(&w->items[i], p, radius, n, &depth)){
                    if(depth>best_depth){ best_depth=depth; best_n[0]=n[0];best_n[1]=n[1];best_n[2]=n[2]; any=1; }
                }
            }
            if(!any) break;
            p[0]+=best_n[0]*best_depth; p[1]+=best_n[1]*best_depth; p[2]+=best_n[2]*best_depth;
            res.hit=true;
            if(it==0) res.contacts++;
            if(best_n[1] > 0.5f){ res.grounded=true; res.ground_y = p[1]-radius; }
        }
    }
    out_pos[0]=p[0]; out_pos[1]=p[1]; out_pos[2]=p[2];
    return res;
}
