/* rt_test — render targets: render a scene, capture the composited frame into a
 * render target, then use that RT's color texture as the albedo of a "monitor"
 * quad — the security-camera-monitor / render-to-texture use case. Also reads
 * pixels back from the RT to confirm it captured non-black content. */
#include "cc/claudecore.h"
#include "cc/render.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/rt_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=560;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    CCMesh sph=cc_mesh_sphere(e,1.0f,32,24);
    CCMesh monitor=cc_mesh_plane(e,3.2f,2.0f,1);
    CCMaterialDesc rd={.base_color={0.85f,0.35f,0.3f,1},.roughness=0.35f,.metallic=0.3f,.tint={1,1,1,1}};
    CCMaterial red=cc_material_create(e,&rd);
    CCMaterialDesc gd={.base_color={0.3f,0.32f,0.35f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);
    CCMesh ground=cc_mesh_plane(e,40,40,4);

    cc_light_set_ambient(e,0.15f,0.16f,0.2f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.7f,-0.5f},.color={1,0.96f,0.9f},.intensity=2.8f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.3f,0.45f,0.7f},h[3]={0.6f,0.64f,0.7f},g[3]={0.2f,0.2f,0.2f};
    cc_light_set_sky_colors(e,z,h,g,1.0f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.2f; fx.taa=true;
    cc_postfx_set(e,&fx);

    /* create an RT and render a "camera feed" scene into the frame, capture it */
    CCRenderTarget rt = cc_rt_create(e, 512, 320, CC_FMT_RGBA8, true, 0);
    if(!rt){ fprintf(stderr,"rt create FAILED\n"); return 2; }

    /* PASS 1: render the subject scene (a red sphere) and capture to RT */
    for(int f=0;f<6;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,1.5f,4.5f},.target={0,0,0},.up={0,1,0},.fov_deg=52,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,-1,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        CCTransform3D s={.pos={0,0,0},.rot={0,0.4f,0,0.9f},.scale={1,1,1}}; cc_draw_mesh(e,sph,red,&s);
        cc_frame_end(e);
    }
    cc_rt_capture(e, rt);   /* grab the composited frame into the RT */

    /* read back to confirm non-black capture */
    uint32_t rw=0,rh=0; cc_rt_read_pixels(e,rt,NULL,&rw,&rh);
    uint8_t* px=malloc((size_t)rw*rh*4);
    cc_rt_read_pixels(e,rt,px,&rw,&rh);
    long sum=0; for(size_t i=0;i<(size_t)rw*rh*4;i++) sum+=px[i];
    double avg=(double)sum/((double)rw*rh*4);
    printf("RT captured %ux%u, avg pixel=%.1f (nonblack=%s)\n", rw,rh,avg, avg>8?"OK":"FAIL");
    free(px);

    /* PASS 2: make a material that samples the RT color texture, show it on a
       "monitor" quad in a new scene alongside the real sphere */
    CCTexture feed = cc_rt_color_texture(e, rt);
    CCMaterialDesc md={.base_color={1,1,1,1},.albedo_map=feed,.roughness=0.5f,.metallic=0,.tint={1,1,1,1}};
    CCMaterial screen=cc_material_create(e,&md);

    const char* out2=NULL;
    for(int f=0;f<6;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,2.0f,7.0f},.target={0,0.6f,0},.up={0,1,0},.fov_deg=55,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,-1,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        /* the real sphere on the right */
        CCTransform3D s={.pos={2.2f,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,sph,red,&s);
        /* the monitor showing the captured feed on the left, tilted up to face cam */
        CCTransform3D mon={.pos={-1.8f,0.6f,0},.rot={0.38f,0,0,0.925f},.scale={1,1,1}};
        cc_draw_mesh(e,monitor,screen,&mon);
        cc_frame_end(e);
    }
    out2=cc_screenshot(e,out);
    printf("screenshot: %s (left=monitor showing RT feed, right=real sphere)\n", out2?out2:"(null)");
    cc_rt_destroy(e,rt);
    cc_shutdown(e); return (avg>8)?0:3;
}
