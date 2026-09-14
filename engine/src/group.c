/* group.c — CCGroup: tag objects with shared group id/name, operate on all at
 * once. See cc/group.h. Membership is an ECS component (travels with the entity,
 * freed on destroy); names map to ids via a process-global registry.
 * Pure CPU, headless-safe.
 */
#include "cc/group.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── name → id registry (process-global) ─────────────────────────────────── */
#define GROUP_MAX_NAMES 1024
#define GROUP_NAME_LEN  64

typedef struct { char name[GROUP_NAME_LEN]; bool used; } GroupName;
static GroupName g_names[GROUP_MAX_NAMES];
static uint32_t  g_name_count = 0;

CCGroupId cc_group(const char* name) {
    if (!name || !*name) return CC_GROUP_NULL;
    for (uint32_t i = 0; i < g_name_count; i++)
        if (g_names[i].used && strcmp(g_names[i].name, name) == 0)
            return (CCGroupId)(i + 1);          /* ids are 1-based */
    if (g_name_count >= GROUP_MAX_NAMES) return CC_GROUP_NULL;
    uint32_t idx = g_name_count++;
    g_names[idx].used = true;
    snprintf(g_names[idx].name, sizeof(g_names[idx].name), "%s", name);
    return (CCGroupId)(idx + 1);
}
const char* cc_group_name(CCGroupId g) {
    if (g == CC_GROUP_NULL || g > g_name_count) return NULL;
    return g_names[g - 1].used ? g_names[g - 1].name : NULL;
}

/* ─── membership component ────────────────────────────────────────────────── */
#define GROUP_MAX_PER_ENTITY 16

typedef struct { CCGroupId ids[GROUP_MAX_PER_ENTITY]; uint32_t count; } GroupMembership;

static CCComponentId membership_comp(void) {
    /* registered once, keyed by name (matches CC_REGISTER semantics) */
    static CCComponentId id = 0;
    if (!id) id = cc_component_register("CCGroupMembership", sizeof(GroupMembership));
    return id;
}

static GroupMembership* membership_get(CCScene* s, CCEntityId e, bool create) {
    CCComponentId c = membership_comp();
    if (cc_component_has(s, e, c)) return (GroupMembership*)cc_component_get(s, e, c);
    if (!create) return NULL;
    GroupMembership* m = (GroupMembership*)cc_component_add(s, e, c);
    if (m) { m->count = 0; }
    return m;
}

void cc_group_add(CCScene* scene, CCActor a, CCGroupId g) {
    if (!scene || g == CC_GROUP_NULL || !cc_actor_valid(a)) return;
    GroupMembership* m = membership_get(scene, a.id, true);
    if (!m) return;
    for (uint32_t i = 0; i < m->count; i++) if (m->ids[i] == g) return;  /* already in */
    if (m->count >= GROUP_MAX_PER_ENTITY) return;
    m->ids[m->count++] = g;
}
void cc_group_add_named(CCScene* scene, CCActor a, const char* name) {
    cc_group_add(scene, a, cc_group(name));
}
void cc_group_remove(CCScene* scene, CCActor a, CCGroupId g) {
    if (!scene || !cc_actor_valid(a)) return;
    GroupMembership* m = membership_get(scene, a.id, false);
    if (!m) return;
    for (uint32_t i = 0; i < m->count; i++) {
        if (m->ids[i] == g) {
            m->ids[i] = m->ids[m->count - 1];
            m->count--;
            return;
        }
    }
}
void cc_group_remove_all(CCScene* scene, CCActor a) {
    if (!scene || !cc_actor_valid(a)) return;
    GroupMembership* m = membership_get(scene, a.id, false);
    if (m) m->count = 0;
}
bool cc_group_contains(CCScene* scene, CCActor a, CCGroupId g) {
    if (!scene || !cc_actor_valid(a)) return false;
    GroupMembership* m = membership_get(scene, a.id, false);
    if (!m) return false;
    for (uint32_t i = 0; i < m->count; i++) if (m->ids[i] == g) return true;
    return false;
}
uint32_t cc_group_membership_count(CCScene* scene, CCActor a) {
    if (!scene || !cc_actor_valid(a)) return 0;
    GroupMembership* m = membership_get(scene, a.id, false);
    return m ? m->count : 0;
}
CCGroupId cc_group_membership_at(CCScene* scene, CCActor a, uint32_t i) {
    if (!scene || !cc_actor_valid(a)) return CC_GROUP_NULL;
    GroupMembership* m = membership_get(scene, a.id, false);
    if (!m || i >= m->count) return CC_GROUP_NULL;
    return m->ids[i];
}

/* ─── whole-group iteration ───────────────────────────────────────────────── */
/* We iterate all entities holding a membership component and filter by group.
 * The query passes the component pointer; we reconstruct the actor from the id. */
typedef struct {
    CCScene*   scene;
    CCGroupId  group;
    CCGroupFn  fn;
    void*      ud;
    CCActor*   out;
    uint32_t   max, written;
    uint32_t   counter;
    int        mode;   /* 0=each 1=collect 2=count */
} GroupWalk;

static bool membership_in(const GroupMembership* m, CCGroupId g) {
    for (uint32_t i = 0; i < m->count; i++) if (m->ids[i] == g) return true;
    return false;
}

static void group_walk_cb(CCEntityId id, void* comp, void* ud) {
    GroupWalk* w = (GroupWalk*)ud;
    GroupMembership* m = (GroupMembership*)comp;
    if (!membership_in(m, w->group)) return;
    CCActor a = { w->scene, id };
    if (!cc_actor_valid(a)) return;
    switch (w->mode) {
        case 0: if (w->fn) w->fn(a, w->ud); break;
        case 1: if (w->written < w->max) w->out[w->written++] = a; break;
        case 2: w->counter++; break;
    }
}

uint32_t cc_group_count(CCScene* scene, CCGroupId g) {
    if (!scene || g == CC_GROUP_NULL) return 0;
    GroupWalk w = { scene, g, NULL, NULL, NULL, 0, 0, 0, 2 };
    cc_query1(scene, membership_comp(), group_walk_cb, &w);
    return w.counter;
}
void cc_group_each(CCScene* scene, CCGroupId g, CCGroupFn fn, void* userdata) {
    if (!scene || g == CC_GROUP_NULL || !fn) return;
    GroupWalk w = { scene, g, fn, userdata, NULL, 0, 0, 0, 0 };
    cc_query1(scene, membership_comp(), group_walk_cb, &w);
}
uint32_t cc_group_members(CCScene* scene, CCGroupId g, CCActor* out, uint32_t max) {
    if (!scene || g == CC_GROUP_NULL || !out) return 0;
    GroupWalk w = { scene, g, NULL, NULL, out, max, 0, 0, 1 };
    cc_query1(scene, membership_comp(), group_walk_cb, &w);
    return w.written;
}
CCActor cc_group_first(CCScene* scene, CCGroupId g) {
    CCActor one[1];
    if (cc_group_members(scene, g, one, 1) == 1) return one[0];
    return CC_ACTOR_NULL;
}

/* ─── convenience whole-group operations ──────────────────────────────────── */
static void op_hide(CCActor a, void* ud)   { cc_actor_set_visible(a, *(bool*)ud); }
static void op_mat(CCActor a, void* ud)    { cc_actor_set_material(a, *(CCMaterial*)ud); }
typedef struct { float d[3]; } Vec3d;
static void op_trans(CCActor a, void* ud)  {
    Vec3d* v = (Vec3d*)ud; float x,y,z; cc_actor_get_position(a,&x,&y,&z);
    cc_actor_set_position(a, x+v->d[0], y+v->d[1], z+v->d[2]);
}
static void op_destroy(CCActor a, void* ud){ (void)ud; cc_actor_destroy(a); }

void cc_group_set_visible(CCScene* scene, CCGroupId g, bool visible) {
    cc_group_each(scene, g, op_hide, &visible);
}
void cc_group_set_material(CCScene* scene, CCGroupId g, CCMaterial mat) {
    cc_group_each(scene, g, op_mat, &mat);
}
void cc_group_translate(CCScene* scene, CCGroupId g, float dx, float dy, float dz) {
    Vec3d v = {{dx,dy,dz}};
    cc_group_each(scene, g, op_trans, &v);
}
void cc_group_destroy_members(CCScene* scene, CCGroupId g) {
    /* collect first (destroying during a query mutates the set) */
    CCActor buf[512]; uint32_t n = cc_group_members(scene, g, buf, 512);
    for (uint32_t i = 0; i < n; i++) op_destroy(buf[i], NULL);
}

uint32_t cc_group_count_named(CCScene* scene, const char* name) {
    return cc_group_count(scene, cc_group(name));
}
void cc_group_set_visible_named(CCScene* scene, const char* name, bool visible) {
    cc_group_set_visible(scene, cc_group(name), visible);
}
