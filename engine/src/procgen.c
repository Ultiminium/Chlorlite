#include "cc/procgen.h"
#include "cc/claudecore.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ─── Stateless value-noise (seed honored on every call, real 3D) ────── */
/* Integer hash → [0,1). Pure function of (ix,iy,iz,seed) — no global state. */
static float hash3i(int64_t x, int64_t y, int64_t z, uint64_t seed) {
    uint64_t h = seed + 0x9E3779B97F4A7C15ULL;
    h ^= (uint64_t)(x * 0xA24BAED4963EE407ULL);
    h ^= (uint64_t)(y * 0x9FB21C651E98DF25ULL);
    h ^= (uint64_t)(z * 0xC2B2AE3D27D4EB4FULL);
    h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 32;
    return (float)(h >> 40) / (float)(1u << 24);  /* 24-bit mantissa → [0,1) */
}

static float fade(float t)  { return t*t*t*(t*(t*6-15)+10); }
static float lerp2(float a, float b, float t) { return a + t*(b-a); }

float cc_noise3(float x, float y, float z, uint64_t seed) {
    int64_t ix=(int64_t)floorf(x), iy=(int64_t)floorf(y), iz=(int64_t)floorf(z);
    float fx=x-ix, fy=y-iy, fz=z-iz;
    float u=fade(fx), v=fade(fy), w=fade(fz);
    /* trilinear interpolation of 8 lattice corners */
    float c000=hash3i(ix,  iy,  iz,  seed), c100=hash3i(ix+1,iy,  iz,  seed);
    float c010=hash3i(ix,  iy+1,iz,  seed), c110=hash3i(ix+1,iy+1,iz,  seed);
    float c001=hash3i(ix,  iy,  iz+1,seed), c101=hash3i(ix+1,iy,  iz+1,seed);
    float c011=hash3i(ix,  iy+1,iz+1,seed), c111=hash3i(ix+1,iy+1,iz+1,seed);
    float x00=lerp2(c000,c100,u), x10=lerp2(c010,c110,u);
    float x01=lerp2(c001,c101,u), x11=lerp2(c011,c111,u);
    float y0=lerp2(x00,x10,v), y1=lerp2(x01,x11,v);
    return lerp2(y0,y1,w)*2.0f-1.0f;   /* → [-1,1] */
}

float cc_noise2(float x, float y, uint64_t seed) {
    return cc_noise3(x, y, 0.0f, seed);
}

float cc_fbm2(float x, float y, uint64_t seed, int octaves, float persistence, float lacunarity) {
    float value=0, amp=1, freq=1, max_val=0;
    for (int i=0;i<octaves;i++) {
        value += cc_noise2(x*freq, y*freq, seed+i) * amp;
        max_val += amp; amp *= persistence; freq *= lacunarity;
    }
    return value / max_val;
}

/* ─── Texture generation ──────────────────────────────────────────────── */
CCTexture cc_procgen_texture(CCEngine* eng, const CCProcTexDesc* desc) {
    uint32_t w=desc->width, h=desc->height;
    uint8_t* px = malloc(w*h*4);
    for (uint32_t y=0;y<h;y++) for (uint32_t x=0;x<w;x++) {
        float u=(float)x/w, v=(float)y/h;
        float val=0;
        switch(desc->type) {
            case CC_PROC_TEX_NOISE:
                val = (cc_fbm2(u*desc->scale, v*desc->scale, desc->seed,
                               (int)desc->octaves, desc->persistence, desc->lacunarity)+1)*0.5f;
                break;
            case CC_PROC_TEX_CHECKER:
                val = ((int)(u*desc->scale)+(int)(v*desc->scale))%2 ? 1.0f : 0.0f;
                break;
            case CC_PROC_TEX_GRADIENT:
                val = u;
                break;
            case CC_PROC_TEX_VORONOI: {
                float minD=1e9f;
                int cells=(int)desc->scale;
                for (int cy=0;cy<cells;cy++) for (int cx=0;cx<cells;cx++) {
                    uint64_t h2 = desc->seed ^ (cx*2654435761ULL) ^ (cy*2246822519ULL);
                    float px2=(cx+(float)(h2&0xffff)/65536.0f)/cells;
                    float py2=(cy+(float)((h2>>16)&0xffff)/65536.0f)/cells;
                    float d=(u-px2)*(u-px2)+(v-py2)*(v-py2);
                    if (d<minD) minD=d;
                }
                val=sqrtf(minD)*cells;
                break;
            }
            case CC_PROC_TEX_CUSTOM:
                if (desc->custom) { uint32_t c=desc->custom(u,v,desc->seed,desc->custom_userdata);
                    px[(y*w+x)*4+0]=(c>>24)&0xff; px[(y*w+x)*4+1]=(c>>16)&0xff;
                    px[(y*w+x)*4+2]=(c>>8)&0xff;  px[(y*w+x)*4+3]=c&0xff; continue; }
                break;
            default: val=(float)(x^y)*0.001f; break;
        }
        val=val<0?0:val>1?1:val;
        uint32_t ca=desc->color_a, cb2=desc->color_b;
        px[(y*w+x)*4+0]=(uint8_t)(((ca>>24)&0xff)*(1-val)+((cb2>>24)&0xff)*val);
        px[(y*w+x)*4+1]=(uint8_t)(((ca>>16)&0xff)*(1-val)+((cb2>>16)&0xff)*val);
        px[(y*w+x)*4+2]=(uint8_t)(((ca>>8)&0xff)*(1-val)+((cb2>>8)&0xff)*val);
        px[(y*w+x)*4+3]=255;
    }
    CCTextureDesc td={.width=w,.height=h,.format=CC_FMT_RGBA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=true};
    CCTexture t = cc_texture_create(eng, &td, px);
    free(px);
    return t;
}

CCTexture cc_procgen_noise_texture(CCEngine* eng, uint32_t w, uint32_t h, uint64_t seed, float scale) {
    CCProcTexDesc d={.width=w,.height=h,.type=CC_PROC_TEX_NOISE,.seed=seed,.scale=scale,
                     .octaves=6,.persistence=0.5f,.lacunarity=2.0f,
                     .color_a=0x000000ff,.color_b=0xffffffff};
    return cc_procgen_texture(eng, &d);
}

/* ─── Terrain generation ─────────────────────────────────────────────────
 * A shared fbm heightfield so the heightmap texture and the terrain mesh agree.
 * height in [0,1]; ridged-ish layered value noise, domain in grid-normalized uv. */
static float terrain_height(float u, float v, uint64_t seed, int octaves){
    if(octaves<1) octaves=1; if(octaves>10) octaves=10;
    float sum=0, amp=0.5f, freq=2.5f, norm=0;
    for(int i=0;i<octaves;i++){
        float n = cc_noise2(u*freq, v*freq, seed + (uint64_t)i*1319u); /* 0..1 */
        sum  += amp * n;
        norm += amp; amp*=0.5f; freq*=2.03f;
    }
    float h = norm>0 ? sum/norm : 0.0f;             /* 0..1 */
    /* gentle ridge shaping: emphasise mid heights, flatten extremes a touch */
    h = h*h*(3.0f - 2.0f*h);                        /* smoothstep */
    return h;
}

CCTextureId cc_procgen_terrain_heightmap(CCEngine* eng, uint32_t w, uint32_t h,
                                          uint64_t seed, int octaves){
    if(!eng||!w||!h) return 0;
    uint8_t* px = (uint8_t*)malloc((size_t)w*h*4);
    if(!px) return 0;
    for(uint32_t y=0;y<h;y++){
        for(uint32_t x=0;x<w;x++){
            float u=(float)x/(float)(w-1?w-1:1);
            float v=(float)y/(float)(h-1?h-1:1);
            float ht=terrain_height(u,v,seed,octaves);
            uint8_t g=(uint8_t)(ht*255.0f+0.5f);
            size_t i=((size_t)y*w+x)*4;
            px[i]=g; px[i+1]=g; px[i+2]=g; px[i+3]=255;   /* grayscale height in RGB */
        }
    }
    CCTextureDesc td={.width=w,.height=h,.format=CC_FMT_RGBA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=false};
    CCTexture t=cc_texture_create(eng,&td,px);
    free(px);
    return t;
}

CCMeshId cc_procgen_terrain(CCEngine* eng, const CCProcTerrainDesc* desc){
    if(!eng||!desc||desc->grid_w<2||desc->grid_h<2) return 0;
    uint32_t gw=desc->grid_w, gh=desc->grid_h;
    float cs = desc->cell_size>0 ? desc->cell_size : 1.0f;
    float hs = desc->height_scale>0 ? desc->height_scale : 1.0f;
    int oct = desc->octaves>0 ? desc->octaves : 5;
    uint64_t seed = desc->heightmap_seed;

    uint32_t nv=gw*gh;
    uint32_t ntri=(gw-1)*(gh-1)*2;
    uint32_t ni=ntri*3;
    CCVertex* V=(CCVertex*)malloc(sizeof(CCVertex)*nv);
    uint32_t* I=(uint32_t*)malloc(sizeof(uint32_t)*ni);
    if(!V||!I){ free(V); free(I); return 0; }

    float ox=-0.5f*(gw-1)*cs, oz=-0.5f*(gh-1)*cs;  /* center the terrain */
    for(uint32_t z=0;z<gh;z++){
        for(uint32_t x=0;x<gw;x++){
            float u=(float)x/(float)(gw-1);
            float v=(float)z/(float)(gh-1);
            float ht=terrain_height(u,v,seed,oct)*hs;
            uint32_t idx=z*gw+x;
            V[idx].pos[0]=ox + x*cs;
            V[idx].pos[1]=ht;
            V[idx].pos[2]=oz + z*cs;
            V[idx].normal[0]=0; V[idx].normal[1]=1; V[idx].normal[2]=0;
            V[idx].uv[0]=u; V[idx].uv[1]=v;
            V[idx].tangent[0]=1; V[idx].tangent[1]=0; V[idx].tangent[2]=0; V[idx].tangent[3]=1;
            /* height-based vertex tint: low=rock, mid=grass, high=snow-ish */
            float hn = hs>0 ? ht/hs : 0;
            uint8_t r,g,b;
            if(hn<0.35f){      r=90;  g=110; b=70;  }      /* low grass */
            else if(hn<0.7f){  r=120; g=130; b=90;  }      /* dry slope */
            else {             r=190; g=195; b=200; }      /* rocky/snow */
            V[idx].color[0]=r; V[idx].color[1]=g; V[idx].color[2]=b; V[idx].color[3]=255;
        }
    }
    uint32_t ii=0;
    for(uint32_t z=0;z<gh-1;z++){
        for(uint32_t x=0;x<gw-1;x++){
            uint32_t a=z*gw+x, b=a+1, c=a+gw, d=c+1;
            I[ii++]=a; I[ii++]=c; I[ii++]=b;
            I[ii++]=b; I[ii++]=c; I[ii++]=d;
        }
    }
    cc_geometry_recompute_normals(V,nv,I,ni,true);   /* smooth terrain normals */
    CCMesh m=cc_mesh_create(eng,V,nv,I,ni,CC_MESH_STATIC);
    free(V); free(I);
    return m;
}

/* ─── Sprite sheet + 2D animation generation ─────────────────────────────
 * Builds a horizontal strip of frame_count frames into one RGBA texture. Each
 * frame is drawn by a built-in generator chosen by anim_type (or a custom cb).
 * The built-ins make simple procedural motion (a bouncing/'breathing' blob,
 * a walk-cycle bob, an expanding explosion) — enough for placeholder sprites
 * and to exercise the animation pipeline. */
static void sprite_put(uint8_t* px, uint32_t W, uint32_t x, uint32_t y, uint32_t col){
    uint8_t r=(col>>24)&255,g=(col>>16)&255,b=(col>>8)&255,a=col&255;
    size_t i=((size_t)y*W+x)*4; px[i]=r;px[i+1]=g;px[i+2]=b;px[i+3]=a;
}
static void sprite_disc(uint8_t* px,uint32_t W,uint32_t fx,uint32_t fw,uint32_t fh,
                        float cx,float cy,float rad,uint32_t col){
    for(uint32_t y=0;y<fh;y++)for(uint32_t x=0;x<fw;x++){
        float dx=x-cx,dy=y-cy;
        if(dx*dx+dy*dy<=rad*rad) sprite_put(px,W,fx+x,y,col);
    }
}

CCTextureId cc_procgen_sprite_sheet(CCEngine* eng, const CCProcSpriteDesc* desc){
    if(!eng||!desc||!desc->frame_count||!desc->frame_w||!desc->frame_h) return 0;
    uint32_t fw=desc->frame_w, fh=desc->frame_h, fc=desc->frame_count;
    uint32_t W=fw*fc, H=fh;
    uint8_t* px=(uint8_t*)calloc((size_t)W*H*4,1);
    if(!px) return 0;
    uint32_t body = desc->palette_count>0 ? desc->palette[0] : 0xE0B070FFu;
    uint32_t accent = desc->palette_count>1 ? desc->palette[1] : 0xFFFFFFFFu;
    for(uint32_t f=0; f<fc; f++){
        uint32_t fx=f*fw;
        float t=(float)f/(float)(fc>1?fc-1:1);       /* 0..1 across the anim */
        if(desc->anim_type==CC_PROC_SPRITE_CUSTOM && desc->custom_frame){
            /* hand the frame's sub-rect to the callback (row-major, frame-local) */
            uint8_t* frame=(uint8_t*)malloc((size_t)fw*fh*4);
            memset(frame,0,(size_t)fw*fh*4);
            desc->custom_frame(frame,fw,fh,f,desc->custom_userdata);
            for(uint32_t y=0;y<fh;y++)for(uint32_t x=0;x<fw;x++){
                size_t si=((size_t)y*fw+x)*4, di=((size_t)y*W+(fx+x))*4;
                px[di]=frame[si];px[di+1]=frame[si+1];px[di+2]=frame[si+2];px[di+3]=frame[si+3];
            }
            free(frame);
            continue;
        }
        float cx=fw*0.5f, cy=fh*0.5f, base=fh*0.32f;
        switch(desc->anim_type){
            case CC_PROC_SPRITE_IDLE: {   /* breathing */
                float r=base*(1.0f+0.06f*sinf(t*6.2831853f));
                sprite_disc(px,W,fx,fw,fh,cx,cy,r,body); break; }
            case CC_PROC_SPRITE_WALK:
            case CC_PROC_SPRITE_RUN: {     /* bob up/down + squash */
                float bob=sinf(t*6.2831853f)*fh*0.08f;
                sprite_disc(px,W,fx,fw,fh,cx,cy+bob,base,body);
                sprite_disc(px,W,fx,fw,fh,cx,cy+bob-base*0.5f,base*0.5f,accent); break; }
            case CC_PROC_SPRITE_JUMP: {    /* rise then stretch */
                float y=cy - t*fh*0.25f;
                sprite_disc(px,W,fx,fw,fh,cx,y,base*(1.0f-0.2f*t),body); break; }
            case CC_PROC_SPRITE_ATTACK: {  /* lunge forward */
                float x=cx + t*fw*0.2f;
                sprite_disc(px,W,fx,fw,fh,x,cy,base,body);
                sprite_disc(px,W,fx,fw,fh,x+base,cy,base*0.4f,accent); break; }
            case CC_PROC_SPRITE_EXPLOSION: { /* expanding ring, fading */
                float r=base*(0.3f+1.6f*t);
                uint32_t a=(uint32_t)(255*(1.0f-t));
                uint32_t col=(accent&0xFFFFFF00u)|(a&255);
                sprite_disc(px,W,fx,fw,fh,cx,cy,r,col); break; }
            default:
                sprite_disc(px,W,fx,fw,fh,cx,cy,base,body); break;
        }
    }
    CCTextureDesc td={.width=W,.height=H,.format=CC_FMT_RGBA8,.mipmaps=false,.linear_filter=false,.wrap_repeat=false};
    CCTexture t=cc_texture_create(eng,&td,px);
    free(px);
    return t;
}

/* small animation registry */
#define CC_MAX_PROC_ANIMS 128
static CCAnimation g_anims[CC_MAX_PROC_ANIMS];
static bool g_anim_valid[CC_MAX_PROC_ANIMS];

CCAnimationId cc_procgen_animation(CCEngine* eng, const CCProcSpriteDesc* desc, float fps){
    if(!eng||!desc) return 0;
    CCTextureId tex=cc_procgen_sprite_sheet(eng,desc);
    if(!tex) return 0;
    for(uint32_t i=1;i<CC_MAX_PROC_ANIMS;i++){
        if(!g_anim_valid[i]){
            g_anims[i]=(CCAnimation){ .texture_id=tex, .frame_w=desc->frame_w, .frame_h=desc->frame_h,
                                      .frame_count=desc->frame_count, .fps=fps>0?fps:12.0f, .loop=true };
            g_anim_valid[i]=true;
            return i;
        }
    }
    return 0;
}
CCAnimation* cc_animation_get(CCEngine* eng, CCAnimationId id){
    (void)eng;
    if(id && id<CC_MAX_PROC_ANIMS && g_anim_valid[id]) return &g_anims[id];
    return NULL;
}
void cc_animation_destroy(CCEngine* eng, CCAnimationId id){
    (void)eng;
    if(id && id<CC_MAX_PROC_ANIMS) g_anim_valid[id]=false;
}
