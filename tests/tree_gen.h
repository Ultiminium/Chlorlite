/* tree_gen.h — procedural "less-perfect" tree geometry for CC.
 *
 * The realism tell this attacks: mathematically perfect primitives read as CG.
 * Real trees are irregular — lumpy foliage, tapering gnarled trunks that lean,
 * boughs that fork. Everything here builds raw CCVertex/index buffers, perturbs
 * every vertex with 3D fbm noise, recomputes normals so the bumps light
 * correctly, and bakes to a CCMesh. Header-only; include after claudecore.h and
 * procgen.h.
 *
 * Provides:
 *   cc_mesh_lumpy_canopy(eng, radius, seed, bumpiness) -> irregular foliage blob
 *   cc_mesh_gnarled_trunk(eng, botR, topR, height, lean, seed) -> tapered+bent
 *   tree_build(eng, TreeParams) -> composes trunk + branches + canopies as props
 */
#ifndef CC_TREE_GEN_H
#define CC_TREE_GEN_H
#include "cc/claudecore.h"
#include "cc/procgen.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── 3D fractal noise (built from cc_noise3) ─────────────────────────────── */
static float tg_fbm3(float x,float y,float z,uint64_t seed,int oct){
    float sum=0, amp=0.5f, freq=1.0f, norm=0;
    for(int i=0;i<oct;i++){
        sum += amp * cc_noise3(x*freq,y*freq,z*freq,seed+i*131);
        norm += amp; amp*=0.5f; freq*=2.03f;
    }
    return norm>0? sum/norm : 0.0f;   /* ~ -1..1 */
}

/* small helpers */
static void tg_setv(CCVertex* V,int i,float px,float py,float pz,float u,float vv,
                    int cr,int cg,int cb){
    V[i].pos[0]=px; V[i].pos[1]=py; V[i].pos[2]=pz;
    V[i].normal[0]=0;V[i].normal[1]=1;V[i].normal[2]=0;
    V[i].uv[0]=u; V[i].uv[1]=vv;
    V[i].tangent[0]=1;V[i].tangent[1]=0;V[i].tangent[2]=0;V[i].tangent[3]=1;
    V[i].color[0]=cr;V[i].color[1]=cg;V[i].color[2]=cb;V[i].color[3]=255;
}

/* ── Lumpy canopy: a UV sphere whose radius is modulated by 3D fbm, with a few
 * larger low-frequency lobes so the silhouette is irregular (not a ball). ── */
static CCMesh cc_mesh_lumpy_canopy(CCEngine* eng,float radius,uint64_t seed,float bump){
    const int SL=28, ST=20;                 /* slices, stacks */
    int nv=(SL+1)*(ST+1);
    int ni=SL*ST*6;
    CCVertex* V=(CCVertex*)malloc(sizeof(CCVertex)*nv);
    uint32_t* I=(uint32_t*)malloc(sizeof(uint32_t)*ni);
    int vi=0;
    for(int y=0;y<=ST;y++){
        float v=(float)y/ST;  float phi=v*3.14159265f;      /* 0..pi */
        for(int x=0;x<=SL;x++){
            float u=(float)x/SL; float th=u*6.2831853f;     /* 0..2pi */
            float sx=sinf(phi)*cosf(th), sy=cosf(phi), sz=sinf(phi)*sinf(th);
            /* radial displacement: big lobes + medium bumps + fine detail */
            float lobes = tg_fbm3(sx*1.3f, sy*1.3f, sz*1.3f, seed, 2);     /* broad */
            float mid   = tg_fbm3(sx*3.1f, sy*3.1f, sz*3.1f, seed+9, 3);   /* clumps */
            float fine  = tg_fbm3(sx*7.0f, sy*7.0f, sz*7.0f, seed+21,2);   /* leaf-mass ripple */
            float disp = 1.0f + bump*(0.45f*lobes + 0.28f*mid + 0.14f*fine);
            if (disp<0.45f) disp=0.45f;
            /* flatten the very bottom a touch so it sits on the branches */
            float rad = radius*disp;
            float px=sx*rad, py=sy*rad, pz=sz*rad;
            /* per-vertex leaf color variation (darker in crevices where disp<1) */
            float shade = 0.5f+0.5f*(disp-1.0f)/(bump>0?bump:1.0f);
            int cg=170+(int)(shade*60), cr=90+(int)(shade*40), cb=60+(int)(shade*20);
            tg_setv(V,vi,px,py,pz,u,v,cr,cg,cb);
            vi++;
        }
    }
    int ii=0;
    for(int y=0;y<ST;y++) for(int x=0;x<SL;x++){
        int a=y*(SL+1)+x, b=a+1, c=a+(SL+1), d=c+1;
        I[ii++]=a;I[ii++]=c;I[ii++]=b;  I[ii++]=b;I[ii++]=c;I[ii++]=d;
    }
    cc_geometry_recompute_normals(V,nv,I,ni,true);   /* smooth over the bumps */
    CCMesh m=cc_mesh_create(eng,V,nv,I,ni,CC_MESH_STATIC);
    free(V);free(I);
    return m;
}

/* ── Gnarled trunk: a cylinder of stacked rings. Each ring's centre drifts
 * (lean + wander), its radius tapers bottom→top and wobbles with noise, and
 * every vertex gets fine bark relief. Root flare at the base. ── */
static CCMesh cc_mesh_gnarled_trunk(CCEngine* eng,float botR,float topR,
                                    float height,float lean,uint64_t seed){
    const int SEG=12, RINGS=14;
    int nv=(SEG+1)*(RINGS+1);
    int ni=SEG*RINGS*6;
    CCVertex* V=(CCVertex*)malloc(sizeof(CCVertex)*nv);
    uint32_t* I=(uint32_t*)malloc(sizeof(uint32_t)*ni);
    int vi=0;
    for(int r=0;r<=RINGS;r++){
        float t=(float)r/RINGS;                     /* 0 base .. 1 top */
        float y=t*height;
        /* centre drift: gentle S-curve lean + low-freq wander */
        float wanderx=tg_fbm3(0,t*2.4f,0,seed+3,3);
        float wanderz=tg_fbm3(0,t*2.4f,9.0f,seed+4,3);
        float cx=lean*height*(t*t)*0.5f + wanderx*height*0.05f;
        float cz=wanderz*height*0.05f;
        /* taper + root flare (extra radius near the very base) */
        float taper=botR+(topR-botR)*t;
        float flare=(t<0.16f)? (0.16f-t)/0.16f : 0.0f;
        float ringR=taper*(1.0f + flare*0.6f);
        for(int s=0;s<=SEG;s++){
            float u=(float)s/SEG; float th=u*6.2831853f;
            float bark=tg_fbm3(cosf(th)*2.0f, t*6.0f, sinf(th)*2.0f, seed+7, 3);
            float rr2=ringR*(1.0f + 0.12f*bark);   /* bark relief + radius wobble */
            float px=cx+cosf(th)*rr2;
            float pz=cz+sinf(th)*rr2;
            int cr=88+(int)(bark*26), cg=60+(int)(bark*20), cb=38+(int)(bark*12);
            tg_setv(V,vi,px,y,pz,u,t*3.0f,cr,cg,cb);
            vi++;
        }
    }
    int ii=0;
    for(int r=0;r<RINGS;r++) for(int s=0;s<SEG;s++){
        int a=r*(SEG+1)+s, b=a+1, c=a+(SEG+1), d=c+1;
        I[ii++]=a;I[ii++]=c;I[ii++]=b;  I[ii++]=b;I[ii++]=c;I[ii++]=d;
    }
    cc_geometry_recompute_normals(V,nv,I,ni,true);
    CCMesh m=cc_mesh_create(eng,V,nv,I,ni,CC_MESH_STATIC);
    free(V);free(I);
    return m;
}

#endif /* CC_TREE_GEN_H */
