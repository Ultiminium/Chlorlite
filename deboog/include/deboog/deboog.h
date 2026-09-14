#ifndef DEBOOG_H
#define DEBOOG_H
/*
 * Deboog — mathematical & geometric debugging. The standard substrate.
 *
 * When an AI (or anyone) makes something spatial or numerical that needs debugging,
 * this is what you turn to. Deboog answers questions with EXACT NUMBERS, never with
 * a picture: is this mesh actually manifold? is this arm actually round or a flat
 * ribbon? is this matrix actually orthonormal? is there a NaN anywhere in here? is
 * this object actually where the math says it should be?
 *
 * DESIGN (so it's THE standard, not just one engine's helper):
 *   - PURE PRIMITIVES. Every function speaks float arrays, ints, matrices (float[16],
 *     column-major), quaternions (float[4], xyzw). NO engine types, NO structs you
 *     must adopt, NO allocation you don't control. Your engine writes a 10-line
 *     adapter that unpacks its own model into these primitives. (Same severability
 *     rule as a good input library: things depend on Deboog; Deboog depends on
 *     nothing but libm.)
 *   - RESULTS ARE DATA, not printouts. Functions fill result structs (counts, ratios,
 *     flags). A thin printer (deboog_report_*) is provided, but the numbers are the
 *     product — so this wraps trivially from Python/JS/etc.
 *   - TRUTH IS MATHEMATICAL. Deboog never looks at pixels. It measures the data.
 *
 * Units: distances/positions are in whatever unit the caller uses (results are in
 * the same unit or are unitless ratios). Angles in degrees unless noted.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 1. NUMERIC INTEGRITY ============================
 * The cheapest, highest-value check: is there garbage in this buffer of floats?
 * NaN and Inf are the silent killers (a single NaN in a transform → black frame,
 * as real engine bugs have shown). Scan anything: vertex arrays, matrices, weights. */
typedef struct {
    size_t count;         /* how many floats scanned */
    size_t nan_count;     /* # NaN */
    size_t inf_count;     /* # +/-Inf */
    size_t denormal_count;/* # subnormal (often a sign of underflow trouble) */
    double min, max;      /* finite min/max seen (ignoring nan/inf) */
    size_t first_bad_index; /* index of the first NaN/Inf, or SIZE_MAX */
    bool   ok;            /* true iff no NaN and no Inf */
} DbNumeric;
DbNumeric deboog_scan_floats(const float* data, size_t count);
/* range assertion: every value must be within [lo,hi]. */
typedef struct { size_t count, out_of_range; double worst; size_t worst_index; bool ok; } DbRange;
DbRange deboog_check_range(const float* data, size_t count, double lo, double hi);

/* ============================ 2. MATRIX / QUATERNION ==========================
 * Transform validity. A "valid" 4x4 model matrix should have no NaN, a sane
 * determinant (not ~0 → collapsed; not huge), and its upper-3x3 should be a proper
 * rotation*scale (columns orthogonal). Catches collapsed/mirrored/degenerate xforms. */
typedef struct {
    bool   finite;        /* no NaN/Inf */
    double determinant;
    bool   invertible;    /* |det| not ~0 */
    bool   orthonormal;   /* upper-3x3 columns orthogonal & unit (pure rotation) */
    double scale_x, scale_y, scale_z; /* recovered column lengths */
    bool   left_handed;   /* det < 0 → mirrored (often a bug) */
    bool   ok;            /* finite && invertible */
} DbMatrix;
DbMatrix deboog_check_matrix(const float m[16]);   /* column-major 4x4 */

typedef struct { double length; bool normalized; bool finite; bool ok; } DbQuat;
DbQuat deboog_check_quat(const float q[4]);         /* xyzw */

/* ============================ 3. GEOMETRY / TOPOLOGY ==========================
 * Mesh correctness from vertex+index data alone. Positions are a flat float array
 * of stride floats each (>=3), the xyz at offset 0. Indices are triangles (3 per). */
typedef struct {
    uint64_t vertex_count, triangle_count;
    uint64_t degenerate_tris;   /* zero-area */
    uint64_t boundary_edges;    /* used by exactly 1 tri → holes / open edges */
    uint64_t nonmanifold_edges; /* used by >2 tris → broken topology */
    uint64_t unused_vertices;   /* not referenced by any tri */
    uint64_t duplicate_positions;/* verts at (near) identical xyz */
    double   bbox_min[3], bbox_max[3], bbox_dim[3];
    bool     closed;            /* boundary_edges == 0 */
    bool     manifold;          /* nonmanifold_edges == 0 && no degenerates */
    bool     ok;                /* manifold (closed is reported but not required) */
} DbMesh;
DbMesh deboog_check_mesh(const float* positions, uint64_t vertex_count, uint32_t stride_floats,
                         const uint32_t* indices, uint64_t index_count);

/* ROUNDNESS / CROSS-SECTION — the ribbon detector, generalized. Given a set of
 * points and an axis (a line through axis_point in direction axis_dir), measure the
 * spread of the points in the two directions perpendicular to the axis. roundness =
 * min_spread/max_spread: 1.0 = round tube, ~0 = flat ribbon. Pass a limb's verts. */
typedef struct {
    uint32_t point_count;
    double   spread_a, spread_b; /* full width in the two perpendicular axes */
    double   roundness;          /* min/max of the two spreads, 0..1 */
    double   length_along_axis;  /* extent projected onto the axis */
    bool     is_ribbon;          /* roundness < ribbon_threshold */
    bool     ok;                 /* !is_ribbon */
} DbCrossSection;
DbCrossSection deboog_cross_section(const float* positions, uint32_t point_count, uint32_t stride_floats,
                                    const float axis_point[3], const float axis_dir[3],
                                    double ribbon_threshold /* e.g. 0.45 */);

/* SYMMETRY — mirror-symmetry of a point set across a plane (through plane_point,
 * normal plane_normal). Returns the fraction of points that have a mirror partner
 * within tolerance. 1.0 = perfectly symmetric. */
typedef struct { uint32_t point_count, matched; double symmetry; double max_error; bool ok; } DbSymmetry;
DbSymmetry deboog_symmetry(const float* positions, uint32_t point_count, uint32_t stride_floats,
                           const float plane_point[3], const float plane_normal[3],
                           double tolerance, double require /* e.g. 0.9 */);

/* SKIN WEIGHTS — GPU skinning validity. weights[i*4..+4] must sum to 1; joints must
 * be < bone_count; every vertex must have >0 total weight; report dead bones. */
typedef struct {
    uint32_t vertex_count;
    uint32_t unweighted;      /* zero total weight */
    uint32_t bad_sum;         /* sum not ~1.0 */
    uint32_t out_of_range;    /* joint index >= bone_count */
    uint32_t dead_bones;      /* bones with no vertex weighted to them */
    bool     ok;
} DbSkin;
DbSkin deboog_check_skin(const uint16_t* joints4, const float* weights4,
                         uint32_t vertex_count, uint32_t bone_count, double sum_tolerance);

/* ============================ 4. SPATIAL / COORDINATE =========================
 * The canonical frame: a FIXED reference so "where is it" is an exact coordinate,
 * comparable across every run. Project a world point through a view-projection
 * matrix into a fixed WxH frame with the origin locked at center. Signed offsets. */
typedef struct {
    bool  visible;        /* in front of camera (w>0) */
    double ndc_x, ndc_y;  /* -1..1 */
    int   x_off, y_off;   /* signed pixel offset from frame center, Y-up */
    int   rot_deg;        /* caller-supplied object rotation, normalized 0..359 */
} DbCanonPoint;
/* frame_w/h define the canonical frame (e.g. 580x720); origin is its center. */
DbCanonPoint deboog_canon_project(const float view_proj[16], const float world_pos[3],
                                  int frame_w, int frame_h, int rotation_deg);
/* format "±XXX-±YYY:RRR" into buf. */
void deboog_canon_format(const DbCanonPoint* p, char* buf, size_t n);

/* ============================ INVARIANTS =====================================
 * Assert mathematical facts that must ALWAYS hold, and get told the instant one
 * breaks (with a label), so you catch the cause not the symptom. Register checks
 * once; run them each frame; deboog_invariants_failed() is nonzero at the first
 * break. Zero-cost to compile out (wrap calls in your own DEBUG guard). */
typedef bool (*DbInvariantFn)(void* user);   /* return true = holds */
typedef struct DbInvariants DbInvariants;
DbInvariants* deboog_invariants_create(void);
void  deboog_invariants_destroy(DbInvariants*);
void  deboog_invariant_add(DbInvariants*, const char* label, DbInvariantFn fn, void* user);
/* run all; returns the number that FAILED this pass; fills first_failed_label. */
int   deboog_invariants_check(DbInvariants*, const char** first_failed_label);

/* ============================ REPORTING (optional) ===========================
 * Thin printers — the DATA above is the product; these just format it. Return the
 * number of FAILs so a caller can gate on it. Print to the given FILE* (stdout ok). */
#include <stdio.h>
int deboog_report_numeric(FILE*, const char* label, DbNumeric);
int deboog_report_matrix(FILE*, const char* label, DbMatrix);
int deboog_report_mesh(FILE*, const char* label, DbMesh);
int deboog_report_cross_section(FILE*, const char* label, DbCrossSection);
int deboog_report_skin(FILE*, const char* label, DbSkin);

/* version */
#define DEBOOG_VERSION "0.1.0"

/* Practical maximum vertex count for deboog_check_mesh. The checks are O(n)
   (spatial-hashed), so this is NOT an algorithmic wall — it's a sanity guard against
   corrupt/absurd inputs. The real limit is host memory. Set to 420,248,323,896 —
   well past uint32, so counts are 64-bit and the guard is a 64-bit compare. */
#define DEBOOG_MAX_VERTS 420248323896ULL

const char* deboog_version(void);

#ifdef __cplusplus
}
#endif
#endif /* DEBOOG_H */
