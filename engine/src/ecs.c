#include "cc/ecs.h"
#include "cc/claudecore.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── Component registry ─────────────────────────────────────────────── */
#define CC_MAX_COMPONENTS 256

typedef struct {
    char     name[64];
    size_t   size;
    bool     used;
} ComponentDef;

static ComponentDef s_components[CC_MAX_COMPONENTS];
static uint32_t     s_component_count = 1;  /* 0 = null sentinel */

CCComponentId cc_component_register(const char* name, size_t size) {
    /* Check if already registered */
    for (uint32_t i = 1; i < s_component_count; i++) {
        if (s_components[i].used && strcmp(s_components[i].name, name) == 0)
            return (CCComponentId)i;
    }
    if (s_component_count >= CC_MAX_COMPONENTS) {
        CC_ERROR("Component registry full");
        return CC_COMPONENT_NULL;
    }
    uint32_t id = s_component_count++;
    strncpy(s_components[id].name, name, sizeof(s_components[id].name) - 1);
    s_components[id].size = size;
    s_components[id].used = true;
    return (CCComponentId)id;
}

/* ─── Archetype ──────────────────────────────────────────────────────── */
/*
 * Each archetype holds all entities with the same set of components.
 * Components are stored in SOA within each archetype for cache efficiency.
 */
#define CC_MAX_ARCHETYPE_COMPONENTS 16
#define CC_ARCHETYPE_INITIAL_CAP 64
#define CC_MAX_ARCHETYPES 512

typedef struct {
    CCComponentId types[CC_MAX_ARCHETYPE_COMPONENTS];
    uint32_t      type_count;
    uint8_t**     data;          /* one buffer per component type */
    CCEntityId*   entity_ids;
    uint32_t      count;
    uint32_t      capacity;
} Archetype;

/* ─── Scene ──────────────────────────────────────────────────────────── */
#define CC_MAX_ENTITIES (1 << 20)  /* 1M entities */

typedef struct EntityRecord {
    uint16_t generation;
    uint32_t archetype_idx;  /* 0 = not in any archetype */
    uint32_t row;            /* index within archetype */
    bool     alive;
    char     name[64];
} EntityRecord;

typedef struct CCSystemRegistry CCSystemRegistry;
CCSystemRegistry* cc_system_registry_create(void);
void cc_system_registry_destroy(CCSystemRegistry*);

struct CCScene {
    const char*   name;
    CCSystemRegistry* systems;
    EntityRecord* entities;
    uint32_t      entity_count;
    uint32_t      entity_capacity;
    uint32_t      free_head;    /* freelist head */

    Archetype*    archetypes;
    uint32_t      archetype_count;
    uint32_t      archetype_capacity;
};

/* ─── Scene creation ─────────────────────────────────────────────────── */

CCScene* cc_scene_create(CCEngine* eng, const char* name) {
    (void)eng;
    CCScene* s = calloc(1, sizeof(CCScene));
    if (!s) return NULL;
    s->name = name;
    s->entity_capacity = 1024;
    s->entities = calloc(s->entity_capacity, sizeof(EntityRecord));
    /* Entity 0 is null sentinel */
    s->entity_count = 1;
    s->free_head = UINT32_MAX;

    s->archetype_capacity = 64;
    s->archetypes = calloc(s->archetype_capacity, sizeof(Archetype));
    s->systems = cc_system_registry_create();
    return s;
}

CCSystemRegistry* cc_scene_systems(CCScene* scene) { return scene ? scene->systems : NULL; }

void cc_scene_destroy(CCScene* scene) {
    if (!scene) return;
    for (uint32_t a = 0; a < scene->archetype_count; a++) {
        Archetype* arch = &scene->archetypes[a];
        for (uint32_t c = 0; c < arch->type_count; c++)
            free(arch->data[c]);
        free(arch->data);
        free(arch->entity_ids);
    }
    free(scene->archetypes);
    free(scene->entities);
    cc_system_registry_destroy(scene->systems);
    free(scene);
}

/* ─── Entity management ──────────────────────────────────────────────── */

static uint32_t alloc_entity_slot(CCScene* scene) {
    if (scene->free_head != UINT32_MAX) {
        uint32_t slot = scene->free_head;
        /* Freelist stored as generation + UINT32_MAX sentinel — use row field */
        scene->free_head = scene->entities[slot].row;
        return slot;
    }
    if (scene->entity_count >= scene->entity_capacity) {
        scene->entity_capacity *= 2;
        scene->entities = realloc(scene->entities,
                                  scene->entity_capacity * sizeof(EntityRecord));
    }
    return scene->entity_count++;
}

CCEntityId cc_entity_create(CCScene* scene) {
    uint32_t idx = alloc_entity_slot(scene);
    EntityRecord* rec = &scene->entities[idx];
    rec->generation++;
    rec->archetype_idx = 0;
    rec->row = 0;
    rec->alive = true;
    rec->name[0] = '\0';
    CCEntityId id = ((CCEntityId)rec->generation << 48) | (CCEntityId)idx;
    return id;
}

void cc_entity_destroy(CCScene* scene, CCEntityId id) {
    uint32_t idx = (uint32_t)(id & 0xFFFFFFFFFFFFULL);
    if (idx >= scene->entity_count) return;
    EntityRecord* rec = &scene->entities[idx];
    if (!rec->alive) return;

    /* Remove from archetype — swap with last */
    if (rec->archetype_idx && rec->archetype_idx <= scene->archetype_count) {
        Archetype* arch = &scene->archetypes[rec->archetype_idx - 1];
        uint32_t last = arch->count - 1;
        if (rec->row != last) {
            /* Move last entity to fill gap */
            for (uint32_t c = 0; c < arch->type_count; c++) {
                size_t sz = s_components[arch->types[c]].size;
                memcpy(arch->data[c] + rec->row * sz,
                       arch->data[c] + last * sz, sz);
            }
            arch->entity_ids[rec->row] = arch->entity_ids[last];
            /* Update moved entity's record */
            CCEntityId moved_id = arch->entity_ids[rec->row];
            uint32_t moved_idx = (uint32_t)(moved_id & 0xFFFFFFFFFFFFULL);
            scene->entities[moved_idx].row = rec->row;
        }
        arch->count--;
    }

    rec->alive = false;
    rec->row = scene->free_head;
    scene->free_head = idx;
}

bool cc_entity_alive(CCScene* scene, CCEntityId id) {
    uint32_t idx = (uint32_t)(id & 0xFFFFFFFFFFFFULL);
    uint16_t gen = (uint16_t)(id >> 48);
    if (idx >= scene->entity_count) return false;
    EntityRecord* rec = &scene->entities[idx];
    return rec->alive && rec->generation == gen;
}

void cc_entity_set_name(CCScene* scene, CCEntityId id, const char* name) {
    uint32_t idx = (uint32_t)(id & 0xFFFFFFFFFFFFULL);
    if (idx >= scene->entity_count) return;
    strncpy(scene->entities[idx].name, name, 63);
}

CCEntityId cc_entity_find(CCScene* scene, const char* name) {
    for (uint32_t i = 1; i < scene->entity_count; i++) {
        EntityRecord* rec = &scene->entities[i];
        if (rec->alive && strcmp(rec->name, name) == 0)
            return ((CCEntityId)rec->generation << 48) | (CCEntityId)i;
    }
    return CC_ENTITY_NULL;
}

const char* cc_entity_name(CCScene* scene, CCEntityId id) {
    uint32_t idx = (uint32_t)(id & 0xFFFFFFFFFFFFULL);
    if (idx >= scene->entity_count) return NULL;
    return scene->entities[idx].name;
}

/* ─── Archetype lookup / creation ────────────────────────────────────── */

static int comp_id_cmp(const void* a, const void* b) {
    return (int)*(CCComponentId*)a - (int)*(CCComponentId*)b;
}

static Archetype* find_or_create_archetype(CCScene* scene,
                                            CCComponentId* sorted_types,
                                            uint32_t count) {
    /* Search existing */
    for (uint32_t a = 0; a < scene->archetype_count; a++) {
        Archetype* arch = &scene->archetypes[a];
        if (arch->type_count != count) continue;
        if (memcmp(arch->types, sorted_types, count * sizeof(CCComponentId)) == 0)
            return arch;
    }
    /* Create new */
    if (scene->archetype_count >= scene->archetype_capacity) {
        scene->archetype_capacity *= 2;
        scene->archetypes = realloc(scene->archetypes,
                                    scene->archetype_capacity * sizeof(Archetype));
    }
    Archetype* arch = &scene->archetypes[scene->archetype_count++];
    memset(arch, 0, sizeof(*arch));
    arch->type_count = count;
    memcpy(arch->types, sorted_types, count * sizeof(CCComponentId));
    arch->capacity = CC_ARCHETYPE_INITIAL_CAP;
    arch->data = calloc(count, sizeof(uint8_t*));
    for (uint32_t c = 0; c < count; c++)
        arch->data[c] = malloc(arch->capacity * s_components[sorted_types[c]].size);
    arch->entity_ids = malloc(arch->capacity * sizeof(CCEntityId));
    return arch;
}

static void archetype_ensure_capacity(Archetype* arch) {
    if (arch->count < arch->capacity) return;
    arch->capacity *= 2;
    for (uint32_t c = 0; c < arch->type_count; c++) {
        size_t sz = s_components[arch->types[c]].size;
        arch->data[c] = realloc(arch->data[c], arch->capacity * sz);
    }
    arch->entity_ids = realloc(arch->entity_ids, arch->capacity * sizeof(CCEntityId));
}

/* ─── Component add/remove/get ───────────────────────────────────────── */

void* cc_component_add(CCScene* scene, CCEntityId id, CCComponentId comp) {
    uint32_t idx = (uint32_t)(id & 0xFFFFFFFFFFFFULL);
    if (idx >= scene->entity_count || !scene->entities[idx].alive) return NULL;
    EntityRecord* rec = &scene->entities[idx];

    /* Collect current components */
    CCComponentId old_types[CC_MAX_ARCHETYPE_COMPONENTS];
    uint32_t old_count = 0;
    void* old_data[CC_MAX_ARCHETYPE_COMPONENTS] = {0};

    Archetype* old_arch = NULL;
    uint32_t old_arch_idx = UINT32_MAX;
    if (rec->archetype_idx) {
        old_arch_idx = rec->archetype_idx - 1;
        old_arch = &scene->archetypes[old_arch_idx];
        old_count = old_arch->type_count;
        memcpy(old_types, old_arch->types, old_count * sizeof(CCComponentId));
        for (uint32_t c = 0; c < old_count; c++)
            old_data[c] = old_arch->data[c] + rec->row * s_components[old_arch->types[c]].size;
        /* Already has this component? */
        for (uint32_t c = 0; c < old_count; c++)
            if (old_types[c] == comp)
                return old_data[c];
    }

    /* Build new component set */
    CCComponentId new_types[CC_MAX_ARCHETYPE_COMPONENTS];
    memcpy(new_types, old_types, old_count * sizeof(CCComponentId));
    new_types[old_count] = comp;
    uint32_t new_count = old_count + 1;
    qsort(new_types, new_count, sizeof(CCComponentId), comp_id_cmp);

    Archetype* new_arch = find_or_create_archetype(scene, new_types, new_count);
    if (old_arch_idx != UINT32_MAX) old_arch = &scene->archetypes[old_arch_idx];  /* re-derive after possible realloc */
    archetype_ensure_capacity(new_arch);

    uint32_t new_row = new_arch->count++;
    new_arch->entity_ids[new_row] = id;

    /* Copy old component data */
    for (uint32_t nc = 0; nc < new_arch->type_count; nc++) {
        CCComponentId t = new_arch->types[nc];
        size_t sz = s_components[t].size;
        void* dst = new_arch->data[nc] + new_row * sz;
        if (t == comp) {
            memset(dst, 0, sz);  /* new component zeroed */
        } else if (old_arch) {
            for (uint32_t oc = 0; oc < old_arch->type_count; oc++) {
                if (old_arch->types[oc] == t) {
                    memcpy(dst, old_arch->data[oc] + rec->row * sz, sz);
                    break;
                }
            }
        }
    }

    /* Remove from old archetype */
    if (old_arch) {
        uint32_t last = old_arch->count - 1;
        if (rec->row != last) {
            for (uint32_t c = 0; c < old_arch->type_count; c++) {
                size_t sz = s_components[old_arch->types[c]].size;
                memcpy(old_arch->data[c] + rec->row * sz,
                       old_arch->data[c] + last * sz, sz);
            }
            old_arch->entity_ids[rec->row] = old_arch->entity_ids[last];
            uint32_t moved = (uint32_t)(old_arch->entity_ids[rec->row] & 0xFFFFFFFFFFFFULL);
            scene->entities[moved].row = rec->row;
        }
        old_arch->count--;
    }

    rec->archetype_idx = (uint32_t)(new_arch - scene->archetypes) + 1;
    rec->row = new_row;

    /* Find and return new component pointer */
    for (uint32_t c = 0; c < new_arch->type_count; c++)
        if (new_arch->types[c] == comp)
            return new_arch->data[c] + new_row * s_components[comp].size;

    return NULL;
}

void* cc_component_get(CCScene* scene, CCEntityId id, CCComponentId comp) {
    uint32_t idx = (uint32_t)(id & 0xFFFFFFFFFFFFULL);
    if (idx >= scene->entity_count || !scene->entities[idx].alive) return NULL;
    EntityRecord* rec = &scene->entities[idx];
    if (!rec->archetype_idx) return NULL;

    Archetype* arch = &scene->archetypes[rec->archetype_idx - 1];
    for (uint32_t c = 0; c < arch->type_count; c++) {
        if (arch->types[c] == comp)
            return arch->data[c] + rec->row * s_components[comp].size;
    }
    return NULL;
}

bool cc_component_has(CCScene* scene, CCEntityId id, CCComponentId comp) {
    return cc_component_get(scene, id, comp) != NULL;
}

void cc_component_remove(CCScene* scene, CCEntityId id, CCComponentId comp) {
    uint32_t idx = (uint32_t)(id & 0xFFFFFFFFFFFFULL);
    if (idx >= scene->entity_count || !scene->entities[idx].alive) return;
    EntityRecord* rec = &scene->entities[idx];
    if (!rec->archetype_idx) return;  /* no components at all */

    uint32_t old_arch_idx = rec->archetype_idx - 1;
    Archetype* old_arch = &scene->archetypes[old_arch_idx];

    /* Verify the entity actually has this component */
    bool has = false;
    for (uint32_t c = 0; c < old_arch->type_count; c++)
        if (old_arch->types[c] == comp) { has = true; break; }
    if (!has) return;

    /* Build the new component set without `comp` */
    CCComponentId new_types[CC_MAX_ARCHETYPE_COMPONENTS];
    uint32_t new_count = 0;
    for (uint32_t c = 0; c < old_arch->type_count; c++)
        if (old_arch->types[c] != comp)
            new_types[new_count++] = old_arch->types[c];

    if (new_count == 0) {
        /* Entity now has no components — just remove from the old archetype */
        uint32_t last = old_arch->count - 1;
        if (rec->row != last) {
            for (uint32_t c = 0; c < old_arch->type_count; c++) {
                size_t sz = s_components[old_arch->types[c]].size;
                memcpy(old_arch->data[c] + rec->row * sz,
                       old_arch->data[c] + last * sz, sz);
            }
            old_arch->entity_ids[rec->row] = old_arch->entity_ids[last];
            uint32_t moved = (uint32_t)(old_arch->entity_ids[rec->row] & 0xFFFFFFFFFFFFULL);
            scene->entities[moved].row = rec->row;
        }
        old_arch->count--;
        rec->archetype_idx = 0;
        rec->row = 0;
        return;
    }

    /* new_types is already sorted (old_arch->types is sorted, we preserved order) */
    Archetype* new_arch = find_or_create_archetype(scene, new_types, new_count);
    old_arch = &scene->archetypes[old_arch_idx];  /* re-derive: array may have realloc'd */
    archetype_ensure_capacity(new_arch);

    uint32_t new_row = new_arch->count++;
    new_arch->entity_ids[new_row] = id;

    /* Copy over the surviving components from old row → new row */
    for (uint32_t nc = 0; nc < new_arch->type_count; nc++) {
        CCComponentId t = new_arch->types[nc];
        size_t sz = s_components[t].size;
        void* dst = new_arch->data[nc] + new_row * sz;
        for (uint32_t oc = 0; oc < old_arch->type_count; oc++) {
            if (old_arch->types[oc] == t) {
                memcpy(dst, old_arch->data[oc] + rec->row * sz, sz);
                break;
            }
        }
    }

    /* Swap-remove from old archetype */
    uint32_t last = old_arch->count - 1;
    if (rec->row != last) {
        for (uint32_t c = 0; c < old_arch->type_count; c++) {
            size_t sz = s_components[old_arch->types[c]].size;
            memcpy(old_arch->data[c] + rec->row * sz,
                   old_arch->data[c] + last * sz, sz);
        }
        old_arch->entity_ids[rec->row] = old_arch->entity_ids[last];
        uint32_t moved = (uint32_t)(old_arch->entity_ids[rec->row] & 0xFFFFFFFFFFFFULL);
        scene->entities[moved].row = rec->row;
    }
    old_arch->count--;

    /* Repoint record — new_arch pointer is stable within scene->archetypes array */
    rec->archetype_idx = (uint32_t)(new_arch - scene->archetypes) + 1;
    rec->row = new_row;
}

/* ─── Query ──────────────────────────────────────────────────────────── */

void cc_query_run(CCScene* scene, CCQuery* query) {
    /* Sort query types for matching */
    CCComponentId sorted[CC_MAX_ARCHETYPE_COMPONENTS];
    memcpy(sorted, query->types, query->count * sizeof(CCComponentId));
    qsort(sorted, query->count, sizeof(CCComponentId), comp_id_cmp);

    void* comp_ptrs[CC_MAX_ARCHETYPE_COMPONENTS];

    for (uint32_t a = 0; a < scene->archetype_count; a++) {
        Archetype* arch = &scene->archetypes[a];
        if (arch->type_count < query->count) continue;

        /* Check all query types are in this archetype */
        bool match = true;
        for (uint32_t qi = 0; qi < query->count; qi++) {
            bool found = false;
            for (uint32_t ac = 0; ac < arch->type_count; ac++) {
                if (arch->types[ac] == sorted[qi]) { found = true; break; }
            }
            if (!found) { match = false; break; }
        }
        if (!match) continue;

        /* Build comp_ptrs for this archetype */
        for (uint32_t qi = 0; qi < query->count; qi++) {
            for (uint32_t ac = 0; ac < arch->type_count; ac++) {
                if (arch->types[ac] == sorted[qi]) {
                    /* We'll advance this pointer per entity */
                    comp_ptrs[qi] = arch->data[ac];
                    break;
                }
            }
        }

        /* Determine sizes */
        size_t sizes[CC_MAX_ARCHETYPE_COMPONENTS];
        for (uint32_t qi = 0; qi < query->count; qi++)
            sizes[qi] = s_components[sorted[qi]].size;

        void* row_ptrs[CC_MAX_ARCHETYPE_COMPONENTS];
        for (uint32_t row = 0; row < arch->count; row++) {
            for (uint32_t qi = 0; qi < query->count; qi++)
                row_ptrs[qi] = (uint8_t*)comp_ptrs[qi] + row * sizes[qi];
            query->fn(arch->entity_ids[row], row_ptrs, query->userdata);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   SYSTEM SCHEDULER (register systems that run automatically each tick)
   ══════════════════════════════════════════════════════════════════════ */

#define CC_MAX_SYSTEMS 64

typedef struct {
    CCSystemFn fn;
    void*      userdata;
    char       name[48];
    int        order;      /* lower runs first */
    bool       enabled;
    bool       used;
} SystemEntry;

/* Systems live on the scene so different scenes can have different logic. */
typedef struct CCSystemRegistry {
    SystemEntry systems[CC_MAX_SYSTEMS];
    uint32_t    count;
} CCSystemRegistry;

/* stored in CCScene via an opaque pointer added to the struct */
uint32_t cc_system_register(CCScene* scene, const char* name, CCSystemFn fn, int order, void* userdata) {
    if (!scene || !fn) return 0;
    CCSystemRegistry* reg = cc_scene_systems(scene);
    if (!reg) return 0;
    for (uint32_t i = 0; i < CC_MAX_SYSTEMS; i++) {
        if (!reg->systems[i].used) {
            reg->systems[i].fn = fn;
            reg->systems[i].userdata = userdata;
            reg->systems[i].order = order;
            reg->systems[i].enabled = true;
            reg->systems[i].used = true;
            if (name) strncpy(reg->systems[i].name, name, sizeof(reg->systems[i].name)-1);
            if (i+1 > reg->count) reg->count = i+1;
            return i+1;
        }
    }
    return 0;
}

void cc_system_set_enabled(CCScene* scene, uint32_t sys_id, bool enabled) {
    CCSystemRegistry* reg = cc_scene_systems(scene);
    if (reg && sys_id > 0 && sys_id <= CC_MAX_SYSTEMS) reg->systems[sys_id-1].enabled = enabled;
}

void cc_system_remove(CCScene* scene, uint32_t sys_id) {
    CCSystemRegistry* reg = cc_scene_systems(scene);
    if (reg && sys_id > 0 && sys_id <= CC_MAX_SYSTEMS) reg->systems[sys_id-1].used = false;
}

/* Run all enabled systems in ascending order. Called by the engine each tick. */
void cc_systems_run(CCScene* scene, CCEngine* eng, double dt) {
    CCSystemRegistry* reg = cc_scene_systems(scene);
    if (!reg) return;
    /* simple insertion order by 'order' field */
    for (int pass_order = -1000000; ; ) {
        int next = 1000000; bool any = false;
        for (uint32_t i = 0; i < reg->count; i++) {
            SystemEntry* s = &reg->systems[i];
            if (!s->used || !s->enabled) continue;
            if (s->order == pass_order) { s->fn(scene, eng, dt, s->userdata); any = true; }
            else if (s->order > pass_order && s->order < next) next = s->order;
        }
        (void)any;
        if (next == 1000000) break;
        pass_order = next;
    }
}

CCSystemRegistry* cc_system_registry_create(void) {
    return calloc(1, sizeof(CCSystemRegistry));
}
void cc_system_registry_destroy(CCSystemRegistry* r) { free(r); }

/* ─── Query shorthands (cc_query1/2/3) ───────────────────────────────────
 * Convenience wrappers over cc_query_run for the common 1–3 component case.
 * cc_query_run sorts the requested component types internally, so the raw
 * callback receives comps[] in SORTED order. These shorthands remap back to the
 * caller's ARGUMENT order so fn() receives components as requested. */
typedef struct {
    void*    user_fn;
    void*    ud;
    uint32_t n;
    CCComponentId req[3];   /* caller's order */
    CCComponentId sorted[3];/* sorted order (as query_run passes) */
} QueryTramp;

static int qtramp_slot(const QueryTramp* t, uint32_t caller_idx){
    /* index in sorted[] of the component the caller listed at caller_idx */
    CCComponentId want = t->req[caller_idx];
    for(uint32_t i=0;i<t->n;i++) if(t->sorted[i]==want) return (int)i;
    return 0;
}
static void qtramp1(CCEntityId id, void** comps, void* ud){
    QueryTramp* t=(QueryTramp*)ud;
    void (*fn)(CCEntityId,void*,void*) = (void(*)(CCEntityId,void*,void*))t->user_fn;
    fn(id, comps[qtramp_slot(t,0)], t->ud);
}
static void qtramp2(CCEntityId id, void** comps, void* ud){
    QueryTramp* t=(QueryTramp*)ud;
    void (*fn)(CCEntityId,void*,void*,void*) = (void(*)(CCEntityId,void*,void*,void*))t->user_fn;
    fn(id, comps[qtramp_slot(t,0)], comps[qtramp_slot(t,1)], t->ud);
}
static void qtramp3(CCEntityId id, void** comps, void* ud){
    QueryTramp* t=(QueryTramp*)ud;
    void (*fn)(CCEntityId,void*,void*,void*,void*) = (void(*)(CCEntityId,void*,void*,void*,void*))t->user_fn;
    fn(id, comps[qtramp_slot(t,0)], comps[qtramp_slot(t,1)], comps[qtramp_slot(t,2)], t->ud);
}
static void qtramp_fill_sorted(QueryTramp* t){
    for(uint32_t i=0;i<t->n;i++) t->sorted[i]=t->req[i];
    /* simple insertion sort of sorted[] */
    for(uint32_t i=1;i<t->n;i++){ CCComponentId v=t->sorted[i]; int j=(int)i-1;
        while(j>=0 && t->sorted[j]>v){ t->sorted[j+1]=t->sorted[j]; j--; } t->sorted[j+1]=v; }
}
void cc_query1(CCScene* scene, CCComponentId c0,
               void (*fn)(CCEntityId, void*, void*), void* ud){
    QueryTramp t={ (void*)fn, ud, 1, {c0,0,0}, {0,0,0} }; qtramp_fill_sorted(&t);
    CCComponentId types[1]={c0};
    CCQuery q={ types, 1, qtramp1, &t }; cc_query_run(scene,&q);
}
void cc_query2(CCScene* scene, CCComponentId c0, CCComponentId c1,
               void (*fn)(CCEntityId, void*, void*, void*), void* ud){
    QueryTramp t={ (void*)fn, ud, 2, {c0,c1,0}, {0,0,0} }; qtramp_fill_sorted(&t);
    CCComponentId types[2]={c0,c1};
    CCQuery q={ types, 2, qtramp2, &t }; cc_query_run(scene,&q);
}
void cc_query3(CCScene* scene, CCComponentId c0, CCComponentId c1, CCComponentId c2,
               void (*fn)(CCEntityId, void*, void*, void*, void*), void* ud){
    QueryTramp t={ (void*)fn, ud, 3, {c0,c1,c2}, {0,0,0} }; qtramp_fill_sorted(&t);
    CCComponentId types[3]={c0,c1,c2};
    CCQuery q={ types, 3, qtramp3, &t }; cc_query_run(scene,&q);
}
