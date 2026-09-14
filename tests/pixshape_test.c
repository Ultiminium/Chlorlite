/* pixshape_test — verifies the "pixel is a diagonal" coverage math is EXACT:
 * known half-plane areas of the unit cell (full/empty/half/corner-triangle),
 * edge-SDF → shaped pixel, custom shapes, and the final flatten. Pure logic.
 * Prints "PIXSHAPE TEST: all checks passed" / returns 0. */
#include "cc/pixshape.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
#define APPROX(a,b,eps) (fabsf((a)-(b)) < (eps))
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* a user-defined pixel shape: always 25% object coverage */
float quarter_cov(const CCShapedPixel* p, void* ud){ (void)p;(void)ud; return 0.25f; }

int main(void) {
    /* ── exact half-plane areas of the unit cell [0,1]^2 ────────────────── */
    /* line n·p = d, n toward object (area = fraction on n·p >= d side). */

    /* vertical line x = 0.5, object on +x side (n=(1,0), d=0.5) → half */
    CHECK(APPROX(cc_pixshape_halfplane_area(1,0,0.5f), 0.5f, 1e-5f), "vertical mid → 0.5");
    /* x=0.25 → object side is x>=0.25 → area 0.75 */
    CHECK(APPROX(cc_pixshape_halfplane_area(1,0,0.25f), 0.75f, 1e-5f), "vertical .25 → 0.75");
    /* horizontal y=0.5 → 0.5 */
    CHECK(APPROX(cc_pixshape_halfplane_area(0,1,0.5f), 0.5f, 1e-5f), "horizontal mid → 0.5");

    /* main DIAGONAL through the cell: line x+y = 1, n=(1,1)/√2, d=1/√2.
       object side x+y>=1 is the upper-right triangle → area 0.5. */
    float inv = 1.0f/sqrtf(2.0f);
    CHECK(APPROX(cc_pixshape_halfplane_area(inv,inv, inv), 0.5f, 1e-5f), "main diagonal → 0.5");

    /* small CORNER triangle: line x+y = 0.5 → object side x+y>=0.5 cuts off the
       lower-left triangle of legs 0.5 → that triangle area = 0.125, so object
       side = 1 - 0.125 = 0.875. */
    CHECK(APPROX(cc_pixshape_halfplane_area(inv,inv, 0.5f*inv), 0.875f, 1e-5f),
          "corner cut → 0.875");
    /* the complementary corner (n=(-1,-1)) at same line → 0.125 */
    CHECK(APPROX(cc_pixshape_halfplane_area(-inv,-inv, -0.5f*inv), 0.125f, 1e-5f),
          "opposite corner → 0.125");

    /* fully inside / outside */
    CHECK(APPROX(cc_pixshape_halfplane_area(1,0,-1.0f), 1.0f, 1e-5f), "line left of cell → full");
    CHECK(APPROX(cc_pixshape_halfplane_area(1,0, 2.0f), 0.0f, 1e-5f), "line right of cell → empty");

    /* symmetry: a 45° line at the center always splits 0.5 regardless of sign */
    CHECK(APPROX(cc_pixshape_halfplane_area(inv,-inv, 0.0f), 0.5f, 1e-5f), "anti-diagonal → 0.5");

    /* ── edge SDF → shaped pixel ─────────────────────────────────────────── */
    /* edge exactly at cell center, gradient +x → DIAGONAL with ~0.5 coverage */
    CCShapedPixel p = cc_pixshape_from_edge(0.0f, 1.0f, 0.0f);
    CHECK(p.shape == CC_PIX_DIAGONAL, "center edge → DIAGONAL shape");
    CHECK(APPROX(cc_pixshape_coverage(&p), 0.5f, 1e-4f), "center edge coverage ~0.5");

    /* far inside → SQUARE full */
    CCShapedPixel pin = cc_pixshape_from_edge(2.0f, 1.0f, 0.0f);
    CHECK(pin.shape == CC_PIX_SQUARE && cc_pixshape_coverage(&pin)==1.0f, "far inside → full square");
    /* far outside → SQUARE empty */
    CCShapedPixel pout = cc_pixshape_from_edge(-2.0f, 1.0f, 0.0f);
    CHECK(pout.shape == CC_PIX_SQUARE && cc_pixshape_coverage(&pout)==0.0f, "far outside → empty square");

    /* partial: edge a bit toward object side → coverage > 0.5 */
    CCShapedPixel pp = cc_pixshape_from_edge(0.25f, 1.0f, 0.0f);
    CHECK(cc_pixshape_coverage(&pp) > 0.5f && cc_pixshape_coverage(&pp) < 1.0f, "shifted edge partial");

    /* monotonic: sweeping the edge from -0.7..+0.7 gives increasing coverage */
    float prev = -1.0f; int mono = 1;
    for (float s=-0.7f; s<=0.7f; s+=0.1f){
        CCShapedPixel q = cc_pixshape_from_edge(s, 1.0f, 0.0f);
        float c = cc_pixshape_coverage(&q);
        if (c < prev - 1e-4f) mono = 0;
        prev = c;
    }
    CHECK(mono, "coverage increases monotonically as edge sweeps across the cell");

    /* ── final flatten (resolve to RGBA) ─────────────────────────────────── */
    uint32_t white=0xffffffff, black=0x000000ff;
    /* full square object → white */
    CHECK(cc_pixshape_resolve(&pin, white, black) == white, "full cell resolves to object");
    CHECK(cc_pixshape_resolve(&pout, white, black) == black, "empty cell resolves to bg");
    /* half diagonal → midway (0x7f-ish per channel) */
    uint32_t half = cc_pixshape_resolve(&p, white, black);
    uint32_t hr = (half>>24)&255;
    CHECK(hr > 100 && hr < 155, "half-diagonal resolves ~50% object");

    /* ── custom shape (user-extensible) ──────────────────────────────────── */
    cc_pixshape_reset_custom();
    int cid = cc_pixshape_register(quarter_cov, NULL);
    CHECK(cid < 0, "custom shape registers with a negative id");
    CCShapedPixel cp; cp.shape = (int16_t)cid; cp.cov = 0;
    CHECK(APPROX(cc_pixshape_coverage(&cp), 0.25f, 1e-5f), "custom shape coverage used");
    uint32_t cres = cc_pixshape_resolve(&cp, white, black);
    CHECK(((cres>>24)&255) > 40 && ((cres>>24)&255) < 90, "custom shape flattens at ~25%");
    cc_pixshape_reset_custom();

    if (failures == 0) {
        printf("PIXSHAPE TEST: all checks passed (exact half-plane coverage: half/quarter/"
               "corner-triangle/diagonal, edge-SDF→shape, monotonic sweep, flatten to RGBA)\n");
        return 0;
    }
    printf("PIXSHAPE TEST: %d check(s) FAILED\n", failures);
    return 1;
}
