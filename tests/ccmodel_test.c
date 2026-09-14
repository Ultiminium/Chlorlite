/* ccmodel_test — the asset-system milestone: the engine LOADS geometry from a
 * text .ccmodel file on disk and renders it.
 *
 * Three spheres:
 *   LEFT   — built in memory with ccm_make_sphere, drawn directly (baseline).
 *   MIDDLE — that same model saved to /tmp/rt.ccmodel (text) then loaded back
 *            with ccm_load_text and drawn: proves save→load round-trips.
 *   RIGHT  — a HAND-AUTHORED text file written here as plain text (a small
 *            octahedron), loaded with cc_mesh_load_ccmodel: proves the engine
 *            reads geometry authored outside the engine.
 * Also asserts the round-tripped vertex/index counts match the original. */
#include "cc/claudecore.h"
#include "cc/ccmodel.h"
#include <stdio.h>
#include <string.h>

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/ccmodel_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=460;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    /* (LEFT) build a model in memory */
    CCModel* orig=ccm_make_sphere("ball",1.0f,32,24);
    CCMesh meshL=cc_mesh_from_model(e,orig);

    /* (MIDDLE) save to text, load back, render */
    if(!ccm_save_text(orig,"/tmp/rt.ccmodel")){ fprintf(stderr,"save_text failed\n"); return 2; }
    CCModel* rt=ccm_load_text("/tmp/rt.ccmodel");
    if(!rt){ fprintf(stderr,"load_text failed\n"); return 3; }
    /* integrity check */
    if(rt->geom.vertex_count!=orig->geom.vertex_count || rt->geom.index_count!=orig->geom.index_count){
        fprintf(stderr,"ROUND-TRIP MISMATCH: v %u->%u  i %u->%u\n",
            orig->geom.vertex_count,rt->geom.vertex_count,
            orig->geom.index_count,rt->geom.index_count);
        return 4;
    }
    printf("round-trip OK: verts=%u tris=%u\n", rt->geom.vertex_count, rt->geom.index_count/3);
    CCMesh meshM=cc_mesh_from_model(e,rt);

    /* (RIGHT) hand-author a text .ccmodel (an octahedron) and load it */
    FILE* fa=fopen("/tmp/hand.ccmodel","w");
    fprintf(fa,
        "ccmodel 1\n"
        "name octahedron   # authored by hand\n"
        "verts 6\n"
        "v  0  1  0   0  1  0   0.5 1  1 0 0 1  230 120 90 255\n"
        "v  1  0  0   1  0  0   1   0.5 0 0 1 1  120 200 120 255\n"
        "v  0  0  1   0  0  1   0.5 0.5 1 0 0 1  120 150 230 255\n"
        "v -1  0  0  -1  0  0   0   0.5 0 0 1 1  230 210 120 255\n"
        "v  0  0 -1   0  0 -1   0.5 0   1 0 0 1  200 120 200 255\n"
        "v  0 -1  0   0 -1  0   0.5 0   1 0 0 1  120 200 200 255\n"
        "tris 8\n"
        "f 0 1 2\nf 0 2 3\nf 0 3 4\nf 0 4 1\n"
        "f 5 2 1\nf 5 3 2\nf 5 4 3\nf 5 1 4\n"
        "material shell\n"
        "  base_color 0.9 0.8 0.7 1\n"
        "  roughness 0.4\n"
        "end\n");
    fclose(fa);
    CCMesh meshR=cc_mesh_load_ccmodel(e,"/tmp/hand.ccmodel");

    /* materials */
    CCMaterialDesc md={.base_color={0.8f,0.82f,0.85f,1},.roughness=0.35f,.metallic=0.1f,.tint={1,1,1,1}};
    CCMaterial mat=cc_material_create(e,&md);
    CCMaterialDesc gd={.base_color={0.3f,0.32f,0.35f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);
    CCMesh ground=cc_mesh_plane(e,40,40,4);

    cc_light_set_ambient(e,0.14f,0.15f,0.18f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.7f,-0.5f},.color={1,0.97f,0.92f},.intensity=2.8f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.3f,0.45f,0.7f},h[3]={0.55f,0.6f,0.68f},g[3]={0.2f,0.2f,0.2f};
    cc_light_set_sky_colors(e,z,h,g,1.0f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.2f;
    fx.taa=true; fx.taa_blend=0.85f; fx.bloom=true; fx.bloom_threshold=1.4f; fx.bloom_intensity=0.05f;
    cc_postfx_set(e,&fx);

    float xs[3]={-3.0f,0.0f,3.0f}; CCMesh meshes[3]={meshL,meshM,meshR};
    for(int f=0;f<8;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,1.6f,7.5f},.target={0,0.2f,0},.up={0,1,0},.fov_deg=52,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,-1.3f,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        for(int i=0;i<3;i++){
            CCTransform3D t={.pos={xs[i],0,0},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(e,meshes[i],mat,&t);
        }
        cc_frame_end(e);
    }
    const char* s=cc_screenshot(e,out);
    printf("screenshot: %s (L=in-memory, M=text round-trip, R=hand-authored file)\n", s?s:"(null)");
    ccm_model_free(orig); ccm_model_free(rt);
    cc_shutdown(e); return 0;
}
