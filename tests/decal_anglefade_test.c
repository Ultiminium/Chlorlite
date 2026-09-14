/* Verify decal angle_fade: a decal projected down should stamp the floor but be
 * culled off a vertical wall when angle_fade is tight. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void gen_solid(uint8_t* px,uint32_t w,uint32_t h,void* ud){(void)ud;
    for(uint32_t i=0;i<w*h;i++){px[i*4+0]=240;px[i*4+1]=60;px[i*4+2]=60;px[i*4+3]=255;}}
int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/decal_af.png";
    float af=(argc>2)?(float)atof(argv[2]):0.0f;
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=800;cfg.height=450;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;
    CCMesh floor=cc_mesh_plane(eng,20,20,2);
    CCMesh wall=cc_mesh_cube(eng,4.0f);
    CCTextureDesc td={.width=8,.height=8,.format=CC_FMT_RGBA8};
    CCTexture tex=cc_texture_proc(eng,&td,gen_solid,NULL);
    CCMaterialDesc md={.base_color={0.7f,0.7f,0.72f,1},.roughness=0.8f};
    CCMaterial mat=cc_material_create(eng,&md);
    cc_light_set_ambient(eng,0.4f,0.4f,0.45f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-1,-0.3f},.color={1,1,1},.intensity=1.5f};
    cc_light_add(eng,&sun);
    CCPostFX fx=cc_postfx_default(); fx.bloom=false;
    cc_postfx_set(eng,&fx);
    for(int f=0;f<3;f++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,5,10},.target={0,1,0},.up={0,1,0},.fov_deg=55,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,floor,mat,&gxf);
        CCTransform3D wxf={.pos={0,2,-2},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,wall,mat,&wxf);   /* a box whose front face is vertical */
        /* one big decal projecting straight DOWN over both floor and box top */
        CCDecal d={0}; d.pos[0]=0; d.pos[1]=6; d.pos[2]=0; d.rot[3]=1;
        d.size[0]=8; d.size[1]=12; d.size[2]=8;
        d.color[0]=d.color[1]=d.color[2]=d.color[3]=1.0f;
        d.angle_fade=af;
        cc_draw_decal(eng,tex,&d);
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out); printf("screenshot: %s af=%.2f\n",s?s:"(null)",af);
    cc_shutdown(eng); return 0;
}
