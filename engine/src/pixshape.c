/* pixshape.c — CCPixelShape: the "a pixel is a diagonal, not a square" model.
 * The heart is cc_pixshape_halfplane_area: the EXACT area of the unit cell
 * [0,1]^2 lying on the object side of a line n·p = d. That fraction is the real
 * coverage of the diagonal cutting the cell — analytic, not sampled.
 * See cc/pixshape.h. Pure CPU, headless-safe; the GPU resolve mirrors this math.
 */
#include "cc/pixshape.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ─── exact half-plane area of the unit cell ──────────────────────────────────
 * We clip the unit square [0,1]x[0,1] against the half-plane { p : n·p >= d }
 * (object side) and return the clipped polygon's area. Sutherland–Hodgman on 4
 * corners — exact, branch-robust, and cheap. n need not be unit for the clip,
 * but callers pass a unit n so d is a true signed distance. */
float cc_pixshape_halfplane_area(float nx, float ny, float d) {
    /* unit square corners CCW */
    float px[8], py[8];
    float ix[4] = {0,1,1,0}, iy[4] = {0,0,1,1};
    int n_in = 4;
    for (int i=0;i<4;i++){ px[i]=ix[i]; py[i]=iy[i]; }

    float ox[8], oy[8]; int n_out = 0;
    for (int i=0;i<n_in;i++){
        int j = (i+1)%n_in;
        float d_i = nx*px[i] + ny*py[i] - d;   /* >=0 : inside (object side) */
        float d_j = nx*px[j] + ny*py[j] - d;
        int in_i = d_i >= 0.0f, in_j = d_j >= 0.0f;
        if (in_i){ ox[n_out]=px[i]; oy[n_out]=py[i]; n_out++; }
        if (in_i != in_j){
            float t = d_i / (d_i - d_j);       /* intersection param */
            ox[n_out]=px[i]+t*(px[j]-px[i]);
            oy[n_out]=py[i]+t*(py[j]-py[i]);
            n_out++;
        }
    }
    if (n_out < 3) return (n_out==0)?0.0f:0.0f;   /* fully outside → 0 */
    /* shoelace area */
    float a = 0.0f;
    for (int i=0;i<n_out;i++){
        int j=(i+1)%n_out;
        a += ox[i]*oy[j] - ox[j]*oy[i];
    }
    a = fabsf(a)*0.5f;
    if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
    return a;
}

/* ─── custom shape registry ───────────────────────────────────────────────── */
#define PIX_MAX_CUSTOM 32
static CCPixelCoverageFn g_custom_fn[PIX_MAX_CUSTOM];
static void*             g_custom_ud[PIX_MAX_CUSTOM];
static int               g_custom_count = 0;

int cc_pixshape_register(CCPixelCoverageFn fn, void* ud) {
    if (!fn || g_custom_count >= PIX_MAX_CUSTOM) return 0;
    int idx = g_custom_count++;
    g_custom_fn[idx] = fn; g_custom_ud[idx] = ud;
    return -(idx + 1);              /* custom ids are negative: -1, -2, ... */
}
void cc_pixshape_reset_custom(void) { g_custom_count = 0; }

/* ─── coverage dispatch ───────────────────────────────────────────────────── */
float cc_pixshape_coverage(const CCShapedPixel* p) {
    if (!p) return 0.0f;
    if (p->shape == CC_PIX_SQUARE) return p->inside ? 1.0f : 0.0f;
    if (p->shape < 0) {
        int idx = (-p->shape) - 1;
        if (idx >= 0 && idx < g_custom_count && g_custom_fn[idx])
            return g_custom_fn[idx](p, g_custom_ud[idx]);
        return p->cov;
    }
    if (p->shape == CC_PIX_DIAGONAL) {
        return cc_pixshape_halfplane_area(p->nx, p->ny, p->d);
    }
    if (p->shape == CC_PIX_TRIANGLE || p->shape == CC_PIX_WEDGE) {
        /* intersection of two half-planes ≈ min of the two coverages (exact for
         * the common corner cases; conservative otherwise). */
        float a = cc_pixshape_halfplane_area(p->nx,  p->ny,  p->d);
        float b = cc_pixshape_halfplane_area(p->nx2, p->ny2, p->d2);
        return a < b ? a : b;
    }
    return p->cov;
}

/* ─── build from an edge SDF sample ───────────────────────────────────────── */
/* signed_dist_px: distance from the CELL CENTER to the edge, in pixel units,
 * positive on the object side. (gx,gy): unit gradient pointing toward object.
 * The cell center is (0.5,0.5); a line with unit normal n through a point at
 * signed distance s from the center is n·(p-center) = s  →  n·p = s + n·center. */
CCShapedPixel cc_pixshape_from_edge(float signed_dist_px, float gx, float gy) {
    CCShapedPixel p; memset(&p, 0, sizeof(p));
    /* normalize gradient */
    float len = sqrtf(gx*gx + gy*gy);
    if (len < 1e-6f) {   /* no gradient → treat as full/empty square */
        p.shape = CC_PIX_SQUARE;
        p.inside = signed_dist_px >= 0.0f ? 1 : 0;
        p.cov = p.inside ? 1.0f : 0.0f;
        return p;
    }
    float nx = gx/len, ny = gy/len;
    /* if the edge is far outside the cell (|dist| > ~0.71 = half diagonal), the
     * cell is trivially full or empty → SQUARE (keeps interiors crisp). */
    if (signed_dist_px >  0.7072f) { p.shape=CC_PIX_SQUARE; p.inside=1; p.cov=1.0f; return p; }
    if (signed_dist_px < -0.7072f) { p.shape=CC_PIX_SQUARE; p.inside=0; p.cov=0.0f; return p; }
    /* line n·p = d. n points toward the object; the center is at signed distance
     * s on the object side, so the object half-plane is n·p >= n·center - s. */
    float d = nx*0.5f + ny*0.5f - signed_dist_px;
    p.shape = CC_PIX_DIAGONAL;
    p.nx = nx; p.ny = ny; p.d = d;
    p.cov = cc_pixshape_halfplane_area(nx, ny, d);
    return p;
}

CCShapedPixel cc_pixshape_square(bool inside) {
    CCShapedPixel p; memset(&p,0,sizeof(p));
    p.shape = CC_PIX_SQUARE; p.inside = inside?1:0; p.cov = inside?1.0f:0.0f;
    return p;
}

/* ─── final flatten: shaped pixel → display RGBA ──────────────────────────── */
static inline uint32_t lerp_rgba(uint32_t a, uint32_t b, float t) {
    if (t<=0) return a; if (t>=1) return b;
    float it = 1.0f - t;
    uint32_t ar=(a>>24)&255, ag=(a>>16)&255, ab=(a>>8)&255, aa=a&255;
    uint32_t br=(b>>24)&255, bg=(b>>16)&255, bb=(b>>8)&255, ba=b&255;
    uint32_t r=(uint32_t)(ar*it+br*t+0.5f), g=(uint32_t)(ag*it+bg*t+0.5f);
    uint32_t bl=(uint32_t)(ab*it+bb*t+0.5f), al=(uint32_t)(aa*it+ba*t+0.5f);
    return (r<<24)|(g<<16)|(bl<<8)|al;
}

uint32_t cc_pixshape_resolve(const CCShapedPixel* p, uint32_t obj_rgba, uint32_t bg_rgba) {
    float cov = cc_pixshape_coverage(p);
    /* cov is the fraction of the display cell the OBJECT covers along its true
     * shape. This is the single unavoidable flatten to the square grid. */
    return lerp_rgba(bg_rgba, obj_rgba, cov);
}
