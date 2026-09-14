#pragma once
/*
 * CCActor — an ergonomic game-object handle over the ECS.
 *
 * The master-list goal: "kill the parallel arrays." Instead of a game keeping
 * lockstep arrays of transforms / meshes / materials (Prop props[N]), each game
 * object is a CCActor: one handle bundling its ECS entity + scene, with plain
 * setters for the things game code actually touches — position, rotation, scale,
 * mesh, material, visibility, name, parent. It is NOT a parallel system: an
 * actor IS an ECS entity with the built-in CCTransform + CCMeshComp components,
 * so cc_scene_render() draws actors for free and raw ECS queries still see them.
 *
 * Typical use:
 *   CCScene* world = cc_scene_create(eng, "level");     // ECS world (ecs.h)
 *   CCActor a = cc_actor_spawn(world, mesh, mat, 0,0,0);
 *   cc_actor_set_rotation_y(a, 45);
 *   cc_actor_set_scale(a, 2,2,2);
 *   cc_actor_set_name(a, "door_01");
 *   ... each frame: cc_scene_render(eng, world);        // draws all actors
 *   CCActor found = cc_actor_find(world, "door_01");
 *
 * A CCActor is a small value (copyable). It is "valid" while its entity is
 * alive; cc_actor_valid() checks. CC_ACTOR_NULL is the empty handle.
 */
#include "cc/ecs.h"
#include "cc/render.h"   /* CCMesh, CCMaterial, CCTransform3D */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCActor {
    CCScene*   scene;   /* the ECS world this actor lives in */
    CCEntityId id;      /* the underlying entity */
} CCActor;

#define CC_ACTOR_NULL ((CCActor){0})

/* ─── lifecycle ─────────────────────────────────────────────────────────── */
/* Spawn an actor with a transform + mesh renderer at (x,y,z). mesh/mat may be 0
 * (an empty actor you fill in later, e.g. a logic-only or grouping node). */
CCActor cc_actor_spawn(CCScene* scene, CCMesh mesh, CCMaterial mat, float x, float y, float z);
/* Spawn a transform-only actor (no mesh) — useful as a parent/pivot or a marker. */
CCActor cc_actor_spawn_empty(CCScene* scene, float x, float y, float z);
void    cc_actor_destroy(CCActor a);
bool    cc_actor_valid(CCActor a);

/* ─── identity ──────────────────────────────────────────────────────────── */
void        cc_actor_set_name(CCActor a, const char* name);
const char* cc_actor_name(CCActor a);
CCActor     cc_actor_find(CCScene* scene, const char* name);

/* ─── transform (position / rotation / scale) ───────────────────────────── */
void cc_actor_set_position(CCActor a, float x, float y, float z);
void cc_actor_get_position(CCActor a, float* x, float* y, float* z);
void cc_actor_translate(CCActor a, float dx, float dy, float dz);
void cc_actor_set_rotation(CCActor a, float rx, float ry, float rz);   /* Euler degrees */
void cc_actor_set_rotation_y(CCActor a, float deg);
void cc_actor_rotate_y(CCActor a, float deg);
void cc_actor_set_scale(CCActor a, float sx, float sy, float sz);
void cc_actor_set_uniform_scale(CCActor a, float s);
/* Read the actor's transform into a renderer CCTransform3D (quaternion form). */
bool cc_actor_get_transform(CCActor a, CCTransform3D* out);

/* ─── appearance ────────────────────────────────────────────────────────── */
void       cc_actor_set_mesh(CCActor a, CCMesh mesh);
void       cc_actor_set_material(CCActor a, CCMaterial mat);
CCMesh     cc_actor_mesh(CCActor a);
CCMaterial cc_actor_material(CCActor a);
/* Visibility: hidden actors keep their components but are skipped by rendering
 * (implemented by zeroing the mesh id in the renderer's view via a flag comp). */
void cc_actor_set_visible(CCActor a, bool visible);
bool cc_actor_visible(CCActor a);
void cc_actor_set_shadow(CCActor a, bool cast_shadow, bool receive_shadow);

#ifdef __cplusplus
}
#endif
