/* soundfield.c — CCSoundField spatial sound propagation. See cc/soundfield.h.
 *
 * Perceived loudness is flood-filled through WALKABLE grid cells from every
 * emitted sound. Each cell stores the loudest value reached and the neighbour it
 * came from (so a listener can be told which way the source lies). Attenuation
 * is falloff-per-world-unit along the propagation PATH, so walls (blocked cells)
 * force sound to detour and arrive quieter — occlusion for free.
 *
 * Algorithm: a max-loudness Dijkstra. Because loudness decreases monotonically
 * with path distance, the first time a cell is popped with the highest loudness
 * it is final. We use a simple binary heap keyed by loudness (descending).
 */
#include "cc/soundfield.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct CCSoundField {
    CCGrid*  grid;
    uint32_t w, h;
    float    cell_size;

    float*   loud;      /* w*h perceived loudness, 0 = silent               */
    int8_t*  from_dx;   /* neighbour offset this cell's loudness came from   */
    int8_t*  from_dz;   /* (points back toward the source)                   */

    /* heap of (cell index, loudness) */
    uint32_t* heap_idx;
    float*    heap_val;
    uint32_t  heap_n, heap_cap;

    float falloff;      /* loudness lost per world unit                      */
    float floor;        /* inaudible threshold                               */

    /* bus */
    CCEventBus* bus;
    CCListenerId listener;
    bool  have_mic_pos;
    float mic_x, mic_z;

    /* pending sounds from the bus, drained at begin() */
    struct { float x, z, loud; }* pending;
    uint32_t pending_n, pending_cap;
};

/* ── heap helpers (max-heap on loudness) ─────────────────────────────────── */
static void heap_push(CCSoundField* sf, uint32_t idx, float val) {
    if (sf->heap_n == sf->heap_cap) {
        uint32_t nc = sf->heap_cap ? sf->heap_cap*2 : 256;
        sf->heap_idx = (uint32_t*)realloc(sf->heap_idx, nc*sizeof(uint32_t));
        sf->heap_val = (float*)   realloc(sf->heap_val, nc*sizeof(float));
        sf->heap_cap = nc;
    }
    uint32_t i = sf->heap_n++;
    sf->heap_idx[i] = idx; sf->heap_val[i] = val;
    while (i > 0) {
        uint32_t p = (i-1)/2;
        if (sf->heap_val[p] >= sf->heap_val[i]) break;
        uint32_t ti=sf->heap_idx[p]; sf->heap_idx[p]=sf->heap_idx[i]; sf->heap_idx[i]=ti;
        float    tv=sf->heap_val[p]; sf->heap_val[p]=sf->heap_val[i]; sf->heap_val[i]=tv;
        i = p;
    }
}
static bool heap_pop(CCSoundField* sf, uint32_t* out_idx, float* out_val) {
    if (sf->heap_n == 0) return false;
    *out_idx = sf->heap_idx[0]; *out_val = sf->heap_val[0];
    sf->heap_n--;
    if (sf->heap_n > 0) {
        sf->heap_idx[0] = sf->heap_idx[sf->heap_n];
        sf->heap_val[0] = sf->heap_val[sf->heap_n];
        uint32_t i = 0;
        for (;;) {
            uint32_t l=2*i+1, r=2*i+2, m=i;
            if (l<sf->heap_n && sf->heap_val[l]>sf->heap_val[m]) m=l;
            if (r<sf->heap_n && sf->heap_val[r]>sf->heap_val[m]) m=r;
            if (m==i) break;
            uint32_t ti=sf->heap_idx[m]; sf->heap_idx[m]=sf->heap_idx[i]; sf->heap_idx[i]=ti;
            float    tv=sf->heap_val[m]; sf->heap_val[m]=sf->heap_val[i]; sf->heap_val[i]=tv;
            i = m;
        }
    }
    return true;
}

CCSoundField* cc_soundfield_create(CCGrid* grid) {
    if (!grid) return NULL;
    CCSoundField* sf = (CCSoundField*)calloc(1, sizeof(CCSoundField));
    if (!sf) return NULL;
    sf->grid = grid;
    sf->w = cc_grid_width(grid);
    sf->h = cc_grid_height(grid);
    /* recover cell_size via two cell->world mappings */
    float x0,z0,x1,z1;
    cc_grid_cell_to_world(grid, 0,0,&x0,&z0);
    cc_grid_cell_to_world(grid, (sf->w>1)?1:0, 0, &x1, &z1);
    sf->cell_size = (sf->w>1) ? fabsf(x1-x0) : 1.0f;
    if (sf->cell_size <= 0.0f) sf->cell_size = 1.0f;

    uint32_t n = sf->w * sf->h;
    sf->loud    = (float*) calloc(n, sizeof(float));
    sf->from_dx = (int8_t*)calloc(n, sizeof(int8_t));
    sf->from_dz = (int8_t*)calloc(n, sizeof(int8_t));
    sf->falloff = 0.06f;
    sf->floor   = 0.02f;
    return sf;
}

void cc_soundfield_destroy(CCSoundField* sf) {
    if (!sf) return;
    if (sf->bus && sf->listener) cc_event_unsubscribe(sf->bus, sf->listener);
    free(sf->loud); free(sf->from_dx); free(sf->from_dz);
    free(sf->heap_idx); free(sf->heap_val);
    free(sf->pending);
    free(sf);
}

void cc_soundfield_set_falloff(CCSoundField* sf, float per_unit) { if (sf) sf->falloff = per_unit>0?per_unit:0; }
void cc_soundfield_set_floor(CCSoundField* sf, float floor)      { if (sf) sf->floor = floor>0?floor:0; }

/* clamp a world point to the nearest in-bounds, walkable cell index; -1 if none */
static int32_t cell_of(CCSoundField* sf, float wx, float wz) {
    int32_t cx, cz;
    cc_grid_world_to_cell(sf->grid, wx, wz, &cx, &cz);
    if (cx < 0) cx = 0; if (cx >= (int32_t)sf->w) cx = sf->w-1;
    if (cz < 0) cz = 0; if (cz >= (int32_t)sf->h) cz = sf->h-1;
    if (!cc_grid_is_blocked(sf->grid, cx, cz))
        return cz*(int32_t)sf->w + cx;
    /* nearest walkable in a small ring */
    for (int32_t r=1; r<=3; ++r)
        for (int32_t dz=-r; dz<=r; ++dz)
            for (int32_t dx=-r; dx<=r; ++dx) {
                int32_t nx=cx+dx, nz=cz+dz;
                if (nx<0||nz<0||nx>=(int32_t)sf->w||nz>=(int32_t)sf->h) continue;
                if (!cc_grid_is_blocked(sf->grid, nx, nz))
                    return nz*(int32_t)sf->w + nx;
            }
    return -1;
}

void cc_soundfield_begin(CCSoundField* sf) {
    if (!sf) return;
    uint32_t n = sf->w * sf->h;
    memset(sf->loud, 0, n*sizeof(float));
    memset(sf->from_dx, 0, n*sizeof(int8_t));
    memset(sf->from_dz, 0, n*sizeof(int8_t));
    sf->heap_n = 0;
    /* drain bus-delivered sounds queued since last frame */
    for (uint32_t i=0;i<sf->pending_n;i++)
        cc_soundfield_emit(sf, sf->pending[i].x, sf->pending[i].z, sf->pending[i].loud);
    sf->pending_n = 0;
}

void cc_soundfield_emit(CCSoundField* sf, float wx, float wz, float loudness) {
    if (!sf || loudness <= sf->floor) return;
    if (loudness > 1.0f) loudness = 1.0f;
    int32_t c = cell_of(sf, wx, wz);
    if (c < 0) return;
    if (loudness > sf->loud[c]) {
        sf->loud[c] = loudness;
        sf->from_dx[c] = 0; sf->from_dz[c] = 0;   /* source cell: no direction */
        heap_push(sf, (uint32_t)c, loudness);
    }
}

void cc_soundfield_propagate(CCSoundField* sf, float max_travel) {
    if (!sf) return;
    (void)max_travel;   /* bound is implicit via floor + falloff */
    const int dxs[8] = {  1,-1, 0, 0,  1, 1,-1,-1 };
    const int dzs[8] = {  0, 0, 1,-1,  1,-1, 1,-1 };
    uint32_t idx; float val;
    while (heap_pop(sf, &idx, &val)) {
        if (val < sf->loud[idx]) continue;         /* stale heap entry */
        if (val <= sf->floor) continue;
        int32_t cx = idx % sf->w, cz = idx / sf->w;
        for (int k=0;k<8;k++) {
            int32_t nx = cx+dxs[k], nz = cz+dzs[k];
            if (nx<0||nz<0||nx>=(int32_t)sf->w||nz>=(int32_t)sf->h) continue;
            if (cc_grid_is_blocked(sf->grid, nx, nz)) continue;
            /* diagonal: don't cut through a blocked orthogonal corner */
            if (k>=4) {
                if (cc_grid_is_blocked(sf->grid, cx, nz) &&
                    cc_grid_is_blocked(sf->grid, nx, cz)) continue;
            }
            float step = (k>=4 ? 1.41421356f : 1.0f) * sf->cell_size;
            float nl = val - sf->falloff * step;
            if (nl <= sf->floor) continue;
            uint32_t ni = (uint32_t)(nz*(int32_t)sf->w + nx);
            if (nl > sf->loud[ni]) {
                sf->loud[ni] = nl;
                /* direction back toward source = opposite of the step we took */
                sf->from_dx[ni] = (int8_t)(-dxs[k]);
                sf->from_dz[ni] = (int8_t)(-dzs[k]);
                heap_push(sf, ni, nl);
            }
        }
    }
}

float cc_soundfield_loudness(const CCSoundField* sf, float ex, float ez) {
    if (!sf) return 0.0f;
    int32_t cx, cz;
    cc_grid_world_to_cell(sf->grid, ex, ez, &cx, &cz);
    if (cx<0||cz<0||cx>=(int32_t)sf->w||cz>=(int32_t)sf->h) return 0.0f;
    return sf->loud[cz*(int32_t)sf->w + cx];
}

bool cc_soundfield_sample(const CCSoundField* sf, float ex, float ez,
                          float* out_loud, CCVec3* out_dir) {
    if (!sf) { if(out_loud)*out_loud=0; if(out_dir)*out_dir=(CCVec3){0,0,0}; return false; }
    int32_t cx, cz;
    cc_grid_world_to_cell(sf->grid, ex, ez, &cx, &cz);
    if (cx<0||cz<0||cx>=(int32_t)sf->w||cz>=(int32_t)sf->h) {
        if(out_loud)*out_loud=0; if(out_dir)*out_dir=(CCVec3){0,0,0}; return false;
    }
    uint32_t i = (uint32_t)(cz*(int32_t)sf->w + cx);
    float l = sf->loud[i];
    if (out_loud) *out_loud = l;
    if (out_dir) {
        float dx = (float)sf->from_dx[i], dz = (float)sf->from_dz[i];
        float len = sqrtf(dx*dx + dz*dz);
        if (len > 1e-6f) *out_dir = (CCVec3){ dx/len, 0.0f, dz/len };
        else             *out_dir = (CCVec3){ 0,0,0 };
    }
    return l > sf->floor;
}

/* ── bus watching ────────────────────────────────────────────────────────── */
static void push_pending(CCSoundField* sf, float x, float z, float loud) {
    if (sf->pending_n == sf->pending_cap) {
        uint32_t nc = sf->pending_cap ? sf->pending_cap*2 : 32;
        sf->pending = realloc(sf->pending, nc*sizeof(*sf->pending));
        sf->pending_cap = nc;
    }
    sf->pending[sf->pending_n].x = x;
    sf->pending[sf->pending_n].z = z;
    sf->pending[sf->pending_n].loud = loud;
    sf->pending_n++;
}
/* CC_EVT_SOUND packs XZ into sender as two float16-ish? Simpler + robust: we
 * encode X,Z as two int16 (metres*8) in the low/high 32 bits of sender. */
static void sf_on_event(const CCEvent* e, void* ud) {
    CCSoundField* sf = (CCSoundField*)ud;
    if (e->channel == CC_EVT_SOUND) {
        int32_t xi = (int32_t)(uint32_t)(e->sender & 0xffffffffu);
        int32_t zi = (int32_t)(uint32_t)((e->sender >> 32) & 0xffffffffu);
        push_pending(sf, xi / 8.0f, zi / 8.0f, e->f);
    } else if (e->channel == CC_EVT_MIC_LEVEL && sf->have_mic_pos) {
        push_pending(sf, sf->mic_x, sf->mic_z, e->f);
    }
}
void cc_soundfield_watch_bus(CCSoundField* sf, CCEventBus* bus) {
    if (!sf) return;
    if (sf->bus && sf->listener) { cc_event_unsubscribe(sf->bus, sf->listener); sf->listener=0; }
    sf->bus = bus;
    if (bus) sf->listener = cc_event_subscribe_any(bus, sf_on_event, sf);
}
void cc_soundfield_set_mic_position(CCSoundField* sf, float wx, float wz) {
    if (!sf) return;
    sf->have_mic_pos = true; sf->mic_x = wx; sf->mic_z = wz;
}
