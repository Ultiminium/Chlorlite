#pragma once
/*
 * cc/editmesh.h — Chlorlite editable mesh (half-edge topology)
 *
 * The modeling substrate for CC. Where cc_mesh_* gives fixed primitives baked
 * straight to GPU buffers, CCEditMesh is a MUTABLE mesh with real topology:
 * vertices, edges, and faces are all first-class, addressable by stable id, and
 * queryable. That is what makes correct, Blender-style editing possible — you
 * can insert a vertex in the middle of an edge or face and the surrounding
 * faces re-stitch correctly, rather than leaving a hole.
 *
 * DESIGNED FOR CLAUDE, NOT A MOUSE. Claude does not click-drag; it works in a
 * perceive→act loop: emit precise ops, bake, screenshot, inspect, correct. So
 * every element has a stable integer id Claude can name and reason about
 * ("split edge 12; move the new vertex to (1,0.5,1)"), the topology is fully
 * inspectable in text between renders (cc_editmesh_dump), and edits are
 * deterministic. This is the "edit mode" that fits how Claude actually models.
 *
 * Coordinate grid: vertex positions are free floats in model space. An optional
 * snap (cc_editmesh_set_snap) rounds new/moved positions to a lattice so Claude
 * can get clean, exact coordinates when it wants them, and turn it off for
 * organic shapes. The grid is a coordinate convention, not a constraint.
 *
 * Half-edge model (standard): each face is bounded by a ring of half-edges;
 * each half-edge points to its origin vertex, its next around the face, and its
 * twin on the adjacent face (or none on a boundary). Edges pair two half-edges.
 *
 * Typical loop:
 *   CCEditMesh* m = cc_editmesh_cube(1.0f);          // 8 verts, 6 quad faces
 *   uint32_t e = cc_editmesh_find_edge(m, v0, v1);
 *   uint32_t nv = cc_editmesh_split_edge(m, e, 0.5f);// add a vertex mid-edge
 *   cc_editmesh_move_vertex(m, nv, (CCVec3){1,0.5,1});
 *   cc_editmesh_extrude_face(m, f, 0.5f);            // pull a face out
 *   CCMesh gpu = cc_editmesh_bake(m, eng);           // → renderable mesh
 */

#include "cc/ccmath.h"
#include "cc/render.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_EM_INVALID 0xFFFFFFFFu   /* "no element" sentinel */

typedef struct CCEditMesh CCEditMesh;

/* ─── Lifecycle ──────────────────────────────────────────────────────── */
CCEditMesh* cc_editmesh_new(void);
void        cc_editmesh_free(CCEditMesh* m);
CCEditMesh* cc_editmesh_copy(const CCEditMesh* m);

/* ─── Starter shapes (topologically clean, ready to edit) ────────────── */
CCEditMesh* cc_editmesh_cube(float size);          /* 8 verts, 6 quads */
CCEditMesh* cc_editmesh_plane(float size, uint32_t subdiv);
CCEditMesh* cc_editmesh_from_mesh_data(const CCVertex* verts, uint32_t nv,
                                       const uint32_t* idx, uint32_t ni); /* triangles → faces */

/* ─── Coordinate grid ────────────────────────────────────────────────── */
/* spacing<=0 disables snapping. When enabled, new/moved vertex positions are
 * rounded to the nearest multiple of `spacing` on each axis. */
void  cc_editmesh_set_snap(CCEditMesh* m, float spacing);
float cc_editmesh_snap(const CCEditMesh* m);      /* 0 = off */
CCVec3 cc_editmesh_apply_snap(const CCEditMesh* m, CCVec3 p); /* snap a coord (respects toggle) */

/* ─── Building from scratch ──────────────────────────────────────────── */
/* Add a free-standing vertex at a coordinate; returns its id. */
uint32_t cc_editmesh_add_vertex(CCEditMesh* m, CCVec3 position);
/* Add a face from an ordered CCW ring of >=3 existing vertex ids; returns face
 * id. Half-edges/edges are created and twinned to any existing neighbours. */
uint32_t cc_editmesh_add_face(CCEditMesh* m, const uint32_t* vert_ids, uint32_t count);

/* ─── The core request: add points anywhere ──────────────────────────── */
/* Split edge `e` by inserting a new vertex at parametric t in [0,1] along it
 * (0=start vertex, 1=end). BOTH faces sharing the edge are re-triangulated so
 * the new vertex is wired into the surrounding topology (no hole). Returns the
 * new vertex id. This is "add a point in the middle of a side". */
uint32_t cc_editmesh_split_edge(CCEditMesh* m, uint32_t e, float t);
/* Poke a face: add a vertex at (or near) its centroid and fan the face into
 * triangles around it. Returns the new center vertex id. This is "add a point
 * in the middle of a face". */
uint32_t cc_editmesh_poke_face(CCEditMesh* m, uint32_t f);
/* Connect two existing vertices of the SAME face with a new edge, splitting the
 * face in two. Returns the new edge id (or CC_EM_INVALID if not co-facial). */
uint32_t cc_editmesh_connect_verts(CCEditMesh* m, uint32_t va, uint32_t vb);

/* ─── Transform / move points ────────────────────────────────────────── */
void cc_editmesh_move_vertex(CCEditMesh* m, uint32_t v, CCVec3 to);       /* absolute (snapped) */
void cc_editmesh_translate_vertex(CCEditMesh* m, uint32_t v, CCVec3 by);  /* relative */
/* Extrude a face along its normal by `distance`, creating side walls; the face
 * ring is duplicated and pushed out. Returns the new (top) face id. */
uint32_t cc_editmesh_extrude_face(CCEditMesh* m, uint32_t f, float distance);
/* Bevel a vertex: replace it with a small face/edges inset by `amount`. */
void cc_editmesh_bevel_vertex(CCEditMesh* m, uint32_t v, float amount);
/* Catmull-Clark-style smooth subdivision of the whole mesh, `iters` times. */
void cc_editmesh_subdivide(CCEditMesh* m, uint32_t iters);

/* ─── Deletion ───────────────────────────────────────────────────────── */
void cc_editmesh_delete_face(CCEditMesh* m, uint32_t f);
void cc_editmesh_delete_vertex(CCEditMesh* m, uint32_t v); /* + incident faces */

/* ─── Query / inspection (how Claude "sees" topology in text) ─────────── */
uint32_t cc_editmesh_vertex_count(const CCEditMesh* m);
uint32_t cc_editmesh_face_count(const CCEditMesh* m);
uint32_t cc_editmesh_edge_count(const CCEditMesh* m);
bool     cc_editmesh_vertex_valid(const CCEditMesh* m, uint32_t v);
CCVec3   cc_editmesh_vertex_position(const CCEditMesh* m, uint32_t v);
uint32_t cc_editmesh_find_edge(const CCEditMesh* m, uint32_t va, uint32_t vb); /* CC_EM_INVALID if none */
/* Fill out_ids with the vertex ids around face f (CCW); returns the count. */
uint32_t cc_editmesh_face_vertices(const CCEditMesh* m, uint32_t f,
                                   uint32_t* out_ids, uint32_t max_out);
/* Vertices within `radius` of a coordinate — the addressable analogue of
 * "click near here". Returns count; fills out_ids up to max_out. */
uint32_t cc_editmesh_pick_vertices(const CCEditMesh* m, CCVec3 near, float radius,
                                   uint32_t* out_ids, uint32_t max_out);
/* Print a human/Claude-readable topology summary to stdout (counts + each
 * vertex coord + each face's vertex ring). `max_elems` caps the listing. */
void cc_editmesh_dump(const CCEditMesh* m, uint32_t max_elems);
/* Validate half-edge invariants (twin/next/vertex consistency). Returns true if
 * the mesh is well-formed; writes the first problem to `err` if non-NULL. */
bool cc_editmesh_validate(const CCEditMesh* m, char* err, uint32_t err_len);

/* ─── Bake to a renderable mesh ──────────────────────────────────────── */
/* Triangulate all faces, recompute normals (smooth or flat) + tangents, and
 * upload via cc_mesh_create. Returns the CCMesh handle. */
CCMesh cc_editmesh_bake(CCEditMesh* m, CCEngine* eng, bool smooth_normals);
/* Bake into caller-provided CPU buffers instead of the GPU (for inspection /
 * further processing). Returns triangle count; writes vertex/index counts. */
uint32_t cc_editmesh_bake_cpu(CCEditMesh* m, bool smooth_normals,
                              CCVertex** out_verts, uint32_t* out_nv,
                              uint32_t** out_idx, uint32_t* out_ni);

#ifdef __cplusplus
}
#endif
