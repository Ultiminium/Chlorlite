/* SSAO test — objects with tight contact points and crevices, rendered with
 * SSAO off (left in filename _off) and on, to show contact-shadow darkening. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/ssao_test.png";
    int ssao_on = (argc>2 && strcmp(argv[2],"on")==0);

    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh ground=cc_mesh_plane(eng,40,40,4);
    CCMesh sphere=cc_mesh_sphere(eng,1.0f,32,32);
    CCMesh cube=cc_mesh_cube(eng,1.2f);

    CCMaterialDesc gd={.base_color={0.75f,0.75f,0.76f,1},.roughness=0.95f};
    CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc wd={.base_color={0.8f,0.8f,0.82f,1},.roughness=0.7f};
    CCMaterial mw=cc_material_create(eng,&wd);

    /* Ambient-heavy lighting so AO is the dominant shading cue. */
    cc_light_set_ambient(eng,0.55f,0.55f,0.6f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.3f,-1.0f,-0.2f},
                 .color={0.5f,0.5f,0.55f},.intensity=0.8f};
    cc_light_add(eng,&sun);

    CCPostFX fx=cc_postfx_default(); fx.bloom=false; fx.vignette=false;
    fx.ssao=ssao_on?true:false; fx.ssao_radius=0.6f; fx.ssao_intensity=1.5f;
    cc_postfx_set(eng,&fx);

    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,5,10},.target={0,0.5f,0},.up={0,1,0},.fov_deg=50,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mg,&gxf);
        /* cluster of touching objects to create crevices/contact zones */
        CCTransform3D a={.pos={-1.5f,1.0f,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,sphere,mw,&a);
        CCTransform3D b={.pos={0.0f,0.6f,0.3f},.rot={0,0.3f,0,0.95f},.scale={1,1,1}};
        cc_draw_mesh(eng,cube,mw,&b);
        CCTransform3D c={.pos={1.4f,1.0f,-0.2f},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,sphere,mw,&c);
        CCTransform3D d={.pos={0.2f,1.7f,-0.1f},.rot={0,0,0,1},.scale={0.7f,0.7f,0.7f}};
        cc_draw_mesh(eng,sphere,mw,&d);
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out); printf("screenshot: %s ssao=%d\n",s?s:"(null)",ssao_on);
    cc_shutdown(eng); return 0;
}
