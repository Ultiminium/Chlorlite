/* Spot-light shadow test — a spot light above casts a cone of light and the
 * objects cast shadows within it. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/spotshadow_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;
    CCMesh ground=cc_mesh_plane(eng,40,40,4);
    CCMesh sphere=cc_mesh_sphere(eng,1.0f,32,32);
    CCMesh cube=cc_mesh_cube(eng,1.4f);
    CCMaterialDesc gd={.base_color={0.7f,0.7f,0.72f,1},.roughness=0.9f};
    CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc rd={.base_color={0.85f,0.4f,0.35f,1},.roughness=0.5f};
    CCMaterial mr=cc_material_create(eng,&rd);
    CCMaterialDesc bd={.base_color={0.4f,0.55f,0.85f,1},.roughness=0.5f};
    CCMaterial mb=cc_material_create(eng,&bd);
    cc_light_set_ambient(eng,0.08f,0.09f,0.12f,1.0f);
    /* Spot light above, pointing down, casting shadows. */
    CCLight spot={.type=CC_LIGHT_SPOT,.pos={0,10,2},.dir={0,-1,-0.15f},
                  .color={1.0f,0.95f,0.8f},.intensity=60.0f,.range=30.0f,
                  .inner_angle=0.35f,.outer_angle=0.55f,.cast_shadows=true};
    cc_light_add(eng,&spot);
    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.2f; fx.bloom_intensity=0.1f;
    cc_postfx_set(eng,&fx);
    for(int f=0;f<3;f++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,6,11},.target={0,0.5f,0},.up={0,1,0},.fov_deg=52,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mg,&gxf);
        CCTransform3D a={.pos={-2,1,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,sphere,mr,&a);
        CCTransform3D b={.pos={1.5f,1.2f,-0.5f},.rot={0,0.3f,0,0.95f},.scale={1,1,1}};
        cc_draw_mesh(eng,cube,mb,&b);
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out); printf("screenshot: %s\n",s?s:"(null)");
    cc_shutdown(eng); return 0;
}
