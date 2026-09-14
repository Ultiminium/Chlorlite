/* nature_test — a small textured nature landscape bringing engine features
 * together: procedural fbm textures (grass ground, bark, leaf canopy, dirt,
 * petals) fed into PBR material albedo maps, composed props (textured-trunk +
 * layered-foliage trees, grass tufts, flowers), a procedural sky + IBL, a warm
 * directional sun with shadows, and bloom. Rendered headless. */
#include "cc/claudecore.h"
#include "cc/procgen.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* ── tiny hash rng for deterministic scattering ── */
static uint32_t RS=1337u;
static float rnd(void){ RS=RS*1664525u+1013904223u; return ((RS>>8)&0xffffff)/16777215.0f; }
static float rrange(float a,float b){ return a+(b-a)*rnd(); }

/* pack helpers */
static uint32_t rgba(int r,int g,int b){ return ((r&255)<<24)|((g&255)<<16)|((b&255)<<8)|255; }

/* ── custom texture generators (u,v in 0..1 → packed RGBA) ─────────────── */
/* lush grass: layered fbm greens with occasional dry/yellow patches + fine blades */
static uint32_t tex_grass(float u,float v,uint64_t seed,void* ud){ (void)ud;
    float f = cc_fbm2(u*8.0f, v*8.0f, seed, 5, 0.5f, 2.0f);      /* -1..1 clumps */
    float blades = cc_fbm2(u*140.0f, v*22.0f, seed+7, 2, 0.5f, 2.0f); /* vertical streaks */
    float t=(f+1)*0.5f;                                          /* 0..1 */
    float g = 70 + t*70 + blades*20;                            /* green channel dominant */
    float r = 25 + t*40 + blades*12;
    float b = 18 + t*26;
    /* dry patches where fbm is high */
    if (t>0.74f){ r+= (t-0.74f)*300; g-= (t-0.74f)*60; }
    return rgba((int)r,(int)g,(int)b);
}
/* tree bark: vertical wood-grain streaks with fbm roughness, brown */
static uint32_t tex_bark(float u,float v,uint64_t seed,void* ud){ (void)ud;
    float rings = sinf(v*3.14159f*3.0f + cc_fbm2(u*6,v*6,seed,3,0.5f,2)*3.0f);
    float grain = cc_fbm2(u*40.0f, v*6.0f, seed, 4, 0.55f, 2.0f);  /* long vertical grain */
    float t=(grain+1)*0.5f;
    float r = 95 + t*70 + rings*10;
    float g = 60 + t*45 + rings*8;
    float b = 35 + t*25;
    return rgba((int)r,(int)g,(int)b);
}
/* leaf canopy: mottled greens, darker in crevices (fbm), some highlights */
static uint32_t tex_leaf(float u,float v,uint64_t seed,void* ud){ (void)ud;
    float f = cc_fbm2(u*14.0f, v*14.0f, seed, 5, 0.6f, 2.1f);
    float t=(f+1)*0.5f;
    float g = 55 + t*95;
    float r = 18 + t*55;
    float b = 15 + t*35;
    /* speckle brighter leaves */
    float sp = cc_noise2(u*90,v*90,seed+3);
    if (sp>0.55f){ g+=30; r+=18; }
    return rgba((int)r,(int)g,(int)b);
}
/* dirt/soil patch under trees */
static uint32_t tex_dirt(float u,float v,uint64_t seed,void* ud){ (void)ud;
    float f=cc_fbm2(u*10,v*10,seed,5,0.5f,2.0f); float t=(f+1)*0.5f;
    float r=95+t*70, g=68+t*45, b=45+t*28;
    float peb=cc_noise2(u*70,v*70,seed+9); if(peb>0.6f){r+=30;g+=28;b+=22;}
    return rgba((int)r,(int)g,(int)b);
}
/* flower petals: radial pattern — colored ring around a yellow center */
typedef struct { int cr,cg,cb; } PetalColor;
static uint32_t tex_petal(float u,float v,uint64_t seed,void* ud){
    PetalColor* pc=(PetalColor*)ud;
    float dx=u-0.5f, dy=v-0.5f; float r=sqrtf(dx*dx+dy*dy)*2.0f; /* 0 center .. ~1 edge */
    float ang=atan2f(dy,dx);
    float petal=0.5f+0.5f*sinf(ang*6.0f);      /* 6 petals */
    if (r<0.28f) return rgba(250,220,60);        /* yellow center */
    float m = petal*(1.0f-r);
    float n = cc_noise2(u*20,v*20,seed)*20;
    int cr=pc->cr, cg=pc->cg, cb=pc->cb;
    return rgba((int)(cr*(0.6f+0.4f*m)+n),(int)(cg*(0.6f+0.4f*m)+n),(int)(cb*(0.6f+0.4f*m)+n));
}

static CCMaterial mat_tex(CCEngine* e, CCTexture tex, float rough, float metal){
    CCMaterialDesc d={0}; d.base_color[0]=d.base_color[1]=d.base_color[2]=d.base_color[3]=1.0f;
    d.albedo_map=tex; d.roughness=rough; d.metallic=metal;
    d.tint[0]=d.tint[1]=d.tint[2]=d.tint[3]=1.0f;
    return cc_material_create(e,&d);
}

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/nature_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1280;cfg.height=720;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    /* ── procedural textures ─────────────────────────────────────────── */
    CCProcTexDesc gd={.width=512,.height=512,.type=CC_PROC_TEX_CUSTOM,.seed=101,.custom=tex_grass};
    CCTexture t_grass=cc_procgen_texture(eng,&gd);
    CCProcTexDesc bd={.width=256,.height=512,.type=CC_PROC_TEX_CUSTOM,.seed=202,.custom=tex_bark};
    CCTexture t_bark=cc_procgen_texture(eng,&bd);
    CCProcTexDesc ld={.width=512,.height=512,.type=CC_PROC_TEX_CUSTOM,.seed=303,.custom=tex_leaf};
    CCTexture t_leaf=cc_procgen_texture(eng,&ld);
    CCProcTexDesc dd={.width=256,.height=256,.type=CC_PROC_TEX_CUSTOM,.seed=404,.custom=tex_dirt};
    CCTexture t_dirt=cc_procgen_texture(eng,&dd);

    static PetalColor pinkc={235,90,150}, whitec={240,240,245}, purplec={170,110,220};
    CCProcTexDesc pd={.width=128,.height=128,.type=CC_PROC_TEX_CUSTOM,.seed=505,.custom=tex_petal,.custom_userdata=&pinkc};
    CCTexture t_pink=cc_procgen_texture(eng,&pd);
    pd.custom_userdata=&whitec; pd.seed=506; CCTexture t_white=cc_procgen_texture(eng,&pd);
    pd.custom_userdata=&purplec;pd.seed=507; CCTexture t_purple=cc_procgen_texture(eng,&pd);

    /* ── materials ───────────────────────────────────────────────────── */
    CCMaterial m_grass=mat_tex(eng,t_grass,0.95f,0.0f);
    CCMaterial m_bark =mat_tex(eng,t_bark,0.85f,0.0f);
    CCMaterial m_leaf =mat_tex(eng,t_leaf,0.7f,0.0f);
    CCMaterial m_dirt =mat_tex(eng,t_dirt,0.95f,0.0f);
    CCMaterial m_pink =mat_tex(eng,t_pink,0.6f,0.0f);
    CCMaterial m_white=mat_tex(eng,t_white,0.6f,0.0f);
    CCMaterial m_purple=mat_tex(eng,t_purple,0.6f,0.0f);
    CCMaterial pflower[3]={m_pink,m_white,m_purple};

    /* ── meshes ──────────────────────────────────────────────────────── */
    CCMesh ground = cc_mesh_plane(eng,80,80,8);
    CCMesh trunk  = cc_mesh_cylinder(eng,1.0f,1.0f,10);   /* scaled per tree */
    CCMesh canopy = cc_mesh_sphere(eng,1.0f,14,12);
    CCMesh cone_fir= cc_mesh_cone(eng,1.0f,1.0f,12);
    CCMesh blade  = cc_mesh_cube(eng,1.0f);                /* thin scaled = grass blade */
    CCMesh petal  = cc_mesh_quad(eng);                    /* flower head */
    CCMesh stem   = cc_mesh_cylinder(eng,1.0f,1.0f,6);
    CCMesh rock   = cc_mesh_sphere(eng,1.0f,8,6);

    /* ── lighting + sky + post ───────────────────────────────────────── */
    cc_light_set_ambient(eng,0.12f,0.14f,0.17f,1.0f);     /* subtle sky fill only */
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.45f,-0.75f,-0.5f},
                 .color={1.0f,0.95f,0.85f},.intensity=2.0f,.cast_shadows=true}; /* warm sun */
    cc_light_add(eng,&sun);
    /* daytime sky gradient — drives hemispheric ambient + sky reflections */
    float zenith[3]  = {0.20f, 0.36f, 0.72f};   /* blue overhead */
    float horizon[3] = {0.62f, 0.74f, 0.88f};   /* pale near horizon */
    float ground_c[3]= {0.14f, 0.18f, 0.10f};   /* greenish bounce */
    cc_light_set_sky_colors(eng, zenith, horizon, ground_c, 0.6f);
    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.15f; fx.bloom_intensity=0.10f;
    fx.vignette=true; fx.vignette_strength=0.26f;
    cc_postfx_set(eng,&fx);

    /* ── scatter props ───────────────────────────────────────────────── */
    typedef struct { CCMesh mesh; CCMaterial mat; CCTransform3D xf; } Prop;
    Prop props[4096]; int np=0;
    #define PUSH(ME,MA,PX,PY,PZ,SX,SY,SZ,RY) do{ if(np<4096){ \
        float _h=(RY)*0.5f; props[np].mesh=ME; props[np].mat=MA; \
        props[np].xf=(CCTransform3D){.pos={PX,PY,PZ},.rot={0,sinf(_h),0,cosf(_h)},.scale={SX,SY,SZ}}; np++; } }while(0)

    /* trees: mix of round (broadleaf) and fir (cone), with dirt patch + rocks */
    for (int i=0;i<22;i++){
        float x=rrange(-30,30), z=rrange(-30,8);
        /* keep a clearing near the camera (which sits at z=20 looking toward -z):
           reject trees too close to the view origin so nothing clips the lens */
        float dcx=x, dcz=z-6.0f;
        if (dcx*dcx+dcz*dcz < 90.0f){ x += (x<0?-11:11); z -= 8; }
        float th=rrange(3.2f,5.5f);                          /* trunk height */
        float tr=rrange(0.28f,0.45f);                        /* trunk radius */
        PUSH(trunk,m_bark, x,th*0.5f,z, tr,th,tr, rrange(0,6.28f));
        /* dirt patch: a flat squashed cube sitting on the grass */
        PUSH(blade,m_dirt, x,0.03f,z, rrange(2.2f,3.2f),0.06f,rrange(2.2f,3.2f), rrange(0,6));
        if (rnd()<0.55f){
            /* broadleaf: 2-3 overlapping canopy spheres */
            float cy=th+rrange(0.4f,1.0f);
            PUSH(canopy,m_leaf, x,cy,z, rrange(1.7f,2.4f),rrange(1.5f,2.1f),rrange(1.7f,2.4f), rrange(0,6));
            PUSH(canopy,m_leaf, x+rrange(-0.8f,0.8f),cy+rrange(-0.3f,0.6f),z+rrange(-0.8f,0.8f),
                 rrange(1.2f,1.8f),rrange(1.1f,1.6f),rrange(1.2f,1.8f), rrange(0,6));
        } else {
            /* fir: stacked cones */
            for (int c=0;c<3;c++){
                float cy=th*0.6f + c*th*0.28f; float s=rrange(2.0f,2.6f)-c*0.5f;
                PUSH(cone_fir,m_leaf, x,cy+s*0.5f,z, s,rrange(1.6f,2.2f),s, 0);
            }
        }
        /* a rock or two near the base */
        if (rnd()<0.6f){ float rr=rrange(0.3f,0.6f);
            PUSH(rock,m_bark, x+rrange(-1.5f,1.5f),rr*0.4f,z+rrange(-1.5f,1.5f), rr,rr*0.7f,rr, rrange(0,6)); }
    }

    /* grass tufts: clusters of thin vertical blades */
    for (int i=0;i<900;i++){
        float x=rrange(-38,38), z=rrange(-34,22);
        float hgt=rrange(0.4f,1.1f);
        PUSH(blade,m_grass, x,hgt*0.5f,z, rrange(0.04f,0.09f),hgt,rrange(0.04f,0.09f), rrange(0,6.28f));
    }

    /* flowers: stem + petal head, 3 colors — clustered in the foreground clearing
       so they read clearly, plus scattered elsewhere */
    for (int i=0;i<200;i++){
        float x, z;
        if (i<120){ /* clearing cluster in front of camera */
            x=rrange(-11,11); z=rrange(-2,12);
        } else {    /* scattered across the meadow */
            x=rrange(-34,34); z=rrange(-28,14);
        }
        float sh=rrange(0.6f,1.1f);
        PUSH(stem,m_leaf, x,sh*0.5f,z, 0.035f,sh,0.035f, 0);
        int c=(int)(rnd()*3.0f); if(c>2)c=2;
        /* petal quad tilted to face up toward the camera, larger so it reads */
        float py=sh+0.08f;
        props[np].mesh=petal; props[np].mat=pflower[c];
        props[np].xf=(CCTransform3D){.pos={x,py,z},
            .rot={0.42f,0,0,0.91f}, /* ~50° tilt from vertical → faces up-and-toward camera */
            .scale={rrange(0.5f,0.75f),rrange(0.5f,0.75f),1}};
        if(np<4096)np++;
    }

    /* ── render (few frames to let sky/IBL settle) ───────────────────── */
    for(int frame=0;frame<4;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,3.2f,19},.target={0,1.8f,-8},.up={0,1,0},
                          .fov_deg=52,.near_plane=0.1f,.far_plane=250,.exposure=0.7f};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,m_grass,&gxf);
        for(int i=0;i<np;i++) cc_draw_mesh(eng,props[i].mesh,props[i].mat,&props[i].xf);
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s  props=%d (trees+grass+flowers+rocks)\n", s?s:"(null)", np);
    cc_shutdown(eng); return 0;
}
