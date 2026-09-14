/* voxel.c — Marching Cubes mesher (see voxel.h). Standalone, libm only.
 * Uses the standard Paul Bourke edge/triangle tables. */
#include "voxel/voxel.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

const char* voxel_version(void){ return VOXEL_VERSION; }

/* ---- standard marching cubes tables (Paul Bourke) ---- */
static const int EDGE_TABLE[256] = {
0x0,0x109,0x203,0x30a,0x406,0x50f,0x605,0x70c,0x80c,0x905,0xa0f,0xb06,0xc0a,0xd03,0xe09,0xf00,
0x190,0x99,0x393,0x29a,0x596,0x49f,0x795,0x69c,0x99c,0x895,0xb9f,0xa96,0xd9a,0xc93,0xf99,0xe90,
0x230,0x339,0x33,0x13a,0x636,0x73f,0x435,0x53c,0xa3c,0xb35,0x83f,0x936,0xe3a,0xf33,0xc39,0xd30,
0x3a0,0x2a9,0x1a3,0xaa,0x7a6,0x6af,0x5a5,0x4ac,0xbac,0xaa5,0x9af,0x8a6,0xfaa,0xea3,0xda9,0xca0,
0x460,0x569,0x663,0x76a,0x66,0x16f,0x265,0x36c,0xc6c,0xd65,0xe6f,0xf66,0x86a,0x963,0xa69,0xb60,
0x5f0,0x4f9,0x7f3,0x6fa,0x1f6,0xff,0x3f5,0x2fc,0xdfc,0xcf5,0xfff,0xef6,0x9fa,0x8f3,0xbf9,0xaf0,
0x650,0x759,0x453,0x55a,0x256,0x35f,0x55,0x15c,0xe5c,0xf55,0xc5f,0xd56,0xa5a,0xb53,0x859,0x950,
0x7c0,0x6c9,0x5c3,0x4ca,0x3c6,0x2cf,0x1c5,0xcc,0xfcc,0xec5,0xdcf,0xcc6,0xbca,0xac3,0x9c9,0x8c0,
0x8c0,0x9c9,0xac3,0xbca,0xcc6,0xdcf,0xec5,0xfcc,0xcc,0x1c5,0x2cf,0x3c6,0x4ca,0x5c3,0x6c9,0x7c0,
0x950,0x859,0xb53,0xa5a,0xd56,0xc5f,0xf55,0xe5c,0x15c,0x55,0x35f,0x256,0x55a,0x453,0x759,0x650,
0xaf0,0xbf9,0x8f3,0x9fa,0xef6,0xfff,0xcf5,0xdfc,0x2fc,0x3f5,0xff,0x1f6,0x6fa,0x7f3,0x4f9,0x5f0,
0xb60,0xa69,0x963,0x86a,0xf66,0xe6f,0xd65,0xc6c,0x36c,0x265,0x16f,0x66,0x76a,0x663,0x569,0x460,
0xca0,0xda9,0xea3,0xfaa,0x8a6,0x9af,0xaa5,0xbac,0x4ac,0x5a5,0x6af,0x7a6,0xaa,0x1a3,0x2a9,0x3a0,
0xd30,0xc39,0xf33,0xe3a,0x936,0x83f,0xb35,0xa3c,0x53c,0x435,0x73f,0x636,0x13a,0x33,0x339,0x230,
0xe90,0xf99,0xc93,0xd9a,0xa96,0xb9f,0x895,0x99c,0x69c,0x795,0x49f,0x596,0x29a,0x393,0x99,0x190,
0xf00,0xe09,0xd03,0xc0a,0xb06,0xa0f,0x905,0x80c,0x70c,0x605,0x50f,0x406,0x30a,0x203,0x109,0x0
};

/* Triangle table: for each of 256 cube configs, up to 5 triangles (edge indices),
   terminated by -1. This is the standard table (kept compact). */
static const int TRI_TABLE[256][16] = {
#include "mc_tritable.inc"
};

/* 12 cube edges → the two corner indices they connect */
static const int EDGE_CORNERS[12][2] = {
 {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
};
/* 8 cube corner offsets */
static const int CORNER[8][3] = {
 {0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}
};

/* simple growable arrays */
typedef struct { float* d; size_t n, cap; } FArr;
typedef struct { uint32_t* d; size_t n, cap; } IArr;
static int fpush3(FArr* a, float x, float y, float z){
    if(a->n+3 > a->cap){ size_t nc=a->cap? a->cap*2 : 4096; float* nd=realloc(a->d,nc*sizeof(float)); if(!nd)return -1; a->d=nd; a->cap=nc; }
    a->d[a->n++]=x; a->d[a->n++]=y; a->d[a->n++]=z; return 0;
}
static int ipush(IArr* a, uint32_t v){
    if(a->n+1 > a->cap){ size_t nc=a->cap? a->cap*2 : 4096; uint32_t* nd=realloc(a->d,nc*sizeof(uint32_t)); if(!nd)return -1; a->d=nd; a->cap=nc; }
    a->d[a->n++]=v; return 0;
}

static void grad_normal(VoxelDensityFn f, void* u, float x,float y,float z, float h, float out[3]){
    float dx = f(x+h,y,z,u)-f(x-h,y,z,u);
    float dy = f(x,y+h,z,u)-f(x,y-h,z,u);
    float dz = f(x,y,z+h,u)-f(x,y,z-h,u);
    float l = sqrtf(dx*dx+dy*dy+dz*dz); if(l<1e-6f)l=1;
    /* surface normal points toward AIR (increasing density), i.e. up out of solid */
    out[0]=dx/l; out[1]=dy/l; out[2]=dz/l;
}

int voxel_generate(const VoxelRegion* R, VoxelDensityFn density, void* user, VoxelMesh* out){
    memset(out,0,sizeof *out);
    FArr pos={0}, nrm={0}; IArr idx={0};
    float sx=(R->max[0]-R->min[0])/R->nx;
    float sy=(R->max[1]-R->min[1])/R->ny;
    float sz=(R->max[2]-R->min[2])/R->nz;
    float hn = 0.5f*fminf(sx,fminf(sy,sz));   /* step for gradient normals */

    for(uint32_t iz=0; iz<R->nz; iz++)
    for(uint32_t iy=0; iy<R->ny; iy++)
    for(uint32_t ix=0; ix<R->nx; ix++){
        float bx=R->min[0]+ix*sx, by=R->min[1]+iy*sy, bz=R->min[2]+iz*sz;
        /* sample the 8 corners */
        float val[8]; float cpx[8][3];
        int cubeindex=0;
        for(int c=0;c<8;c++){
            float px=bx+CORNER[c][0]*sx, py=by+CORNER[c][1]*sy, pz=bz+CORNER[c][2]*sz;
            cpx[c][0]=px; cpx[c][1]=py; cpx[c][2]=pz;
            val[c]=density(px,py,pz,user);
            if(val[c] < R->iso) cubeindex |= (1<<c);   /* corner is solid */
        }
        int edges=EDGE_TABLE[cubeindex];
        if(edges==0) continue;   /* fully in or out — no surface here */
        /* interpolate vertex on each crossed edge */
        float everts[12][3];
        for(int e=0;e<12;e++) if(edges & (1<<e)){
            int a=EDGE_CORNERS[e][0], b=EDGE_CORNERS[e][1];
            float va=val[a], vb=val[b];
            float t = (fabsf(vb-va)<1e-6f)? 0.5f : (R->iso - va)/(vb-va);
            everts[e][0]=cpx[a][0]+t*(cpx[b][0]-cpx[a][0]);
            everts[e][1]=cpx[a][1]+t*(cpx[b][1]-cpx[a][1]);
            everts[e][2]=cpx[a][2]+t*(cpx[b][2]-cpx[a][2]);
        }
        /* emit triangles */
        const int* tri=TRI_TABLE[cubeindex];
        for(int t=0; tri[t]!=-1; t+=3){
            for(int k=0;k<3;k++){
                const float* v=everts[tri[t+k]];
                float n[3]; grad_normal(density,user, v[0],v[1],v[2], hn, n);
                if(fpush3(&pos, v[0],v[1],v[2])) goto oom;
                if(fpush3(&nrm, n[0],n[1],n[2])) goto oom;
                if(ipush(&idx, (uint32_t)(pos.n/3 - 1))) goto oom;
            }
        }
    }
    out->positions=pos.d; out->normals=nrm.d; out->indices=idx.d;
    out->vertex_count=(uint32_t)(pos.n/3); out->index_count=(uint32_t)idx.n;
    return 0;
oom:
    free(pos.d); free(nrm.d); free(idx.d);
    return -1;
}

void voxel_mesh_free(VoxelMesh* m){
    if(!m) return; free(m->positions); free(m->normals); free(m->indices);
    memset(m,0,sizeof *m);
}

/* ---- ready-made terrain + caves density ---- */
/* cheap 3D value noise (hash-based) so this file needs no external noise dep */
static float hash3(int x,int y,int z,uint64_t seed){
    uint64_t h = (uint64_t)x*73856093u ^ (uint64_t)y*19349663u ^ (uint64_t)z*83492791u ^ seed;
    h = (h ^ (h>>13)) * 0x9E3779B97F4A7C15ull; h ^= h>>16;
    return (float)((h & 0xFFFFFF)/(double)0xFFFFFF)*2.0f-1.0f;   /* -1..1 */
}
static float vnoise3(float x,float y,float z,uint64_t seed){
    int xi=(int)floorf(x), yi=(int)floorf(y), zi=(int)floorf(z);
    float xf=x-xi, yf=y-yi, zf=z-zi;
    float u=xf*xf*(3-2*xf), v=yf*yf*(3-2*yf), w=zf*zf*(3-2*zf);
    float c[8];
    for(int i=0;i<8;i++) c[i]=hash3(xi+(i&1),yi+((i>>1)&1),zi+((i>>2)&1),seed);
    float x00=c[0]+u*(c[1]-c[0]), x10=c[2]+u*(c[3]-c[2]);
    float x01=c[4]+u*(c[5]-c[4]), x11=c[6]+u*(c[7]-c[6]);
    float y0=x00+v*(x10-x00), y1=x01+v*(x11-x01);
    return y0+w*(y1-y0);
}
static float fbm3(float x,float y,float z,uint64_t seed,int oct){
    float sum=0, amp=0.5f, f=1;
    for(int i=0;i<oct;i++){ sum+=vnoise3(x*f,y*f,z*f,seed+i)*amp; f*=2; amp*=0.5f; }
    return sum;
}

float voxel_terrain_density(float x, float y, float z, void* params){
    VoxelTerrainParams* p=(VoxelTerrainParams*)params;
    /* --- surface (hills) --- */
    float hills = fbm3(x*p->hill_freq, 0.0f, z*p->hill_freq, p->seed, 4) * p->hill_amp;
    float surface = p->ground_height + hills;
    float d = y - surface;              /* >0 air above ground, <0 solid below */
    float depth = surface - y;          /* 0 at surface, grows downward */

    /* --- 1. TUNNELS: thin winding worm-like passages (the exception, not the rule) --- */
    float n1 = fbm3(x*p->cave_freq,       y*p->cave_freq*0.7f, z*p->cave_freq,       p->seed+101, 3);
    float n2 = fbm3(x*p->cave_freq+31.4f, y*p->cave_freq*0.7f, z*p->cave_freq-17.2f, p->seed+202, 3);
    /* cave_threshold (0..~0.15) is the ONE knob for how cavey it is: bigger =
       wider tunnels + more chambers. ~0.05 = sparse caves in mostly-solid rock. */
    float tw = p->cave_threshold*0.4f + 0.02f;
    float tA = tw - fabsf(n1);
    float tB = tw - fabsf(n2);
    float worm = (tA>0 && tB>0) ? fminf(tA,tB) : -1.0f;

    /* --- 2. CHAMBERS: big caverns, but RARE and only well underground --- */
    float chamber_field = fbm3(x*p->cave_freq*0.30f, y*p->cave_freq*0.30f, z*p->cave_freq*0.30f, p->seed+303, 3);
    float depth_gate = (depth-4.0f)/8.0f; if(depth_gate<0)depth_gate=0; if(depth_gate>1)depth_gate=1;
    /* high threshold → chambers only where the field peaks; eased a little by depth */
    float chamber = chamber_field - (0.88f - p->cave_threshold*1.5f - 0.15f*depth_gate);

    /* --- 3. ENTRANCES: widen tunnels near the surface so a few open as mouths --- */
    if(depth > -1.0f && depth < 3.5f){
        float widen = 0.06f * (1.0f - fabsf(depth-0.8f)/2.5f);
        if(widen>0) worm += widen;
    }

    /* combine: hollow where inside a tunnel OR a chamber */
    float hollow = fmaxf(worm, chamber);
    if(d < 0.0f && hollow > 0.0f) d = hollow;   /* carve solid rock → air */
    return d;
}
