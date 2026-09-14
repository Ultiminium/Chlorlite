/* actor_test — the Actor/Entity abstraction ("kill the parallel arrays").
 *
 * Instead of lockstep arrays of transforms/meshes/materials, each object is a
 * CCActor handle. We spawn a grid of actors, manipulate them individually
 * through the handle API (per-actor rotation, scale, material), hide one via
 * cc_actor_set_visible, look one up by name with cc_actor_find and recolor it,
 * then render the whole ECS world with cc_scene_render — actors ARE entities,
 * so they draw for free. No Prop[] array anywhere. */
#include "cc/claudecore.h"
#include "cc/ecs.h"
#include "cc/actor.h"
#include <stdio.h>
#include <math.h>

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/actor_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=560;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    CCScene* world=cc_scene_create(e,"actors");   /* the ECS world (ecs.h) */

    CCMesh cube=cc_mesh_cube(e,1.0f);
    CCMesh sph =cc_mesh_sphere(e,0.6f,24,18);
    CCMaterialDesc md={.base_color={0.7f,0.72f,0.75f,1},.roughness=0.4f,.metallic=0.1f,.tint={1,1,1,1}};
    CCMaterial base=cc_material_create(e,&md);
    CCMaterialDesc rd={.base_color={0.85f,0.3f,0.25f,1},.roughness=0.35f,.tint={1,1,1,1}};
    CCMaterial red=cc_material_create(e,&rd);
    CCMaterialDesc bd={.base_color={0.25f,0.45f,0.85f,1},.roughness=0.3f,.metallic=0.5f,.tint={1,1,1,1}};
    CCMaterial blue=cc_material_create(e,&bd);
    CCMaterialDesc gd={.base_color={0.3f,0.32f,0.35f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);
    CCMesh ground=cc_mesh_plane(e,60,60,4);

    /* spawn a 5×4 grid of actors — each is an independent handle, no arrays */
    int gi=0;
    for(int row=0; row<4; row++){
        for(int col=0; col<5; col++){
            float x=(col-2)*1.8f, z=(row-1.5f)*1.8f;
            CCMesh mesh = ((row+col)&1) ? cube : sph;
            CCActor a = cc_actor_spawn(world, mesh, base, x, 0.5f, z);
            /* manipulate each actor individually through its handle */
            cc_actor_set_rotation_y(a, (float)(gi*24));
            float s = 0.7f + 0.12f*((gi*7)%5);
            cc_actor_set_uniform_scale(a, s);
            if ((gi%3)==0) cc_actor_set_material(a, red);
            else if ((gi%3)==1) cc_actor_set_material(a, blue);
            /* name one specific actor so we can find it later */
            if (row==1 && col==2) cc_actor_set_name(a, "hero");
            /* hide one to prove visibility toggling */
            if (row==0 && col==0) cc_actor_set_visible(a, false);
            gi++;
        }
    }

    /* look the hero up by name and make it big + gold — no index bookkeeping */
    CCActor hero = cc_actor_find(world, "hero");
    if (cc_actor_valid(hero)){
        CCMaterialDesc goldd={.base_color={0.95f,0.78f,0.32f,1},.roughness=0.28f,.metallic=0.9f,.tint={1,1,1,1}};
        cc_actor_set_material(hero, cc_material_create(e,&goldd));
        cc_actor_set_uniform_scale(hero, 1.5f);
        cc_actor_set_position(hero, 0, 1.0f, 0);
        printf("found '%s', made it the gold hero\n", cc_actor_name(hero));
    } else printf("WARN: hero not found\n");

    cc_light_set_ambient(e,0.14f,0.15f,0.18f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.7f,-0.5f},.color={1,0.96f,0.9f},.intensity=2.9f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.3f,0.45f,0.7f},h[3]={0.6f,0.64f,0.7f},g[3]={0.2f,0.2f,0.2f};
    cc_light_set_sky_colors(e,z,h,g,1.0f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.2f;
    fx.taa=true; fx.taa_blend=0.85f; fx.ssgi=true; fx.ssgi_intensity=1.3f; fx.ssgi_radius=2.2f;
    fx.bloom=true; fx.bloom_threshold=1.4f; fx.bloom_intensity=0.05f; fx.vignette=true; fx.vignette_strength=0.24f;
    cc_postfx_set(e,&fx);

    int drawn=0;
    for(int f=0;f<8;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,5.5f,9.5f},.target={0,0.4f,0},.up={0,1,0},.fov_deg=52,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        drawn = cc_scene_render(e, world);   /* draws every actor via the ECS */
        cc_frame_end(e);
    }
    const char* s=cc_screenshot(e,out);
    printf("actors drawn by cc_scene_render: %d (1 hidden, so 19 of 20)\n", drawn);
    printf("screenshot: %s\n", s?s:"(null)");
    cc_scene_destroy(world);
    cc_shutdown(e); return 0;
}
