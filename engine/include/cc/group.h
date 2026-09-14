#pragma once
/*
 * CCGroup — tag any objects with a shared GROUP (by numeric id and/or name) and
 * operate on them all at once. A group is NOT an asset or a container that owns
 * anything; it's a lightweight label over existing objects so you can treat many
 * as one: hide every "enemy", tint every "humanModel", set the AA mode for every
 * "foliage", count how many "pickup"s remain, or iterate them to apply anything.
 *
 * An object (any CCActor / entity) can belong to MANY groups at once (it's a
 * humanModel AND an enemy AND selectable). Groups are addressed by name
 * ("humanModel") or by a stable numeric id; a name maps to an id the first time
 * it's used, so both refer to the same group.
 *
 *   CCGroupId enemies = cc_group("enemy");            // name → id (creates it)
 *   cc_group_add(scene, guard, enemies);              // tag an actor
 *   cc_group_add_named(scene, boss, "enemy");         // or straight by name
 *   ...
 *   cc_group_set_visible(scene, enemies, false);      // affect the whole group
 *   cc_group_each(scene, enemies, dim_actor, ctx);    // or do anything per member
 *   int n = cc_group_count(scene, enemies);
 *
 * Membership lives on the entity (ECS component), so it travels with the object
 * and is cleaned up when the object is destroyed. Pure CPU, headless-safe.
 */
#include "cc/actor.h"
#include "cc/ecs.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t CCGroupId;
#define CC_GROUP_NULL ((CCGroupId)0)

/* Resolve a group name to its id, creating the group if new. Stable for the
 * process lifetime; the same name always returns the same id. */
CCGroupId   cc_group(const char* name);
/* The name a group id was created with (or NULL). */
const char* cc_group_name(CCGroupId g);

/* ─── membership ──────────────────────────────────────────────────────────── */
void cc_group_add(CCScene* scene, CCActor a, CCGroupId g);
void cc_group_add_named(CCScene* scene, CCActor a, const char* name);
void cc_group_remove(CCScene* scene, CCActor a, CCGroupId g);
void cc_group_remove_all(CCScene* scene, CCActor a);        /* leave every group */
bool cc_group_contains(CCScene* scene, CCActor a, CCGroupId g);
/* How many groups this actor belongs to, and the i-th (for introspection). */
uint32_t  cc_group_membership_count(CCScene* scene, CCActor a);
CCGroupId cc_group_membership_at(CCScene* scene, CCActor a, uint32_t i);

/* ─── whole-group queries ─────────────────────────────────────────────────── */
/* Number of live members of a group in this scene. */
uint32_t cc_group_count(CCScene* scene, CCGroupId g);
/* Visit every member. `fn` gets each member actor + your userdata. */
typedef void (*CCGroupFn)(CCActor a, void* userdata);
void cc_group_each(CCScene* scene, CCGroupId g, CCGroupFn fn, void* userdata);
/* Fill `out` with up to `max` member actors; returns the number written. */
uint32_t cc_group_members(CCScene* scene, CCGroupId g, CCActor* out, uint32_t max);
/* First member (or CC_ACTOR_NULL) — handy for singletons like "player". */
CCActor  cc_group_first(CCScene* scene, CCGroupId g);

/* ─── convenience whole-group operations ──────────────────────────────────── */
/* These apply a common action to every member in one call. */
void cc_group_set_visible(CCScene* scene, CCGroupId g, bool visible);
void cc_group_set_material(CCScene* scene, CCGroupId g, CCMaterial mat);
void cc_group_translate(CCScene* scene, CCGroupId g, float dx, float dy, float dz);
void cc_group_destroy_members(CCScene* scene, CCGroupId g);  /* despawn all */

/* name-addressed shorthands (resolve then act) */
uint32_t cc_group_count_named(CCScene* scene, const char* name);
void     cc_group_set_visible_named(CCScene* scene, const char* name, bool visible);

#ifdef __cplusplus
}
#endif
