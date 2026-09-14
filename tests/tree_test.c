/* tree_test — close-up of the procedural "less-perfect" trees: lumpy displaced
 * canopies on gnarled tapering trunks. Compares the new irregular geometry
 * against a plain sphere-on-cylinder tree (the old approach) side by side. */
#include "cc/claudecore.h"
#include "cc/procgen.h"
#include "tree_gen.h"
#include <math.h>
#include <stdio.h>

static uint32_t RS=77u;
static float rnd(void){ RS=RS*1664525u+1013904223u; return ((RS>>8)&0xffffff)/16777215.0f; }
static float rr(float a,float b){ return a+(b-a)*rnd(); }
static uint32_t rgba(int r,int g,int b){ return ((r&255)<<24)|((g&255)<<16)|((b&255)<<8)|255; }

static uint32_t tex_bark(float u,float v,uint64_t s,void* ud){ (void)ud;
    float grain=cc_fbm2(u*44.0f,v*7.0f,s,4,0.55f,2.0f); float t=(grain+1)*0.5f;
    float rings=sinf(v*3.14159f*2.0f+cc_fbm2(u*6,v*6,s,3,0.5f,2)*3.0f);
    return rgba((int)(78+t*54+rings*9),(int)(52+t*36+rings*7),(int)(32+t*20));
}
static uint32_t tex_leaf(float u,float v,uint64_t s,void* ud){ (void)ud;
    float f=cc_fbm2(u*13.0f,v*13.0f,s,5,0.6f,2.1f); float t=(f+1)*0.5f;
    float sp=cc_noise2(u*80,v*80,s+3);
    int r=22+(int)(t*44), g=48+(int)(t*88), b=16+(int)(t*26);
    if(sp>0.6f){g+=26;r+=14;}
    return rgba(r,g,b);
}

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/tree_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1100;cfg.height=680;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    CCProcTexDesc bd={.width=256,.height=512,.type=CC_PROC_TEX_CUSTOM,.seed=202,.custom=tex_bark};
    CCTexture t_bark=cc_procgen_texture(e,&bd);
    CCProcTexDesc ld={.width=512,.height=512,.type=CC_PROC_TEX_CUSTOM,.seed=303,.custom=tex_leaf};
    CCTexture t_leaf=cc_procgen_texture(e,&ld);
    CCMaterialDesc bdm={0};bdm.base_color[0]=bdm.base_color[1]=bdm.base_color[2]=bdm.base_color[3]=1;
    bdm.albedo_map=t_bark;bdm.roughness=0.88f;bdm.tint[0]=bdm.tint[1]=bdm.tint[2]=bdm.tint[3]=1;
    CCMaterial m_bark=cc_material_create(e,&bdm);
    CCMaterialDesc ldm={0};ldm.base_color[0]=ldm.base_color[1]=ldm.base_color[2]=ldm.base_color[3]=1;
    ldm.albedo_map=t_leaf;ldm.roughness=0.72f;ldm.tint[0]=ldm.tint[1]=ldm.tint[2]=ldm.tint[3]=1;
    CCMaterial m_leaf=cc_material_create(e,&ldm);

    CCMesh ground=cc_mesh_plane(e,60,60,4);
    CCMaterialDesc gdm={0};gdm.base_color[0]=0.28f;gdm.base_color[1]=0.34f;gdm.base_color[2]=0.16f;gdm.base_color[3]=1;
    gdm.roughness=0.95f;gdm.tint[0]=gdm.tint[1]=gdm.tint[2]=gdm.tint[3]=1;
    CCMaterial m_ground=cc_material_create(e,&gdm);

    /* NEW imperfect tree (left) */
    CCMesh trunkA=cc_mesh_gnarled_trunk(e,0.5f,0.22f,5.0f,0.10f,101);
    CCMesh canA1=cc_mesh_lumpy_canopy(e,1.9f,201,0.9f);
    CCMesh canA2=cc_mesh_lumpy_canopy(e,1.4f,202,1.0f);
    CCMesh canA3=cc_mesh_lumpy_canopy(e,1.2f,203,1.1f);
    /* OLD smooth tree (right) */
    CCMesh trunkB=cc_mesh_cylinder(e,1.0f,1.0f,12);
    CCMesh canB=cc_mesh_sphere(e,1.0f,20,16);

    cc_light_set_ambient(e,0.11f,0.12f,0.15f,1);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.6f,-0.55f,-0.4f},.color={1,0.9f,0.72f},.intensity=3.0f,.cast_shadows=true};
    cc_light_add(e,&sun);
    float z[3]={0.24f,0.36f,0.6f},h[3]={0.85f,0.72f,0.55f},g[3]={0.15f,0.16f,0.1f};
    cc_light_set_sky_colors(e,z,h,g,0.7f);
    CCPostFX fx=cc_postfx_default();
    fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.18f;
    fx.ssgi=true; fx.ssgi_intensity=1.5f; fx.ssgi_radius=2.5f;
    fx.taa=true; fx.taa_blend=0.85f;
    fx.bloom=true; fx.bloom_threshold=1.3f; fx.bloom_intensity=0.06f;
    fx.vignette=true; fx.vignette_strength=0.25f;
    cc_postfx_set(e,&fx);

    for(int f=0;f<8;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,3.0f,10.5f},.target={0,2.6f,0},.up={0,1,0},.fov_deg=50,.near_plane=0.1f,.far_plane=120,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,m_ground,&g2);

        /* LEFT: imperfect tree */
        CCTransform3D tA={.pos={-3.0f,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,trunkA,m_bark,&tA);
        CCTransform3D cA1={.pos={-3.4f,5.2f,-0.1f},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,canA1,m_leaf,&cA1);
        CCTransform3D cA2={.pos={-2.3f,5.0f,0.4f},.rot={0,0.3f,0,0.95f},.scale={1,1,1}}; cc_draw_mesh(e,canA2,m_leaf,&cA2);
        CCTransform3D cA3={.pos={-3.1f,6.1f,0.3f},.rot={0,0.7f,0,0.7f},.scale={1,1,1}}; cc_draw_mesh(e,canA3,m_leaf,&cA3);

        /* RIGHT: old smooth tree (cylinder trunk + sphere canopy) */
        CCTransform3D tB={.pos={3.0f,2.5f,0},.rot={0,0,0,1},.scale={0.35f,5.0f,0.35f}}; cc_draw_mesh(e,trunkB,m_bark,&tB);
        CCTransform3D cB={.pos={3.0f,5.6f,0},.rot={0,0,0,1},.scale={2.1f,1.9f,2.1f}}; cc_draw_mesh(e,canB,m_leaf,&cB);

        cc_frame_end(e);
    }
    const char* s=cc_screenshot(e,out);
    printf("screenshot: %s (L=imperfect procedural, R=smooth primitive)\n", s?s:"(null)");
    cc_shutdown(e); return 0;
}
