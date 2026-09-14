/* autoexposure_test — realism via eye adaptation. Same scene at three light
 * levels (dim/normal/blinding), rendered with auto-exposure OFF vs ON. OFF:
 * dim crushes to black, bright blows out. ON: all adapt to a filmic range.
 * Fresh engine per panel isolates lighting + adaptation state. */
#include "cc/claudecore.h"
#include <stdio.h>

static void panel(const char* out, float li, int autoexp){
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=380;cfg.height=340;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return;
    CCMesh sphere=cc_mesh_sphere(eng,1.0f,48,48);
    CCMesh cube=cc_mesh_cube(eng,1.2f);
    CCMesh ground=cc_mesh_plane(eng,60,60,4);
    CCMaterialDesc md1={.base_color={0.85f,0.35f,0.3f,1},.roughness=0.35f,.metallic=0.1f,.tint={1,1,1,1}};
    CCMaterial m1=cc_material_create(eng,&md1);
    CCMaterialDesc md2={.base_color={0.35f,0.55f,0.9f,1},.roughness=0.25f,.metallic=0.6f,.tint={1,1,1,1}};
    CCMaterial m2=cc_material_create(eng,&md2);
    CCMaterialDesc gd={.base_color={0.55f,0.55f,0.58f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(eng,&gd);
    cc_light_set_ambient(eng,0.02f,0.02f,0.025f,1.0f);
    CCLight key={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-0.75f,-0.45f},.color={1.0f,0.96f,0.9f},.intensity=li,.cast_shadows=true};
    cc_light_add(eng,&key);
    float z[3]={0.10f,0.16f,0.30f},h[3]={0.30f,0.35f,0.42f},g[3]={0.08f,0.09f,0.08f};
    cc_light_set_sky_colors(eng,z,h,g,li*0.12f);
    CCPostFX fx=cc_postfx_default();
    fx.bloom=true; fx.bloom_threshold=1.4f; fx.bloom_intensity=0.07f;
    fx.auto_exposure=autoexp?true:false; fx.ae_key=0.18f;
    cc_postfx_set(eng,&fx);
    for(int frame=0;frame<8;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,2.2f,7},.target={0,0.5f,0},.up={0,1,0},.fov_deg=45,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D g2={.pos={0,-1.0f,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,ground,mg,&g2);
        CCTransform3D s={.pos={-1.3f,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,sphere,m1,&s);
        CCTransform3D c={.pos={1.3f,-0.1f,0},.rot={0,0.2f,0,0.98f},.scale={1,1,1}}; cc_draw_mesh(eng,cube,m2,&c);
        cc_frame_end(eng);
    }
    cc_screenshot(eng,out); cc_shutdown(eng);
}
int main(void){
    const float lv[3]={0.4f,3.0f,24.0f};
    const char* off[3]={"/tmp/ae_off_dim.png","/tmp/ae_off_nrm.png","/tmp/ae_off_bri.png"};
    const char* on [3]={"/tmp/ae_on_dim.png","/tmp/ae_on_nrm.png","/tmp/ae_on_bri.png"};
    for(int i=0;i<3;i++){ panel(off[i],lv[i],0); panel(on[i],lv[i],1);
        printf("level %.1f done\n",lv[i]); }
    return 0;
}
