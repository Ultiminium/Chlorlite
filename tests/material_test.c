/* material_test — material/texture realism: anisotropic filtering + detail
 * normals. (1) The tiled checker ground recedes to the horizon staying crisp
 * (16x anisotropic filtering) instead of blurring. (2) Right sphere carries a
 * detail normal map (coarse ripples) showing micro-surface relief under light;
 * left sphere is plain for contrast. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/material_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1100;cfg.height=520;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    /* checker albedo (repeating) — anisotropy stress test */
    int TW=256; uint8_t* chk=malloc(TW*TW*4);
    for(int y=0;y<TW;y++)for(int x=0;x<TW;x++){
        int c=((x/16)^(y/16))&1; uint8_t v=c?220:45; int i=(y*TW+x)*4;
        chk[i]=v; chk[i+1]=v; chk[i+2]=(uint8_t)(v*0.9f); chk[i+3]=255;
    }
    CCTextureDesc cd={.width=TW,.height=TW,.format=CC_FMT_RGBA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=true};
    CCTexture t_chk=cc_texture_create(eng,&cd,chk); free(chk);

    /* coarse detail normal: rounded bumps (dome grid), reads as deliberate relief */
    int NW=256; uint8_t* nm=malloc(NW*NW*4);
    for(int y=0;y<NW;y++)for(int x=0;x<NW;x++){
        float u=x/(float)NW*6.2831853f*4, v=y/(float)NW*6.2831853f*4;
        float nx=-0.5f*cosf(u)*0.5f, ny=-0.5f*cosf(v)*0.5f, nz=1.0f;  /* gentle domes */
        float l=sqrtf(nx*nx+ny*ny+nz*nz);
        int i=(y*NW+x)*4;
        nm[i]=(uint8_t)((nx/l*0.5f+0.5f)*255); nm[i+1]=(uint8_t)((ny/l*0.5f+0.5f)*255);
        nm[i+2]=(uint8_t)((nz/l*0.5f+0.5f)*255); nm[i+3]=255;
    }
    CCTextureDesc nd={.width=NW,.height=NW,.format=CC_FMT_RGBA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=true};
    CCTexture t_det=cc_texture_create(eng,&nd,nm); free(nm);

    CCMesh ground=cc_mesh_plane(eng,120,120,1);
    CCMesh sphere=cc_mesh_sphere(eng,1.4f,64,64);

    CCMaterialDesc gd={.base_color={1,1,1,1},.albedo_map=t_chk,.roughness=0.7f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc sd_plain={.base_color={0.75f,0.45f,0.38f,1},.roughness=0.4f,.metallic=0.1f,.tint={1,1,1,1}};
    CCMaterial m_plain=cc_material_create(eng,&sd_plain);
    CCMaterialDesc sd_det={.base_color={0.75f,0.45f,0.38f,1},.roughness=0.4f,.metallic=0.1f,.tint={1,1,1,1},
                           .detail_normal_map=t_det,.detail_normal_scale=3.0f,.detail_normal_strength=1.0f};
    CCMaterial m_det=cc_material_create(eng,&sd_det);

    cc_light_set_ambient(eng,0.10f,0.11f,0.14f,1.0f);
    CCLight key={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-0.55f,-0.4f},.color={1,0.96f,0.88f},.intensity=2.8f,.cast_shadows=true};
    cc_light_add(eng,&key);
    float z[3]={0.18f,0.30f,0.55f},h2[3]={0.45f,0.5f,0.6f},g2[3]={0.15f,0.16f,0.15f};
    cc_light_set_sky_colors(eng,z,h2,g2,0.5f);
    CCPostFX fx=cc_postfx_default(); fx.auto_exposure=true; fx.ae_key=0.20f;
    fx.bloom=true; fx.bloom_threshold=1.4f; fx.bloom_intensity=0.06f;
    cc_postfx_set(eng,&fx);

    for(int frame=0;frame<6;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,1.4f,7},.target={0,0.6f,-14},.up={0,1,0},
                          .fov_deg=55,.near_plane=0.1f,.far_plane=300,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D g={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,ground,mg,&g);
        CCTransform3D sl={.pos={-2.0f,1.2f,-1},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,sphere,m_plain,&sl);
        CCTransform3D sr={.pos={ 2.0f,1.2f,-1},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,sphere,m_det,&sr);
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s  (L plain sphere, R detail-normal sphere, anisotropic ground)\n", s?s:"(null)");
    cc_shutdown(eng); return 0;
}
