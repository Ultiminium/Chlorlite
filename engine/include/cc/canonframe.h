#pragma once
/*
 * cc/canonframe.h — the CANONICAL RENDER FRAME: one fixed, shared coordinate space
 * every object is measured in, so alignment is an EXACT number, never "looks centered".
 *
 * The frame is a 580×720 grid of 1×1 pixel cubes. Its center C=0 is LOCKED at pixel
 * (290, 360) and is NOT configurable — every measurement originates there, even if the
 * rendered model is visually offset. Coordinates are SIGNED offsets from C=0, Y-UP:
 *     X ∈ [-290, +289]   (+X = right)
 *     Y ∈ [-360, +359]   (+Y = up)
 * C=0 itself is 000-000.
 *
 * An object's positional ID is its offset in this frame, written "XYZ-ABC[:rot]":
 *     XYZ = signed X, ABC = signed Y, rot = rotation degrees 0..359 (optional).
 *   e.g.  "+000-+000"        object dead-center, no rotation
 *         "+050-+030:190"    50 right, 30 up, rotated 190°
 *         "-120--045"        120 left, 45 down
 *
 * CRITICAL: the ID is computed from the object's ACTUAL world transform/geometry,
 * projected into this fixed frame — it is a data-derived FACT, not a pixel reading.
 * The overlay (grid + concentric rotation rings at C=0) merely DISPLAYS that truth.
 * This is the alignment analogue of modelcheck: measure the data, show the render.
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CCEngine;
typedef struct { float x,y,z; } CCCanonVec3;   /* mirror of CCVec3 to avoid include order issues */

/* The fixed, non-configurable frame constants. */
#define CC_CANON_W       580
#define CC_CANON_H       720
#define CC_CANON_CX      290      /* C=0 x pixel (W/2)  */
#define CC_CANON_CY      360      /* C=0 y pixel (H/2)  */

/* A canonical coordinate: signed offset from C=0, Y-up. Also reports whether the
   point is in front of the camera (behind → not projectable). */
typedef struct {
    int  x;          /* -290..+289, +right */
    int  y;          /* -360..+359, +up    */
    bool on_frame;   /* within the 580×720 bounds */
    bool visible;    /* in front of camera (projectable) */
} CCCanonCoord;

/* Project a world position into the canonical frame. Uses the engine's current
   camera matrices, then remaps into the fixed 580×720 space with C=0 at center.
   The frame is resolution-independent: whatever the actual render size, the result
   is expressed against the locked 580×720 / (290,360) origin. */
CCCanonCoord cc_canon_coord(struct CCEngine* eng, CCCanonVec3 world_pos);

/* Format a positional ID string "XYZ-ABC" (no rotation) or "XYZ-ABC:rot".
   Pass rotation_deg < 0 to omit the :rot suffix. `out` must hold >= 24 bytes.
   Signs are explicit (+/-) and magnitudes zero-padded to 3 digits. */
void cc_canon_id(struct CCEngine* eng, CCCanonVec3 world_pos,
                 int rotation_deg, char* out, uint32_t out_size);

/* Format an ID directly from an already-computed coord (no projection). */
void cc_canon_id_from_coord(CCCanonCoord c, int rotation_deg, char* out, uint32_t out_size);

/* Draw the canonical overlay into the current frame: the C=0 crosshair, a light
   grid, and the concentric ROTATION RINGS (with degree ticks) centered on C=0.
   `ring_count` inner rings (>=1). Purely a visual aid over the locked frame. */
void cc_canon_overlay(struct CCEngine* eng, int ring_count);

/* Convenience: after computing an object's ID, stamp it as text near C=0 or near
   the object's projected position, so a render carries its own coordinate label. */
void cc_canon_stamp(struct CCEngine* eng, CCCanonVec3 world_pos, int rotation_deg);

#ifdef __cplusplus
}
#endif
