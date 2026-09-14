/* realism_integration_test — ALL realism features working together in ONE scene,
 * arranged so each is verifiable:
 *  - AUTO-EXPOSURE: dim-ish key light; scene should still expose to a filmic mid.
 *  - PCSS SHADOWS: a tall pillar + a low block cast onto the floor; penumbra
 *    should be tight at the contact and widen with height/distance.
 *  - ANISOTROPIC: tiled checker floor receding to horizon stays crisp.
 *  - DETAIL NORMALS: the big sphere carries a coarse detail-normal (micro relief).
 *  - SHADING MODELS: PBR sphere (left of center) vs TOON sphere (right) under the
 *    SAME lights — realistic + stylized coexisting.
 * This is an integration test, not a geometry lineup: it stresses how the
 * features interact (e.g. does auto-exposure meter correctly with a bright toon
 * object present? do PCSS shadows land right under auto-exposure?). */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/realism_integration_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1280;cfg.height=640;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    /* tiled checker floor texture (anisotropy) */
    int TW=256; uint8_t* chk=malloc(TW*TW*4);
    for(int y=0;y<TW;y++)for(int x=0;x<TW;x++){int c=((x/16)^(y/16))&1;uint8_t v=c?205:60;int i=(y*TW+x)*4;chk[i]=v;chk[i+1]=v;chk[i+2]=(uint8_t)(v*0.92f);chk[i+3]=255;}
    CCTextureDesc cd={.width=TW,.height=TW,.format=CC_FMT_RGBA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=true};
    CCTexture t_floor=cc_texture_create(eng,&cd,chk); free(chk);

    /* coarse detail normal (dome bumps) */
    int NW=256; uint8_t* nm=malloc(NW*NW*4);
    for(int y=0;y<NW;y++)for(int x=0;x<NW;x++){float u=x/(float)NW*6.283f*4,v=y/(float)NW*6.283f*4;
        float nx=-0.4f*cosf(u),ny=-0.4f*cosf(v),nz=1.0f;float l=sqrtf(nx*nx+ny*ny+nz*nz);int i=(y*NW+x)*4;
        nm[i]=(uint8_t)((nx/l*.5f+.5f)*255);nm[i+1]=(uint8_t)((ny/l*.5f+.5f)*255);nm[i+2]=(uint8_t)((nz/l*.5f+.5f)*255);nm[i+3]=255;}
    CCTextureDesc ndd={.width=NW,.height=NW,.format=CC_FMT_RGBA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=true};
    CCTexture t_det=cc_texture_create(eng,&ndd,nm); free(nm);

    CCMesh floor=cc_mesh_plane(eng,120,120,1);
    CCMesh sphere=cc_mesh_sphere(eng,1.1f,64,64);
    CCMesh pillar=cc_mesh_cylinder(eng,0.35f,3.2f,32);
    CCMesh block=cc_mesh_cube(eng,1.0f);

    CCMaterialDesc fd={.base_color={1,1,1,1},.albedo_map=t_floor,.roughness=0.75f,.tint={1,1,1,1}};
    CCMaterial mFloor=cc_material_create(eng,&fd);
    CCMaterialDesc pbrDet={.base_color={0.8f,0.42f,0.36f,1},.roughness=0.38f,.metallic=0.1f,.tint={1,1,1,1},
                          .detail_normal_map=t_det,.detail_normal_scale=3.0f,.detail_normal_strength=1.0f};
    CCMaterial mPBRdet=cc_material_create(eng,&pbrDet);
    CCMaterialDesc pbr={.base_color={0.35f,0.55f,0.85f,1},.roughness=0.3f,.metallic=0.5f,.tint={1,1,1,1}};
    CCMaterial mPBR=cc_material_create(eng,&pbr);
    CCMaterialDesc toon={.base_color={0.9f,0.6f,0.25f,1},.tint={1,1,1,1},.shading_model=CC_SHADE_TOON,.toon_bands=4,.toon_specular=0.5f};
    CCMaterial mToon=cc_material_create(eng,&toon);
    CCMaterialDesc pil={.base_color={0.7f,0.7f,0.72f,1},.roughness=0.6f,.tint={1,1,1,1}};
    CCMaterial mPil=cc_material_create(eng,&pil);

    cc_light_set_ambient(eng,0.06f,0.07f,0.09f,1.0f);
    CCLight key={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.55f,-0.6f,-0.35f},.color={1,0.95f,0.85f},.intensity=2.4f,.cast_shadows=true};
    cc_light_add(eng,&key);
    cc_light_set_shadow_softness(eng, 4.0f);  /* PCSS: clearly soft penumbra */
    float z[3]={0.14f,0.22f,0.42f},h2[3]={0.35f,0.4f,0.5f},g2[3]={0.12f,0.13f,0.12f};
    cc_light_set_sky_colors(eng,z,h2,g2,0.5f);
    CCPostFX fx=cc_postfx_default();
    fx.auto_exposure=true; fx.ae_key=0.18f;      /* AUTO-EXPOSURE on */
    fx.bloom=true; fx.bloom_threshold=1.3f; fx.bloom_intensity=0.06f;
    fx.fxaa=true;
    cc_postfx_set(eng,&fx);

    for(int frame=0;frame<8;frame++){   /* let auto-exposure settle */
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,2.6f,9},.target={0,1.0f,-3},.up={0,1,0},
                          .fov_deg=52,.near_plane=0.1f,.far_plane=250,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D g={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,floor,mFloor,&g);
        /* shadow casters: tall pillar (wide penumbra up high) + low block (tight contact) */
        CCTransform3D p={.pos={-3.5f,1.6f,-2},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,pillar,mPil,&p);
        CCTransform3D bl={.pos={3.6f,0.5f,-1},.rot={0,0.4f,0,0.92f},.scale={1,1,1}}; cc_draw_mesh(eng,block,mPil,&bl);
        /* shading-model coexistence + detail normals */
        CCTransform3D s1={.pos={-1.6f,1.1f,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,sphere,mPBRdet,&s1);
        CCTransform3D s2={.pos={0.0f,1.1f,0},.rot={0,0,0,1},.scale={1,1,1}};  cc_draw_mesh(eng,sphere,mPBR,&s2);
        CCTransform3D s3={.pos={1.6f,1.1f,0},.rot={0,0,0,1},.scale={1,1,1}};  cc_draw_mesh(eng,sphere,mToon,&s3);
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s (auto-exp + PCSS + aniso floor + detail-normal + PBR/toon coexist)\n", s?s:"(null)");
    cc_shutdown(eng); return 0;
}
