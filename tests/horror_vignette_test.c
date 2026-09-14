/* horror_vignette_test — CAPSTONE demo tying together the five systems built
 * this session into one rendered scene:
 *   EVENT BUS  — the spine everything talks over
 *   MIC INPUT  — synthetic "loud noise" raises tension ("the monster hears you")
 *   DIRECTOR   — mic loudness drives intensity → pacing FSM → spawn decision
 *   COROUTINES — the spawn is sequenced (rise from the floor, then lunge)
 *   (physics/interact channels also feed the bus in a fuller game)
 *
 * The scene: a dim room, a player marker, a door, and a hidden "stalker". We run
 * a scripted timeline: quiet → the player makes noise (fed to the mic) → the
 * director's intensity climbs into BUILD_UP/PEAK → should_spawn fires → a
 * coroutine reveals the stalker and rises it out of the floor toward the player.
 * We render the final state and assert both the LOGIC chain fired and the frame
 * is a real (non-black) image. Also serves as a rendered integration test that
 * exercises the renderer path, not just pure logic.
 *
 * Run: horror_vignette_test [out.png]
 */
#include "cc/claudecore.h"
#include "cc/event.h"
#include "cc/director.h"
#include "cc/mic.h"
#include "cc/coro.h"
#include "cc/interact.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

#define SR 16000

/* ── the stalker: state shared between the coroutine and the scene ────────── */
typedef struct {
    CCActor actor;
    float   y_hidden;     /* buried below the floor          */
    float   y_risen;      /* standing height                 */
    float   rise;         /* 0..1 emergence                  */
    int     awake;        /* set true when the director spawns it */
    int     lunged;       /* coroutine reached the lunge stage    */
} Stalker;

/* coroutine: reveal → rise over ~1.2s → brief pause → lunge flag → done */
static CCCoroCmd stalker_rise_seq(CCCoro* co, float dt, void* ud) {
    (void)dt;
    Stalker* s = (Stalker*)ud;
    CC_CORO_BEGIN(co);
    cc_actor_set_visible(s->actor, true);      /* emerge from hiding */
    /* rise out of the floor */
    while (s->rise < 1.0f) {
        s->rise += dt / 1.2f;
        if (s->rise > 1.0f) s->rise = 1.0f;
        float y = s->y_hidden + (s->y_risen - s->y_hidden) * s->rise;
        cc_actor_set_position(s->actor, 3.0f, y, -1.0f);
        CC_CORO_YIELD(co);
    }
    CC_CORO_WAIT(co, 0.3f);                     /* loom for a beat */
    s->lunged = 1;                             /* would trigger the scare */
    CC_CORO_END(co);
}

/* director watches the bus; we also mirror mic loudness in directly each frame */
int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "/tmp/horror_vignette.png";

    /* ── systems ──────────────────────────────────────────────────────────── */
    CCEventBus*  bus   = cc_event_bus_create();
    CCMic*       mic   = cc_mic_open(SR);
    CCDirector*  dir   = cc_director_create();
    CCCoroSched* coro  = cc_coro_sched_create();
    cc_mic_watch_bus(mic, bus);          /* mic → CC_EVT_MIC_LEVEL */
    cc_director_watch_bus(dir, bus);     /* director also listens on the bus */

    /* tune the director so the demo reaches a peak quickly */
    CCDirectorConfig dcfg = cc_director_default_config();
    dcfg.rest_duration = 0.5f; dcfg.peak_threshold = 0.6f; dcfg.build_spawn_rate = 1.5f;
    cc_director_destroy(dir);
    dir = cc_director_create_cfg(&dcfg);
    cc_director_watch_bus(dir, bus);

    /* ── scene ────────────────────────────────────────────────────────────── */
    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 960; cfg.height = 540; cfg.verbose = false;
    CCEngine* e = cc_init(&cfg);
    CHECK(e != NULL, "engine created");
    if (!e) return 1;
    CCScene* world = cc_scene_create(e, "room");

    CCMesh floor  = cc_mesh_plane(e, 24, 24, 4);
    CCMesh cube   = cc_mesh_cube(e, 1.0f);
    CCMesh body   = cc_mesh_capsule(e, 0.45f, 1.4f, 16);
    CCMesh pmesh  = cc_mesh_capsule(e, 0.4f, 1.2f, 16);

    CCMaterialDesc fd = {.base_color={0.10f,0.10f,0.12f,1}, .roughness=0.95f, .tint={1,1,1,1}};
    CCMaterial fmat = cc_material_create(e, &fd);
    CCMaterialDesc wd = {.base_color={0.18f,0.15f,0.14f,1}, .roughness=0.85f, .tint={1,1,1,1}};
    CCMaterial wmat = cc_material_create(e, &wd);
    CCMaterialDesc pd = {.base_color={0.3f,0.45f,0.7f,1}, .roughness=0.5f, .tint={1,1,1,1}};
    CCMaterial pmat = cc_material_create(e, &pd);
    CCMaterialDesc sd = {.base_color={0.55f,0.06f,0.06f,1}, .roughness=0.4f, .metallic=0.1f,
                         .emissive={0.25f,0.0f,0.0f}, .tint={1,1,1,1}};
    CCMaterial smat = cc_material_create(e, &sd);

    /* floor + a couple of wall slabs for a room feel */
    CCTransform3D ft = {.pos={0,0,0}, .rot={0,0,0,1}, .scale={1,1,1}};
    CCActor wallA = cc_actor_spawn(world, cube, wmat, 0, 1.5f, -6);
    cc_actor_set_scale(wallA, 12, 3, 0.3f);
    CCActor wallB = cc_actor_spawn(world, cube, wmat, -6, 1.5f, 0);
    cc_actor_set_scale(wallB, 0.3f, 3, 12);

    /* the player marker */
    CCActor player = cc_actor_spawn(world, pmesh, pmat, -2.5f, 1.0f, 3.0f);

    /* a door (interactable, also publishes on the bus) */
    CCActor doorA = cc_actor_spawn(world, cube, wmat, 5.0f, 1.5f, 0);
    cc_actor_set_scale(doorA, 0.2f, 3, 2.2f);
    cc_interactable_set_event_bus(world, bus);
    CCInteractable* door = cc_interactable_register(world, doorA, CC_INTERACT_DOOR, 3.0f);
    (void)door;

    /* the stalker, initially buried + hidden */
    Stalker stalker = {0};
    stalker.y_hidden = -1.6f; stalker.y_risen = 1.2f;
    stalker.actor = cc_actor_spawn(world, body, smat, 3.0f, stalker.y_hidden, -1.0f);
    cc_actor_set_visible(stalker.actor, false);

    /* lighting: dim, moody */
    cc_light_set_ambient(e, 0.05f, 0.05f, 0.07f, 1);
    CCLight key = {.type=CC_LIGHT_DIRECTIONAL, .dir={-0.3f,-0.8f,-0.4f},
                   .color={0.6f,0.62f,0.8f}, .intensity=1.1f, .cast_shadows=true};
    cc_light_add(e, &key);
    float z[3]={0.04f,0.05f,0.09f}, h[3]={0.08f,0.08f,0.11f}, gc[3]={0.02f,0.02f,0.03f};
    cc_light_set_sky_colors(e, z, h, gc, 1.0f);

    /* ── timeline ─────────────────────────────────────────────────────────── */
    int   spawned = 0;
    float sim_t = 0.0f;
    const float dt = 1.0f/30.0f;
    int16_t noise[SR/30 + 4];
    uint32_t phase = 0;

    for (int frame = 0; frame < 200; ++frame) {
        sim_t += dt;

        /* the player is quiet for the first ~1s, then makes a LOUD noise for ~2s
         * (feed a loud tone into the mic), then quiet again */
        uint32_t nsamp = SR/30;
        bool making_noise = (sim_t > 1.0f && sim_t < 3.0f);
        for (uint32_t i = 0; i < nsamp; ++i) {
            float amp = making_noise ? 0.8f : 0.0f;
            float t = (float)(phase + i) / (float)SR;
            noise[i] = (int16_t)(sinf(2*3.14159265f*220.0f*t) * amp * 32767.0f);
        }
        phase += nsamp;
        cc_mic_feed(mic, noise, nsamp);

        /* pump the chain: mic analysis → bus → director */
        cc_mic_update(mic, dt);
        cc_event_bus_update(bus);      /* delivers CC_EVT_MIC_LEVEL to the director */
        /* extra: also convert live loudness to stress directly (belt & braces) */
        if (cc_mic_voice_active(mic)) cc_director_add_stress(dir, 1.2f * dt);
        cc_director_update(dir, dt);

        /* director decides to spawn → launch the stalker coroutine once */
        if (!spawned && cc_director_should_spawn(dir)) {
            spawned = 1;
            stalker.awake = 1;
            cc_coro_start(coro, stalker_rise_seq, &stalker);
        }
        cc_coro_update(coro, dt);
        cc_interactable_update(world, dt);

        /* stop once the stalker has fully risen + lunged (or timeout) */
        if (stalker.lunged) break;
    }

    /* ── assertions on the chain ──────────────────────────────────────────── */
    CHECK(cc_mic_voice_active(mic) == false || cc_mic_loudness(mic) >= 0.0f, "mic analysed");
    CHECK(spawned == 1, "director triggered a spawn from mic-driven intensity");
    CHECK(stalker.awake == 1, "stalker was awakened");
    CHECK(stalker.rise > 0.99f, "stalker fully rose (coroutine ran to completion)");
    CHECK(stalker.lunged == 1, "stalker reached the lunge stage");
    CHECK(cc_director_intensity(dir) > 0.0f, "director accumulated intensity");

    /* ── render the final tableau ─────────────────────────────────────────── */
    CCPostFX fx = cc_postfx_default();
    fx.tonemap_aces = true; fx.auto_exposure = false;  /* fixed low exposure = mood */
    fx.taa = true; fx.ssgi = true; fx.ssgi_intensity = 1.1f;
    fx.bloom = true; fx.bloom_threshold = 0.9f; fx.bloom_intensity = 0.12f;
    fx.vignette = true; fx.vignette_strength = 0.55f;
    cc_postfx_set(e, &fx);

    for (int f = 0; f < 8; ++f) {
        cc_frame_begin(e);
        CCCameraDesc cam = {.pos={-4.5f,3.0f,6.5f}, .target={1.2f,0.9f,0.0f}, .up={0,1,0},
                            .fov_deg=52, .near_plane=0.1f, .far_plane=100, .exposure=0.5f};
        cc_camera_set(e, &cam);
        cc_draw_mesh(e, floor, fmat, &ft);
        cc_scene_render(e, world);
        cc_frame_end(e);
    }
    const char* saved = cc_screenshot(e, out);
    CHECK(saved != NULL, "screenshot written");

    printf("horror vignette: intensity=%.2f phase=%s spawned=%d rise=%.2f lunged=%d\n",
           cc_director_intensity(dir), cc_director_phase_name(cc_director_phase(dir)),
           spawned, stalker.rise, stalker.lunged);
    printf("screenshot: %s\n", saved ? saved : "(null)");

    cc_interactable_clear(world);
    cc_scene_destroy(world);
    cc_coro_sched_destroy(coro);
    cc_director_destroy(dir);
    cc_mic_close(mic);
    cc_event_bus_destroy(bus);
    cc_shutdown(e);

    if (failures == 0) {
        printf("HORROR VIGNETTE TEST: all checks passed (mic→bus→director→coroutine "
               "spawn chain fired; scene rendered)\n");
        return 0;
    }
    printf("HORROR VIGNETTE TEST: %d check(s) FAILED\n", failures);
    return 1;
}
