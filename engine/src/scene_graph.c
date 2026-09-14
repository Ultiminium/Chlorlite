/*
 * scene_graph.c — hierarchical transform nodes (parent-child)
 *
 * A CCNode has a local transform (TRS) and computes its world transform by
 * composing with its parent's. Attach meshes/lights/cameras to nodes; moving a
 * parent moves all children. This is the standard scene-graph layer that sits
 * above the flat ECS for things that need hierarchy (skeletons, vehicles,
 * articulated props, attachment points).
 */
#include "cc/ccmath.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define CC_MAX_NODES 8192

typedef struct CCNode {
    CCVec3 local_pos;
    CCQuat local_rot;
    CCVec3 local_scale;
    CCMat4 world;           /* computed */
    int32_t parent;         /* -1 = root */
    int32_t first_child;
    int32_t next_sibling;
    uint32_t mesh_id;       /* optional attached mesh */
    uint32_t material_id;
    bool   dirty;
    bool   used;
    char   name[32];
} CCNode;

typedef struct CCSceneGraph {
    CCNode  nodes[CC_MAX_NODES];
    uint32_t count;
} CCSceneGraph;

CCSceneGraph* cc_scenegraph_create(void) {
    CCSceneGraph* g = calloc(1, sizeof(CCSceneGraph));
    g->count = 1;  /* 0 = null */
    return g;
}
void cc_scenegraph_destroy(CCSceneGraph* g) { free(g); }

uint32_t cc_node_create(CCSceneGraph* g, const char* name, int32_t parent) {
    uint32_t slot = 0;
    for (uint32_t i = 1; i < g->count; i++) if (!g->nodes[i].used) { slot = i; break; }
    if (!slot) { if (g->count >= CC_MAX_NODES) return 0; slot = g->count++; }
    CCNode* n = &g->nodes[slot];
    memset(n, 0, sizeof(*n));
    n->local_pos = (CCVec3){0,0,0};
    n->local_rot = (CCQuat){0,0,0,1};
    n->local_scale = (CCVec3){1,1,1};
    n->parent = parent;
    n->first_child = -1;
    n->next_sibling = -1;
    n->dirty = true;
    n->used = true;
    if (name) strncpy(n->name, name, sizeof(n->name)-1);
    /* link into parent's child list */
    if (parent > 0 && (uint32_t)parent < g->count && g->nodes[parent].used) {
        n->next_sibling = g->nodes[parent].first_child;
        g->nodes[parent].first_child = (int32_t)slot;
    }
    return slot;
}

void cc_node_destroy(CCSceneGraph* g, uint32_t id) {
    if (id == 0 || id >= g->count || !g->nodes[id].used) return;
    /* recursively destroy children */
    int32_t c = g->nodes[id].first_child;
    while (c > 0) { int32_t nxt = g->nodes[c].next_sibling; cc_node_destroy(g, (uint32_t)c); c = nxt; }
    g->nodes[id].used = false;
}

void cc_node_set_position(CCSceneGraph* g, uint32_t id, CCVec3 p) {
    if (id>0 && id<g->count && g->nodes[id].used) { g->nodes[id].local_pos=p; g->nodes[id].dirty=true; }
}
void cc_node_set_rotation(CCSceneGraph* g, uint32_t id, CCQuat q) {
    if (id>0 && id<g->count && g->nodes[id].used) { g->nodes[id].local_rot=q; g->nodes[id].dirty=true; }
}
void cc_node_set_scale(CCSceneGraph* g, uint32_t id, CCVec3 s) {
    if (id>0 && id<g->count && g->nodes[id].used) { g->nodes[id].local_scale=s; g->nodes[id].dirty=true; }
}
void cc_node_set_mesh(CCSceneGraph* g, uint32_t id, uint32_t mesh, uint32_t material) {
    if (id>0 && id<g->count && g->nodes[id].used) { g->nodes[id].mesh_id=mesh; g->nodes[id].material_id=material; }
}

void cc_node_set_parent(CCSceneGraph* g, uint32_t id, int32_t new_parent) {
    if (id==0 || id>=g->count || !g->nodes[id].used) return;
    CCNode* n = &g->nodes[id];
    /* unlink from old parent */
    if (n->parent > 0) {
        CCNode* op = &g->nodes[n->parent];
        if (op->first_child == (int32_t)id) op->first_child = n->next_sibling;
        else {
            int32_t c = op->first_child;
            while (c > 0 && g->nodes[c].next_sibling != (int32_t)id) c = g->nodes[c].next_sibling;
            if (c > 0) g->nodes[c].next_sibling = n->next_sibling;
        }
    }
    /* link to new parent */
    n->parent = new_parent;
    if (new_parent > 0 && (uint32_t)new_parent < g->count) {
        n->next_sibling = g->nodes[new_parent].first_child;
        g->nodes[new_parent].first_child = (int32_t)id;
    } else n->next_sibling = -1;
    n->dirty = true;
}

/* Recompute world matrices top-down. Call once per frame after moving nodes. */
static void update_node(CCSceneGraph* g, uint32_t id, const CCMat4* parent_world, bool parent_dirty) {
    CCNode* n = &g->nodes[id];
    bool d = n->dirty || parent_dirty;
    if (d) {
        CCMat4 local = mat4_trs(n->local_pos, n->local_rot, n->local_scale);
        n->world = parent_world ? mat4_mul(*parent_world, local) : local;
        n->dirty = false;
    }
    for (int32_t c = n->first_child; c > 0; c = g->nodes[c].next_sibling)
        update_node(g, (uint32_t)c, &n->world, d);
}

void cc_scenegraph_update(CCSceneGraph* g) {
    for (uint32_t i = 1; i < g->count; i++)
        if (g->nodes[i].used && g->nodes[i].parent <= 0)
            update_node(g, i, NULL, false);
}

/* World-space position of a node (after update). */
CCVec3 cc_node_world_position(CCSceneGraph* g, uint32_t id) {
    if (id>0 && id<g->count && g->nodes[id].used)
        return (CCVec3){ g->nodes[id].world.m[12], g->nodes[id].world.m[13], g->nodes[id].world.m[14] };
    return (CCVec3){0,0,0};
}
const float* cc_node_world_matrix(CCSceneGraph* g, uint32_t id) {
    if (id>0 && id<g->count && g->nodes[id].used) return g->nodes[id].world.m;
    return NULL;
}
uint32_t cc_node_mesh(CCSceneGraph* g, uint32_t id, uint32_t* out_material) {
    if (id>0 && id<g->count && g->nodes[id].used) {
        if (out_material) *out_material = g->nodes[id].material_id;
        return g->nodes[id].mesh_id;
    }
    return 0;
}
uint32_t cc_scenegraph_node_count(CCSceneGraph* g) {
    uint32_t n=0; for(uint32_t i=1;i<g->count;i++) if(g->nodes[i].used) n++; return n;
}
