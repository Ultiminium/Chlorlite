/* ccscene_test — load a whole SCENE from text files on disk.
 *
 * Writes two .ccmodel files (a cube and a pyramid) and one .cclist manifest that
 * places several instances of each with different transforms + material slots,
 * then loads the manifest with cc_sceneasset_load and draws it with cc_sceneasset_draw.
 * Nothing about the scene layout is hard-coded in C — it all comes from the
 * .cclist file. Proves the engine loads multi-object scenes from text. */
#include "cc/claudecore.h"
#include "cc/ccscene.h"
#include <stdio.h>

static void write_cube(const char* path){
    FILE* f=fopen(path,"w");
    fprintf(f,
      "ccmodel 1\nname cube\nverts 24\n"
      /* 6 faces, 4 verts each, flat normals, per-face color via material slot */
      /* +X */
      "v 0.5 -0.5 -0.5  1 0 0  0 0  0 0 1 1 255 255 255 255\n"
      "v 0.5  0.5 -0.5  1 0 0  1 0  0 0 1 1 255 255 255 255\n"
      "v 0.5  0.5  0.5  1 0 0  1 1  0 0 1 1 255 255 255 255\n"
      "v 0.5 -0.5  0.5  1 0 0  0 1  0 0 1 1 255 255 255 255\n"
      /* -X */
      "v -0.5 -0.5  0.5  -1 0 0  0 0  0 0 1 1 255 255 255 255\n"
      "v -0.5  0.5  0.5  -1 0 0  1 0  0 0 1 1 255 255 255 255\n"
      "v -0.5  0.5 -0.5  -1 0 0  1 1  0 0 1 1 255 255 255 255\n"
      "v -0.5 -0.5 -0.5  -1 0 0  0 1  0 0 1 1 255 255 255 255\n"
      /* +Y */
      "v -0.5 0.5 -0.5  0 1 0  0 0  1 0 0 1 255 255 255 255\n"
      "v -0.5 0.5  0.5  0 1 0  1 0  1 0 0 1 255 255 255 255\n"
      "v  0.5 0.5  0.5  0 1 0  1 1  1 0 0 1 255 255 255 255\n"
      "v  0.5 0.5 -0.5  0 1 0  0 1  1 0 0 1 255 255 255 255\n"
      /* -Y */
      "v -0.5 -0.5  0.5  0 -1 0  0 0  1 0 0 1 255 255 255 255\n"
      "v -0.5 -0.5 -0.5  0 -1 0  1 0  1 0 0 1 255 255 255 255\n"
      "v  0.5 -0.5 -0.5  0 -1 0  1 1  1 0 0 1 255 255 255 255\n"
      "v  0.5 -0.5  0.5  0 -1 0  0 1  1 0 0 1 255 255 255 255\n"
      /* +Z */
      "v -0.5 -0.5 0.5  0 0 1  0 0  1 0 0 1 255 255 255 255\n"
      "v  0.5 -0.5 0.5  0 0 1  1 0  1 0 0 1 255 255 255 255\n"
      "v  0.5  0.5 0.5  0 0 1  1 1  1 0 0 1 255 255 255 255\n"
      "v -0.5  0.5 0.5  0 0 1  0 1  1 0 0 1 255 255 255 255\n"
      /* -Z */
      "v  0.5 -0.5 -0.5  0 0 -1  0 0  1 0 0 1 255 255 255 255\n"
      "v -0.5 -0.5 -0.5  0 0 -1  1 0  1 0 0 1 255 255 255 255\n"
      "v -0.5  0.5 -0.5  0 0 -1  1 1  1 0 0 1 255 255 255 255\n"
      "v  0.5  0.5 -0.5  0 0 -1  0 1  1 0 0 1 255 255 255 255\n"
      "tris 12\n"
      "f 0 1 2\nf 0 2 3\nf 4 5 6\nf 4 6 7\nf 8 9 10\nf 8 10 11\n"
      "f 12 13 14\nf 12 14 15\nf 16 17 18\nf 16 18 19\nf 20 21 22\nf 20 22 23\n"
      "material red\n  base_color 0.85 0.25 0.2 1\n  roughness 0.5\nend\n"
      "material blue\n  base_color 0.25 0.4 0.85 1\n  roughness 0.3\n  metallic 0.6\nend\n");
    fclose(f);
}

static void write_pyramid(const char* path){
    FILE* f=fopen(path,"w");
    fprintf(f,
      "ccmodel 1\nname pyramid\nverts 5\n"
      "v  0   1  0    0 1 0    0.5 1  1 0 0 1 240 220 120 255\n"
      "v -0.7 0 -0.7  0 0 -1   0 0    1 0 0 1 240 220 120 255\n"
      "v  0.7 0 -0.7  0 0 -1   1 0    1 0 0 1 240 220 120 255\n"
      "v  0.7 0  0.7  0 0 1    1 1    1 0 0 1 240 220 120 255\n"
      "v -0.7 0  0.7  0 0 1    0 1    1 0 0 1 240 220 120 255\n"
      "tris 6\n"
      "f 0 2 1\nf 0 3 2\nf 0 4 3\nf 0 1 4\nf 1 2 3\nf 1 3 4\n"
      "material gold\n  base_color 0.9 0.75 0.3 1\n  roughness 0.35\n  metallic 0.8\nend\n");
    fclose(f);
}

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/ccscene_test.png";
    write_cube("/tmp/cube.ccmodel");
    write_pyramid("/tmp/pyramid.ccmodel");
    /* the scene manifest — layout lives entirely in this text file */
    FILE* sc=fopen("/tmp/scene.cclist","w");
    fprintf(sc,
      "cclist 1\n"
      "name demo\n"
      "# a row of cubes (blue slot) and pyramids between them\n"
      "model cube.ccmodel\n"
      "  at -3 0.5 0\n  slot 1\n  rot_y 20\n"
      "instance -1 0.5 0   1 1 1   40\n"
      "instance  1 0.5 0   1 1 1   0\n"
      "instance  3 0.5 0   1.2 1.2 1.2   60\n"
      "model pyramid.ccmodel\n"
      "  at -2 0 -1.5\n"
      "instance  0 0 -1.5   1.3 1.6 1.3   30\n"
      "instance  2 0 -1.5   1 1.2 1   0\n"
      "# one red cube (slot 0) up front\n"
      "model cube.ccmodel\n  at 0 0.5 2\n  slot 0\n  scale 1.4 1.4 1.4\n  rot_y 35\n");
    fclose(sc);

    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1100;cfg.height=520;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    CCSceneAsset* scene=cc_sceneasset_load(e,"/tmp/scene.cclist");
    if(!scene){ fprintf(stderr,"scene load FAILED\n"); return 2; }
    printf("scene '%s': %u instances, %u meshes, %u materials\n",
        scene->name, scene->instance_count, scene->mesh_count, scene->material_count);

    CCMaterialDesc gd={.base_color={0.28f,0.3f,0.33f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);
    CCMesh ground=cc_mesh_plane(e,40,40,4);

    cc_light_set_ambient(e,0.14f,0.15f,0.18f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.45f,-0.7f,-0.5f},.color={1,0.96f,0.9f},.intensity=2.9f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.3f,0.45f,0.7f},h[3]={0.6f,0.64f,0.7f},g[3]={0.2f,0.2f,0.2f};
    cc_light_set_sky_colors(e,z,h,g,1.0f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.2f;
    fx.taa=true; fx.taa_blend=0.85f; fx.ssgi=true; fx.ssgi_intensity=1.3f; fx.ssgi_radius=2.2f;
    fx.bloom=true; fx.bloom_threshold=1.4f; fx.bloom_intensity=0.05f; fx.vignette=true; fx.vignette_strength=0.24f;
    cc_postfx_set(e,&fx);

    for(int f=0;f<8;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0.5f,4.0f,8.0f},.target={0,0.5f,-0.5f},.up={0,1,0},.fov_deg=52,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        cc_sceneasset_draw(e,scene);   /* the whole manifest, one call */
        cc_frame_end(e);
    }
    const char* s=cc_screenshot(e,out);
    printf("screenshot: %s (scene loaded from /tmp/scene.cclist)\n", s?s:"(null)");
    cc_sceneasset_free(e,scene,false);
    cc_shutdown(e); return 0;
}
