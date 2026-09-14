#pragma once
/*
 * CCPrefab — entity templates. Define an actor's makeup once (mesh, material,
 * scale, rotation, shadow, name), then stamp out many instances into a world
 * with per-instance position (and optional rotation/scale). The authoring win
 * behind spawning crowds, props, projectiles, tiles, pickups — anything you need
 * many of. Built on CCActor, so every spawned instance IS a normal ECS entity.
 *
 *   CCPrefab tree = cc_prefab_new("tree");
 *   cc_prefab_set_mesh(&tree, trunk_mesh);
 *   cc_prefab_set_material(&tree, bark_mat);
 *   cc_prefab_set_scale(&tree, 1, 1.5f, 1);
 *   for (int i=0;i<100;i++)
 *       cc_prefab_spawn(world, &tree, xs[i], 0, zs[i]);   // 100 trees
 *
 * Or capture a prefab from an actor you already set up, then clone it:
 *   CCPrefab p = cc_prefab_from_actor(hero);
 *   CCActor clone = cc_prefab_spawn(world, &p, x,0,z);
 *
 * CCPrefab is a small value type (copyable). Spawn returns the new CCActor so
 * you can tweak the instance further.
 */
#include "cc/actor.h"
#include "cc/render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCPrefab {
    char       name[64];       /* base name; instances get "name_N" */
    CCMesh     mesh;
    CCMaterial material;
    float      scale[3];       /* base scale */
    float      rot_euler[3];   /* base rotation, degrees */
    bool       cast_shadow, receive_shadow;
    bool       visible;
    uint32_t   spawn_count;    /* running counter for unique instance names */
} CCPrefab;

/* create an empty prefab (identity scale, visible, shadows on). */
CCPrefab cc_prefab_new(const char* name);
/* capture a prefab from an existing actor's current properties. */
CCPrefab cc_prefab_from_actor(CCActor a);

void cc_prefab_set_mesh(CCPrefab* p, CCMesh mesh);
void cc_prefab_set_material(CCPrefab* p, CCMaterial mat);
void cc_prefab_set_scale(CCPrefab* p, float sx, float sy, float sz);
void cc_prefab_set_rotation(CCPrefab* p, float rx, float ry, float rz);  /* degrees */
void cc_prefab_set_shadow(CCPrefab* p, bool cast_shadow, bool receive_shadow);
void cc_prefab_set_visible(CCPrefab* p, bool visible);

/* Spawn an instance at (x,y,z) using the prefab's base scale/rotation. Returns
 * the new actor (also named "<prefab>_<n>"). */
CCActor cc_prefab_spawn(CCScene* world, CCPrefab* p, float x, float y, float z);
/* Spawn with an extra per-instance yaw (added to the prefab's base rotation)
 * and a uniform scale multiplier — handy for natural variation in crowds/props. */
CCActor cc_prefab_spawn_ex(CCScene* world, CCPrefab* p, float x, float y, float z,
                           float extra_yaw_deg, float scale_mul);

#ifdef __cplusplus
}
#endif
