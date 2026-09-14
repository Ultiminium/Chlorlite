/* nature_photoreal — a photorealistic meadow at golden hour.
 *
 * Pulls the whole realism stack together: procedural PBR ground/bark/leaf
 * albedo, a low warm sun with soft PCSS shadows, procedural sky + IBL, the
 * temporally-accumulated SSGI (green bounce from grass/canopy into shadow),
 * depth of field focused on the mid-ground, exponential height fog for aerial
 * perspective, auto-exposure + ACES tonemap + bloom + vignette + fine grain.
 *
 * Composition fixes over the stylized nature_test: realistically small,
 * clustered flowers (not giant floating quads), dense layered cross-quad grass
 * tufts (not sparse posts), irregular lumpy canopies + tapered trunks, and a
 * golden-hour key light for long warm shadows and depth. */
#include "cc/claudecore.h"
#include "cc/procgen.h"
#include "tree_gen.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t RS=20260814u;
static float rnd(void){ RS=RS*1664525u+1013904223u; return ((RS>>8)&0xffffff)/16777215.0f; }
static float rr(float a,float b){ return a+(b-a)*rnd(); }
static uint32_t rgba(int r,int g,int b){
    if(r<0)r=0;if(r>255)r=255;if(g<0)g=0;if(g>255)g=255;if(b<0)b=0;if(b>255)b=255;
    return ((r&255)<<24)|((g&255)<<16)|((b&255)<<8)|255; }

/* ── textures (authored as sRGB color; loaded via sRGB path in mat_tex) ── */
/* meadow grass: layered fbm greens, dry clumps, fine blade streaks, earthy base */
static uint32_t tex_grass(float u,float v,uint64_t seed,void* ud){ (void)ud;
    float f=cc_fbm2(u*7.0f,v*7.0f,seed,5,0.5f,2.0f);
    float clump=cc_fbm2(u*2.3f,v*2.3f,seed+11,3,0.5f,2.0f);
    float blades=cc_fbm2(u*160.0f,v*26.0f,seed+7,2,0.5f,2.0f);
    float t=(f+1)*0.5f, c=(clump+1)*0.5f;
    float g=62+t*66+blades*16+c*22;
    float r=34+t*34+blades*10+c*18;
    float b=22+t*20;
    if(c>0.72f){ r+=(c-0.72f)*260; g-=(c-0.72f)*40; }   /* sun-dried patches */
    float soil=cc_noise2(u*40,v*40,seed+3); if(soil>0.7f){ r+=20;g-=8;b-=4; }
    return rgba((int)r,(int)g,(int)b);
}
static uint32_t tex_bark(float u,float v,uint64_t seed,void* ud){ (void)ud;
    float rings=sinf(v*3.14159f*2.0f+cc_fbm2(u*6,v*6,seed,3,0.5f,2)*3.0f);
    float grain=cc_fbm2(u*44.0f,v*7.0f,seed,4,0.55f,2.0f);
    float t=(grain+1)*0.5f;
    float r=78+t*54+rings*9, g=52+t*36+rings*7, b=32+t*20;
    return rgba((int)r,(int)g,(int)b);
}
static uint32_t tex_leaf(float u,float v,uint64_t seed,void* ud){ (void)ud;
    float f=cc_fbm2(u*13.0f,v*13.0f,seed,5,0.6f,2.1f);
    float big=cc_fbm2(u*3.0f,v*3.0f,seed+5,3,0.5f,2.0f);
    float t=(f+1)*0.5f, tb=(big+1)*0.5f;
    float g=48+t*88+tb*24, r=22+t*44+tb*8, b=16+t*26;
    float sp=cc_noise2(u*80,v*80,seed+3); if(sp>0.6f){ g+=26;r+=14; }
    return rgba((int)r,(int)g,(int)b);
}
static uint32_t tex_rock(float u,float v,uint64_t seed,void* ud){ (void)ud;
    float f=cc_fbm2(u*9,v*9,seed,5,0.5f,2.0f); float t=(f+1)*0.5f;
    int base=96+(int)(t*70);
    float moss=cc_fbm2(u*5,v*5,seed+2,3,0.5f,2.0f);
    int r=base, g=base+6, b=base-4;
    if(moss>0.35f){ g+=(int)((moss-0.35f)*90); r+=(int)((moss-0.35f)*20); } /* mossy top */
    return rgba(r,g,b);
}
/* grass BLADE card with real alpha cutout: several tapering vertical blades,
   transparent between them, so a textured quad reads as blade silhouettes
   instead of a solid rectangle. Green with sun-warmed tips, v=0 bottom. */
static uint32_t tex_blade(float u,float v,uint64_t seed,void* ud){ (void)ud;
    /* a few blades across u, each a thin vertical shape that tapers with height */
    float alpha=0.0f; float shade=0.0f;
    const int NB=5;
    for(int b=0;b<NB;b++){
        float bx=(b+0.5f)/NB + (cc_noise2((float)b,7.0f,seed)-0.5f)*0.10f;
        float sway=(cc_noise2((float)b,3.0f,seed)-0.5f)*0.18f; /* lean */
        float cx=bx + sway*v;
        float halfw=0.055f*(1.0f-v*0.85f);                    /* taper to tip */
        float d=fabsf(u-cx);
        if(d<halfw && v<0.96f){ alpha=1.0f; shade=(1.0f-d/halfw)*0.5f+0.5f; }
    }
    if(alpha<0.5f) return 0x00000000u;                        /* transparent gap */
    float t=v;                                                /* base→tip */
    int g=70+(int)(t*80+shade*20);
    int r=34+(int)(t*70+shade*14);                            /* warmer, drier tips */
    int bl=24+(int)((1.0f-t)*18);
    float n=cc_noise2(u*30,v*60,seed+2)*14;
    return rgba(r+(int)n,g+(int)n,bl+(int)(n*0.5f)) | 0xffu;   /* opaque blade */
}
typedef struct { int cr,cg,cb; } PetalColor;
static uint32_t tex_petal(float u,float v,uint64_t seed,void* ud){
    PetalColor* pc=(PetalColor*)ud;
    float dx=u-0.5f,dy=v-0.5f; float r=sqrtf(dx*dx+dy*dy)*2.0f; float ang=atan2f(dy,dx);
    float petal=0.5f+0.5f*sinf(ang*5.0f);
    if(r<0.30f) return rgba(250,214,70);
    if(r>1.0f) return rgba(60,90,40);          /* outside petal → leaf green (reads as small) */
    float m=petal*(1.0f-r); float n=cc_noise2(u*22,v*22,seed)*16;
    return rgba((int)(pc->cr*(0.55f+0.45f*m)+n),(int)(pc->cg*(0.55f+0.45f*m)+n),(int)(pc->cb*(0.55f+0.45f*m)+n));
}

/* sRGB-correct albedo (color maps must go through the sRGB path). */
static CCMaterial mat_tex(CCEngine* e,CCTexture tex,float rough,float metal){
    CCMaterialDesc d={0}; d.base_color[0]=d.base_color[1]=d.base_color[2]=d.base_color[3]=1.0f;
    d.albedo_map=tex; d.roughness=rough; d.metallic=metal;
    d.tint[0]=d.tint[1]=d.tint[2]=d.tint[3]=1.0f;
    return cc_material_create(e,&d);
}

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/nature_photoreal.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1280;cfg.height=720;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCProcTexDesc gd={.width=1024,.height=1024,.type=CC_PROC_TEX_CUSTOM,.seed=101,.custom=tex_grass};
    CCTexture t_grass=cc_procgen_texture(eng,&gd);
    CCProcTexDesc gd2={.width=256,.height=256,.type=CC_PROC_TEX_CUSTOM,.seed=131,.custom=tex_blade};
    CCTexture t_gblade=cc_procgen_texture(eng,&gd2);
    CCProcTexDesc bd={.width=256,.height=512,.type=CC_PROC_TEX_CUSTOM,.seed=202,.custom=tex_bark};
    CCTexture t_bark=cc_procgen_texture(eng,&bd);
    CCProcTexDesc ld={.width=512,.height=512,.type=CC_PROC_TEX_CUSTOM,.seed=303,.custom=tex_leaf};
    CCTexture t_leaf=cc_procgen_texture(eng,&ld);
    CCProcTexDesc rd={.width=256,.height=256,.type=CC_PROC_TEX_CUSTOM,.seed=404,.custom=tex_rock};
    CCTexture t_rock=cc_procgen_texture(eng,&rd);
    static PetalColor whitec={244,240,236}, yellowc={250,210,70}, lilacc={196,168,224};
    CCProcTexDesc pd={.width=128,.height=128,.type=CC_PROC_TEX_CUSTOM,.seed=505,.custom=tex_petal,.custom_userdata=&whitec};
    CCTexture t_pw=cc_procgen_texture(eng,&pd);
    pd.custom_userdata=&yellowc; pd.seed=506; CCTexture t_py=cc_procgen_texture(eng,&pd);
    pd.custom_userdata=&lilacc;  pd.seed=507; CCTexture t_pl=cc_procgen_texture(eng,&pd);

    CCMaterial m_grass=mat_tex(eng,t_grass,0.93f,0.0f);
    CCMaterialDesc bladed={0}; bladed.base_color[0]=bladed.base_color[1]=bladed.base_color[2]=bladed.base_color[3]=1.0f;
    bladed.albedo_map=t_gblade; bladed.roughness=0.85f; bladed.alpha_cutoff=0.5f;
    bladed.tint[0]=bladed.tint[1]=bladed.tint[2]=bladed.tint[3]=1.0f;
    CCMaterial m_gblade=cc_material_create(eng,&bladed);
    CCMaterial m_bark=mat_tex(eng,t_bark,0.88f,0.0f);
    CCMaterial m_leaf=mat_tex(eng,t_leaf,0.72f,0.0f);
    CCMaterial m_rock=mat_tex(eng,t_rock,0.8f,0.0f);
    CCMaterial fl[3]={mat_tex(eng,t_pw,0.55f,0.0f),mat_tex(eng,t_py,0.55f,0.0f),mat_tex(eng,t_pl,0.55f,0.0f)};

    CCMesh ground=cc_mesh_plane(eng,300,300,16);
    /* pools of imperfect procedural geometry — variety without per-tree cost */
    #define NTRUNK 5
    #define NCAN 6
    CCMesh trunks[NTRUNK]; CCMesh canopies[NCAN];
    for(int i=0;i<NTRUNK;i++)
        trunks[i]=cc_mesh_gnarled_trunk(eng, rr(0.34f,0.5f), rr(0.16f,0.24f),
                                        1.0f, rr(-0.14f,0.14f), 900+i);
    for(int i=0;i<NCAN;i++)
        canopies[i]=cc_mesh_lumpy_canopy(eng, 1.0f, 700+i, rr(0.85f,1.15f));
    CCMesh gquad=cc_mesh_quad(eng);      /* grass tuft cross-quads */
    CCMesh petal=cc_mesh_quad(eng);
    CCMesh stem=cc_mesh_cylinder(eng,1.0f,1.0f,5);
    CCMesh rock=cc_mesh_sphere(eng,1.0f,10,8);

    /* ── golden-hour lighting ─────────────────────────────────────────── */
    cc_light_set_ambient(eng,0.10f,0.11f,0.14f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,
                 .dir={-0.82f,-0.32f,-0.47f},      /* LOW sun → long shadows */
                 .color={1.0f,0.78f,0.52f},        /* warm gold */
                 .intensity=3.4f,.cast_shadows=true};
    cc_light_add(eng,&sun);
    /* warm sky near horizon, deeper blue overhead, greenish ground bounce */
    float zenith[3]={0.22f,0.34f,0.62f};
    float horizon[3]={0.92f,0.72f,0.50f};   /* golden haze band */
    float groundc[3]={0.16f,0.16f,0.09f};
    cc_light_set_sky_colors(eng,zenith,horizon,groundc,0.7f);

    CCPostFX fx=cc_postfx_default();
    fx.tonemap_aces=true;
    fx.auto_exposure=true; fx.ae_key=0.17f;
    fx.bloom=true; fx.bloom_threshold=1.25f; fx.bloom_intensity=0.08f;
    fx.ssgi=true; fx.ssgi_intensity=1.7f; fx.ssgi_radius=2.6f;   /* green bounce into shadow */
    fx.taa=true; fx.taa_blend=0.85f;
    fx.fog=true; fx.fog_density=0.018f; fx.fog_color[0]=0.80f; fx.fog_color[1]=0.72f; fx.fog_color[2]=0.62f;
    fx.fog_height=14.0f;                                          /* low-lying golden haze */
    fx.dof=true; fx.dof_focus_dist=17.0f; fx.dof_focus_range=10.0f; fx.dof_max_blur=8.0f;
    fx.vignette=true; fx.vignette_strength=0.30f;
    fx.film_grain=true; fx.grain_strength=0.035f;
    cc_postfx_set(eng,&fx);

    typedef struct { CCMesh mesh; CCMaterial mat; CCTransform3D xf; } Prop;
    static Prop props[60000]; int np=0;
    #define PUSH(ME,MA,PX,PY,PZ,SX,SY,SZ,RY) do{ if(np<60000){ \
        float _h=(RY)*0.5f; props[np].mesh=ME; props[np].mat=MA; \
        props[np].xf=(CCTransform3D){.pos={PX,PY,PZ},.rot={0,sinf(_h),0,cosf(_h)},.scale={SX,SY,SZ}}; np++; } }while(0)
    /* a cross-quad tuft = 2 perpendicular textured quads → volumetric-looking grass */
    #define TUFT(MA,PX,PY,PZ,W,H) do{ \
        float a=rr(0,3.14159f); \
        props[np]=(Prop){gquad,MA,{.pos={PX,PY,PZ},.rot={0,sinf(a*0.5f),0,cosf(a*0.5f)},.scale={W,H,1}}}; if(np<60000)np++; \
        float a2=a+1.5708f; \
        props[np]=(Prop){gquad,MA,{.pos={PX,PY,PZ},.rot={0,sinf(a2*0.5f),0,cosf(a2*0.5f)},.scale={W,H,1}}}; if(np<60000)np++; \
    }while(0)

    /* a HERO tree closer in, kept within DOF focus so its irregular geometry
       reads sharply (the payoff of the less-perfect-geometry work) */
    {
        float hx=-7.5f, hz=-9.0f, th=6.2f, tw=1.1f;
        PUSH(trunks[0],m_bark, hx,0,hz, tw,th,tw, 0.4f);
        float cy=th+0.4f;
        float lox[5]={-1.3f,1.2f,0.1f,-0.6f,0.8f}, loz[5]={0.4f,-0.5f,1.2f,-1.1f,0.6f};
        float loy[5]={0.2f,0.5f,-0.2f,1.0f,1.3f}, lsz[5]={2.6f,2.4f,2.2f,2.0f,1.8f};
        for(int c=0;c<5;c++)
            PUSH(canopies[c%NCAN],m_leaf, hx+lox[c],cy+loy[c],hz+loz[c], lsz[c],lsz[c]*0.92f,lsz[c], (float)c*1.1f);
    }

    /* trees — gnarled trunk from the pool + a few lumpy canopy lobes */
    for(int i=0;i<26;i++){
        float x=rr(-46,46), z=rr(-60,-6);
        float th=rr(4.0f,7.0f), tw=rr(0.85f,1.2f);
        CCMesh tk=trunks[(int)(rnd()*NTRUNK)%NTRUNK];
        /* trunk mesh is unit-height → scale Y by th, X/Z by trunk width */
        PUSH(tk,m_bark, x,0,z, tw,th,tw, rr(0,6.28f));
        float cy=th+rr(0.1f,0.7f);
        int lobes=3+(int)(rnd()*3);
        for(int c=0;c<lobes;c++){
            float ox=rr(-1.5f,1.5f), oz=rr(-1.5f,1.5f), oy=rr(-0.5f,1.0f);
            float s=rr(1.7f,2.8f);
            CCMesh cm=canopies[(int)(rnd()*NCAN)%NCAN];
            PUSH(cm,m_leaf, x+ox,cy+oy,z+oz, s,s*rr(0.82f,1.05f),s, rr(0,6.28f));
        }
        if(rnd()<0.7f){ float k=rr(0.35f,0.8f);
            PUSH(rock,m_rock, x+rr(-2,2),k*0.4f,z+rr(-2,2), k,k*0.7f,k*rr(0.8f,1.1f), rr(0,6)); }
    }

    /* scattered mid-ground rocks */
    for(int i=0;i<40;i++){ float k=rr(0.3f,0.9f);
        PUSH(rock,m_rock, rr(-40,40),k*0.4f,rr(-40,6), k,k*rr(0.6f,0.9f),k, rr(0,6)); }

    /* dense grass — a continuous mat of overlapping alpha-cut blade cards */
    for(int i=0;i<26000;i++){
        float x=rr(-55,55), z=rr(-55,16);
        float near=(z+55.0f)/71.0f;                 /* 0 far .. 1 near */
        if(rnd()>0.35f+0.65f*near) continue;
        float h=rr(0.4f,0.85f)*(0.8f+0.5f*near);    /* blade height */
        float w=rr(0.35f,0.6f);                      /* card width holds several blades */
        TUFT(m_gblade, x,h*0.5f,z, w,h);
    }

    /* flowers — clustered patches, tall enough to emerge above the grass */
    for(int cl=0; cl<30; cl++){
        float cx=rr(-32,32), cz=rr(-28,12);
        int cnt=10+(int)(rnd()*18);
        int col=(int)(rnd()*3.0f); if(col>2)col=2;
        for(int i=0;i<cnt;i++){
            float x=cx+rr(-2.4f,2.4f), z=cz+rr(-2.4f,2.4f);
            float sh=rr(0.7f,1.1f);                          /* taller than grass */
            PUSH(stem,m_leaf, x,sh*0.5f,z, 0.028f,sh,0.028f, 0);
            float py=sh+0.06f; float sc=rr(0.24f,0.4f);      /* readable heads */
            int c=col; if(rnd()<0.2f){ c=(int)(rnd()*3); if(c>2)c=2; }
            props[np]=(Prop){petal,fl[c],{.pos={x,py,z},.rot={0.5f,0,0,0.866f},.scale={sc,sc,1}}};
            if(np<60000)np++;
        }
    }

    /* ── render (several frames so SSGI/TAA/auto-exposure converge) ────── */
    for(int frame=0;frame<8;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={2,3.6f,17},.target={-6.0f,3.0f,-9},.up={0,1,0},
                          .fov_deg=52,.near_plane=0.1f,.far_plane=400,.exposure=1.0f};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,m_grass,&gxf);
        for(int i=0;i<np;i++) cc_draw_mesh(eng,props[i].mesh,props[i].mat,&props[i].xf);
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s  props=%d\n", s?s:"(null)", np);
    cc_shutdown(eng); return 0;
}
