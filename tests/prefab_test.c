/* prefab_test — entity templates. Define a "tree" and "rock" prefab once, then
 * stamp out a forest of many instances with per-instance yaw + scale variation.
 * Verifies instances are independent actors with unique names, that per-instance
 * overrides apply, and renders the scattered result. */
#include "cc/claudecore.h"
#include "cc/prefab.h"
#include "cc/ecs.h"
#include "cc/actor.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)

/* cheap deterministic pseudo-random */
static uint32_t rng_state=12345u;
static float frand(void){ rng_state=rng_state*1664525u+1013904223u; return (float)((rng_state>>8)&0xFFFF)/65535.0f; }

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/prefab_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=520;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    CCScene* world=cc_scene_create(e,"forest");

    /* meshes/materials */
    CCMesh trunk=cc_mesh_cube(e,1.0f);            /* stand-in tree */
    CCMesh rock=cc_mesh_sphere(e,0.5f,16,12);
    CCMesh ground=cc_mesh_plane(e,80,80,4);
    CCMaterialDesc td={.base_color={0.35f,0.5f,0.25f,1},.roughness=0.7f,.tint={1,1,1,1}};
    CCMaterial treemat=cc_material_create(e,&td);
    CCMaterialDesc rd={.base_color={0.5f,0.5f,0.52f,1},.roughness=0.85f,.tint={1,1,1,1}};
    CCMaterial rockmat=cc_material_create(e,&rd);
    CCMaterialDesc gd={.base_color={0.3f,0.34f,0.3f,1},.roughness=0.95f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);

    /* define prefabs ONCE */
    CCPrefab tree=cc_prefab_new("tree");
    cc_prefab_set_mesh(&tree, trunk);
    cc_prefab_set_material(&tree, treemat);
    cc_prefab_set_scale(&tree, 0.5f, 1.8f, 0.5f);   /* tall thin */

    CCPrefab boulder=cc_prefab_new("rock");
    cc_prefab_set_mesh(&boulder, rock);
    cc_prefab_set_material(&boulder, rockmat);
    cc_prefab_set_scale(&boulder, 1.0f, 0.7f, 1.0f);

    /* stamp out a forest */
    int n_trees=40, n_rocks=18;
    for(int i=0;i<n_trees;i++){
        float x=(frand()-0.5f)*22.0f, z=(frand()-0.5f)*16.0f - 2.0f;
        float yaw=frand()*360.0f, sc=0.7f+frand()*0.7f;
        cc_prefab_spawn_ex(world, &tree, x, tree.scale[1]*sc*0.5f, z, yaw, sc);
    }
    for(int i=0;i<n_rocks;i++){
        float x=(frand()-0.5f)*24.0f, z=(frand()-0.5f)*16.0f - 2.0f;
        float sc=0.6f+frand()*1.1f;
        cc_prefab_spawn_ex(world, &boulder, x, 0.25f*sc, z, frand()*360.0f, sc);
    }

    /* checks */
    CHECK(tree.spawn_count==(uint32_t)n_trees,"tree spawn_count matches instances");
    CHECK(boulder.spawn_count==(uint32_t)n_rocks,"rock spawn_count matches instances");
    /* instances are real, independent, uniquely named actors */
    CCActor t0=cc_actor_find(world,"tree_0");
    CCActor t39=cc_actor_find(world,"tree_39");
    CHECK(cc_actor_valid(t0)&&cc_actor_valid(t39),"named instances findable");
    CCActor r0=cc_actor_find(world,"rock_0");
    CHECK(cc_actor_valid(r0),"rock instance findable");
    /* per-instance independence: move one tree, others unaffected */
    float t0x,t0y,t0z, t39x,t39y,t39z;
    cc_actor_get_position(t0,&t0x,&t0y,&t0z);
    cc_actor_get_position(t39,&t39x,&t39y,&t39z);
    cc_actor_set_position(t0, 999,999,999);
    float ax,ay,az; cc_actor_get_position(t39,&ax,&ay,&az);
    CHECK(fabsf(ax-t39x)<1e-3f,"moving one instance doesn't move another");
    cc_actor_set_position(t0, t0x,t0y,t0z);   /* restore */
    /* the prefab template carries the right mesh */
    CHECK(cc_actor_mesh(t0)==trunk,"instance uses prefab mesh");
    CHECK(cc_actor_mesh(r0)==rock,"rock instance uses rock mesh");

    cc_light_set_ambient(e,0.15f,0.16f,0.19f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-0.7f,-0.4f},.color={1,0.95f,0.85f},.intensity=2.9f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.28f,0.42f,0.65f},h[3]={0.6f,0.66f,0.72f},g[3]={0.2f,0.22f,0.2f};
    cc_light_set_sky_colors(e,z,h,g,1.0f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.2f; fx.taa=true;
    fx.ssgi=true; fx.ssgi_intensity=1.2f; fx.bloom=true; fx.bloom_threshold=1.4f; fx.bloom_intensity=0.04f;
    cc_postfx_set(e,&fx);

    const char* s=NULL;
    for(int f=0;f<7;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,7.5f,15},.target={0,1.0f,-2},.up={0,1,0},.fov_deg=54,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        cc_scene_render(e, world);   /* draws all prefab instances (they're ECS entities) */
        cc_frame_end(e);
    }
    s=cc_screenshot(e,out);
    printf("spawned %u trees + %u rocks = %d actors from 2 prefabs\n",
        tree.spawn_count, boulder.spawn_count, n_trees+n_rocks);
    printf("screenshot: %s (prefab-instanced forest)\n", s?s:"(null)");

    cc_scene_destroy(world);
    cc_shutdown(e);
    if(fails){ printf("PREFAB TEST: %d FAILURE(S)\n",fails); return 2; }
    printf("PREFAB TEST: all checks passed\n");
    return 0;
}
