/*
 * Chlorlite — Stealth AI Starter Template
 *
 * A working stealth foundation: a guard patrols a walled room; the player moves
 * with WASD and can SPRINT (shift) which is LOUD. Noise propagates through the
 * level via the sound field (occluded by walls — sound leaks through doorways,
 * not through solid geometry), and when the guard HEARS the player it plans an
 * A* route and investigates. Sneak (walk) to stay quiet; sprint and the guard
 * comes looking.
 *
 * This demonstrates the intended architecture:
 *   HEARING (cc/soundfield.h) is the trigger — occlusion-aware, so a wall hides
 *   you. PATHFINDING (cc/ai.h A*) is the execution — it threads doorways. The
 *   event bus + director could be layered on to escalate; kept minimal here.
 *
 * Build:  bash SKILL_DIR/scripts/build_game.sh templates/game_stealth/
 * Dev:    bash SKILL_DIR/scripts/cc dev templates/game_stealth/main.c -o /tmp/stealth
 *         (then run headless: unset DISPLAY && /tmp/stealth out.png)
 *
 * Replace the layout, tune thresholds, add more guards, wire the director.
 */
#include "cc/claudecore.h"
#include "cc/soundfield.h"
#include "cc/ai.h"
#include <stdio.h>
#include <math.h>

/* ── tunables ─────────────────────────────────────────────────────────── */
#define GRID_W        24
#define GRID_H        24
#define CELL          1.0f
#define WALK_LOUD     0.25f     /* footstep loudness while walking            */
#define SPRINT_LOUD   1.00f     /* loudness while sprinting                   */
#define HEAR_THRESH   0.06f     /* guard investigates above this loudness     */
#define PLAYER_SPEED  4.0f
#define GUARD_SPEED   3.0f

/* ── game state ───────────────────────────────────────────────────────── */
typedef struct {
    CCScene*      scene;
    CCGrid*       grid;
    CCSoundField* sound;

    CCMesh   mesh_floor, mesh_cube, mesh_cap;
    CCMaterial mat_floor, mat_wall, mat_player, mat_guard;

    CCActor  player, guard;
    CCVec3   player_pos;
    CCAgent  guard_agent;

    /* guard AI */
    int      investigating;
    CCCell   path[256];
    uint32_t path_len, wp;
    CCVec3   patrol_a, patrol_b;   /* patrol endpoints */
    int      patrol_target_b;
    float    heard_loud;

    float    time;
} Stealth;

static Stealth s;

/* place a straight wall of blocked cells along a column, leaving a gap */
static void wall_column(CCGrid* g, uint32_t cx, uint32_t gap_z0, uint32_t gap_z1) {
    for (uint32_t z = 0; z < GRID_H; ++z)
        if (z < gap_z0 || z > gap_z1) cc_grid_set_blocked(g, cx, z, true);
}

void cc_game_init(CCEngine* eng) {
    s.scene = cc_scene_create(eng, "stealth");

    /* sound grid: one dividing wall with a doorway gap */
    s.grid = cc_grid_create(GRID_W, GRID_H, CELL);
    wall_column(s.grid, 15, 6, 7);          /* wall at cell x=15, gap z rows 6-7 */
    s.sound = cc_soundfield_create(s.grid);
    cc_soundfield_set_falloff(s.sound, 0.035f);

    /* meshes + materials */
    s.mesh_floor = cc_mesh_plane(eng, 30, 30, 6);
    s.mesh_cube  = cc_mesh_cube(eng, 1.0f);
    s.mesh_cap   = cc_mesh_capsule(eng, 0.4f, 1.2f, 16);
    CCMaterialDesc fd={.base_color={0.13f,0.13f,0.15f,1},.roughness=0.95f,.tint={1,1,1,1}};
    s.mat_floor = cc_material_create(eng,&fd);
    CCMaterialDesc wd={.base_color={0.22f,0.20f,0.18f,1},.roughness=0.85f,.tint={1,1,1,1}};
    s.mat_wall = cc_material_create(eng,&wd);
    CCMaterialDesc pd={.base_color={0.3f,0.55f,0.85f,1},.roughness=0.5f,.tint={1,1,1,1}};
    s.mat_player = cc_material_create(eng,&pd);
    CCMaterialDesc gd={.base_color={0.65f,0.1f,0.1f,1},.roughness=0.4f,
                       .emissive={0.2f,0,0},.tint={1,1,1,1}};
    s.mat_guard = cc_material_create(eng,&gd);

    /* dividing wall (two slabs + doorway gap), matching the sound grid */
    CCActor wt = cc_actor_spawn(s.scene, s.mesh_cube, s.mat_wall, 3.5f, 1.5f, 3.0f);
    cc_actor_set_scale(wt, 0.4f, 3.0f, 14.0f);
    CCActor wb = cc_actor_spawn(s.scene, s.mesh_cube, s.mat_wall, 3.5f, 1.5f, -9.0f);
    cc_actor_set_scale(wb, 0.4f, 3.0f, 6.0f);

    /* player + guard */
    s.player_pos = (CCVec3){ 7.0f, 0.0f, 0.0f };
    s.player = cc_actor_spawn(s.scene, s.mesh_cap, s.mat_player,
                              s.player_pos.x, 1.0f, s.player_pos.z);

    CCVec3 gstart = { -6.0f, 0.0f, 5.0f };
    s.guard = cc_actor_spawn(s.scene, s.mesh_cap, s.mat_guard, gstart.x, 1.0f, gstart.z);
    s.guard_agent = cc_agent_make(gstart, GUARD_SPEED, 8.0f);
    s.patrol_a = gstart;
    s.patrol_b = (CCVec3){ -6.0f, 0.0f, -6.0f };
    s.patrol_target_b = 1;

    /* lighting */
    cc_light_set_ambient(eng, 0.06f,0.06f,0.08f, 1);
    CCLight key={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.35f,-0.8f,-0.4f},
                 .color={0.7f,0.72f,0.85f},.intensity=1.3f,.cast_shadows=true};
    cc_light_add(eng,&key);
    float z[3]={0.05f,0.06f,0.10f},h[3]={0.09f,0.09f,0.12f},gc[3]={0.02f,0.02f,0.03f};
    cc_light_set_sky_colors(eng,z,h,gc,1.0f);
    CCPostFX fx=cc_postfx_default();
    fx.tonemap_aces=true; fx.taa=true; fx.ssgi=true; fx.bloom=true;
    fx.bloom_threshold=0.9f; fx.bloom_intensity=0.10f; fx.vignette=true;
    cc_postfx_set(eng,&fx);

    CC_INFO("Stealth template: WASD move, SHIFT sprint (loud). Guard hears + investigates.");
}

/* move the guard along its patrol loop when not investigating */
static void patrol(float dt) {
    CCVec3 target = s.patrol_target_b ? s.patrol_b : s.patrol_a;
    CCVec3 steer = cc_steer_arrive(&s.guard_agent, target, 1.5f);
    cc_agent_integrate(&s.guard_agent, steer, dt);
    float dx=s.guard_agent.position.x-target.x, dz=s.guard_agent.position.z-target.z;
    if (dx*dx+dz*dz < 0.5f) s.patrol_target_b = !s.patrol_target_b;
}

void cc_game_tick(CCEngine* eng, double dtd) {
    float dt = (float)dtd;
    s.time += dt;

    /* ── player movement (WASD + sprint) ──────────────────────────────── */
    float mx=0, mz=0;
    if (cc_key_down(eng, QKEY_W) || cc_key_down(eng, QKEY_UP))    mz -= 1;
    if (cc_key_down(eng, QKEY_S) || cc_key_down(eng, QKEY_DOWN))  mz += 1;
    if (cc_key_down(eng, QKEY_A) || cc_key_down(eng, QKEY_LEFT))  mx -= 1;
    if (cc_key_down(eng, QKEY_D) || cc_key_down(eng, QKEY_RIGHT)) mx += 1;
    if (cc_key_pressed(eng, QKEY_ESCAPE)) cc_quit(eng);
    bool sprinting = cc_key_down(eng, QKEY_LSHIFT) || cc_key_down(eng, QKEY_RSHIFT);
    bool moving = (mx*mx+mz*mz) > 0.001f;
    if (moving) {
        float inv = 1.0f/sqrtf(mx*mx+mz*mz);
        float sp = PLAYER_SPEED * (sprinting ? 1.8f : 1.0f);
        s.player_pos.x += mx*inv*sp*dt;
        s.player_pos.z += mz*inv*sp*dt;
        cc_actor_set_position(s.player, s.player_pos.x, 1.0f, s.player_pos.z);
    }

    /* ── emit the player's noise into the sound field ─────────────────── */
    cc_soundfield_begin(s.sound);
    if (moving) {
        float loud = sprinting ? SPRINT_LOUD : WALK_LOUD;
        cc_soundfield_emit(s.sound, s.player_pos.x, s.player_pos.z, loud);
    }
    cc_soundfield_propagate(s.sound, 200.0f);

    /* ── guard: hear → investigate (A*) → else patrol ─────────────────── */
    float loud; CCVec3 toward;
    bool heard = cc_soundfield_sample(s.sound, s.guard_agent.position.x,
                                      s.guard_agent.position.z, &loud, &toward);
    s.heard_loud = heard ? loud : 0.0f;

    if (heard && loud > HEAR_THRESH) {
        /* (re)plan a route to the player whenever we hear a strong-enough noise */
        int32_t sx,sy,gx,gy;
        cc_grid_world_to_cell(s.grid, s.guard_agent.position.x, s.guard_agent.position.z, &sx,&sy);
        cc_grid_world_to_cell(s.grid, s.player_pos.x, s.player_pos.z, &gx,&gy);
        uint32_t n = cc_astar(s.grid, sx,sy,gx,gy, true, s.path, 256);
        if (n > 0) { s.path_len = n; s.wp = 0; s.investigating = 1; }
    }

    if (s.investigating && s.path_len > 0) {
        bool done=false;
        CCVec3 steer = cc_steer_path_follow(&s.guard_agent, s.grid, s.path, s.path_len,
                                            &s.wp, 0.6f, &done);
        cc_agent_integrate(&s.guard_agent, steer, dt);
        if (done) s.investigating = 0;   /* reached last known position; resume patrol */
    } else {
        patrol(dt);
    }
    cc_actor_set_position(s.guard, s.guard_agent.position.x, 1.0f, s.guard_agent.position.z);

    /* ── render ───────────────────────────────────────────────────────── */
    cc_frame_begin(eng);
    CCCameraDesc cam={.pos={-2.0f, 16.0f, 14.0f}, .target={2.0f,0.0f,-1.0f}, .up={0,1,0},
                      .fov_deg=50, .near_plane=0.1f, .far_plane=200, .exposure=0.6f};
    cc_camera_set(eng, &cam);
    CCTransform3D ft={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
    cc_draw_mesh(eng, s.mesh_floor, s.mat_floor, &ft);
    cc_scene_render(eng, s.scene);
    cc_frame_end(eng);
}

void cc_game_shutdown(CCEngine* eng) {
    (void)eng;
    cc_soundfield_destroy(s.sound);
    cc_grid_destroy(s.grid);
    CC_INFO("Stealth template: shutdown");
}
