#pragma once
/*
 * cc/world.h — Chlorlite World tier
 *
 * A spatial world of renderable objects with the "cull before you draw" layer
 * scene-heavy games need. Built on the frustum tests already in ccmath.h:
 *
 *   1. CCWorld       — a flat registry of objects (id, AABB, mesh+material, LODs)
 *   2. Octree        — loose spatial partition, rebuilt on demand, for O(log n)
 *                      frustum queries instead of O(n) brute force
 *   3. Frustum cull  — cc_world_cull() returns the visible object ids for a camera
 *   4. LOD           — per-object distance thresholds pick a mesh by camera distance
 *
 * The world does not own meshes/materials — it stores handles (CCMesh/CCMaterial
 * from render.h) plus a world-space AABB per object, and hands back a visible set
 * you iterate and draw. This keeps it renderer-agnostic and headless-testable.
 */

#include "cc/ccmath.h"
#include "cc/render.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_MAX_LODS 4

/* A single renderable placed in the world. */
typedef struct {
    CCVec3     position;                 /* world position */
    CCVec3     aabb_min, aabb_max;       /* world-space bounds (for culling) */
    CCMesh     lod_mesh[CC_MAX_LODS];    /* mesh per LOD level (0 = highest detail) */
    float      lod_distance[CC_MAX_LODS];/* switch to LOD i when dist >= this; last unused */
    uint32_t   lod_count;                /* number of valid LODs (>=1) */
    CCMaterial material;
    bool       visible;                  /* user enable flag (independent of culling) */
    void*      user;                     /* optional back-pointer for the game */
} CCWorldObject;

typedef struct CCWorld CCWorld;

CCWorld* cc_world_create(void);
void     cc_world_destroy(CCWorld* w);

/* Add an object; returns its id. Provide either a single mesh (lod_count=1) or
 * several via cc_world_object_add_lod after creation. AABB is world-space. */
uint32_t cc_world_add(CCWorld* w, CCMesh mesh, CCMaterial mat,
                      CCVec3 position, CCVec3 aabb_min, CCVec3 aabb_max);
void     cc_world_remove(CCWorld* w, uint32_t id);
void     cc_world_clear(CCWorld* w);
uint32_t cc_world_count(const CCWorld* w);

/* Add an LOD level to an existing object: `mesh` is used when the camera is at
 * least `distance` away. Levels should be added in increasing distance order. */
void cc_world_add_lod(CCWorld* w, uint32_t id, CCMesh mesh, float distance);

/* Move an object (updates its AABB by the delta and marks the octree dirty). */
void cc_world_set_position(CCWorld* w, uint32_t id, CCVec3 position);
void cc_world_set_visible(CCWorld* w, uint32_t id, bool visible);

const CCWorldObject* cc_world_get(const CCWorld* w, uint32_t id);

/* ─── Octree spatial index ───────────────────────────────────────────────
 * Rebuilt lazily on the next query after any add/remove/move. You can force it
 * with cc_world_rebuild(). max_depth/max_per_node tune the partition. */
void cc_world_rebuild(CCWorld* w);
void cc_world_set_octree_params(CCWorld* w, uint32_t max_depth, uint32_t max_per_node);

/* ─── Culling ────────────────────────────────────────────────────────────
 * Fill out_ids (capacity max_out) with the ids of objects whose AABB intersects
 * the frustum AND that are user-visible. Returns the count. Uses the octree to
 * skip whole subtrees outside the frustum. Pass the camera's view-projection. */
uint32_t cc_world_cull(CCWorld* w, CCMat4 view_proj,
                       uint32_t* out_ids, uint32_t max_out);

/* Brute-force cull (no octree) — same result, O(n); handy for validating the
 * octree path and for tiny worlds. */
uint32_t cc_world_cull_bruteforce(CCWorld* w, CCMat4 view_proj,
                                  uint32_t* out_ids, uint32_t max_out);

/* ─── LOD selection ──────────────────────────────────────────────────────
 * Pick the mesh for object `id` given the camera position: returns the LOD
 * mesh whose distance band contains dist(cam, object). out_lod (may be NULL)
 * receives the chosen LOD index. */
CCMesh cc_world_select_lod(const CCWorld* w, uint32_t id, CCVec3 camera_pos, uint32_t* out_lod);

/* Convenience stats from the last cull (visible / tested / culled counts). */
typedef struct { uint32_t visible, tested, culled, octree_nodes; } CCWorldCullStats;
CCWorldCullStats cc_world_cull_stats(const CCWorld* w);

#ifdef __cplusplus
}
#endif
