/*
 * world.c — Chlorlite World tier: spatial object registry + octree frustum
 * culling + distance LOD. Pure CPU, headless-safe (uses only ccmath frustum
 * tests; never touches GL). See cc/world.h for the contract.
 */
#include "cc/world.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ─── Octree node ────────────────────────────────────────────────────── */
typedef struct OctNode {
    CCVec3   center, half;          /* node bounds */
    uint32_t child[8];              /* indices into node pool, 0 = none (root is 0 but never a child) */
    uint32_t* ids;                  /* object ids stored at this node (leaf) */
    uint32_t  count, cap;
    bool      is_leaf;
} OctNode;

struct CCWorld {
    CCWorldObject* objs;
    bool*          used;
    uint32_t       cap, count, high_water;

    /* octree pool */
    OctNode*  nodes;
    uint32_t  node_count, node_cap;
    bool      dirty;                /* needs rebuild */
    uint32_t  max_depth, max_per_node;

    CCWorldCullStats stats;
};

CCWorld* cc_world_create(void) {
    CCWorld* w = calloc(1, sizeof(CCWorld));
    w->cap = 64; w->objs = calloc(w->cap, sizeof(CCWorldObject));
    w->used = calloc(w->cap, sizeof(bool));
    w->max_depth = 6; w->max_per_node = 8;
    w->dirty = true;
    return w;
}

static void free_nodes(CCWorld* w) {
    for (uint32_t i=0;i<w->node_count;i++) free(w->nodes[i].ids);
    free(w->nodes); w->nodes=NULL; w->node_count=0; w->node_cap=0;
}

void cc_world_destroy(CCWorld* w) {
    if (!w) return;
    free_nodes(w);
    free(w->objs); free(w->used); free(w);
}

static void grow(CCWorld* w, uint32_t need) {
    if (need <= w->cap) return;
    uint32_t nc = w->cap*2; while (nc < need) nc*=2;
    w->objs = realloc(w->objs, nc*sizeof(CCWorldObject));
    w->used = realloc(w->used, nc*sizeof(bool));
    memset(w->objs+w->cap, 0, (nc-w->cap)*sizeof(CCWorldObject));
    memset(w->used+w->cap, 0, (nc-w->cap)*sizeof(bool));
    w->cap = nc;
}

uint32_t cc_world_add(CCWorld* w, CCMesh mesh, CCMaterial mat,
                     CCVec3 position, CCVec3 aabb_min, CCVec3 aabb_max) {
    if (!w) return 0;
    /* find a free slot (ids are 1-based; 0 = invalid) */
    uint32_t id = 0;
    for (uint32_t i=0;i<w->high_water;i++) if (!w->used[i]) { id=i+1; break; }
    if (!id) { grow(w, w->high_water+1); id = ++w->high_water; }
    uint32_t idx = id-1;
    w->used[idx] = true; w->count++;
    CCWorldObject* o = &w->objs[idx];
    memset(o, 0, sizeof(*o));
    o->position = position;
    o->aabb_min = aabb_min; o->aabb_max = aabb_max;
    o->lod_mesh[0] = mesh; o->lod_distance[0] = 0.0f; o->lod_count = 1;
    o->material = mat; o->visible = true;
    w->dirty = true;
    return id;
}

void cc_world_remove(CCWorld* w, uint32_t id) {
    if (!w || id==0 || id>w->high_water || !w->used[id-1]) return;
    w->used[id-1] = false; w->count--; w->dirty = true;
}
void cc_world_clear(CCWorld* w) {
    if (!w) return;
    memset(w->used, 0, w->high_water*sizeof(bool));
    w->count=0; w->high_water=0; w->dirty=true;
}
uint32_t cc_world_count(const CCWorld* w){ return w?w->count:0; }

void cc_world_add_lod(CCWorld* w, uint32_t id, CCMesh mesh, float distance) {
    if (!w || id==0 || id>w->high_water || !w->used[id-1]) return;
    CCWorldObject* o=&w->objs[id-1];
    if (o->lod_count >= CC_MAX_LODS) return;
    o->lod_mesh[o->lod_count] = mesh;
    o->lod_distance[o->lod_count] = distance;
    o->lod_count++;
}

void cc_world_set_position(CCWorld* w, uint32_t id, CCVec3 p) {
    if (!w || id==0 || id>w->high_water || !w->used[id-1]) return;
    CCWorldObject* o=&w->objs[id-1];
    CCVec3 d = vec3_sub(p, o->position);
    o->position = p;
    o->aabb_min = vec3_add(o->aabb_min, d);
    o->aabb_max = vec3_add(o->aabb_max, d);
    w->dirty = true;
}
void cc_world_set_visible(CCWorld* w, uint32_t id, bool v) {
    if (!w || id==0 || id>w->high_water || !w->used[id-1]) return;
    w->objs[id-1].visible = v;
}
const CCWorldObject* cc_world_get(const CCWorld* w, uint32_t id) {
    if (!w || id==0 || id>w->high_water || !w->used[id-1]) return NULL;
    return &w->objs[id-1];
}

void cc_world_set_octree_params(CCWorld* w, uint32_t max_depth, uint32_t max_per_node) {
    if (!w) return;
    w->max_depth = max_depth?max_depth:1;
    w->max_per_node = max_per_node?max_per_node:1;
    w->dirty = true;
}

/* ─── Octree build ───────────────────────────────────────────────────── */
static uint32_t node_alloc(CCWorld* w, CCVec3 c, CCVec3 h) {
    if (w->node_count >= w->node_cap) {
        w->node_cap = w->node_cap?w->node_cap*2:64;
        w->nodes = realloc(w->nodes, w->node_cap*sizeof(OctNode));
    }
    OctNode* n=&w->nodes[w->node_count];
    memset(n,0,sizeof(*n));
    n->center=c; n->half=h; n->is_leaf=true;
    return w->node_count++;
}
static void node_add_id(OctNode* n, uint32_t id) {
    if (n->count>=n->cap){ n->cap=n->cap?n->cap*2:8; n->ids=realloc(n->ids,n->cap*sizeof(uint32_t)); }
    n->ids[n->count++]=id;
}
/* Does an AABB fit inside a node's bounds (loose containment by center)? We use
 * the object center to pick the octant; loose octree keeps objects that straddle
 * a boundary at the parent. */
static int octant_for(OctNode* n, CCVec3 p) {
    int o=0;
    if (p.x >= n->center.x) o|=1;
    if (p.y >= n->center.y) o|=2;
    if (p.z >= n->center.z) o|=4;
    return o;
}
static void subdivide(CCWorld* w, uint32_t ni, uint32_t depth);

static void node_insert(CCWorld* w, uint32_t ni, uint32_t id, uint32_t depth) {
    OctNode* n=&w->nodes[ni];
    if (n->is_leaf) {
        node_add_id(n, id);
        if (n->count > w->max_per_node && depth < w->max_depth) subdivide(w, ni, depth);
        return;
    }
    /* internal: route by object center */
    CCVec3 c = w->objs[id-1].position;
    int oc = octant_for(n, c);
    node_insert(w, n->child[oc], id, depth+1);
}
static void subdivide(CCWorld* w, uint32_t ni, uint32_t depth) {
    /* create 8 children, redistribute ids by center */
    CCVec3 c, h;
    { OctNode* n=&w->nodes[ni]; c=n->center; h=vec3_scale(n->half,0.5f); }
    uint32_t children[8];
    for (int i=0;i<8;i++){
        CCVec3 cc = { c.x + ((i&1)?h.x:-h.x), c.y + ((i&2)?h.y:-h.y), c.z + ((i&4)?h.z:-h.z) };
        children[i] = node_alloc(w, cc, h);   /* may realloc w->nodes */
    }
    OctNode* n=&w->nodes[ni]; /* re-fetch after possible realloc */
    memcpy(n->child, children, sizeof(children));
    n->is_leaf=false;
    uint32_t cnt=n->count; uint32_t* ids=n->ids;
    n->ids=NULL; n->count=n->cap=0;
    for (uint32_t k=0;k<cnt;k++) node_insert(w, ni, ids[k], depth);
    free(ids);
}

void cc_world_rebuild(CCWorld* w) {
    if (!w) return;
    free_nodes(w);
    /* compute world bounds over all used objects */
    if (w->count==0){ w->dirty=false; return; }
    CCVec3 mn={1e30f,1e30f,1e30f}, mx={-1e30f,-1e30f,-1e30f};
    for (uint32_t i=0;i<w->high_water;i++){
        if (!w->used[i]) continue;
        CCWorldObject* o=&w->objs[i];
        mn.x=fminf(mn.x,o->aabb_min.x); mn.y=fminf(mn.y,o->aabb_min.y); mn.z=fminf(mn.z,o->aabb_min.z);
        mx.x=fmaxf(mx.x,o->aabb_max.x); mx.y=fmaxf(mx.y,o->aabb_max.y); mx.z=fmaxf(mx.z,o->aabb_max.z);
    }
    CCVec3 center={ (mn.x+mx.x)*0.5f,(mn.y+mx.y)*0.5f,(mn.z+mx.z)*0.5f };
    CCVec3 half={ fmaxf((mx.x-mn.x)*0.5f,0.5f), fmaxf((mx.y-mn.y)*0.5f,0.5f), fmaxf((mx.z-mn.z)*0.5f,0.5f) };
    node_alloc(w, center, half); /* root = node 0 */
    for (uint32_t i=0;i<w->high_water;i++)
        if (w->used[i]) node_insert(w, 0, i+1, 0);
    w->dirty=false;
}

/* ─── Culling ────────────────────────────────────────────────────────── */
typedef struct { uint32_t* out; uint32_t max, n; CCFrustum* f; CCWorld* w; uint32_t tested; } CullCtx;

static void cull_node(CullCtx* cx, uint32_t ni) {
    OctNode* n=&cx->w->nodes[ni];
    CCVec3 nmn={n->center.x-n->half.x,n->center.y-n->half.y,n->center.z-n->half.z};
    CCVec3 nmx={n->center.x+n->half.x,n->center.y+n->half.y,n->center.z+n->half.z};
    if (!frustum_test_aabb(cx->f, nmn, nmx)) return;  /* whole subtree outside */
    if (n->is_leaf) {
        for (uint32_t k=0;k<n->count;k++){
            uint32_t id=n->ids[k]; CCWorldObject* o=&cx->w->objs[id-1];
            cx->tested++;
            if (!o->visible) continue;
            if (frustum_test_aabb(cx->f, o->aabb_min, o->aabb_max)) {
                if (cx->n < cx->max) cx->out[cx->n++]=id;
            }
        }
    } else {
        for (int i=0;i<8;i++) cull_node(cx, n->child[i]);
    }
}

uint32_t cc_world_cull(CCWorld* w, CCMat4 vp, uint32_t* out, uint32_t max_out) {
    if (!w || !out) return 0;
    if (w->dirty) cc_world_rebuild(w);
    CCFrustum f = frustum_from_vp(vp);
    CullCtx cx = { out, max_out, 0, &f, w, 0 };
    if (w->node_count>0 && w->count>0) cull_node(&cx, 0);
    w->stats.visible = cx.n; w->stats.tested = cx.tested;
    w->stats.culled = (w->count>cx.n)?(w->count-cx.n):0;
    w->stats.octree_nodes = w->node_count;
    return cx.n;
}

uint32_t cc_world_cull_bruteforce(CCWorld* w, CCMat4 vp, uint32_t* out, uint32_t max_out) {
    if (!w || !out) return 0;
    CCFrustum f = frustum_from_vp(vp);
    uint32_t n=0, tested=0;
    for (uint32_t i=0;i<w->high_water;i++){
        if (!w->used[i]) continue;
        CCWorldObject* o=&w->objs[i];
        tested++;
        if (!o->visible) continue;
        if (frustum_test_aabb(&f, o->aabb_min, o->aabb_max))
            if (n<max_out) out[n++]=i+1;
    }
    w->stats.visible=n; w->stats.tested=tested;
    w->stats.culled=(w->count>n)?(w->count-n):0; w->stats.octree_nodes=0;
    return n;
}

/* ─── LOD ────────────────────────────────────────────────────────────── */
CCMesh cc_world_select_lod(const CCWorld* w, uint32_t id, CCVec3 cam, uint32_t* out_lod) {
    const CCWorldObject* o = cc_world_get(w, id);
    if (!o) { if(out_lod)*out_lod=0; return 0; }
    float dx=cam.x-o->position.x, dy=cam.y-o->position.y, dz=cam.z-o->position.z;
    float dist=sqrtf(dx*dx+dy*dy+dz*dz);
    uint32_t chosen=0;
    for (uint32_t i=0;i<o->lod_count;i++)
        if (dist >= o->lod_distance[i]) chosen=i;   /* last band whose threshold we've passed */
    if (out_lod) *out_lod=chosen;
    return o->lod_mesh[chosen];
}

CCWorldCullStats cc_world_cull_stats(const CCWorld* w) {
    CCWorldCullStats z={0}; return w?w->stats:z;
}
