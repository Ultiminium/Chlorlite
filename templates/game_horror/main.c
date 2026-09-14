/*
 * Chlorlite — Horror Vignette Starter Template
 *
 * A minimal horror pacing loop wiring together the event bus, the AI director,
 * the mic (synthetic-fed here), and coroutines:
 *
 *   player makes NOISE  →  mic loudness / CC_EVT_MIC_LEVEL on the bus
 *                       →  DIRECTOR intensity climbs (peaks & valleys)
 *                       →  when the director calls for it, a STALKER spawns
 *                       →  a COROUTINE sequences the stalker rising from the floor
 *
 * Headless (dev) build feeds a scripted noise burst so the scare triggers with
 * no microphone. In a real windowed build you'd feed cc_mic from a device
 * (cc_mic_open + cc_mic_update) and drive the player with input; the pacing +
 * spawn + coroutine logic is unchanged.
 *
 * Build:  bash SKILL_DIR/scripts/build_game.sh templates/game_horror/
 * Dev:    bash SKILL_DIR/scripts/cc dev templates/game_horror/main.c -o /tmp/horror
 *
 * Replace the timeline with real input/mic, add more stalker behaviours, wire
 * the sound field so the stalker also HEARS where the player is (see the stealth
 * template and cc/soundfield.h).
 */
#include "cc/claudecore.h"
#include "cc/event.h"
#include "cc/director.h"
#include "cc/mic.h"
#include "cc/coro.h"
#include <stdio.h>
#include <math.h>

#define SR 16000

typedef struct {
    CCActor actor;
    float   y_hidden, y_risen, rise;
    int     awake, lunged;
} Stalker;

typedef struct {
    CCScene*     scene;
    CCEventBus*  bus;
    CCMic*       mic;
    CCDirector*  dir;
    CCCoroSched* coro;

    CCMesh   mesh_floor, mesh_cube, mesh_cap;
    CCMaterial mat_floor, mat_wall, mat_player, mat_stalker;

    CCActor  player;
    Stalker  stalker;
    int      spawned;
    uint32_t noise_phase;
    float    time;
} Horror;

static Horror h;

/* coroutine: reveal → rise from the floor over ~1.2s → loom → lunge → done */
static CCCoroCmd stalker_rise_seq(CCCoro* co, float dt, void* ud) {
    (void)dt;
    Stalker* st = (Stalker*)ud;
    CC_CORO_BEGIN(co);
    cc_actor_set_visible(st->actor, true);
    while (st->rise < 1.0f) {
        st->rise += dt / 1.2f;
        if (st->rise > 1.0f) st->rise = 1.0f;
        float y = st->y_hidden + (st->y_risen - st->y_hidden) * st->rise;
        cc_actor_set_position(st->actor, 3.0f, y, -1.0f);
        CC_CORO_YIELD(co);
    }
    CC_CORO_WAIT(co, 0.3f);
    st->lunged = 1;
    CC_CORO_END(co);
}

void cc_game_init(CCEngine* eng) {
    h.scene = cc_scene_create(eng, "room");
    h.bus   = cc_event_bus_create();
    h.mic   = cc_mic_open(SR);
    h.dir   = cc_director_create();
    h.coro  = cc_coro_sched_create();
    cc_mic_watch_bus(h.mic, h.bus);
    cc_director_watch_bus(h.dir, h.bus);

    /* tune the director for a quick demo peak */
    CCDirectorConfig dc = cc_director_default_config();
    dc.rest_duration = 0.5f; dc.peak_threshold = 0.6f; dc.build_spawn_rate = 1.5f;
    cc_director_destroy(h.dir);
    h.dir = cc_director_create_cfg(&dc);
    cc_director_watch_bus(h.dir, h.bus);

    h.mesh_floor = cc_mesh_plane(eng, 24, 24, 4);
    h.mesh_cube  = cc_mesh_cube(eng, 1.0f);
    h.mesh_cap   = cc_mesh_capsule(eng, 0.4f, 1.2f, 16);
    CCMaterialDesc fd={.base_color={0.10f,0.10f,0.12f,1},.roughness=0.95f,.tint={1,1,1,1}};
    h.mat_floor=cc_material_create(eng,&fd);
    CCMaterialDesc wd={.base_color={0.18f,0.15f,0.14f,1},.roughness=0.85f,.tint={1,1,1,1}};
    h.mat_wall=cc_material_create(eng,&wd);
    CCMaterialDesc pd={.base_color={0.3f,0.45f,0.7f,1},.roughness=0.5f,.tint={1,1,1,1}};
    h.mat_player=cc_material_create(eng,&pd);
    CCMaterialDesc sd={.base_color={0.55f,0.06f,0.06f,1},.roughness=0.4f,
                       .emissive={0.25f,0,0},.tint={1,1,1,1}};
    h.mat_stalker=cc_material_create(eng,&sd);

    CCActor wa=cc_actor_spawn(h.scene,h.mesh_cube,h.mat_wall,0,1.5f,-6);
    cc_actor_set_scale(wa,12,3,0.3f);
    CCActor wb=cc_actor_spawn(h.scene,h.mesh_cube,h.mat_wall,-6,1.5f,0);
    cc_actor_set_scale(wb,0.3f,3,12);

    h.player=cc_actor_spawn(h.scene,h.mesh_cap,h.mat_player,-2.5f,1.0f,3.0f);

    h.stalker.y_hidden=-1.6f; h.stalker.y_risen=1.2f;
    h.stalker.actor=cc_actor_spawn(h.scene,h.mesh_cap,h.mat_stalker,3.0f,h.stalker.y_hidden,-1.0f);
    cc_actor_set_visible(h.stalker.actor,false);

    cc_light_set_ambient(eng,0.05f,0.05f,0.07f,1);
    CCLight key={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.3f,-0.8f,-0.4f},
                 .color={0.6f,0.62f,0.8f},.intensity=1.1f,.cast_shadows=true};
    cc_light_add(eng,&key);
    float z[3]={0.04f,0.05f,0.09f},hh[3]={0.08f,0.08f,0.11f},gc[3]={0.02f,0.02f,0.03f};
    cc_light_set_sky_colors(eng,z,hh,gc,1.0f);
    CCPostFX fx=cc_postfx_default();
    fx.tonemap_aces=true; fx.taa=true; fx.ssgi=true; fx.bloom=true;
    fx.bloom_threshold=0.9f; fx.bloom_intensity=0.12f; fx.vignette=true; fx.vignette_strength=0.55f;
    cc_postfx_set(eng,&fx);

    CC_INFO("Horror template: noise → director tension → stalker spawn (coroutine rise).");
}

void cc_game_tick(CCEngine* eng, double dtd) {
    float dt=(float)dtd;
    h.time+=dt;

    /* Scripted noise: quiet, then a LOUD burst ~0.5s..2.5s (stands in for the
       player's mic / footsteps). In a real build, feed cc_mic from a device or
       from gameplay events instead. */
    uint32_t n = SR/60;
    bool noisy = (h.time>0.5f && h.time<2.5f);
    static int16_t buf[SR/60 + 4];
    for (uint32_t i=0;i<n;i++){
        float amp = noisy?0.8f:0.0f;
        float t=(float)(h.noise_phase+i)/(float)SR;
        buf[i]=(int16_t)(sinf(2*3.14159265f*220.0f*t)*amp*32767.0f);
    }
    h.noise_phase+=n;
    cc_mic_feed(h.mic, buf, n);

    /* chain: mic → bus → director */
    cc_mic_update(h.mic, dt);
    cc_event_bus_update(h.bus);
    if (cc_mic_voice_active(h.mic)) cc_director_add_stress(h.dir, 1.2f*dt);
    cc_director_update(h.dir, dt);

    /* director decides to spawn → sequence the stalker via a coroutine */
    if (!h.spawned && cc_director_should_spawn(h.dir)) {
        h.spawned=1; h.stalker.awake=1;
        cc_coro_start(h.coro, stalker_rise_seq, &h.stalker);
    }
    cc_coro_update(h.coro, dt);

    /* render */
    cc_frame_begin(eng);
    CCCameraDesc cam={.pos={-4.5f,3.0f,6.5f},.target={1.2f,0.9f,0.0f},.up={0,1,0},
                      .fov_deg=52,.near_plane=0.1f,.far_plane=100,.exposure=0.5f};
    cc_camera_set(eng,&cam);
    CCTransform3D ft={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
    cc_draw_mesh(eng,h.mesh_floor,h.mat_floor,&ft);
    cc_scene_render(eng,h.scene);
    cc_frame_end(eng);
}

void cc_game_shutdown(CCEngine* eng) {
    (void)eng;
    cc_coro_sched_destroy(h.coro);
    cc_director_destroy(h.dir);
    cc_mic_close(h.mic);
    cc_event_bus_destroy(h.bus);
    CC_INFO("Horror template: shutdown");
}
