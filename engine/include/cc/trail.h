#pragma once
/*
 * CCTrail — motion trails / ribbons. A trail records recent world positions of a
 * moving thing (projectile, sword tip, vehicle, cursor, comet) and draws them as
 * a fading line/ribbon. Domain-general: useful for weapons, VFX, sports/racing
 * lines, pointer feedback, any "leave a streak behind" effect.
 *
 *   CCTrail* t = cc_trail_create(32, 0.6f);   // up to 32 points, 0.6s lifetime
 *   ... each frame:
 *       cc_trail_push(t, x, y, z);            // record current position
 *       cc_trail_update(t, dt);               // age points, drop expired
 *       cc_trail_draw(eng, t, r,g,b);         // draw the fading streak
 *   cc_trail_destroy(t);
 *
 * Points older than the lifetime are dropped automatically. push() ignores
 * near-duplicate positions so a stationary emitter doesn't fill the buffer.
 *
 * NOTE: cc_trail_draw emits world-space lines via the gizmo system, which is
 * flushed by cc_debug_overlay(eng). Call cc_debug_overlay once per frame after
 * your trails (and other gizmos) to composite them. (A dedicated non-debug line
 * pass could replace this later; the gizmo path is what the renderer exposes.)
 */
#include "cc/claudecore.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCTrail CCTrail;

/* max_points: ring-buffer capacity. lifetime: seconds a point persists. */
CCTrail* cc_trail_create(uint32_t max_points, float lifetime);
void     cc_trail_destroy(CCTrail* t);

/* record a world-space position (ignored if ~identical to the last one). */
void cc_trail_push(CCTrail* t, float x, float y, float z);
/* age all points by dt; expired points are removed. */
void cc_trail_update(CCTrail* t, float dt);
/* draw the trail as connected fading line segments (newest = brightest). */
void cc_trail_draw(CCEngine* eng, const CCTrail* t, float r, float g, float b);
/* forget all points (e.g. on teleport). */
void cc_trail_clear(CCTrail* t);
uint32_t cc_trail_point_count(const CCTrail* t);

#ifdef __cplusplus
}
#endif
