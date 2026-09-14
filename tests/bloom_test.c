/* Bloom quality test — bright emissive shapes on a dark ground.
 * Manual-loop, headless: steps N frames, screenshots. No C++ lambdas. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "/tmp/bloom_test.png";
    float threshold = (argc > 2) ? (float)atof(argv[2]) : 1.0f;
    float intensity = (argc > 3) ? (float)atof(argv[3]) : 0.06f;

    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 1024; cfg.height = 576; cfg.verbose = false;
    CCEngine* eng = cc_init(&cfg);
    if (!eng) { fprintf(stderr, "init failed\n"); return 1; }

    CCScene* scene = cc_scene_active(eng);
    (void)scene;

    CCMesh ground = cc_mesh_plane(eng, 40.0f, 40.0f, 4);
    CCMesh sphere = cc_mesh_sphere(eng, 1.0f, 32, 32);
    CCMesh cube   = cc_mesh_cube(eng, 1.2f);

    CCMaterialDesc dark = { .base_color={0.03f,0.03f,0.04f,1.0f}, .roughness=0.9f, .metallic=0.0f };
    CCMaterial mat_ground = cc_material_create(eng, &dark);

    /* Emissive materials at varying brightness so the threshold has a gradient to bite. */
    CCMaterialDesc em_dim  = { .base_color={1,1,1,1}, .emissive={0.8f,0.2f,0.1f}, .roughness=0.5f };
    CCMaterialDesc em_mid  = { .base_color={1,1,1,1}, .emissive={2.5f,1.6f,0.3f}, .roughness=0.5f };
    CCMaterialDesc em_hot  = { .base_color={1,1,1,1}, .emissive={8.0f,6.0f,4.0f}, .roughness=0.5f };
    CCMaterialDesc em_blue = { .base_color={1,1,1,1}, .emissive={0.4f,1.2f,5.0f}, .roughness=0.5f };
    CCMaterial m_dim  = cc_material_create(eng, &em_dim);
    CCMaterial m_mid  = cc_material_create(eng, &em_mid);
    CCMaterial m_hot  = cc_material_create(eng, &em_hot);
    CCMaterial m_blue = cc_material_create(eng, &em_blue);

    cc_light_set_ambient(eng, 0.02f, 0.02f, 0.03f, 1.0f);
    CCLight sun = { .type=CC_LIGHT_DIRECTIONAL, .dir={-0.4f,-1.0f,-0.3f},
                    .color={0.4f,0.45f,0.6f}, .intensity=1.0f };
    cc_light_add(eng, &sun);

    CCPostFX fx = cc_postfx_default();
    fx.bloom = true; fx.bloom_threshold = threshold; fx.bloom_intensity = intensity;
    fx.tonemap_aces = true; fx.gamma = 2.2f; fx.fxaa = true;
    fx.vignette = true; fx.vignette_strength = 0.3f;
    cc_postfx_set(eng, &fx);

    /* Step a few frames so buffers settle. */
    for (int frame = 0; frame < 3; frame++) {
        cc_frame_begin(eng);

        CCCameraDesc cam = { .pos={0,4.5f,11.0f}, .target={0,1.2f,0}, .up={0,1,0},
                             .fov_deg=55.0f, .near_plane=0.1f, .far_plane=200.0f, .exposure=1.0f };
        cc_camera_set(eng, &cam);

        CCTransform3D gxf = {.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng, ground, mat_ground, &gxf);

        CCMaterial mats[4] = { m_dim, m_mid, m_hot, m_blue };
        for (int i = 0; i < 4; i++) {
            float x = -6.0f + i * 4.0f;
            CCTransform3D xf = {.pos={x,1.2f,0},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(eng, sphere, mats[i], &xf);
        }
        /* a couple of small hot cubes to test firefly/aliasing behaviour */
        CCTransform3D cxf1 = {.pos={-2,0.4f,4},.rot={0.2f,0.3f,0,0.93f},.scale={0.3f,0.3f,0.3f}};
        cc_draw_mesh(eng, cube, m_hot, &cxf1);
        CCTransform3D cxf2 = {.pos={2.5f,0.4f,4},.rot={0.1f,0.5f,0,0.85f},.scale={0.25f,0.25f,0.25f}};
        cc_draw_mesh(eng, cube, m_blue, &cxf2);

        cc_frame_end(eng);
    }

    const char* saved = cc_screenshot(eng, out);
    printf("screenshot: %s\n", saved ? saved : "(null)");
    cc_shutdown(eng);
    return 0;
}
