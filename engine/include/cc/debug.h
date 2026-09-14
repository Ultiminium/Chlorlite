#pragma once
/* cc/debug.h — Seeing & verifying: stats, JSON dumps, gizmos, debug views. */
#include "ccmath.h"
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CCEngine CCEngine;

/* Per-frame stats (from the frame just ended). */
typedef struct { uint32_t draw_calls, triangles, meshes_drawn, verts, lights, textures; size_t tex_bytes; } CCFrameStats;
CCFrameStats cc_debug_stats(CCEngine* e);

/* Dump this frame's draw calls + scene as JSON (returns malloc'd string; caller frees, or NULL). */
char* cc_debug_drawcalls_json(CCEngine* e);
char* cc_debug_scene_json(CCEngine* e);      /* meshes, materials, transforms, lights */
void  cc_debug_dump_json(CCEngine* e, const char* path);  /* writes scene+stats+draws to file */

/* Debug render views. 0=normal 1=wireframe 2=normals 3=depth 4=albedo 5=overdraw 6=lighting-only */
typedef enum { CC_VIEW_NORMAL=0, CC_VIEW_WIREFRAME, CC_VIEW_NORMALS, CC_VIEW_DEPTH,
               CC_VIEW_ALBEDO, CC_VIEW_OVERDRAW, CC_VIEW_LIGHTING } CCDebugView;
void cc_debug_set_view(CCEngine* e, CCDebugView v);

/* Gizmos — drawn as overlay lines this frame (cleared each frame_begin). */
void cc_gizmo_line(CCEngine* e, CCVec3 a, CCVec3 b, CCVec3 color);
void cc_gizmo_box(CCEngine* e, CCVec3 center, CCVec3 half, CCVec3 color);
void cc_gizmo_sphere(CCEngine* e, CCVec3 center, float radius, CCVec3 color);
void cc_gizmo_arrow(CCEngine* e, CCVec3 from, CCVec3 to, CCVec3 color);
void cc_gizmo_cross(CCEngine* e, CCVec3 at, float size, CCVec3 color);
void cc_gizmo_label(CCEngine* e, CCVec3 at, const char* text, CCVec3 color);

/* On-screen debug overlay (draw-call/tri/light/texmem counters). Call after your draws. */
void cc_debug_overlay(CCEngine* e);

/* Screenshot provenance: burn seed + build hash into the corner + emit a manifest. */
void cc_debug_set_seed(CCEngine* e, uint64_t seed);
void cc_debug_stamp(CCEngine* e);   /* draws seed/hash/frame text in corner */
const char* cc_screenshot_manifest(CCEngine* e, const char* png_path, const char* cam_desc);

/* Object-ID picking: which drawlog entry (object) is at pixel (x,y). 0 = none. */
uint32_t cc_debug_pick(CCEngine* e, int x, int y);

/* Auto-frame a point/entity: computes a camera that frames a bounding sphere. */
void cc_debug_frame_point(CCEngine* e, CCVec3 center, float radius, float yaw_deg, float pitch_deg,
                          float* out_view16, float* out_proj16);
/* Orbit capture: writes N framed screenshots around center. Returns count written. */
int cc_debug_orbit_capture(CCEngine* e, CCVec3 center, float radius, int shots, const char* dir);
#ifdef __cplusplus
}
#endif

/* Visualize all lights as range spheres + count (call in-frame before overlay). */
void cc_debug_draw_lights(CCEngine* e);
/* Freecam: mutate an eye/target with WASD+arrows style deltas; returns view matrix. */
typedef struct { CCVec3 pos; float yaw, pitch; } CCFreecam;
void cc_freecam_update(CCEngine* e, CCFreecam* cam, float dt, float move, float look);
void cc_freecam_view(const CCFreecam* cam, float* out_view16, CCVec3* out_target);
/* Walked-heatmap: accumulate positions, render as gizmo crosses colored by visit count. */
void cc_heatmap_mark(CCEngine* e, CCVec3 pos);
void cc_heatmap_draw(CCEngine* e);
/* PNG diff: writes highlight PNG, returns % pixels changed (-1 on error). */
double cc_screenshot_diff(const char* a_png, const char* b_png, const char* out_highlight_png);
/* Regression snapshot: compare shot to golden; if golden absent, save it. Returns 0=match/new,1=differ. */
int cc_regression_check(CCEngine* e, const char* golden_png, double tolerance_pct);
