/*
 * canonframe.c — the canonical render frame (see cc/canonframe.h).
 *
 * Projects a world point using the engine's current camera, then remaps it into the
 * FIXED 580×720 space with C=0 locked at (290,360). Resolution-independent: the
 * actual render can be any size; the coordinate is always against the locked frame.
 */
#include "cc/canonframe.h"
#include "cc/claudecore.h"
#include "cc/render.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

extern void cc_renderer_camera_vp(CCRenderer*, float*);
extern CCRenderer* cc_engine_renderer(CCEngine*);

/* world → normalized device coords (-1..1), returns 0 if behind camera */
static int world_to_ndc(CCEngine* e, CCCanonVec3 w, float* ndc_x, float* ndc_y){
    CCRenderer* r = cc_engine_renderer(e); if(!r) return 0;
    float vp[16]; cc_renderer_camera_vp(r, vp);
    float x = vp[0]*w.x + vp[4]*w.y + vp[8]*w.z  + vp[12];
    float y = vp[1]*w.x + vp[5]*w.y + vp[9]*w.z  + vp[13];
    float wc= vp[3]*w.x + vp[7]*w.y + vp[11]*w.z + vp[15];
    if(wc <= 1e-4f) return 0;
    *ndc_x = x/wc;   /* -1..1, +right */
    *ndc_y = y/wc;   /* -1..1, +up (NDC is already y-up) */
    return 1;
}

CCCanonCoord cc_canon_coord(CCEngine* e, CCCanonVec3 world_pos){
    CCCanonCoord c; memset(&c,0,sizeof c);
    float nx, ny;
    if(!world_to_ndc(e, world_pos, &nx, &ny)){ c.visible=false; return c; }
    c.visible = true;
    /* NDC (-1..1) → signed offset from C=0 in the fixed frame.
       +X right = +nx * (W/2);  +Y up = +ny * (H/2). C=0 is the center, so the
       offset IS the scaled NDC (no origin add — the origin is 0 by construction). */
    c.x = (int)lroundf(nx * (CC_CANON_W * 0.5f));
    c.y = (int)lroundf(ny * (CC_CANON_H * 0.5f));
    c.on_frame = (c.x >= -CC_CANON_CX && c.x < CC_CANON_W-CC_CANON_CX &&
                  c.y >= -CC_CANON_CY && c.y < CC_CANON_H-CC_CANON_CY);
    return c;
}

void cc_canon_id_from_coord(CCCanonCoord c, int rot, char* out, uint32_t n){
    char xs = c.x<0?'-':'+', ys = c.y<0?'-':'+';
    int ax = c.x<0?-c.x:c.x, ay = c.y<0?-c.y:c.y;
    if(rot >= 0){
        int r = ((rot % 360) + 360) % 360;
        snprintf(out, n, "%c%03d-%c%03d:%d", xs, ax, ys, ay, r);
    } else {
        snprintf(out, n, "%c%03d-%c%03d", xs, ax, ys, ay);
    }
}

void cc_canon_id(CCEngine* e, CCCanonVec3 world_pos, int rot, char* out, uint32_t n){
    CCCanonCoord c = cc_canon_coord(e, world_pos);
    if(!c.visible){ snprintf(out, n, "OFFSCREEN"); return; }
    cc_canon_id_from_coord(c, rot, out, n);
}

/* ---- overlay: grid + concentric rotation rings + C=0 crosshair ----
   Drawn in canonical space but scaled to the actual render size so it lines up with
   what's on screen. Everything is expressed relative to the locked center. */
void cc_canon_overlay(CCEngine* e, int ring_count){
    CCRenderer* r = cc_engine_renderer(e); if(!r) return;
    uint32_t W=0,H=0; uint8_t* px=NULL; cc_frame_pixels(e,&px,&W,&H);
    if(!W){ W=CC_CANON_W; H=CC_CANON_H; }
    /* scale canonical (580×720) → actual render size */
    float sx = (float)W / CC_CANON_W, sy = (float)H / CC_CANON_H;
    float cx = CC_CANON_CX * sx, cy = CC_CANON_CY * sy;

    /* faint grid every 58px X / 72px Y in canonical space (10 divisions each way) */
    uint32_t grid_col = 0x2a2a3aff, axis_col = 0x4060a0ff, ring_col = 0x50c0e0ff;
    for(int gx=0; gx<=CC_CANON_W; gx+=58){
        float x = gx*sx; cc_draw_rect(e, (int)x, 0, 1, (int)H, grid_col, 0,0);
    }
    for(int gy=0; gy<=CC_CANON_H; gy+=72){
        float y = gy*sy; cc_draw_rect(e, 0, (int)y, (int)W, 1, grid_col, 0,0);
    }
    /* the two C=0 axes (brighter) */
    cc_draw_rect(e, (int)cx, 0, 1, (int)H, axis_col, 0,0);
    cc_draw_rect(e, 0, (int)cy, (int)W, 1, axis_col, 0,0);

    /* concentric rotation rings centered on C=0. radii step outward evenly to the
       nearest frame edge; drawn as point-circles. */
    if(ring_count < 1) ring_count = 1;
    float rmax = fminf(cx, cy) * 0.95f;
    for(int ring=1; ring<=ring_count; ring++){
        float rad = rmax * ring / ring_count;
        int steps = (int)(rad * 6.283f); if(steps<64)steps=64; if(steps>2000)steps=2000;
        for(int s=0;s<steps;s++){
            float th = (float)s/steps * 6.2831853f;
            int px_ = (int)(cx + cosf(th)*rad), py_ = (int)(cy - sinf(th)*rad);
            cc_draw_rect(e, px_, py_, 1, 1, ring_col, 0,0);
        }
    }
    /* degree ticks every 30° on the outer ring (longer marks at 0/90/180/270) */
    for(int deg=0; deg<360; deg+=30){
        float th = deg * 3.14159265f/180.0f;
        float r0 = rmax*0.90f, r1 = (deg%90==0)? rmax*1.02f : rmax*0.97f;
        for(float t=r0; t<=r1; t+=1.0f){
            int px_ = (int)(cx + cosf(th)*t), py_ = (int)(cy - sinf(th)*t);
            cc_draw_rect(e, px_, py_, 1, 1, ring_col, 0,0);
        }
    }
    /* C=0 marker */
    cc_draw_rect(e, (int)cx-2, (int)cy-2, 5, 5, 0xffd020ff, 0,0);
}

void cc_canon_stamp(CCEngine* e, CCCanonVec3 world_pos, int rot){
    extern CCFont cc_font_builtin(CCEngine*);
    char id[32]; cc_canon_id(e, world_pos, rot, id, sizeof id);
    CCCanonCoord c = cc_canon_coord(e, world_pos);
    CCRenderer* r = cc_engine_renderer(e);
    uint32_t W=0,H=0; uint8_t* px=NULL; cc_frame_pixels(e,&px,&W,&H); if(!W){W=CC_CANON_W;H=CC_CANON_H;}
    float sx=(float)W/CC_CANON_W, sy=(float)H/CC_CANON_H;
    int tx = (int)((CC_CANON_CX + c.x)*sx), ty = (int)((CC_CANON_CY - c.y)*sy);
    if(tx<2)tx=2; if(ty<2)ty=2;
    cc_draw_text(e, cc_font_builtin(e), id, tx, ty, 16, 0xffe060ff);
}
