/* Instancing test — a grid of cubes rendered in a single instanced draw call.
 * Verifies: correct placement/lighting, and that stats report 1 draw call for
 * the whole grid. Second arg "single" re-renders the same grid with individual
 * cc_draw_mesh calls so the two can be compared for visual parity. */
#include "cc/claudecore.h"
#include "cc/debug.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "/tmp/instancing_test.png";
    int use_single  = (argc > 2 && strcmp(argv[2],"single")==0);

    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 1024; cfg.height = 576; cfg.verbose = false;
    CCEngine* eng = cc_init(&cfg);
    if (!eng) { fprintf(stderr,"init failed\n"); return 1; }

    CCMesh ground = cc_mesh_plane(eng, 60.0f, 60.0f, 4);
    CCMesh cube   = cc_mesh_cube(eng, 0.7f);

    CCMaterialDesc gd = { .base_color={0.15f,0.16f,0.18f,1}, .roughness=0.9f };
    CCMaterial mat_ground = cc_material_create(eng, &gd);
    CCMaterialDesc cd = { .base_color={0.8f,0.35f,0.2f,1}, .roughness=0.4f, .metallic=0.1f };
    CCMaterial mat_cube = cc_material_create(eng, &cd);

    cc_light_set_ambient(eng, 0.10f,0.11f,0.14f, 1.0f);
    CCLight sun = { .type=CC_LIGHT_DIRECTIONAL, .dir={-0.5f,-1.0f,-0.35f},
                    .color={1.0f,0.95f,0.85f}, .intensity=3.0f };
    cc_light_add(eng, &sun);

    CCPostFX fx = cc_postfx_default();
    fx.bloom=false; fx.vignette=true; fx.vignette_strength=0.3f;
    cc_postfx_set(eng, &fx);

    /* Build a grid of instances with a little height wave. */
    const int N = 20;             /* 20x20 = 400 cubes */
    const int total = N*N;
    CCTransform3D* xf = (CCTransform3D*)malloc(sizeof(CCTransform3D)*total);
    int idx=0;
    for (int z=0; z<N; z++) for (int x=0; x<N; x++) {
        float wx = (x - N/2) * 1.4f;
        float wz = (z - N/2) * 1.4f;
        float h  = 0.5f + 0.4f*sinf(x*0.6f)*cosf(z*0.6f);
        xf[idx].pos[0]=wx; xf[idx].pos[1]=h; xf[idx].pos[2]=wz;
        xf[idx].rot[0]=0; xf[idx].rot[1]=0; xf[idx].rot[2]=0; xf[idx].rot[3]=1;
        xf[idx].scale[0]=xf[idx].scale[1]=xf[idx].scale[2]=1.0f;
        idx++;
    }

    for (int frame=0; frame<3; frame++) {
        cc_frame_begin(eng);
        CCCameraDesc cam = { .pos={0,16,22}, .target={0,0,0}, .up={0,1,0},
                             .fov_deg=55.0f, .near_plane=0.1f, .far_plane=300.0f, .exposure=1.0f };
        cc_camera_set(eng, &cam);

        CCTransform3D gxf = {.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng, ground, mat_ground, &gxf);

        if (use_single) {
            for (int i=0;i<total;i++) cc_draw_mesh(eng, cube, mat_cube, &xf[i]);
        } else {
            cc_draw_mesh_instanced(eng, cube, mat_cube, xf, total);
        }
        cc_frame_end(eng);
    }

    CCFrameStats st = cc_debug_stats(eng);
    printf("mode=%s cubes=%d draw_calls=%u meshes_drawn=%u triangles=%u\n",
           use_single?"single":"instanced", total,
           st.draw_calls, st.meshes_drawn, st.triangles);

    const char* saved = cc_screenshot(eng, out);
    printf("screenshot: %s\n", saved?saved:"(null)");
    free(xf);
    cc_shutdown(eng);
    return 0;
}
