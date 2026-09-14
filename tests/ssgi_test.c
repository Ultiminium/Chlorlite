/* ssgi_test — screen-space global illumination: color bleed + indirect fill.
 * A saturated RED wall stands beside a WHITE floor and WHITE sphere. With SSGI
 * on, indirect light bounces off the red wall and tints the nearby white
 * surfaces reddish (color bleed) and fills the shadowed side — the core "lit by
 * the world" GI cue. Rendered SSGI off (left) vs on (right). */
#include "cc/claudecore.h"
#include <stdio.h>

static void scene(CCEngine* e, int ssgi){
    CCMesh floor=cc_mesh_plane(e,20,20,1);
    CCMesh wall=cc_mesh_cube(e,1.0f);
    CCMesh sph=cc_mesh_sphere(e,1.0f,48,48);
    CCMaterialDesc wd={.base_color={0.95f,0.95f,0.95f,1},.roughness=0.8f,.tint={1,1,1,1}};
    CCMaterial mWhite=cc_material_create(e,&wd);
    CCMaterialDesc rd={.base_color={0.9f,0.08f,0.06f,1},.roughness=0.7f,.tint={1,1,1,1}};
    CCMaterial mRed=cc_material_create(e,&rd);
    cc_light_set_ambient(e,0.04f,0.04f,0.05f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={0.3f,-0.7f,-0.4f},.color={1,0.98f,0.95f},.intensity=2.5f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.1f,0.14f,0.25f},h[3]={0.25f,0.28f,0.34f},g[3]={0.1f,0.1f,0.1f};
    cc_light_set_sky_colors(e,z,h,g,0.3f);
    CCPostFX fx=cc_postfx_default();
    fx.auto_exposure=true; fx.ae_key=0.18f; fx.fxaa=true;
    fx.ssgi = ssgi?true:false; fx.ssgi_intensity=2.2f; fx.ssgi_radius=3.0f;
    cc_postfx_set(e,&fx);
    for(int f=0;f<8;f++){cc_frame_begin(e);
        CCCameraDesc cam={.pos={3.5f,2.2f,4.5f},.target={-0.3f,0.6f,0},.up={0,1,0},.fov_deg=48,.near_plane=0.1f,.far_plane=100,.exposure=1};cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};cc_draw_mesh(e,floor,mWhite,&g2);
        /* tall red wall on the left */
        CCTransform3D w={.pos={-1.6f,1.2f,0},.rot={0,0,0,1},.scale={0.15f,2.4f,2.4f}};cc_draw_mesh(e,wall,mRed,&w);
        /* white sphere near the red wall — should catch red bounce on its left side */
        CCTransform3D s={.pos={-0.2f,1.0f,0},.rot={0,0,0,1},.scale={1,1,1}};cc_draw_mesh(e,sph,mWhite,&s);
        cc_frame_end(e);}
}
int main(int argc,char**argv){
    /* two panels: off, on */
    { CCEngineConfig cfg=cc_sandbox_config();cfg.width=520;cfg.height=460;cfg.verbose=false;CCEngine*e=cc_init(&cfg);
      scene(e,0); cc_screenshot(e,"/tmp/ssgi_off.png"); cc_shutdown(e); }
    { CCEngineConfig cfg=cc_sandbox_config();cfg.width=520;cfg.height=460;cfg.verbose=false;CCEngine*e=cc_init(&cfg);
      scene(e,1); cc_screenshot(e,"/tmp/ssgi_on.png"); cc_shutdown(e); }
    printf("rendered ssgi off + on\n");
    (void)argc;(void)argv; return 0;
}
