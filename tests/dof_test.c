/* dof_test — depth of field. A row of spheres marches from near (bottom) to
 * far (top). The camera focuses on the MIDDLE sphere: nearer and farther
 * spheres blur, the focal one stays sharp. Renders two images:
 *   /tmp/dof_off.png — DOF disabled (everything sharp — the CG "tell")
 *   /tmp/dof_on.png  — DOF enabled (focus falloff, depth-aware)
 * If an out path arg is given, the ON image is also written there (for the
 * regression harness). */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>

static void scene(CCEngine* eng, int dof_on, const char* out){
    CCMesh sphere=cc_mesh_sphere(eng,0.55f,48,48);
    CCMesh ground=cc_mesh_plane(eng,200,200,1);

    /* checker ground so the blur is obvious on a textured surface too */
    int TW=256; static uint8_t chk[256*256*4];
    for(int y=0;y<TW;y++)for(int x=0;x<TW;x++){
        int c=((x/16)^(y/16))&1; uint8_t v=c?210:60; int i=(y*TW+x)*4;
        chk[i]=v; chk[i+1]=(uint8_t)(v*0.95f); chk[i+2]=(uint8_t)(v*0.8f); chk[i+3]=255;
    }
    CCTextureDesc cd={.width=(uint32_t)TW,.height=(uint32_t)TW,.format=CC_FMT_RGBA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=true};
    CCTexture t_chk=cc_texture_create(eng,&cd,chk);
    CCMaterialDesc gd={.base_color={1,1,1,1},.albedo_map=t_chk,.roughness=0.8f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(eng,&gd);

    /* colored spheres down the Z axis */
    const int N=7;
    CCMaterial mats[7];
    float cols[7][3]={{0.9f,0.3f,0.25f},{0.9f,0.6f,0.2f},{0.85f,0.85f,0.3f},
                      {0.3f,0.8f,0.35f},{0.3f,0.6f,0.9f},{0.5f,0.4f,0.9f},{0.85f,0.4f,0.8f}};
    for(int i=0;i<N;i++){
        CCMaterialDesc d={.base_color={cols[i][0],cols[i][1],cols[i][2],1},.roughness=0.35f,.metallic=0.0f,.tint={1,1,1,1}};
        mats[i]=cc_material_create(eng,&d);
    }

    cc_light_set_ambient(eng,0.14f,0.15f,0.18f,1.0f);
    CCLight key={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.7f,-0.5f},.color={1,0.97f,0.9f},.intensity=2.8f,.cast_shadows=true};
    cc_light_add(eng,&key);
    float z[3]={0.25f,0.4f,0.7f},h2[3]={0.5f,0.55f,0.62f},g2[3]={0.2f,0.2f,0.2f};
    cc_light_set_sky_colors(eng,z,h2,g2,0.8f);

    CCPostFX fx=cc_postfx_default();
    fx.auto_exposure=true; fx.ae_key=0.2f;
    fx.taa=true; fx.taa_blend=0.85f;
    if(dof_on){
        fx.dof=true;
        fx.dof_focus_dist=9.0f;   /* focus on the middle sphere */
        fx.dof_focus_range=1.6f;
        fx.dof_max_blur=10.0f;
    }
    cc_postfx_set(eng,&fx);

    /* camera low and looking down the row */
    for(int frame=0;frame<8;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={1.4f,1.3f,3.0f},.target={-0.4f,0.5f,-9},.up={0,1,0},
                          .fov_deg=50,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D g={.pos={0,-0.55f,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,ground,mg,&g);
        for(int i=0;i<N;i++){
            float zz = 0.0f - i*3.0f;   /* 0,-3,-6,-9,-12,-15,-18 */
            CCTransform3D s={.pos={ (i%2? -0.7f:0.2f), 0.0f, zz},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(eng,sphere,mats[i],&s);
        }
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s (dof=%s)\n", s?s:"(null)", dof_on?"ON":"OFF");
}

int main(int argc,char**argv){
    { CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=620;cfg.verbose=false;
      CCEngine* e=cc_init(&cfg); if(!e)return 1; scene(e,0,"/tmp/dof_off.png"); cc_shutdown(e); }
    { CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=620;cfg.verbose=false;
      CCEngine* e=cc_init(&cfg); if(!e)return 1;
      const char* out=(argc>1)?argv[1]:"/tmp/dof_on.png";
      scene(e,1,out); cc_shutdown(e); }
    (void)argc;(void)argv; return 0;
}
