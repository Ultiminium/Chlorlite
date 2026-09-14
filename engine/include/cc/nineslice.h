#pragma once
/*
 * CCNineSlice — nine-slice (9-patch) panels: a bordered box that scales to any
 * size without distorting its corners. The standard technique for menus, dialog
 * boxes, tooltips, buttons, health-bar frames — any resizable UI chrome.
 *
 * TWO forms:
 *  1. Texture 9-patch: slice a source texture into corners/edges/center by an
 *     inset (in source pixels) and stamp it into a destination rect. Corners
 *     stay native size; edges stretch along one axis; center fills.
 *       CCNineSlice ns = { panel_tex, tex_w, tex_h, 12,12,12,12 };  // border insets
 *       cc_nineslice_draw(eng, &ns, x,y,w,h, 0xFFFFFFFF);
 *
 *  2. Solid styled panel (no texture): a filled rounded-ish panel with a border
 *     of fixed thickness — corners never smear because the border is drawn as
 *     fixed-size rects.
 *       cc_panel_draw(eng, x,y,w,h, fill_rgba, border_px, border_rgba);
 *
 * All coords are pixels from top-left. Draw between cc_frame_begin/end, after
 * the 3D scene, like the other 2D UI.
 */
#include "cc/claudecore.h"
#include "cc/render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCNineSlice {
    CCTexture tex;
    float     tex_w, tex_h;                 /* source texture size in pixels */
    float     left, right, top, bottom;     /* border insets in source pixels */
} CCNineSlice;

/* Draw a texture 9-patch into destination rect (x,y,w,h), tinted by `tint`. */
void cc_nineslice_draw(CCEngine* eng, const CCNineSlice* ns,
                       float x, float y, float w, float h, uint32_t tint);

/* Solid styled panel: filled rect + fixed-thickness border (no corner smear). */
void cc_panel_draw(CCEngine* eng, float x, float y, float w, float h,
                   uint32_t fill, float border_px, uint32_t border_col);

#ifdef __cplusplus
}
#endif
