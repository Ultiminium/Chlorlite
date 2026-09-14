/* Outline test — a small scene rendered with edge-detection outlines (toon look). */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/outline_test.png";
    int on=(argc>2 && strcmp(argv[2],"on")==0);
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;
    CCMesh ground=cc_mesh_plane(eng,40,40,4);
    CCMesh sphere=cc_mesh_sphere(eng,1.0f,32,32);
    CCMesh cube=cc_mesh_cube(eng,1.4f);
    CCMesh torus=cc_mesh_sphere(eng,0.8f,24,24);
    CCMaterialDesc gd={.base_color={0.55f,0.6f,0.5f,1},.roughness=0.9f};
    CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc rd={.base_color={0.85f,0.4f,0.35f,1},.roughness=0.6f};
    CCMaterial mr=cc_material_create(eng,&rd);
    CCMaterialDesc bd={.base_color={0.4f,0.55f,0.85f,1},.roughness=0.5f};
    CCMaterial mb=cc_material_create(eng,&bd);
    CCMaterialDesc yd={.base_color={0.9f,0.8f,0.35f,1},.roughness=0.5f};
    CCMaterial my=cc_material_create(eng,&yd);
    cc_light_set_ambient(eng,0.3f,0.32f,0.35f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.9f,-0.35f},.color={1,0.97f,0.9f},.intensity=2.2f,.cast_shadows=true};
    cc_light_add(eng,&sun);
    CCPostFX fx=cc_postfx_default(); fx.bloom=false; fx.fxaa=true;
    fx.outline=on?true:false;
    fx.outline_color[0]=0.05f; fx.outline_color[1]=0.05f; fx.outline_color[2]=0.08f;
    fx.outline_thickness=1.5f; fx.outline_depth_sensitivity=1.0f; fx.outline_normal_sensitivity=1.2f;
    cc_postfx_set(eng,&fx);
    for(int f=0;f<3;f++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,4,9},.target={0,1,0},.up={0,1,0},.fov_deg=52,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mg,&gxf);
        CCTransform3D a={.pos={-2.5f,1,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,sphere,mr,&a);
        CCTransform3D b={.pos={0,1,-0.5f},.rot={0,0.4f,0,0.92f},.scale={1,1,1}};
        cc_draw_mesh(eng,cube,mb,&b);
        CCTransform3D c={.pos={2.5f,0.8f,0.3f},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,torus,my,&c);
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out); printf("screenshot: %s outline=%d\n",s?s:"(null)",on);
    cc_shutdown(eng); return 0;
}
