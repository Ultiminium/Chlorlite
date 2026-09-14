/* realism_test — the realism axis: one sphere, four shading models, same lights.
 *   PBR   (hyper-real, physically-based Cook-Torrance)
 *   TOON  (quantized cel bands + stepped specular)
 *   FLAT  (single hard lambert step, matte stylized)
 *   RIM   (lambert + strong fresnel rim accent)
 * Proves CC spans hyper-real ↔ non-real with per-material shading selection,
 * all lit by the same scene lights so realistic + stylized objects coexist. */
#include "cc/claudecore.h"
#include <stdio.h>

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/realism_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1200;cfg.height=400;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh sphere=cc_mesh_sphere(eng,1.0f,48,48);
    CCMesh ground=cc_mesh_plane(eng,60,60,4);

    float col[4]={0.90f,0.25f,0.22f,1};   /* same base color for all four */

    /* PBR — hyper-real */
    CCMaterialDesc pbr={.base_color={col[0],col[1],col[2],1},.roughness=0.35f,.metallic=0.1f,
                        .tint={1,1,1,1},.shading_model=CC_SHADE_PBR};
    CCMaterial mPBR=cc_material_create(eng,&pbr);
    /* TOON — 4 cel bands + stepped highlight */
    CCMaterialDesc toon={.base_color={col[0],col[1],col[2],1},.roughness=0.4f,
                         .tint={1,1,1,1},.shading_model=CC_SHADE_TOON,.toon_bands=4,.toon_specular=0.6f};
    CCMaterial mTOON=cc_material_create(eng,&toon);
    /* FLAT — matte single-step */
    CCMaterialDesc flat={.base_color={col[0],col[1],col[2],1},
                         .tint={1,1,1,1},.shading_model=CC_SHADE_FLAT};
    CCMaterial mFLAT=cc_material_create(eng,&flat);
    /* RIM — fresnel accent */
    CCMaterialDesc rim={.base_color={col[0],col[1],col[2],1},
                        .tint={1,1,1,1},.shading_model=CC_SHADE_RIM,
                        .rim_strength=1.2f,.rim_power=2.5f,.rim_color={0.4f,0.7f,1.0f}};
    CCMaterial mRIM=cc_material_create(eng,&rim);

    CCMaterialDesc gd={.base_color={0.5f,0.52f,0.55f,1},.roughness=0.95f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(eng,&gd);

    cc_light_set_ambient(eng,0.04f,0.045f,0.055f,1.0f);   /* low, so shading dominates */
    CCLight key={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-0.7f,-0.5f},.color={1,0.95f,0.85f},.intensity=2.6f,.cast_shadows=true};
    cc_light_add(eng,&key);
    /* dim sky so the PBR sphere still gets some IBL but stylized bands read strongly */
    float zc[3]={0.10f,0.16f,0.32f},hc[3]={0.25f,0.30f,0.38f},gc[3]={0.10f,0.11f,0.10f};
    cc_light_set_sky_colors(eng,zc,hc,gc,0.35f);
    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.2f; fx.bloom_intensity=0.1f;
    cc_postfx_set(eng,&fx);

    CCMaterial mats[4]={mPBR,mTOON,mFLAT,mRIM};
    float xs[4]={-4.8f,-1.6f,1.6f,4.8f};
    for(int frame=0;frame<4;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,1.5f,8},.target={0,0.4f,0},.up={0,1,0},
                          .fov_deg=42,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D g={.pos={0,-1.2f,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,ground,mg,&g);
        for(int i=0;i<4;i++){ CCTransform3D t={.pos={xs[i],0,0},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(eng,sphere,mats[i],&t); }
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s  (PBR | TOON | FLAT | RIM)\n", s?s:"(null)");
    cc_shutdown(eng); return 0;
}
