/* IBL test — a metallic/roughness sphere grid lit purely by the sky (no direct
 * lights), showing real cubemap image-based lighting: irradiance + prefiltered
 * specular reflections varying across the grid. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/ibl_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;
    CCMesh sphere=cc_mesh_sphere(eng,0.45f,48,48);

    /* A vivid sky so reflections are clearly visible. */
    float zenith[3]={0.15f,0.30f,0.65f}, horizon[3]={0.85f,0.70f,0.55f}, grnd[3]={0.10f,0.09f,0.08f};
    cc_light_set_sky_colors(eng,zenith,horizon,grnd,1.4f);
    /* Only a faint ambient; the sky IBL should do the lighting. */
    cc_light_set_ambient(eng,0.02f,0.02f,0.03f,1.0f);

    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.4f; fx.bloom_intensity=0.08f;
    cc_postfx_set(eng,&fx);

    int NX=6, NY=4;
    CCMaterial mats[6*4];
    for(int y=0;y<NY;y++)for(int x=0;x<NX;x++){
        float rough=(float)x/(NX-1); rough=fmaxf(0.05f,rough);
        float metal=(float)y/(NY-1);
        CCMaterialDesc d={.base_color={0.9f,0.85f,0.85f,1},.roughness=rough,.metallic=metal};
        mats[y*NX+x]=cc_material_create(eng,&d);
    }
    for(int f=0;f<3;f++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,0,7.5f},.target={0,0,0},.up={0,1,0},.fov_deg=48,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(eng,&cam);
        for(int y=0;y<NY;y++)for(int x=0;x<NX;x++){
            CCTransform3D xf={.pos={(x-(NX-1)/2.0f)*1.2f,(y-(NY-1)/2.0f)*1.2f,0},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(eng,sphere,mats[y*NX+x],&xf);
        }
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out); printf("screenshot: %s\n",s?s:"(null)");
    cc_shutdown(eng); return 0;
}
