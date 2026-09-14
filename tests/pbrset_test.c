/* pbrset_test — real scanned-PBR texture-set loading.
 *
 * LEFT   : full material loaded from a folder of maps via cc_material_load_pbr
 *          (basecolor sRGB + normal + roughness + metallic + ao, packed &
 *          color-space-correct). Metallic blue tiles + terracotta dielectric.
 * MIDDLE : the SAME basecolor loaded as sRGB (correct: GPU linearizes on
 *          sample) — physically-correct albedo.
 * RIGHT  : the SAME basecolor loaded LINEAR (wrong: the old behavior) — visibly
 *          washed-out / too bright because sRGB bytes are treated as linear.
 * The middle-vs-right pair is the point: it proves the gamma fix is real and
 * that color maps now go through the correct sRGB path.
 *
 * Set dir defaults to /tmp/pbr_set (produced by gen_pbr_set). Arg1 = out png,
 * arg2 = set dir. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/pbrset_test.png";
    const char* dir=(argc>2)?argv[2]:"/tmp/pbr_set";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1200;cfg.height=460;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh sphere=cc_mesh_sphere(eng,1.3f,64,64);
    CCMesh ground=cc_mesh_plane(eng,80,80,1);

    /* (1) FULL PBR SET via the new loader. base supplies shading defaults. */
    CCMaterialDesc base={.base_color={1,1,1,1},.roughness=1.0f,.metallic=1.0f,.tint={1,1,1,1}};
    CCMaterial m_set = cc_material_load_pbr(eng, dir, &base);
    if(!m_set){ fprintf(stderr,"FAILED to load PBR set from %s\n",dir); cc_shutdown(eng); return 2; }

    /* (2)/(3) sRGB-correct vs linear albedo, same file. */
    char cpath[512]; snprintf(cpath,sizeof(cpath),"%s/tile_basecolor.png",dir);
    CCTexture t_srgb = cc_texture_load_srgb(eng, cpath);
    CCTexture t_lin  = cc_texture_load(eng, cpath);
    CCMaterialDesc dsr={.base_color={1,1,1,1},.albedo_map=t_srgb,.roughness=0.6f,.metallic=0.0f,.tint={1,1,1,1}};
    CCMaterialDesc dli={.base_color={1,1,1,1},.albedo_map=t_lin, .roughness=0.6f,.metallic=0.0f,.tint={1,1,1,1}};
    CCMaterial m_srgb=cc_material_create(eng,&dsr);
    CCMaterial m_lin =cc_material_create(eng,&dli);

    CCMaterialDesc gd={.base_color={0.4f,0.4f,0.42f,1},.roughness=0.85f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(eng,&gd);

    /* Environment: metals need sky IBL to read as metal (documented gotcha). */
    cc_light_set_ambient(eng,0.12f,0.13f,0.16f,1.0f);
    CCLight key={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-0.6f,-0.45f},.color={1,0.97f,0.9f},.intensity=3.0f,.cast_shadows=true};
    cc_light_add(eng,&key);
    float z[3]={0.30f,0.45f,0.72f},h2[3]={0.55f,0.6f,0.68f},g2[3]={0.2f,0.2f,0.2f};
    cc_light_set_sky_colors(eng,z,h2,g2,1.0f);

    CCPostFX fx=cc_postfx_default();
    fx.auto_exposure=true; fx.ae_key=0.20f;
    fx.bloom=true; fx.bloom_threshold=1.5f; fx.bloom_intensity=0.05f;
    cc_postfx_set(eng,&fx);

    float xs[3]={-3.1f,0.0f,3.1f};
    CCMaterial mats[3]={m_set,m_srgb,m_lin};
    for(int frame=0;frame<8;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,1.2f,7.5f},.target={0,0.4f,-8},.up={0,1,0},
                          .fov_deg=52,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D g={.pos={0,-1.35f,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mg,&g);
        for(int i=0;i<3;i++){
            CCTransform3D s={.pos={xs[i],0.1f,0},.rot={0,0,0,1},.scale={1,1,1}};
            /* slow spin so the tile pattern + normals read in 3D */
            float a=0.5f; s.rot[1]=sinf(a*0.5f); s.rot[3]=cosf(a*0.5f);
            cc_draw_mesh(eng,sphere,mats[i],&s);
        }
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s  (L=full PBR set, M=sRGB albedo [correct], R=linear albedo [wrong/washed])\n", s?s:"(null)");
    cc_shutdown(eng); return 0;
}
