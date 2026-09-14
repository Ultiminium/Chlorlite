#pragma once

/*
 * Chlorlite Game Engine — v0.2.0
 * Skill-stack edition. Self-contained. No installation required.
 *
 * Usage: #include "cc/claudecore.h"
 * Link:  -lclaudecore -lGL -lOSMesa -lpthread   (headless/sandbox)
 *        -lclaudecore -lGL -lglfw3 -lpthread      (windowed)
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Sub-system headers */
#include "cc/ecs.h"
#include "cc/render.h"
#include "cc/audio.h"
#include "cc/input.h"
#include "cc/scripting.h"
#include "cc/procgen.h"
#include "cc/player.h"
#include "cc/ai.h"
#include "cc/world.h"
#include "cc/editmesh.h"
#include "cc/save.h"
#include "cc/interact.h"
#include "cc/dialogue.h"
#include "cc/particles.h"
#include "cc/tween.h"
#include "cc/trail.h"
#include "cc/move.h"
#include "cc/worldui.h"
#include "cc/prefab.h"
#include "cc/inputrec.h"
#include "cc/nineslice.h"
#include "cc/event.h"
#include "cc/coro.h"
#include "cc/director.h"
#include "cc/mic.h"
#include "cc/soundfield.h"
#include "cc/audiofx.h"
#include "cc/console.h"
#include "cc/savegame.h"
#include "cc/gui.h"
#include "cc/aa.h"
#include "cc/group.h"
#include "cc/pixshape.h"
#include "cc/jobs.h"
#include "cc/net.h"
#include "cc/steam.h"
#include "cc/debug.h"
#include "cc/scene_graph.h"
#include "cc/input_map.h"
#include "cc/physics.h"
#include "cc/camera.h"
#include "cc/anim.h"
#include "cc/actor.h"

/* Version */
#define CC_VERSION_MAJOR 0
#define CC_VERSION_MINOR 2
#define CC_VERSION_PATCH 0
const char* cc_version_string(void);

/* Forward declarations */
typedef struct CCEngine CCEngine;
typedef struct CCScene  CCScene;
typedef struct CCRenderer CCRenderer;

/* ─── Engine config ──────────────────────────────────────────────────── */
typedef struct CCEngine CCEngine;   /* fwd */

typedef struct CCEngineConfig {
    const char*       title;
    uint32_t          width, height;
    bool              fullscreen;
    bool              resizable;
    bool              vsync;
    CCRendererBackend renderer_backend;   /* AUTO tries windowed first */
    bool              headless;           /* force headless */
    const char*       screenshot_dir;
    bool              audio_enabled;
    uint32_t          audio_sample_rate;
    bool              grab_input;
    bool              verbose;
    /* Game loop callbacks — cc_run() drives these. Two models are supported:

       (A) SINGLE  — set on_frame: called once per rendered frame with wall-clock
           dt; does logic + draw together. Simple; simulation is tied to frame rate.

       (B) TPS/FPS SPLIT (recommended) — set on_tick AND on_render:
           - on_tick(fixed_dt): the SIMULATION step. Called at a FIXED rate
             (cfg.tick_rate Hz) via an accumulator — zero, one, or several times per
             rendered frame so the sim advances in real time regardless of FPS.
             Deterministic and frame-rate independent. Put physics/logic/input here.
           - on_render(alpha): the RENDER step. Called ONCE per frame. `alpha` is the
             0..1 fraction between the last two ticks, for interpolating drawn state
             so motion is smooth even when FPS != TPS. Make GL/draw calls here.
           If both on_tick and on_render are set, the accumulator loop is used and
           on_frame is ignored. This decouples simulation rate from render rate. */
    void  (*on_frame)  (CCEngine* eng, double dt, void* userdata);  /* logic + draw */
    void  (*on_tick)   (CCEngine* eng, double fixed_dt, void* userdata); /* fixed sim */
    void  (*on_render) (CCEngine* eng, double alpha, void* userdata);   /* interp draw */
    double            tick_rate;    /* sim ticks/sec for the split loop (0 → 60) */
    void  (*on_resize) (CCEngine* eng, uint32_t w, uint32_t h, void* userdata);
    void*             userdata;
    double            target_fps;   /* 0 = uncapped / vsync-limited */
} CCEngineConfig;

static inline CCEngineConfig cc_default_config(void) {
    return (CCEngineConfig){
        .title             = "Chlorlite",
        .width             = 1280,
        .height            = 720,
        .renderer_backend  = CC_RENDERER_AUTO,
        .headless          = false,
        .screenshot_dir    = "/tmp/cc_screenshots",
        .audio_enabled     = true,
        .audio_sample_rate = 44100,
        .verbose           = false,
    };
}

static inline CCEngineConfig cc_sandbox_config(void) {
    return (CCEngineConfig){
        .title             = "Chlorlite",
        .width             = 1280,
        .height            = 720,
        .renderer_backend  = CC_RENDERER_OSMESA,
        .headless          = true,
        .screenshot_dir    = "/tmp/cc_screenshots",
        .audio_enabled     = false,
        .verbose           = true,
    };
}

/* ─── Lifecycle ──────────────────────────────────────────────────────── */
CCEngine* cc_init(const CCEngineConfig* cfg);
void      cc_shutdown(CCEngine* eng);
void      cc_run(CCEngine* eng);         /* blocking game loop */

/* ── Headless gameplay RECORDING (the "live gameplay viewer") ───────────────
   Runs the game's on_frame callback headless at a fixed timestep for `frames`
   frames, delivering a scripted INPUT TIMELINE (key events at given frames, exactly
   as cc_run would), and captures every frame to `out_dir`/frame_%06d.png. This is
   how gameplay is inspected without a physical screen: the PNG sequence assembles
   into an MP4 / GIF / contact sheet (tools/make_gameplay_video.py). Unlike a static
   screenshot it shows MOTION, timing, and input response over time.
   Requires cfg.on_frame to be set (same callback cc_run uses). Returns frames
   captured. A NULL/empty timeline just records idle gameplay. */
typedef struct { int frame; int key; int down; } CCInputEvent;  /* key = QKey */
uint32_t  cc_demo_run(CCEngine* eng, const char* out_dir,
                      uint32_t frames, double fixed_dt,
                      const CCInputEvent* timeline, uint32_t timeline_len);
/* (in-process final-frame pixel readback is cc_frame_pixels in cc/render.h — the
   primitive that lets a program perceive the rendered frame and act on it.) */
void      cc_quit(CCEngine* eng);

/* Step one frame manually (sandbox/scripted mode) */
void      cc_tick(CCEngine* eng, double dt);
QContext* cc_engine_qwerty(CCEngine* eng);
const char* cc_asset_path(const char* rel);  /* path relative to the executable (for bundled assets) */
/* Resolve a bare asset name (e.g. "player.gltf", "hero.png") to a path, working
   both in development (finds it in the assets-dev/<type>/ library) and when shipped
   (finds it in the game's bundled assets/ folder). Use this to reference assets;
   the bundler copies exactly the ones you cc_asset() into the shipped game. */
const char* cc_asset(const char* name);

/* ─── Scene ──────────────────────────────────────────────────────────── */
CCScene*  cc_scene_create(CCEngine* eng, const char* name);
void      cc_scene_destroy(CCScene* s);
void      cc_scene_set_active(CCEngine* eng, CCScene* s);
CCScene*  cc_scene_active(CCEngine* eng);

/* ─── Frame ──────────────────────────────────────────────────────────── */
void        cc_frame_begin(CCEngine* eng);
void        cc_frame_end(CCEngine* eng);
/* Snapshot input state as 'previous' for next-frame edge detection. cc_run() calls
   this automatically; a manual cc_tick+frame loop must call it once per frame after
   reading input (see docs on cc_key_pressed). */
void        cc_input_end_frame(CCEngine* eng);
/* Snapshot this frame's key state once + latch edges. cc_run() calls it after
   polling events and before on_frame; manual loops call it after poll / before
   reading input. Pairs with cc_input_end_frame. */
void        cc_input_begin_frame(CCEngine* eng);
const char* cc_screenshot(CCEngine* eng, const char* path);
void        cc_frame_pixels(CCEngine* eng, uint8_t** px, uint32_t* w, uint32_t* h);
void        cc_resize(CCEngine* eng, uint32_t w, uint32_t h);

/* ─── Input ──────────────────────────────────────────────────────────── */
bool    cc_key_down(CCEngine* e, QKey k);
bool    cc_key_pressed(CCEngine* e, QKey k);
bool    cc_key_released(CCEngine* e, QKey k);
bool    cc_mouse_down(CCEngine* e, QMouseButton b);
void    cc_mouse_pos(CCEngine* e, int32_t* x, int32_t* y);
void    cc_display_size(CCEngine* e, uint32_t* w, uint32_t* h);
void    cc_mouse_delta(CCEngine* e, int32_t* dx, int32_t* dy);
/* Lock + hide the cursor for FPS mouselook (windowed only; no-op headless). */
void    cc_capture_mouse(CCEngine* e, bool capture);
QMod    cc_mods(CCEngine* e);
void    cc_input_inject_key(CCEngine* e, QKey k, bool down);
void    cc_input_inject_mouse_move(CCEngine* e, int32_t x, int32_t y);

/* ─── Timing ─────────────────────────────────────────────────────────── */
double   cc_time(CCEngine* e);
double   cc_delta(CCEngine* e);
uint64_t cc_frame_num(CCEngine* e);
uint64_t cc_now_ns(void);

/* ─── Log ────────────────────────────────────────────────────────────── */
typedef enum { CC_LOG_DEBUG=0, CC_LOG_INFO, CC_LOG_WARN, CC_LOG_ERROR } CCLogLevel;
void cc_log(CCLogLevel level, const char* fmt, ...);
#define CC_DEBUG(...) cc_log(CC_LOG_DEBUG,__VA_ARGS__)
#define CC_INFO(...)  cc_log(CC_LOG_INFO, __VA_ARGS__)
#define CC_WARN(...)  cc_log(CC_LOG_WARN, __VA_ARGS__)
#define CC_ERROR(...) cc_log(CC_LOG_ERROR,__VA_ARGS__)


void cc_upload_camera_matrices(CCEngine* e, const float* view, const float* proj, const float* view_proj, const float* inv_vp, const float* cam_pos_xyz);
#ifdef __cplusplus
}
#endif
