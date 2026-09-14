#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Chlorlite ECS — archetype-based, cache-friendly
 *
 * Entity IDs are 64-bit: [ generation:16 | index:48 ]
 * This allows stable handles across destruction/reuse cycles.
 */

typedef uint64_t CCEntityId;
#define CC_ENTITY_NULL ((CCEntityId)0)

typedef struct CCEngine CCEngine;
typedef uint32_t CCComponentId;
#define CC_COMPONENT_NULL ((CCComponentId)0)

typedef struct CCScene CCScene;

/* ─── Entity ─────────────────────────────────────────────────────────── */
CCEntityId cc_entity_create(CCScene* scene);
void       cc_entity_destroy(CCScene* scene, CCEntityId id);
bool       cc_entity_alive(CCScene* scene, CCEntityId id);

/* Tag an entity with a name for debugging/lookup */
void       cc_entity_set_name(CCScene* scene, CCEntityId id, const char* name);
CCEntityId cc_entity_find(CCScene* scene, const char* name);
const char* cc_entity_name(CCScene* scene, CCEntityId id);

/* ─── Component registration ─────────────────────────────────────────── */
/*
 * Register a component type by name and size.
 * Returns a stable CCComponentId for this type in this engine instance.
 * Safe to call multiple times — idempotent.
 */
CCComponentId cc_component_register(const char* name, size_t size);

/* Convenience macro — registers based on C type */
#define CC_REGISTER(T) cc_component_register(#T, sizeof(T))

/* ─── Component access ───────────────────────────────────────────────── */
void* cc_component_add(CCScene* scene, CCEntityId id, CCComponentId comp);
void  cc_component_remove(CCScene* scene, CCEntityId id, CCComponentId comp);
void* cc_component_get(CCScene* scene, CCEntityId id, CCComponentId comp);
bool  cc_component_has(CCScene* scene, CCEntityId id, CCComponentId comp);

/* Typed convenience (C11 _Generic / macro) */
#define CC_ADD(scene, id, T)  ((T*)cc_component_add(scene, id, CC_REGISTER(T)))
#define CC_GET(scene, id, T)  ((T*)cc_component_get(scene, id, CC_REGISTER(T)))
#define CC_HAS(scene, id, T)  (cc_component_has(scene, id, CC_REGISTER(T)))

/* ─── Query / iteration ──────────────────────────────────────────────── */
/*
 * Query: iterate all entities that have ALL listed components.
 * Callback receives entity id + pointer to each requested component in order.
 * Components are guaranteed contiguous in memory within a single archetype.
 */
typedef void (*CCQueryFn)(CCEntityId id, void** comps, void* userdata);

typedef struct CCQuery {
    CCComponentId* types;
    uint32_t       count;
    CCQueryFn      fn;
    void*          userdata;
} CCQuery;

void cc_query_run(CCScene* scene, CCQuery* query);

/* ─── System scheduler — register systems that run automatically each tick ── */
typedef void (*CCSystemFn)(CCScene* scene, CCEngine* eng, double dt, void* userdata);
uint32_t cc_system_register(CCScene* scene, const char* name, CCSystemFn fn, int order, void* userdata);
void     cc_system_set_enabled(CCScene* scene, uint32_t sys_id, bool enabled);
void     cc_system_remove(CCScene* scene, uint32_t sys_id);
void     cc_systems_run(CCScene* scene, CCEngine* eng, double dt);  /* called by engine each tick */

/* Shorthand for 1-3 component queries (most common) */
void cc_query1(CCScene* scene, CCComponentId c0,
               void (*fn)(CCEntityId, void*, void*), void* ud);
void cc_query2(CCScene* scene, CCComponentId c0, CCComponentId c1,
               void (*fn)(CCEntityId, void*, void*, void*), void* ud);
void cc_query3(CCScene* scene, CCComponentId c0, CCComponentId c1, CCComponentId c2,
               void (*fn)(CCEntityId, void*, void*, void*, void*), void* ud);

/* ─── Built-in components ────────────────────────────────────────────── */

typedef struct CCTransform {
    float x, y, z;
    float rx, ry, rz;   /* Euler rotation degrees */
    float sx, sy, sz;   /* scale */
} CCTransform;

typedef struct CCVelocity {
    float vx, vy, vz;
    float ax, ay, az;   /* angular velocity */
} CCVelocity;

typedef struct CCSpriteComp {
    uint32_t texture_id;
    float    u0, v0, u1, v1;  /* UV rect */
    float    width, height;
    uint32_t tint;             /* RGBA packed */
    int      layer;
} CCSpriteComp;

typedef struct CCMeshComp {
    uint32_t mesh_id;
    uint32_t material_id;
    bool     cast_shadow;
    bool     receive_shadow;
} CCMeshComp;

typedef struct CCCameraComp {
    float fov;          /* vertical FOV degrees — 0 for orthographic */
    float near_plane;
    float far_plane;
    float ortho_size;   /* used when fov == 0 */
    bool  is_active;
} CCCameraComp;

typedef struct CCAudioSource {
    uint32_t sound_id;
    float    volume;    /* 0.0 - 1.0 */
    float    pitch;
    bool     looping;
    bool     playing;
    float    spatial_radius; /* 0 = 2D */
} CCAudioSource;

typedef struct CCScript {
    void* instance;         /* language-specific handle */
    void (*on_update)(void* inst, double dt);
    void (*on_destroy)(void* inst);
} CCScript;

#ifdef __cplusplus
}
#endif
