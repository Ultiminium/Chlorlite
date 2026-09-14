/* SSR test — glossy metallic floor reflecting colored objects above it. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/ssr_test.png";
    int ssr_on=(argc>2 && strcmp(argv[2],"on")==0);

    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh floor=cc_mesh_plane(eng,40,40,4);
    CCMesh sphere=cc_mesh_sphere(eng,1.0f,32,32);
    CCMesh cube=cc_mesh_cube(eng,1.4f);

    /* Highly reflective floor: metallic + low roughness = SSR mirror. */
    CCMaterialDesc fd={.base_color={0.15f,0.15f,0.17f,1},.roughness=0.08f,.metallic=1.0f};
    CCMaterial mfloor=cc_material_create(eng,&fd);
    CCMaterialDesc rd={.base_color={0.9f,0.25f,0.2f,1},.roughness=0.4f,.metallic=0.0f};
    CCMaterial mred=cc_material_create(eng,&rd);
    CCMaterialDesc gd={.base_color={0.2f,0.8f,0.3f,1},.roughness=0.4f};
    CCMaterial mgreen=cc_material_create(eng,&gd);
    CCMaterialDesc bd={.base_color={0.25f,0.4f,0.9f,1},.roughness=0.3f};
    CCMaterial mblue=cc_material_create(eng,&bd);

    cc_light_set_ambient(eng,0.15f,0.15f,0.18f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.9f,-0.3f},.color={1,0.96f,0.9f},.intensity=2.8f};
    cc_light_add(eng,&sun);
    float zenith[3]={0.1f,0.15f,0.35f},horizon[3]={0.6f,0.5f,0.45f},grnd[3]={0.1f,0.1f,0.1f};
    cc_light_set_sky_colors(eng,zenith,horizon,grnd,1.0f);

    CCPostFX fx=cc_postfx_default();
    fx.bloom=true; fx.bloom_threshold=1.3f; fx.bloom_intensity=0.1f;
    fx.ssr=ssr_on?true:false; fx.ssr_intensity=1.0f; fx.ssr_max_distance=30.0f;
    cc_postfx_set(eng,&fx);

    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,2.2f,9},.target={0,1.5f,0},.up={0,1,0},.fov_deg=52,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,floor,mfloor,&gxf);
        CCTransform3D a={.pos={-3,1.6f,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,sphere,mred,&a);
        CCTransform3D b={.pos={0,1.4f,-1},.rot={0,0.3f,0,0.95f},.scale={1,1,1}};
        cc_draw_mesh(eng,cube,mgreen,&b);
        CCTransform3D c={.pos={3,1.6f,0.5f},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,sphere,mblue,&c);
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out); printf("screenshot: %s ssr=%d\n",s?s:"(null)",ssr_on);
    cc_shutdown(eng); return 0;
}
