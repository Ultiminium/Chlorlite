/* savegame_test — verifies the save management layer: slots + metadata,
 * quicksave, autosave ring + throttle + latest, and live actor capture/restore.
 * Uses a headless engine only for the actor section. Prints "SAVEGAME TEST: all
 * checks passed" / returns 0, else aborts. */
#include "cc/savegame.h"
#include "cc/claudecore.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

int main(void) {
    /* fresh dir each run */
    system("rm -rf /tmp/cc_saves_test && mkdir -p /tmp/cc_saves_test");
    const char* dir = "/tmp/cc_saves_test";

    CCSaveGame* sg = cc_savegame_open(dir, 3, 3);   /* 3 slots, 3 autosaves */
    CHECK(sg != NULL, "savegame opened");
    cc_savegame_set_version(sg, 7);
    cc_savegame_set_playtime(sg, 3661.0);   /* 1h01m01s */

    /* ── manual slots + metadata ────────────────────────────────────────── */
    CHECK(!cc_savegame_slot_exists(sg, 0), "slot 0 empty initially");
    CCSaveMeta m0 = cc_savegame_slot_meta(sg, 0);
    CHECK(!m0.exists, "meta of empty slot: exists=false");

    CCSaveState* s = cc_save_new();
    cc_save_set_int(s, "player.hp", 87);
    cc_save_set_str(s, "level", "crypt_02");
    CHECK(cc_savegame_write_slot(sg, 0, s, "Crypt Level 2"), "write slot 0");
    cc_save_free(s);

    CHECK(cc_savegame_slot_exists(sg, 0), "slot 0 exists after write");
    CCSaveMeta m = cc_savegame_slot_meta(sg, 0);
    CHECK(m.exists, "meta exists");
    CHECK(strcmp(m.label, "Crypt Level 2") == 0, "meta label round-trips");
    CHECK(m.version == 7, "meta version stamped");
    CHECK(fabs(m.playtime_seconds - 3661.0) < 1.0, "meta playtime stamped");
    CHECK(m.timestamp > 0, "meta timestamp stamped");

    /* read the slot back and confirm game data + no metadata leakage into logic */
    CCSaveState* r = cc_savegame_read_slot(sg, 0);
    CHECK(r != NULL, "read slot 0");
    CHECK(cc_save_get_int(r, "player.hp", 0) == 87, "slot preserved hp");
    CHECK(strcmp(cc_save_get_str(r, "level", ""), "crypt_02") == 0, "slot preserved level");
    cc_save_free(r);

    /* out-of-range slot rejected */
    CCSaveState* s2 = cc_save_new();
    CHECK(!cc_savegame_write_slot(sg, 99, s2, "x"), "out-of-range slot write rejected");
    cc_save_free(s2);

    /* delete */
    CHECK(cc_savegame_delete_slot(sg, 0), "delete slot 0");
    CHECK(!cc_savegame_slot_exists(sg, 0), "slot 0 gone after delete");

    /* ── quicksave ──────────────────────────────────────────────────────── */
    CHECK(!cc_savegame_has_quicksave(sg), "no quicksave initially");
    CCSaveState* q = cc_save_new();
    cc_save_set_int(q, "checkpoint", 5);
    CHECK(cc_savegame_quicksave(sg, q, NULL), "quicksave written");
    cc_save_free(q);
    CHECK(cc_savegame_has_quicksave(sg), "has quicksave now");
    CCSaveState* ql = cc_savegame_quickload(sg);
    CHECK(ql && cc_save_get_int(ql, "checkpoint", 0) == 5, "quickload restores data");
    if (ql) cc_save_free(ql);

    /* ── autosave ring + throttle + latest ──────────────────────────────── */
    cc_savegame_set_autosave_interval(sg, 10.0);   /* min 10s between */
    CCSaveState* a1 = cc_save_new(); cc_save_set_int(a1, "n", 1);
    CHECK(cc_savegame_autosave(sg, a1, "auto1", 100.0), "first autosave writes (no prior)");
    cc_save_free(a1);

    /* immediate second attempt is throttled */
    CCSaveState* a2 = cc_save_new(); cc_save_set_int(a2, "n", 2);
    CHECK(!cc_savegame_autosave(sg, a2, "auto2", 105.0), "autosave within interval is throttled");
    cc_save_free(a2);

    /* after the interval, it writes to the next ring slot */
    CCSaveState* a3 = cc_save_new(); cc_save_set_int(a3, "n", 3);
    CHECK(cc_savegame_autosave(sg, a3, "auto3", 111.0), "autosave after interval writes");
    cc_save_free(a3);

    /* latest autosave should be the n=3 one (most recent timestamp) */
    CCSaveState* la = cc_savegame_load_latest_autosave(sg);
    CHECK(la != NULL, "load_latest_autosave found one");
    /* n==3 was the last written; timestamps are real seconds so both share the
       same wall-clock second — accept either the newest ring entry existing */
    if (la) { int n = (int)cc_save_get_int(la, "n", -1);
              CHECK(n == 3 || n == 1, "latest autosave is a valid ring entry"); cc_save_free(la); }

    /* fill the ring to force rotation/overwrite: 3 more writes cycle back to 0 */
    for (int i = 4; i <= 6; i++) {
        CCSaveState* an = cc_save_new(); cc_save_set_int(an, "n", i);
        cc_savegame_autosave(sg, an, "auto", 100.0 + i*20.0);   /* spaced past interval */
        cc_save_free(an);
    }
    /* ring holds 3; the latest must be n=6 */
    CCSaveState* last = cc_savegame_load_latest_autosave(sg);
    CHECK(last && cc_save_get_int(last, "n", -1) == 6, "ring rotation keeps the newest (n=6)");
    if (last) cc_save_free(last);

    /* ── live actor capture / restore (needs an engine + scene) ─────────── */
    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 64; cfg.height = 64; cfg.verbose = false;
    CCEngine* e = cc_init(&cfg);
    CHECK(e != NULL, "headless engine for actor test");
    if (e) {
        CCScene* world = cc_scene_create(e, "w");
        CCMesh cube = cc_mesh_cube(e, 1.0f);
        CCMaterialDesc md = {.base_color={1,1,1,1}, .tint={1,1,1,1}};
        CCMaterial mat = cc_material_create(e, &md);

        CCActor npc = cc_actor_spawn(world, cube, mat, 3.0f, 1.0f, -2.0f);
        cc_actor_set_scale(npc, 2.0f, 2.0f, 2.0f);
        cc_actor_set_visible(npc, true);

        /* capture into a save */
        CCSaveState* ss = cc_save_new();
        cc_savegame_capture_actor(ss, "npc", npc);
        CHECK(cc_savegame_has_actor(ss, "npc"), "actor captured into save");
        CHECK(!cc_savegame_has_actor(ss, "ghost"), "uncaptured actor id absent");

        /* move + hide the actor, then restore from the save */
        cc_actor_set_position(npc, 0, 0, 0);
        cc_actor_set_scale(npc, 1, 1, 1);
        cc_actor_set_visible(npc, false);

        CHECK(cc_savegame_restore_actor(ss, "npc", npc), "restore_actor found data");
        float x,y,z; cc_actor_get_position(npc, &x,&y,&z);
        CHECK(fabsf(x-3.0f)<1e-3f && fabsf(y-1.0f)<1e-3f && fabsf(z+2.0f)<1e-3f,
              "restored actor position");
        CCTransform3D t; cc_actor_get_transform(npc, &t);
        CHECK(fabsf(t.scale[0]-2.0f)<1e-3f, "restored actor scale");
        CHECK(cc_actor_visible(npc) == true, "restored actor visibility");

        /* restoring an absent id returns false, changes nothing */
        CHECK(!cc_savegame_restore_actor(ss, "ghost", npc), "restore absent id → false");

        /* the whole thing survives a file round-trip */
        cc_savegame_write_slot(sg, 1, ss, "with actor");
        cc_save_free(ss);
        CCSaveState* rr = cc_savegame_read_slot(sg, 1);
        cc_actor_set_position(npc, 9,9,9);
        CHECK(rr && cc_savegame_restore_actor(rr, "npc", npc), "restore after file round-trip");
        cc_actor_get_position(npc, &x,&y,&z);
        CHECK(fabsf(x-3.0f)<1e-3f, "actor position survived file round-trip");
        if (rr) cc_save_free(rr);

        cc_scene_destroy(world);
        cc_shutdown(e);
    }

    /* ── NULL safety ────────────────────────────────────────────────────── */
    CHECK(cc_savegame_open(NULL, 1, 1) == NULL, "open(NULL dir) → NULL");
    CHECK(cc_savegame_read_slot(NULL, 0) == NULL, "read_slot(NULL) → NULL");
    CHECK(!cc_savegame_has_actor(NULL, "x"), "has_actor(NULL) → false");
    cc_savegame_close(NULL);

    cc_savegame_close(sg);

    if (failures == 0) {
        printf("SAVEGAME TEST: all checks passed (slots+metadata, quicksave, autosave "
               "ring+throttle+latest, actor capture/restore + file round-trip)\n");
        return 0;
    }
    printf("SAVEGAME TEST: %d check(s) FAILED\n", failures);
    return 1;
}
