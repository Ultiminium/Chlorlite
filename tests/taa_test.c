/* taa_test — temporal anti-aliasing. A scene full of hard high-frequency edges
 * (a thin picket row + tilted cube + cone) is where aliasing/jaggies are worst.
 * TAA off (left): stair-stepped, shimmery edges. TAA on (right): jitter+history
 * accumulate to clean, near-supersampled edges over the frames rendered. */
#include "cc/claudecore.h"
#include <stdio.h>

static void scene(CCEngine* e, int taa){
    CCMesh floor=cc_mesh_plane(e,40,40,1);
    CCMesh picket=cc_mesh_cube(e,1.0f);
    CCMesh cone=cc_mesh_cone(e,0.6f,1.5f,24);
    CCMaterialDesc md={.base_color={0.8f,0.4f,0.35f,1},.roughness=0.5f,.tint={1,1,1,1}};
    CCMaterial m=cc_material_create(e,&md);
    CCMaterialDesc gd={.base_color={0.5f,0.52f,0.55f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);
    CCMaterialDesc wd={.base_color={0.85f,0.85f,0.5f,1},.roughness=0.4f,.metallic=0.3f,.tint={1,1,1,1}};
    CCMaterial mw=cc_material_create(e,&wd);
    cc_light_set_ambient(e,0.12f,0.13f,0.16f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-0.6f,-0.5f},.color={1,0.96f,0.9f},.intensity=2.6f,.cast_shadows=true};cc_light_add(e,&k);
    float z[3]={0.2f,0.32f,0.6f},h[3]={0.5f,0.56f,0.66f},g[3]={0.18f,0.19f,0.18f};
    cc_light_set_sky_colors(e,z,h,g,0.6f);
    CCPostFX fx=cc_postfx_default();
    fx.fxaa=false;                 /* isolate TAA (don't mix with FXAA) */
    fx.taa = taa?true:false; fx.taa_blend=0.88f;
    cc_postfx_set(e,&fx);
    for(int f=0;f<8;f++){cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,2.0f,7},.target={0,0.6f,0},.up={0,1,0},.fov_deg=48,.near_plane=0.1f,.far_plane=100,.exposure=1};cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};cc_draw_mesh(e,floor,mg,&g2);
        /* a row of thin tall pickets — dense vertical edges = aliasing stress */
        for(int i=0;i<9;i++){ float x=-4.0f+i*1.0f;
            CCTransform3D p={.pos={x,0.9f,-1.5f},.rot={0,0,0,1},.scale={0.08f,1.8f,0.08f}};cc_draw_mesh(e,picket,mw,&p); }
        CCTransform3D c={.pos={0,0.75f,1.2f},.rot={0,0.5f,0,0.86f},.scale={1,1,1}};cc_draw_mesh(e,cone,m,&c);
        cc_frame_end(e);}
}
int main(void){
    { CCEngineConfig cfg=cc_sandbox_config();cfg.width=560;cfg.height=420;cfg.verbose=false;CCEngine*e=cc_init(&cfg);
      scene(e,0); cc_screenshot(e,"/tmp/taa_off.png"); cc_shutdown(e); }
    { CCEngineConfig cfg=cc_sandbox_config();cfg.width=560;cfg.height=420;cfg.verbose=false;CCEngine*e=cc_init(&cfg);
      scene(e,1); cc_screenshot(e,"/tmp/taa_on.png"); cc_shutdown(e); }
    printf("rendered taa off+on\n"); return 0;
}
