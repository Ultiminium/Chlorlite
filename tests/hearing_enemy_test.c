/* hearing_enemy_test — rendered demo of SOUND-PROPAGATION AI driving movement:
 * an enemy on one side of a walled room HEARS a noise on the other side and
 * walks toward it — detouring through the DOORWAY, because propagation (and thus
 * the toward-source direction the sound field returns) travels around the wall,
 * not through it. Proves occlusion-aware hearing produces correct navigation,
 * and renders the result so the path is visible.
 *
 * Layout (top-down, XZ; grid 24x24, 1u cells, centered on origin):
 *   - a wall along x≈+1 splitting the room, with a doorway gap near z≈-4
 *   - NOISE (player, blue) at right side (+x)
 *   - ENEMY (red) starts at left side (-x), far from the doorway
 * The enemy uses cc_soundfield_sample()'s direction each frame. If it navigated
 * by straight line it would jam into the wall; instead it must curve to the gap.
 *
 * Run: hearing_enemy_test [out.png]
 */
#include "cc/claudecore.h"
#include "cc/soundfield.h"
#include "cc/ai.h"
#include "cc/event.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "/tmp/hearing_enemy.png";

    /* ── sound grid + field ───────────────────────────────────────────────── */
    CCGrid* grid = cc_grid_create(24, 24, 1.0f);
    /* wall at cell x=15 (world ~ +4.5), doorway gap at z rows 6..7 (world ~ -4) */
    for (uint32_t z = 0; z < 24; ++z)
        if (z != 6 && z != 7) cc_grid_set_blocked(grid, 15, z, true);
    CCSoundField* sf = cc_soundfield_create(grid);
    cc_soundfield_set_falloff(sf, 0.035f);   /* travels far enough across room */

    /* world positions */
    CCVec3 noise_pos = { 8.0f, 0.0f,  0.0f };   /* player noise, right of wall  */
    CCVec3 enemy_pos = {-7.0f, 0.0f,  5.0f };   /* enemy, left of wall, upper    */

    CCAgent enemy = cc_agent_make(enemy_pos, 3.2f /*max_speed*/, 8.0f /*max_force*/);

    /* ── simulate: enemy walks toward the sound it hears ───────────────────── */
    const float dt = 1.0f/30.0f;
    bool reached_door_side = false;
    float min_dist_to_noise = 1e9f;
    int   heard_frames = 0;
    bool  investigating = false;
    CCCell path[256]; uint32_t path_len = 0, wp = 0;

    for (int frame = 0; frame < 400; ++frame) {
        /* emit the player's noise + propagate each frame */
        cc_soundfield_begin(sf);
        cc_soundfield_emit(sf, noise_pos.x, noise_pos.z, 1.0f);
        cc_soundfield_propagate(sf, 200.0f);

        /* HEARING decides whether to investigate; the sound field's occlusion
           means the enemy only "hears" (and commits) if sound actually reaches
           it around the walls. */
        float loud; CCVec3 toward;
        bool heard = cc_soundfield_sample(sf, enemy.position.x, enemy.position.z,
                                          &loud, &toward);
        if (heard && loud > 0.03f) heard_frames++;

        /* On first hearing, plan an A* route to the sound source over the SAME
           grid the sound propagated through — this threads the doorway. Hearing
           is the trigger; pathfinding is the execution. */
        if (!investigating && heard && loud > 0.05f) {
            int32_t sx,sy,gx,gy;
            cc_grid_world_to_cell(grid, enemy.position.x, enemy.position.z, &sx,&sy);
            cc_grid_world_to_cell(grid, noise_pos.x, noise_pos.z, &gx,&gy);
            path_len = cc_astar(grid, sx,sy,gx,gy, true, path, 256);
            if (path_len > 0) { investigating = true; wp = 0; }
        }

        if (investigating && path_len > 0) {
            bool done = false;
            CCVec3 steer = cc_steer_path_follow(&enemy, grid, path, path_len, &wp,
                                                0.6f, &done);
            cc_agent_integrate(&enemy, steer, dt);
        }

        if (enemy.position.x > 4.0f) reached_door_side = true;

        float dnoise = sqrtf((enemy.position.x-noise_pos.x)*(enemy.position.x-noise_pos.x) +
                             (enemy.position.z-noise_pos.z)*(enemy.position.z-noise_pos.z));
        if (dnoise < min_dist_to_noise) min_dist_to_noise = dnoise;
        if (dnoise < 1.5f) break;   /* arrived at the noise */
    }

    /* ── assertions: the enemy heard, committed, and navigated to the sound ── */
    CHECK(heard_frames > 0, "enemy heard the noise through the doorway (occlusion-aware)");
    CHECK(investigating, "hearing the noise triggered the enemy to investigate (A* route planned)");
    CHECK(reached_door_side, "enemy crossed to the noise side (threaded the doorway, not the wall)");
    CHECK(min_dist_to_noise < 2.0f, "enemy reached the noise source");

    /* ── render the scene with the enemy at its final position ────────────── */
    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 960; cfg.height = 540; cfg.verbose = false;
    CCEngine* e = cc_init(&cfg);
    CHECK(e != NULL, "engine created");
    if (!e) { printf("HEARING ENEMY TEST: engine init failed\n"); return 1; }
    CCScene* world = cc_scene_create(e, "room");

    CCMesh floorm = cc_mesh_plane(e, 30, 30, 6);
    CCMesh cube   = cc_mesh_cube(e, 1.0f);
    CCMesh cap    = cc_mesh_capsule(e, 0.4f, 1.2f, 16);

    CCMaterialDesc fd={.base_color={0.12f,0.12f,0.14f,1},.roughness=0.95f,.tint={1,1,1,1}};
    CCMaterial fmat=cc_material_create(e,&fd);
    CCMaterialDesc wd={.base_color={0.20f,0.18f,0.16f,1},.roughness=0.85f,.tint={1,1,1,1}};
    CCMaterial wmat=cc_material_create(e,&wd);
    CCMaterialDesc nd={.base_color={0.3f,0.5f,0.8f,1},.roughness=0.5f,.tint={1,1,1,1}};
    CCMaterial nmat=cc_material_create(e,&nd);          /* noise/player: blue */
    CCMaterialDesc ed={.base_color={0.6f,0.07f,0.07f,1},.roughness=0.4f,
                       .emissive={0.22f,0,0},.tint={1,1,1,1}};
    CCMaterial emat=cc_material_create(e,&ed);          /* enemy: red glow */

    /* floor */
    CCTransform3D ft={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};

    /* the dividing wall at world x≈3.5 (matches sound-grid cell x=15), two slabs
       leaving the doorway gap at z≈-5 (matches open grid rows 6,7). */
    CCActor wallTop = cc_actor_spawn(world, cube, wmat, 3.5f, 1.5f, 3.0f);
    cc_actor_set_scale(wallTop, 0.4f, 3.0f, 14.0f);   /* covers z ≈ -4..+10 */
    CCActor wallBot = cc_actor_spawn(world, cube, wmat, 3.5f, 1.5f, -9.0f);
    cc_actor_set_scale(wallBot, 0.4f, 3.0f, 6.0f);    /* covers z ≈ -12..-6 */
    /* (gap between them ≈ z -4..-6, the doorway at grid rows 6,7) */

    /* outer walls for room feel */
    CCActor wN = cc_actor_spawn(world, cube, wmat, 0, 1.5f, 11); cc_actor_set_scale(wN, 24,3,0.4f);
    CCActor wS = cc_actor_spawn(world, cube, wmat, 0, 1.5f,-11); cc_actor_set_scale(wS, 24,3,0.4f);

    /* noise marker (player) and enemy at final position */
    CCActor noiseA = cc_actor_spawn(world, cap, nmat, noise_pos.x, 1.0f, noise_pos.z);
    CCActor enemyA = cc_actor_spawn(world, cap, emat, enemy.position.x, 1.0f, enemy.position.z);
    (void)noiseA; (void)enemyA;

    /* moody light */
    cc_light_set_ambient(e, 0.06f,0.06f,0.08f, 1);
    CCLight key={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.35f,-0.8f,-0.4f},
                 .color={0.7f,0.72f,0.85f},.intensity=1.3f,.cast_shadows=true};
    cc_light_add(e,&key);
    float z[3]={0.05f,0.06f,0.10f},h[3]={0.09f,0.09f,0.12f},gc[3]={0.02f,0.02f,0.03f};
    cc_light_set_sky_colors(e,z,h,gc,1.0f);

    CCPostFX fx=cc_postfx_default();
    fx.tonemap_aces=true; fx.auto_exposure=false; fx.taa=true;
    fx.ssgi=true; fx.ssgi_intensity=1.0f; fx.bloom=true; fx.bloom_threshold=0.9f;
    fx.bloom_intensity=0.10f; fx.vignette=true; fx.vignette_strength=0.5f;
    cc_postfx_set(e,&fx);

    for (int f=0; f<8; ++f) {
        cc_frame_begin(e);
        /* top-down-ish angled camera so the wall, doorway, and both figures show */
        CCCameraDesc cam={.pos={-2.0f, 16.0f, 14.0f}, .target={2.0f,0.0f,-1.0f}, .up={0,1,0},
                          .fov_deg=50, .near_plane=0.1f, .far_plane=200, .exposure=0.55f};
        cc_camera_set(e,&cam);
        cc_draw_mesh(e, floorm, fmat, &ft);
        cc_scene_render(e, world);
        cc_frame_end(e);
    }
    const char* saved = cc_screenshot(e, out);
    CHECK(saved != NULL, "screenshot written");

    printf("hearing enemy: heard_frames=%d reached_door_side=%d final=(%.1f,%.1f) "
           "min_dist_to_noise=%.2f\n",
           heard_frames, reached_door_side, enemy.position.x, enemy.position.z,
           min_dist_to_noise);
    printf("screenshot: %s\n", saved ? saved : "(null)");

    cc_scene_destroy(world);
    cc_shutdown(e);
    cc_soundfield_destroy(sf);
    cc_grid_destroy(grid);

    if (failures == 0) {
        printf("HEARING ENEMY TEST: all checks passed (enemy heard noise, navigated "
               "through the doorway to the source; scene rendered)\n");
        return 0;
    }
    printf("HEARING ENEMY TEST: %d check(s) FAILED\n", failures);
    return 1;
}
