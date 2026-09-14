/* Shadow test — several objects casting directional shadows onto a ground plane. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/shadow_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh ground=cc_mesh_plane(eng,40,40,4);
    CCMesh sphere=cc_mesh_sphere(eng,1.0f,32,32);
    CCMesh cube=cc_mesh_cube(eng,1.5f);

    CCMaterialDesc gd={.base_color={0.6f,0.6f,0.62f,1},.roughness=0.9f};
    CCMaterial mground=cc_material_create(eng,&gd);
    CCMaterialDesc rd={.base_color={0.85f,0.3f,0.25f,1},.roughness=0.4f};
    CCMaterial mred=cc_material_create(eng,&rd);
    CCMaterialDesc bd={.base_color={0.3f,0.5f,0.85f,1},.roughness=0.35f,.metallic=0.3f};
    CCMaterial mblue=cc_material_create(eng,&bd);

    cc_light_set_ambient(eng,0.12f,0.13f,0.16f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.55f,-0.9f,-0.4f},
                 .color={1.0f,0.95f,0.85f},.intensity=3.0f,.cast_shadows=true};
    cc_light_add(eng,&sun);

    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.2f; fx.bloom_intensity=0.1f;
    fx.vignette=true; fx.vignette_strength=0.25f;
    cc_postfx_set(eng,&fx);

    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,7,13},.target={0,1,0},.up={0,1,0},.fov_deg=50,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mground,&gxf);
        CCTransform3D s1={.pos={-3,1.0f,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,sphere,mred,&s1);
        CCTransform3D c1={.pos={0.5f,1.5f,-1},.rot={0,0.4f,0,0.92f},.scale={1,1,1}};
        cc_draw_mesh(eng,cube,mblue,&c1);
        CCTransform3D s2={.pos={3.5f,1.0f,1},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,sphere,mblue,&s2);
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out); printf("screenshot: %s\n",s?s:"(null)");
    cc_shutdown(eng); return 0;
}
