#pragma once
/*
 * CCMove — collide-and-slide character movement. The response any game with a
 * moving player/AI needs: attempt to move by a vector, and when you hit a wall,
 * SLIDE along it instead of stopping dead (and stack multiple contacts, e.g. a
 * corner). Independent of the full rigid-body physics solver — you give it a
 * moving sphere/capsule and a set of static colliders (boxes + planes, the
 * bread-and-butter of level geometry), it returns the collided position.
 *
 *   CCMoveWorld* w = cc_move_world_create();
 *   cc_move_add_box(w, cx,cy,cz, hx,hy,hz);      // AABB collider
 *   cc_move_add_plane(w, nx,ny,nz, d);           // half-space (ground/walls)
 *   ...
 *   float np[3];
 *   CCMoveResult r = cc_move_slide(w, pos, radius, disp, np); // sphere of `radius`
 *   // np = new position after sliding; r tells you what was hit
 *   cc_move_world_destroy(w);
 *
 * Uses an iterative slide (a few passes) so motion into a corner resolves
 * against both surfaces. Grounded detection is reported for jump/coyote logic.
 */
#include "cc/ccmath.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCMoveWorld CCMoveWorld;

typedef struct CCMoveResult {
    bool  hit;          /* did we contact anything this move? */
    bool  grounded;     /* standing on a surface with an up-ish normal */
    float ground_y;     /* contact height if grounded (else unchanged) */
    uint32_t contacts;  /* number of surfaces contacted */
} CCMoveResult;

/* ─── world of static colliders ─── */
CCMoveWorld* cc_move_world_create(void);
void         cc_move_world_destroy(CCMoveWorld* w);
void         cc_move_clear(CCMoveWorld* w);
/* axis-aligned box: center + half-extents */
void cc_move_add_box(CCMoveWorld* w, float cx,float cy,float cz, float hx,float hy,float hz);
/* infinite plane / half-space: outward normal (nx,ny,nz) + offset d
   (points where dot(n,p) >= d are "outside"/solid boundary). */
void cc_move_add_plane(CCMoveWorld* w, float nx,float ny,float nz, float d);
uint32_t cc_move_collider_count(const CCMoveWorld* w);

/* ─── the move ───────────────────────────────────────────────────────────
 * Move a sphere of `radius` at `pos` by `disp`, sliding along contacts. Writes
 * the resolved position to out_pos[3] and returns contact info. */
CCMoveResult cc_move_slide(const CCMoveWorld* w, const float pos[3], float radius,
                           const float disp[3], float out_pos[3]);

#ifdef __cplusplus
}
#endif
