#pragma once
/*
 * CCWorldUI — screen-space UI anchored to world positions: health bars and
 * nameplates that float above characters, and damage/score numbers that pop up,
 * rise, and fade. Domain-general: RPGs, shooters, RTS, MOBAs, tower defense.
 *
 * Projection primitive:
 *   float sx, sy; bool vis;
 *   if (cc_world_to_screen(eng, wx,wy,wz, &sx, &sy, &vis) && vis)
 *       ... draw at (sx, sy) ...
 *
 * Convenience drawers (call between cc_frame_begin/end, after the 3D scene):
 *   cc_worldui_bar(eng, wx,wy,wz, 0.7f, ...)   // health bar at 70%
 *   cc_worldui_label(eng, wx,wy,wz, "Goblin", ...)
 *
 * Floating numbers are managed (they animate over time):
 *   CCFloaters* f = cc_floaters_create();
 *   cc_floaters_spawn(f, wx,wy,wz, "-24", 1.0f,0.3f,0.2f);  // red "-24"
 *   ... each frame: cc_floaters_update(f, dt); cc_floaters_draw(eng, f);
 */
#include "cc/claudecore.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Project a world point to screen pixels. Returns false if behind the camera.
 * *visible is set false if behind camera or outside the viewport (still returns
 * the projected coords when in front). Screen origin is top-left. */
bool cc_world_to_screen(CCEngine* eng, float wx, float wy, float wz,
                        float* out_sx, float* out_sy, bool* out_visible);

/* Draw a health/progress bar centered above a world point. `fill` is 0..1.
 * width/height in pixels; fg/bg are packed RGBA (see cc_draw_rect). y_offset
 * lifts it in pixels (e.g. above a character's head). */
void cc_worldui_bar(CCEngine* eng, float wx, float wy, float wz,
                    float fill, float width, float height, float y_offset,
                    uint32_t fg, uint32_t bg);

/* Draw a centered text label anchored to a world point. */
void cc_worldui_label(CCEngine* eng, float wx, float wy, float wz,
                      const char* text, float size, float y_offset, uint32_t color);

/* ─── floating numbers (damage/score pop-ups) ───────────────────────────── */
typedef struct CCFloaters CCFloaters;
CCFloaters* cc_floaters_create(void);
void        cc_floaters_destroy(CCFloaters* f);
/* spawn a floater at a world position; it rises and fades over `lifetime`. */
void cc_floaters_spawn(CCFloaters* f, float wx, float wy, float wz,
                       const char* text, float r, float g, float b);
void cc_floaters_update(CCFloaters* f, float dt);
void cc_floaters_draw(CCEngine* eng, const CCFloaters* f);
uint32_t cc_floaters_active(const CCFloaters* f);

#ifdef __cplusplus
}
#endif
