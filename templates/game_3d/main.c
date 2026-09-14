/*
 * Chlorlite — 3D Game Template
 * Demonstrates: 3D mesh rendering, PBR materials, lighting, camera, ECS
 *
 * Build:  bash SKILL_DIR/scripts/build_game.sh game_3d/
 * Run:    bash SKILL_DIR/scripts/run_headless.sh game_3d --ticks 180 --screenshot 10
 */

#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>

typedef struct {
    CCEngine*   eng;
    CCScene*    scene;

    /* Scene objects */
    CCMesh      mesh_ground;
    CCMesh      mesh_cube;
    CCMesh      mesh_sphere;
    CCMaterial  mat_ground;
    CCMaterial  mat_metal;
    CCMaterial  mat_plastic;

    /* Lights */
    CCLightId   sun;
    CCLightId   point_r;
    CCLightId   point_b;

    /* Camera orbit state */
    float cam_yaw;
    float cam_pitch;
    float cam_dist;
    float cam_target[3];

    float time;
} G3D;

static G3D g;

static float dot3(float* a, float* b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static void norm3(float* v) {
    float l=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(l>1e-5f){v[0]/=l;v[1]/=l;v[2]/=l;}
}

/* Procedural ground texture: checkerboard tiles with a little noise.
   A named C function — cc_texture_proc takes a plain function pointer, so this
   must not be an inline lambda (this is a C engine, compiled as gnu17). */
static void gen_ground_tex(uint8_t* px, uint32_t w, uint32_t h, void* ud) {
    (void)ud;
    for (uint32_t y=0;y<h;y++) for (uint32_t x=0;x<w;x++) {
        bool tx = ((x/32)+(y/32))%2;
        int base = tx ? 80 : 60;
        int n = (int)((x*3+y*7+x*y)%30) - 15;   /* cheap noise detail */
        base += n;
        if (base < 0) base = 0; if (base > 255) base = 255;
        px[(y*w+x)*4+0]=(uint8_t)base;
        px[(y*w+x)*4+1]=(uint8_t)(base*0.9f);
        px[(y*w+x)*4+2]=(uint8_t)(base*0.8f);
        px[(y*w+x)*4+3]=255;
    }
}

void cc_game_init(CCEngine* eng) {
    g.eng   = eng;
    g.scene = cc_scene_active(eng);
    g.cam_yaw   = 45.0f;
    g.cam_pitch = 25.0f;
    g.cam_dist  = 12.0f;
    g.cam_target[1] = 1.0f;

    /* Ground plane */
    g.mesh_ground = cc_mesh_plane(eng, 20.0f, 20.0f, 8);

    /* Procedural noise texture for ground */
    CCTextureDesc td = {.width=512,.height=512,.format=CC_FMT_RGBA8,.mipmaps=true,.linear_filter=true,.wrap_repeat=true};
    CCTexture ground_tex = cc_texture_proc(eng, &td, gen_ground_tex, NULL);

    CCMaterialDesc ground_mat = {
        .base_color={1,1,1,1}, .albedo_map=ground_tex,
        .roughness=0.85f, .metallic=0.0f
    };
    g.mat_ground = cc_material_create(eng, &ground_mat);

    /* Metal material (no texture needed) */
    CCMaterialDesc metal_mat = {
        .base_color={0.7f,0.72f,0.75f,1.0f},
        .roughness=0.15f, .metallic=0.95f
    };
    g.mat_metal = cc_material_create(eng, &metal_mat);

    /* Plastic material */
    CCMaterialDesc plastic_mat = {
        .base_color={0.05f,0.4f,0.9f,1.0f},
        .roughness=0.35f, .metallic=0.0f
    };
    g.mat_plastic = cc_material_create(eng, &plastic_mat);

    /* Meshes */
    g.mesh_cube   = cc_mesh_cube(eng, 1.5f);
    g.mesh_sphere = cc_mesh_sphere(eng, 1.0f, 32, 32);

    /* Lighting */
    CCLight sun = {
        .type=CC_LIGHT_DIRECTIONAL,
        .dir={-0.6f,-1.0f,-0.4f},
        .color={1.0f,0.92f,0.80f},
        .intensity=4.0f, .cast_shadows=true, .shadow_map_size=2048
    };
    g.sun = cc_light_add(eng, &sun);

    CCLight pt_r = {
        .type=CC_LIGHT_POINT,
        .pos={4,2,0}, .color={1,0.1f,0.05f},
        .intensity=8.0f, .range=10.0f
    };
    g.point_r = cc_light_add(eng, &pt_r);

    CCLight pt_b = {
        .type=CC_LIGHT_POINT,
        .pos={-4,2,0}, .color={0.05f,0.3f,1.0f},
        .intensity=8.0f, .range=10.0f
    };
    g.point_b = cc_light_add(eng, &pt_b);

    cc_light_set_ambient(eng, 0.08f, 0.10f, 0.14f, 1.0f);

    /* Post FX */
    CCPostFX fx = cc_postfx_default();
    fx.bloom=true; fx.bloom_intensity=0.15f; fx.bloom_threshold=1.0f;
    fx.fxaa=true; fx.vignette=true; fx.vignette_strength=0.25f;
    fx.tonemap_aces=true; fx.gamma=2.2f;
    cc_postfx_set(eng, &fx);

    CC_INFO("3D scene initialized");
}

void cc_game_tick(CCEngine* eng, double dt) {
    g.time += (float)dt;

    /* Orbit camera with arrow keys */
    float speed = 60.0f;
    if (cc_key_down(eng, QKEY_LEFT))  g.cam_yaw   -= speed*(float)dt;
    if (cc_key_down(eng, QKEY_RIGHT)) g.cam_yaw   += speed*(float)dt;
    if (cc_key_down(eng, QKEY_UP))    g.cam_pitch  = fminf(g.cam_pitch+speed*(float)dt, 85.0f);
    if (cc_key_down(eng, QKEY_DOWN))  g.cam_pitch  = fmaxf(g.cam_pitch-speed*(float)dt, 5.0f);
    if (cc_key_pressed(eng, QKEY_ESCAPE)) cc_quit(eng);

    /* Camera position */
    float yaw_r  = g.cam_yaw   * (3.14159f/180.0f);
    float pitch_r = g.cam_pitch * (3.14159f/180.0f);
    float cx = g.cam_target[0] + g.cam_dist * cosf(pitch_r) * sinf(yaw_r);
    float cy = g.cam_target[1] + g.cam_dist * sinf(pitch_r);
    float cz = g.cam_target[2] + g.cam_dist * cosf(pitch_r) * cosf(yaw_r);

    CCCameraDesc cam = {
        .pos={cx,cy,cz},
        .target={g.cam_target[0],g.cam_target[1],g.cam_target[2]},
        .up={0,1,0},
        .fov_deg=60.0f, .near_plane=0.1f, .far_plane=500.0f,
        .exposure=1.0f
    };
    cc_camera_set(eng, &cam);

    /* Animate point lights */
    float lr = 5.0f;
    CCLight pt_r = {.type=CC_LIGHT_POINT,
        .pos={lr*sinf(g.time*0.7f),2.0f,lr*cosf(g.time*0.7f)},
        .color={1,0.1f,0.05f},.intensity=8.0f,.range=10.0f};
    cc_light_update(eng, g.point_r, &pt_r);
    CCLight pt_b = {.type=CC_LIGHT_POINT,
        .pos={lr*sinf(g.time*0.7f+3.14f),2.0f,lr*cosf(g.time*0.7f+3.14f)},
        .color={0.05f,0.3f,1.0f},.intensity=8.0f,.range=10.0f};
    cc_light_update(eng, g.point_b, &pt_b);

    /* Draw scene */
    /* Ground */
    CCTransform3D ground_xf = {.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
    cc_draw_mesh(eng, g.mesh_ground, g.mat_ground, &ground_xf);

    /* Metal cube — spinning */
    CCTransform3D cube_xf = {
        .pos={0,0.75f,0},
        .rot={sinf(g.time*0.5f)*0.707f, cosf(g.time*0.5f)*0.707f, 0, 0.707f},
        .scale={1,1,1}
    };
    cc_draw_mesh(eng, g.mesh_cube, g.mat_metal, &cube_xf);

    /* Plastic spheres */
    for (int i=0;i<5;i++) {
        float angle = g.time*0.4f + i*(2*3.14159f/5.0f);
        float r=4.5f;
        CCTransform3D s_xf = {
            .pos={r*sinf(angle), 1.0f + 0.3f*sinf(g.time*2+i), r*cosf(angle)},
            .rot={0,0,0,1},.scale={0.6f,0.6f,0.6f}
        };
        cc_draw_mesh(eng, g.mesh_sphere, g.mat_plastic, &s_xf);
    }
}

void cc_game_shutdown(CCEngine* eng) {
    (void)eng;
    cc_material_destroy(eng, g.mat_ground);
    cc_material_destroy(eng, g.mat_metal);
    cc_material_destroy(eng, g.mat_plastic);
}
