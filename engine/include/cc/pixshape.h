#pragma once
/*
 * CCPixelShape — the "a pixel is not a square" model. A pixel, render-side, is a
 * CELL THAT CARRIES A SHAPE, not a forced square. Most cells are trivially SQUARE
 * (fully inside or fully outside an object). An EDGE cell is literally split by a
 * diagonal: it stores the true edge line passing through it and which side is the
 * object. This shape data rides through the pipeline and only FLATTENS to the
 * physical square display grid at the very last step — so the visible boundary
 * follows the real slanted geometry instead of a stairstep of squares or a gray
 * fade.
 *
 * The pixel's SHAPE is what anti-aliasing actually is here:
 *   SQUARE   — the naive full cell (no AA).
 *   DIAGONAL — the cell is cut by one straight edge line (angle + offset); one
 *              side is object, the other background. The smooth-edge default.
 *   TRIANGLE / WEDGE — a corner cell cut by two edges (for convex corners).
 *   CUSTOM   — a user-registered shape: you provide the coverage function.
 *
 * A shape record is compact (shape id + edge line params). The final resolve
 * evaluates each cell's shape to composite object vs background exactly along the
 * stored line. This is analytic coverage carried as data, not a post blur.
 *
 * Pixel shape is per-OBJECT (see cc/aa.h + groups): different objects can carry
 * different pixel shapes. This file is the representation + the CPU resolve used
 * to flatten a shape buffer to a final RGBA image (headless-testable), plus the
 * math a GPU resolve mirrors.
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Built-in pixel shapes. Negative = custom (user-registered). */
typedef enum CCPixelShapeId {
    CC_PIX_SQUARE   = 0,   /* whole cell (no edge)                              */
    CC_PIX_DIAGONAL = 1,   /* cell cut by one edge line                         */
    CC_PIX_TRIANGLE = 2,   /* cell cut by two edges meeting inside (corner)     */
    CC_PIX_WEDGE    = 3,   /* thin sliver (two near-parallel edges)             */
    CC_PIX_CUSTOM   = -1,  /* user coverage fn                                  */
} CCPixelShapeId;

/* A shaped pixel. For SQUARE, coverage is 1 (object) or 0 (background) via
 * `inside`. For DIAGONAL/TRIANGLE/WEDGE, the edge line(s) are stored in
 * normalized cell space: the cell spans [0,1]x[0,1]; a line is n·p = d with unit
 * normal n=(nx,ny) pointing toward the OBJECT side. TRIANGLE/WEDGE use both
 * lines. `cov` caches the analytic coverage fraction (0..1) for fast resolve. */
typedef struct CCShapedPixel {
    int16_t shape;      /* CCPixelShapeId (int16 to pack)                        */
    uint8_t inside;     /* SQUARE: 1=object 0=background                         */
    uint8_t _pad;
    float   nx, ny, d;  /* primary edge line: n·p = d, n toward object          */
    float   nx2, ny2, d2;/* secondary edge (TRIANGLE/WEDGE)                      */
    float   cov;        /* cached coverage fraction 0..1                        */
} CCShapedPixel;

/* ─── coverage math (the exact diagonal split of a unit cell) ─────────────── */
/* Analytic area of the unit cell [0,1]^2 on the object side of the line
 * n·p = d (n unit, pointing to object). Returns 0..1. This IS "the pixel is a
 * diagonal": the fraction of the cell the object's geometry truly covers. */
float cc_pixshape_halfplane_area(float nx, float ny, float d);
/* Coverage for a shaped pixel (dispatches by shape; custom shapes via the
 * registered fn). */
float cc_pixshape_coverage(const CCShapedPixel* p);

/* Build a DIAGONAL shaped pixel from a signed distance field sample: given the
 * edge's signed distance at the cell center (in pixels, + = object side) and the
 * edge gradient direction (gx,gy, normalized), produce the cell's edge line. */
CCShapedPixel cc_pixshape_from_edge(float signed_dist_px, float gx, float gy);
CCShapedPixel cc_pixshape_square(bool inside);

/* ─── user-extensible shapes ──────────────────────────────────────────────── */
/* A custom coverage fn gets the shaped pixel and returns 0..1 object coverage. */
typedef float (*CCPixelCoverageFn)(const CCShapedPixel* p, void* userdata);
/* Register a custom shape; returns its (negative) shape id to store in pixels. */
int  cc_pixshape_register(CCPixelCoverageFn fn, void* userdata);
void cc_pixshape_reset_custom(void);   /* clear registrations (tests)          */

/* ─── final flatten (shape buffer → display RGBA) ─────────────────────────── */
/* Composite one shaped pixel: object color vs background color by its coverage.
 * Colors are 0xRRGGBBAA. This is the LAST step where a cell becomes a square. */
uint32_t cc_pixshape_resolve(const CCShapedPixel* p, uint32_t obj_rgba, uint32_t bg_rgba);

#ifdef __cplusplus
}
#endif
