/* interact_test — the interactable system: a door, a switch, and a pickup as
 * actors, registered as interactables. From a "player" position we query the
 * nearest target, trigger each, and confirm: the door swings open (rotates), the
 * switch toggles on, the pickup fires its callback + hides. Renders the scene
 * AFTER triggering so the open door + collected pickup are visible. */
#include "cc/claudecore.h"
#include "cc/interact.h"
#include <stdio.h>
#include <string.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)

static int g_item_granted=0;
static void on_pickup(CCInteractable* it, bool on, void* ud){
    (void)it;(void)ud; if(on) g_item_granted++;
}
static int g_switch_fires=0;
static void on_switch(CCInteractable* it, bool on, void* ud){
    (void)it;(void)on;(void)ud; g_switch_fires++;
}

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/interact_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=560;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    CCScene* world=cc_scene_create(e,"level");

    CCMesh door=cc_mesh_cube(e,1.0f);
    CCMesh sw=cc_mesh_cube(e,0.4f);
    CCMesh gem=cc_mesh_sphere(e,0.35f,20,16);
    CCMesh ground=cc_mesh_plane(e,40,40,4);
    CCMaterialDesc wd={.base_color={0.55f,0.4f,0.28f,1},.roughness=0.7f,.tint={1,1,1,1}};
    CCMaterial wood=cc_material_create(e,&wd);
    CCMaterialDesc sd={.base_color={0.7f,0.2f,0.2f,1},.roughness=0.5f,.metallic=0.3f,.tint={1,1,1,1}};
    CCMaterial red=cc_material_create(e,&sd);
    CCMaterialDesc gemd={.base_color={0.3f,0.8f,0.9f,1},.roughness=0.15f,.metallic=0.7f,.tint={1,1,1,1}};
    CCMaterial gemmat=cc_material_create(e,&gemd);
    CCMaterialDesc gd={.base_color={0.3f,0.32f,0.35f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);

    /* door: a tall thin slab, pivot at one edge (offset the mesh so it swings
       like a hinged door) — we approximate with a scaled cube at x=0 */
    CCActor a_door=cc_actor_spawn(world, door, wood, -2.0f, 1.0f, 0);
    cc_actor_set_scale(a_door, 0.15f, 2.0f, 1.6f);
    CCActor a_switch=cc_actor_spawn(world, sw, red, 0.5f, 1.0f, 0);
    CCActor a_gem=cc_actor_spawn(world, gem, gemmat, 2.6f, 0.6f, 0);

    /* register interactables */
    CCInteractable* i_door=cc_interactable_register(world, a_door, CC_INTERACT_DOOR, 2.5f);
    CCInteractable* i_switch=cc_interactable_register(world, a_switch, CC_INTERACT_SWITCH, 2.0f);
    cc_interactable_set_callback(i_switch, on_switch, NULL);
    CCInteractable* i_gem=cc_interactable_register(world, a_gem, CC_INTERACT_PICKUP, 2.0f);
    cc_interactable_set_callback(i_gem, on_pickup, NULL);

    /* prompts */
    CHECK(!strcmp(cc_interactable_prompt(i_door),"Open"),"default door prompt");
    cc_interactable_set_prompt(i_door,"Open Door");

    /* query near the door (player at x=-3.5) facing +x */
    CCInteractable* near_door=cc_interactable_query(world, -3.4f,1.0f,0, 1.0f,0.0f);
    CHECK(near_door==i_door,"query finds the door");

    /* trigger each */
    CHECK(cc_interactable_trigger(world,i_door),"door triggers");
    CHECK(cc_interactable_is_on(i_door),"door now open");
    CHECK(cc_interactable_trigger(world,i_switch),"switch triggers");
    CHECK(cc_interactable_is_on(i_switch),"switch now on");
    CHECK(g_switch_fires==1,"switch callback fired");
    CHECK(cc_interactable_trigger(world,i_gem),"pickup triggers");
    CHECK(g_item_granted==1,"pickup granted item");
    CHECK(!cc_actor_visible(a_gem),"pickup hidden after take");
    /* re-trigger pickup should fail (already collected) */
    CHECK(!cc_interactable_trigger(world,i_gem),"pickup can't be taken twice");

    /* locked door test */
    CCActor a_locked=cc_actor_spawn(world, door, wood, -6, 1, 3);
    cc_actor_set_scale(a_locked,0.15f,2.0f,1.6f);
    CCInteractable* i_locked=cc_interactable_register(world,a_locked,CC_INTERACT_DOOR,2.5f);
    cc_interactable_set_locked(i_locked,true);
    CHECK(!cc_interactable_trigger(world,i_locked),"locked door won't open");
    CHECK(!cc_interactable_is_on(i_locked),"locked door stays closed");

    /* advance the door swing animation to completion */
    for(int i=0;i<40;i++) cc_interactable_update(world, 0.016f);

    cc_light_set_ambient(e,0.16f,0.17f,0.2f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.7f,-0.5f},.color={1,0.96f,0.9f},.intensity=2.9f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.3f,0.45f,0.7f},h[3]={0.6f,0.64f,0.7f},g[3]={0.2f,0.2f,0.2f};
    cc_light_set_sky_colors(e,z,h,g,1.0f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.2f; fx.taa=true;
    fx.ssgi=true; fx.ssgi_intensity=1.3f; fx.bloom=true; fx.bloom_threshold=1.4f; fx.bloom_intensity=0.05f;
    cc_postfx_set(e,&fx);

    for(int f=0;f<8;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={-1,3.5f,8},.target={0,0.8f,0},.up={0,1,0},.fov_deg=54,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        cc_scene_render(e, world);   /* draws all actors incl. the swung-open door */
        cc_frame_end(e);
    }
    const char* s=cc_screenshot(e,out);
    printf("interactables: door open=%d, switch on=%d (fires=%d), gem taken=%d\n",
        cc_interactable_is_on(i_door), cc_interactable_is_on(i_switch), g_switch_fires, g_item_granted);
    printf("screenshot: %s\n", s?s:"(null)");

    cc_interactable_clear(world);
    cc_scene_destroy(world);
    if(fails){ printf("INTERACT TEST: %d FAILURE(S)\n",fails); cc_shutdown(e); return 2; }
    printf("INTERACT TEST: all checks passed\n");
    cc_shutdown(e); return 0;
}
