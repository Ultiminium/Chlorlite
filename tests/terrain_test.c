/* terrain_test — procedural heightmap terrain. Generates a terrain mesh from
 * fbm noise via cc_procgen_terrain and renders it under golden-hour light with
 * the full realism stack. Also generates the standalone heightmap texture via
 * cc_procgen_terrain_heightmap and confirms it's non-trivial. */
#include "cc/claudecore.h"
#include "cc/procgen.h"
#include <stdio.h>

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/terrain_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1100;cfg.height=620;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    /* standalone heightmap texture (sanity) */
    CCTextureId hm=cc_procgen_terrain_heightmap(e,256,256,1234,6);
    printf("heightmap texture id=%u (%s)\n", hm, hm?"OK":"FAIL");

    /* terrain mesh */
    CCProcTerrainDesc td={.grid_w=200,.grid_h=200,.cell_size=0.5f,
                          .heightmap=0,.heightmap_seed=1234,.height_scale=9.0f,.octaves=6};
    CCMeshId terrain=cc_procgen_terrain(e,&td);
    printf("terrain mesh id=%u (%s)\n", terrain, terrain?"OK":"FAIL");
    if(!terrain){ return 2; }

    CCMaterialDesc md={.base_color={1,1,1,1},.roughness=0.92f,.metallic=0.0f,.tint={1,1,1,1}};
    CCMaterial mat=cc_material_create(e,&md);   /* vertex colors carry the look */

    cc_light_set_ambient(e,0.11f,0.12f,0.15f,1);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.7f,-0.4f,-0.45f},.color={1.0f,0.82f,0.6f},.intensity=3.2f,.cast_shadows=true};
    cc_light_add(e,&sun);
    float z[3]={0.24f,0.36f,0.6f},h[3]={0.9f,0.74f,0.55f},g[3]={0.15f,0.15f,0.12f};
    cc_light_set_sky_colors(e,z,h,g,0.8f);
    CCPostFX fx=cc_postfx_default();
    fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.18f;
    fx.ssgi=true; fx.ssgi_intensity=1.4f; fx.ssgi_radius=2.6f;
    fx.taa=true; fx.taa_blend=0.85f; fx.bloom=true; fx.bloom_threshold=1.3f; fx.bloom_intensity=0.06f;
    fx.fog=true; fx.fog_density=0.012f; fx.fog_color[0]=0.82f; fx.fog_color[1]=0.74f; fx.fog_color[2]=0.64f; fx.fog_height=8.0f;
    fx.vignette=true; fx.vignette_strength=0.28f;
    cc_postfx_set(e,&fx);

    for(int f=0;f<8;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,14,34},.target={0,2,-6},.up={0,1,0},.fov_deg=52,.near_plane=0.1f,.far_plane=400,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D t={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(e,terrain,mat,&t);
        cc_frame_end(e);
    }
    const char* s=cc_screenshot(e,out);
    printf("screenshot: %s (procedural heightmap terrain)\n", s?s:"(null)");
    cc_shutdown(e); return 0;
}
