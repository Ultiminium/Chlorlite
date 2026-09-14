/* Sky + IBL test — a row of PBR spheres (varying roughness/metalness) plus a
 * ground plane, lit ONLY by the procedural sky (no explicit lights). Verifies:
 * the sky renders in the background, and surfaces pick up hemispheric diffuse +
 * sky-reflection specular from it. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "/tmp/sky_test.png";

    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 1024; cfg.height = 576; cfg.verbose = false;
    CCEngine* eng = cc_init(&cfg);
    if (!eng) { fprintf(stderr,"init failed\n"); return 1; }

    CCMesh ground = cc_mesh_plane(eng, 40.0f, 40.0f, 4);
    CCMesh sphere = cc_mesh_sphere(eng, 1.0f, 48, 48);

    CCMaterialDesc gd = { .base_color={0.35f,0.33f,0.30f,1}, .roughness=0.8f };
    CCMaterial mat_ground = cc_material_create(eng, &gd);

    /* Almost no constant ambient — the sky should do the lighting. */
    cc_light_set_ambient(eng, 0.02f,0.02f,0.02f, 1.0f);

    /* Enable + style the sky: warm sunset gradient. */
    float zenith[3]  = {0.08f, 0.14f, 0.40f};
    float horizon[3] = {0.95f, 0.55f, 0.30f};
    float ground_c[3]= {0.18f, 0.12f, 0.10f};
    cc_light_set_sky_colors(eng, zenith, horizon, ground_c, 1.0f);

    CCPostFX fx = cc_postfx_default();
    fx.bloom=true; fx.bloom_threshold=1.2f; fx.bloom_intensity=0.12f;
    fx.vignette=true; fx.vignette_strength=0.25f;
    cc_postfx_set(eng, &fx);

    for (int frame=0; frame<3; frame++) {
        cc_frame_begin(eng);
        CCCameraDesc cam = { .pos={0,2.2f,9.0f}, .target={0,1.0f,0}, .up={0,1,0},
                             .fov_deg=55.0f, .near_plane=0.1f, .far_plane=200.0f, .exposure=1.0f };
        cc_camera_set(eng, &cam);

        CCTransform3D gxf = {.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng, ground, mat_ground, &gxf);

        /* 5 spheres: left=rough dielectric → right=smooth metal. */
        for (int i=0;i<5;i++) {
            float t = i/4.0f;
            CCMaterialDesc sd = {
                .base_color={0.9f,0.9f,0.92f,1},
                .roughness = 0.9f - 0.85f*t,
                .metallic  = t,
            };
            CCMaterial m = cc_material_create(eng, &sd);
            CCTransform3D xf = {.pos={-6.0f+i*3.0f,1.0f,0},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(eng, sphere, m, &xf);
        }
        cc_frame_end(eng);
    }

    const char* saved = cc_screenshot(eng, out);
    printf("screenshot: %s\n", saved?saved:"(null)");
    cc_shutdown(eng);
    return 0;
}
