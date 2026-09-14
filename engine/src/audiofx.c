/* audiofx.c — CCAudioFX spatial audio shaping (occlusion + reverb zones).
 * See cc/audiofx.h. Pure CPU, deterministic, headless-safe. Occlusion is a grid
 * line-march counting wall cells between source and listener; reverb is a
 * smallest-containing-box lookup. Composes with cc/audio.h (game applies the
 * returned volume/lowpass/wet to instances / a reverb send).
 */
#include "cc/audiofx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FX_MAX_ZONES 64

typedef struct { CCReverbBox box; CCReverbParams params; bool used; } Zone;

struct CCAudioFX {
    CCGrid* grid;
    float   lx, lz;            /* listener XZ                                    */
    float   occ_min_volume;    /* volume when fully occluded                     */
    float   occ_per_cell;      /* muffle added per wall cell crossed             */
    Zone    zones[FX_MAX_ZONES];
    uint32_t zone_count;
};

/* ── presets ─────────────────────────────────────────────────────────────── */
CCReverbParams cc_reverb_none(void){ CCReverbParams r={0.0f,0.0f,0.0f}; return r; }
CCReverbParams cc_reverb_room(void){ CCReverbParams r={0.20f,0.6f,0.5f};  return r; }
CCReverbParams cc_reverb_hall(void){ CCReverbParams r={0.55f,2.4f,0.25f}; return r; }
CCReverbParams cc_reverb_cave(void){ CCReverbParams r={0.70f,3.8f,0.7f};  return r; }

/* ── lifecycle ───────────────────────────────────────────────────────────── */
CCAudioFX* cc_audiofx_create(void) {
    CCAudioFX* fx = (CCAudioFX*)calloc(1, sizeof(CCAudioFX));
    if (!fx) return NULL;
    fx->occ_min_volume = 0.25f;
    fx->occ_per_cell   = 0.35f;
    return fx;
}
void cc_audiofx_destroy(CCAudioFX* fx) { free(fx); }

void cc_audiofx_set_grid(CCAudioFX* fx, CCGrid* grid) { if (fx) fx->grid = grid; }
void cc_audiofx_set_occlusion(CCAudioFX* fx, float min_volume, float per_cell) {
    if (!fx) return;
    fx->occ_min_volume = min_volume < 0 ? 0 : (min_volume > 1 ? 1 : min_volume);
    fx->occ_per_cell   = per_cell < 0 ? 0 : per_cell;
}

/* ── reverb zones ────────────────────────────────────────────────────────── */
int cc_audiofx_add_zone(CCAudioFX* fx, CCReverbBox box, CCReverbParams params) {
    if (!fx) return -1;
    for (uint32_t i = 0; i < FX_MAX_ZONES; i++) {
        if (!fx->zones[i].used) {
            fx->zones[i].used = true;
            fx->zones[i].box = box;
            fx->zones[i].params = params;
            if (i + 1 > fx->zone_count) fx->zone_count = i + 1;
            return (int)i;
        }
    }
    return -1;
}
void cc_audiofx_clear_zones(CCAudioFX* fx) {
    if (!fx) return;
    memset(fx->zones, 0, sizeof(fx->zones));
    fx->zone_count = 0;
}
uint32_t cc_audiofx_zone_count(const CCAudioFX* fx) {
    if (!fx) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < FX_MAX_ZONES; i++) if (fx->zones[i].used) n++;
    return n;
}

void cc_audiofx_set_listener(CCAudioFX* fx, float x, float z) {
    if (fx) { fx->lx = x; fx->lz = z; }
}

/* ── occlusion: count wall cells on the source→listener line (DDA march) ───── */
static int walls_between(const CCAudioFX* fx, float sx, float sz, float ex, float ez) {
    if (!fx->grid) return 0;
    int32_t cx0, cz0, cx1, cz1;
    cc_grid_world_to_cell(fx->grid, sx, sz, &cx0, &cz0);
    cc_grid_world_to_cell(fx->grid, ex, ez, &cx1, &cz1);
    /* Bresenham-ish supercover-lite: step along the longer axis, sampling cells.
     * Endpoints themselves (source cell, listener cell) don't count as blockers. */
    int dx = abs((int)cx1 - (int)cx0), dz = abs((int)cz1 - (int)cz0);
    int sxs = cx0 < cx1 ? 1 : -1, szs = cz0 < cz1 ? 1 : -1;
    int err = dx - dz;
    int32_t x = cx0, z = cz0;
    int walls = 0, steps = 0, maxsteps = (dx + dz) + 2;
    while ((x != cx1 || z != cz1) && steps++ < maxsteps) {
        int e2 = 2 * err;
        if (e2 > -dz) { err -= dz; x += sxs; }
        if (e2 <  dx) { err += dx; z += szs; }
        if (x == cx1 && z == cz1) break;              /* reached listener cell   */
        if (cc_grid_is_blocked(fx->grid, x, z)) walls++;
    }
    return walls;
}

CCOcclusion cc_audiofx_occlusion(const CCAudioFX* fx, float sx, float sz) {
    CCOcclusion o = { 1.0f, 0.0f, false };
    if (!fx || !fx->grid) return o;
    int walls = walls_between(fx, sx, sz, fx->lx, fx->lz);
    if (walls <= 0) return o;                          /* clear line            */
    o.occluded = true;
    /* muffle grows with wall thickness, saturating at 1 */
    float muffle = walls * fx->occ_per_cell;
    if (muffle > 1.0f) muffle = 1.0f;
    o.lowpass = muffle;
    /* volume drops from 1 toward occ_min_volume as muffle → 1 */
    o.volume = 1.0f - muffle * (1.0f - fx->occ_min_volume);
    if (o.volume < fx->occ_min_volume) o.volume = fx->occ_min_volume;
    return o;
}

/* ── reverb: smallest containing zone ──────────────────────────────────────── */
static bool box_contains(const CCReverbBox* b, float x, float z) {
    return x >= b->min_x && x <= b->max_x && z >= b->min_z && z <= b->max_z;
}
static float box_area(const CCReverbBox* b) {
    float w = b->max_x - b->min_x, h = b->max_z - b->min_z;
    if (w < 0) w = 0; if (h < 0) h = 0;
    return w * h;
}
CCReverbParams cc_audiofx_reverb_at(const CCAudioFX* fx, float x, float z) {
    CCReverbParams best = cc_reverb_none();
    if (!fx) return best;
    float best_area = 1e30f;
    bool found = false;
    for (uint32_t i = 0; i < FX_MAX_ZONES; i++) {
        if (!fx->zones[i].used) continue;
        if (box_contains(&fx->zones[i].box, x, z)) {
            float a = box_area(&fx->zones[i].box);
            if (!found || a < best_area) { best_area = a; best = fx->zones[i].params; found = true; }
        }
    }
    return best;
}
CCReverbParams cc_audiofx_reverb_here(const CCAudioFX* fx) {
    if (!fx) return cc_reverb_none();
    return cc_audiofx_reverb_at(fx, fx->lx, fx->lz);
}
