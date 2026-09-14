/* Decal test — a textured ground + some boxes, with projected decals stamped
 * across them (a logo/cross pattern) to verify projection onto varied geometry. */
#include "cc/claudecore.h"
#include "cc/debug.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void gen_checker(uint8_t* px,uint32_t w,uint32_t h,void* ud){(void)ud;
    for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++){
        int c=((x/16)+(y/16))%2?200:120;
        px[(y*w+x)*4+0]=c;px[(y*w+x)*4+1]=c;px[(y*w+x)*4+2]=c;px[(y*w+x)*4+3]=255;}}

/* decal texture: a red ring with transparent center + outside */
static void gen_ring(uint8_t* px,uint32_t w,uint32_t h,void* ud){(void)ud;
    for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++){
        float dx=(x+0.5f)/w-0.5f, dy=(y+0.5f)/h-0.5f;
        float d=sqrtf(dx*dx+dy*dy)*2.0f;
        float a=(d>0.55f&&d<0.9f)?1.0f:0.0f;
        px[(y*w+x)*4+0]=230;px[(y*w+x)*4+1]=40;px[(y*w+x)*4+2]=40;
        px[(y*w+x)*4+3]=(uint8_t)(a*255);}}

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/decal_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh ground=cc_mesh_plane(eng,40,40,4);
    CCMesh box=cc_mesh_cube(eng,2.0f);
    CCTextureDesc ctd={.width=256,.height=256,.format=CC_FMT_RGBA8,.linear_filter=true};
    CCTexture tex_ck=cc_texture_proc(eng,&ctd,gen_checker,NULL);
    CCTextureDesc rtd={.width=128,.height=128,.format=CC_FMT_RGBA8,.linear_filter=true};
    CCTexture tex_ring=cc_texture_proc(eng,&rtd,gen_ring,NULL);

    CCMaterialDesc gd={.base_color={1,1,1,1},.roughness=0.8f,.albedo_map=tex_ck};
    CCMaterial mat_ground=cc_material_create(eng,&gd);
    CCMaterialDesc bd={.base_color={0.6f,0.6f,0.65f,1},.roughness=0.5f};
    CCMaterial mat_box=cc_material_create(eng,&bd);

    cc_light_set_ambient(eng,0.1f,0.1f,0.12f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-1,-0.3f},.color={1,0.96f,0.9f},.intensity=2.5f};
    cc_light_add(eng,&sun);

    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.0f; fx.bloom_intensity=0.12f;
    cc_postfx_set(eng,&fx);

    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,9,12},.target={0,0,0},.up={0,1,0},.fov_deg=55,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mat_ground,&gxf);
        /* two boxes for decals to wrap onto */
        CCTransform3D b1={.pos={-3,1,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,box,mat_box,&b1);
        CCTransform3D b2={.pos={3.5f,1,-1},.rot={0,0.38f,0,0.92f},.scale={1,1,1}};
        cc_draw_mesh(eng,box,mat_box,&b2);

        /* Row of ring decals projected straight down onto the ground+boxes. */
        for(int i=0;i<5;i++){
            CCDecal dc={0};
            dc.pos[0]=-6.0f+i*3.0f; dc.pos[1]=2.5f; dc.pos[2]=0.0f;
            dc.rot[3]=1.0f;
            dc.size[0]=3.0f; dc.size[1]=6.0f; dc.size[2]=3.0f; /* y = projection depth */
            dc.color[0]=dc.color[1]=dc.color[2]=dc.color[3]=1.0f;
            dc.emissive=(i==2)?2.5f:0.0f;   /* middle one glows */
            dc.angle_fade=0.0f;
            cc_draw_decal(eng, tex_ring, &dc);
        }
        cc_frame_end(eng);
    }
    CCFrameStats st=cc_debug_stats(eng);
    printf("draw_calls=%u\n",st.draw_calls);
    const char* s=cc_screenshot(eng,out); printf("screenshot: %s\n",s?s:"(null)");
    cc_shutdown(eng); return 0;
}
