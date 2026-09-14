/* group_test — verifies the group system: name↔id mapping, multi-group
 * membership, whole-group count/iterate/collect, convenience ops (visibility,
 * translate, destroy), and cleanup on actor destroy. Uses a headless engine for
 * real actors. Prints "GROUP TEST: all checks passed" / returns 0. */
#include "cc/claudecore.h"
#include "cc/group.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

static int g_visited = 0;
static void count_visitor(CCActor a, void* ud){ (void)a;(void)ud; g_visited++; }

int main(void) {
    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 64; cfg.height = 64; cfg.verbose = false;
    CCEngine* e = cc_init(&cfg);
    CHECK(e != NULL, "engine created");
    if (!e) { printf("GROUP TEST: init failed\n"); return 1; }
    CCScene* w = cc_scene_create(e, "w");
    CCMesh cube = cc_mesh_cube(e, 1.0f);
    CCMaterialDesc md = {.base_color={1,1,1,1}, .tint={1,1,1,1}};
    CCMaterial mat = cc_material_create(e, &md);

    /* ── name ↔ id ──────────────────────────────────────────────────────── */
    CCGroupId humans = cc_group("humanModel");
    CCGroupId enemies = cc_group("enemy");
    CHECK(humans != CC_GROUP_NULL && enemies != CC_GROUP_NULL, "groups created");
    CHECK(humans != enemies, "distinct names → distinct ids");
    CHECK(cc_group("humanModel") == humans, "same name → same id (stable)");
    CHECK(strcmp(cc_group_name(humans), "humanModel") == 0, "id → name round-trips");

    /* ── membership (an actor can be in many groups) ────────────────────── */
    CCActor player = cc_actor_spawn(w, cube, mat, 0,0,0);
    cc_group_add(w, player, humans);
    cc_group_add_named(w, player, "enemy");   /* the player is also an enemy */
    CHECK(cc_group_contains(w, player, humans), "player in humanModel");
    CHECK(cc_group_contains(w, player, enemies), "player in enemy");
    CHECK(cc_group_membership_count(w, player) == 2, "player belongs to 2 groups");

    /* adding twice is idempotent */
    cc_group_add(w, player, humans);
    CHECK(cc_group_membership_count(w, player) == 2, "re-add is idempotent");

    /* spawn more humans */
    CCActor npc1 = cc_actor_spawn(w, cube, mat, 2,0,0);
    CCActor npc2 = cc_actor_spawn(w, cube, mat, 4,0,0);
    cc_group_add(w, npc1, humans);
    cc_group_add(w, npc2, humans);

    /* ── whole-group count / iterate / collect ──────────────────────────── */
    CHECK(cc_group_count(w, humans) == 3, "humanModel has 3 members");
    CHECK(cc_group_count(w, enemies) == 1, "enemy has 1 member");
    CHECK(cc_group_count_named(w, "humanModel") == 3, "count_named works");

    g_visited = 0;
    cc_group_each(w, humans, count_visitor, NULL);
    CHECK(g_visited == 3, "each visits all 3 humans");

    CCActor buf[8];
    uint32_t n = cc_group_members(w, humans, buf, 8);
    CHECK(n == 3, "members() returns 3");

    CCActor first = cc_group_first(w, enemies);
    CHECK(cc_actor_valid(first) && first.id == player.id, "first(enemy) is the player");

    /* ── whole-group operations ─────────────────────────────────────────── */
    cc_group_set_visible(w, humans, false);
    CHECK(!cc_actor_visible(npc1) && !cc_actor_visible(npc2), "set_visible hid the group");
    cc_group_set_visible(w, humans, true);
    CHECK(cc_actor_visible(npc1), "set_visible re-showed the group");

    /* translate the whole group */
    float x0,y0,z0; cc_actor_get_position(npc1, &x0,&y0,&z0);
    cc_group_translate(w, humans, 0, 5, 0);
    float x1,y1,z1; cc_actor_get_position(npc1, &x1,&y1,&z1);
    CHECK(fabsf(y1-(y0+5.0f)) < 1e-4f, "translate moved a group member");

    /* ── remove + leave-all ─────────────────────────────────────────────── */
    cc_group_remove(w, player, enemies);
    CHECK(!cc_group_contains(w, player, enemies), "removed player from enemy");
    CHECK(cc_group_contains(w, player, humans), "player still in humanModel");
    CHECK(cc_group_count(w, enemies) == 0, "enemy now empty");

    cc_group_remove_all(w, player);
    CHECK(cc_group_membership_count(w, player) == 0, "remove_all cleared membership");
    CHECK(cc_group_count(w, humans) == 2, "humanModel down to 2 after player left");

    /* ── cleanup on destroy ─────────────────────────────────────────────── */
    cc_actor_destroy(npc1);
    CHECK(cc_group_count(w, humans) == 1, "destroying a member drops it from the group");

    /* ── destroy whole group ────────────────────────────────────────────── */
    CCGroupId debris = cc_group("debris");
    for (int i = 0; i < 5; i++) {
        CCActor d = cc_actor_spawn(w, cube, mat, (float)i, 0, 3);
        cc_group_add(w, d, debris);
    }
    CHECK(cc_group_count(w, debris) == 5, "5 debris spawned + grouped");
    cc_group_destroy_members(w, debris);
    CHECK(cc_group_count(w, debris) == 0, "destroy_members cleared the group");

    /* ── NULL / empty safety ────────────────────────────────────────────── */
    CHECK(cc_group("") == CC_GROUP_NULL, "empty name → null group");
    CHECK(cc_group_count(w, CC_GROUP_NULL) == 0, "count(null group) → 0");
    CHECK(cc_group_first(w, cc_group("nobody")) .id == 0, "first of empty group → null actor");

    cc_scene_destroy(w);
    cc_shutdown(e);

    if (failures == 0) {
        printf("GROUP TEST: all checks passed (name↔id, multi-group membership, count/"
               "iterate/collect, whole-group ops, remove/leave-all, destroy cleanup)\n");
        return 0;
    }
    printf("GROUP TEST: %d check(s) FAILED\n", failures);
    return 1;
}
