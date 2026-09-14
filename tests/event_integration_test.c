/* event_integration_test — proves the engine's own subsystems (interactables,
 * dialogue) actually PUBLISH on the reserved CC_EVT_* channels once an event bus
 * is attached, and that with NO bus attached their behavior is unchanged.
 * Pure logic; prints "EVENT INTEGRATION TEST: all checks passed" / returns 0. */
#include "cc/claudecore.h"
#include "cc/interact.h"
#include "cc/dialogue.h"
#include "cc/event.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* ── capture every event on every channel ───────────────────────────────── */
#define MAXLOG 64
static uint32_t log_ch[MAXLOG];
static int64_t  log_i[MAXLOG];
static uint64_t log_sender[MAXLOG];
static int      log_n = 0;
static void monitor(const CCEvent* e, void* u) {
    (void)u;
    if (log_n < MAXLOG) {
        log_ch[log_n]=e->channel; log_i[log_n]=e->i; log_sender[log_n]=e->sender; log_n++;
    }
}
static int count_ch(uint32_t ch){ int c=0; for(int k=0;k<log_n;k++) if(log_ch[k]==ch) c++; return c; }
static int find_ch(uint32_t ch){ for(int k=0;k<log_n;k++) if(log_ch[k]==ch) return k; return -1; }

/* track that the per-interactable callback STILL fires (additive, not replaced) */
static int g_cb_fired = 0;
static void door_cb(CCInteractable* it, bool on, void* ud){ (void)it;(void)on;(void)ud; g_cb_fired++; }

int main(void) {
    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 320; cfg.height = 240; cfg.verbose = false;
    CCEngine* eng = cc_init(&cfg);
    CHECK(eng != NULL, "engine created (headless)");
    if (!eng) return 1;
    CCScene* scene = cc_scene_create(eng, "level");

    CCMesh cube = cc_mesh_cube(eng, 1.0f);
    CCMaterialDesc md = {.base_color={0.6f,0.6f,0.6f,1}, .roughness=0.7f, .tint={1,1,1,1}};
    CCMaterial mat = cc_material_create(eng, &md);

    CCEventBus* bus = cc_event_bus_create();
    cc_event_subscribe_any(bus, monitor, NULL);

    /* ── INTERACTABLES ──────────────────────────────────────────────────── */
    cc_interactable_set_event_bus(scene, bus);

    CCActor door = cc_actor_spawn(scene, cube, mat, 0,1,0);
    CCInteractable* d = cc_interactable_register(scene, door, CC_INTERACT_DOOR, 3.0f);
    cc_interactable_set_callback(d, door_cb, NULL);

    CCActor sw = cc_actor_spawn(scene, cube, mat, 2,1,0);
    CCInteractable* s = cc_interactable_register(scene, sw, CC_INTERACT_SWITCH, 3.0f);

    CCActor gem = cc_actor_spawn(scene, cube, mat, 4,1,0);
    CCInteractable* p = cc_interactable_register(scene, gem, CC_INTERACT_PICKUP, 3.0f);

    CCActor vault = cc_actor_spawn(scene, cube, mat, 6,1,0);
    CCInteractable* v = cc_interactable_register(scene, vault, CC_INTERACT_DOOR, 3.0f);
    cc_interactable_set_locked(v, true);

    /* trigger them */
    CHECK(cc_interactable_trigger(scene, d), "door triggers");          /* -> DOOR_OPENED */
    CHECK(cc_interactable_trigger(scene, s), "switch triggers");        /* -> SWITCH_TOGGLED on */
    CHECK(cc_interactable_trigger(scene, p), "pickup triggers");        /* -> ITEM_PICKED_UP */
    CHECK(!cc_interactable_trigger(scene, v), "locked door blocked");   /* -> INTERACT_LOCKED */

    /* events are DEFERRED — nothing logged until we pump the bus */
    CHECK(log_n == 0, "no events delivered before bus update (deferred)");
    cc_event_bus_update(bus);

    CHECK(count_ch(CC_EVT_DOOR_OPENED) == 1, "DOOR_OPENED published once");
    CHECK(count_ch(CC_EVT_SWITCH_TOGGLED) == 1, "SWITCH_TOGGLED published once");
    CHECK(count_ch(CC_EVT_ITEM_PICKED_UP) == 1, "ITEM_PICKED_UP published once");
    CHECK(count_ch(CC_EVT_INTERACT_LOCKED) == 1, "INTERACT_LOCKED published once");
    CHECK(g_cb_fired == 1, "per-interactable callback ALSO fired (additive)");

    /* sender carries the actor's entity id */
    int di = find_ch(CC_EVT_DOOR_OPENED);
    CHECK(di >= 0 && log_sender[di] == door.id, "DOOR_OPENED sender = door actor id");
    int si = find_ch(CC_EVT_SWITCH_TOGGLED);
    CHECK(si >= 0 && log_i[si] == 1, "SWITCH_TOGGLED i=1 (on)");

    /* re-trigger the door: it's mid-swing (animating) so it must NOT re-publish */
    log_n = 0;
    cc_interactable_trigger(scene, d);
    cc_event_bus_update(bus);
    CHECK(count_ch(CC_EVT_DOOR_OPENED)==0 && count_ch(CC_EVT_DOOR_CLOSED)==0,
          "animating door does not re-publish");

    /* ── DIALOGUE ───────────────────────────────────────────────────────── */
    const char* script =
        "ccdlg 1\n"
        "node root\n"
        "  speaker Guard\n"
        "  text Halt. State your business.\n"
        "  choice Just passing -> bye\n"
        "  choice Open up -> gate\n"
        "node gate\n"
        "  speaker Guard\n"
        "  text The gate stays shut.\n"
        "  goto bye\n"
        "node bye\n"
        "  speaker Guard\n"
        "  text Move along.\n"
        "  end\n";
    CCDialogue* dlg = cc_dialogue_parse(script);
    CHECK(dlg != NULL, "dialogue parsed");

    log_n = 0;
    CCDialogueRunner* r = cc_dialogue_start(dlg, "root");
    cc_dialogue_set_event_bus(r, bus, 0x7777);   /* convo id = sender */
    cc_event_bus_update(bus);
    CHECK(count_ch(CC_EVT_DIALOGUE_STARTED) == 1, "DIALOGUE_STARTED published");
    CHECK(count_ch(CC_EVT_DIALOGUE_NODE) == 1, "first DIALOGUE_NODE published");
    int ni = find_ch(CC_EVT_DIALOGUE_NODE);
    CHECK(ni >= 0 && log_sender[ni] == 0x7777, "dialogue sender = convo id");

    /* pick 'Open up' -> gate, which auto-advances (goto) to bye */
    log_n = 0;
    CHECK(cc_dialogue_choose(r, 1), "chose 'Open up'");   /* -> NODE(gate) */
    cc_dialogue_advance(r);                                /* gate goto bye -> NODE(bye) */
    cc_event_bus_update(bus);
    CHECK(count_ch(CC_EVT_DIALOGUE_NODE) == 2, "two node transitions published (gate, bye)");

    /* end the conversation */
    log_n = 0;
    cc_dialogue_advance(r);                                /* bye is end -> ENDED */
    cc_event_bus_update(bus);
    CHECK(count_ch(CC_EVT_DIALOGUE_ENDED) == 1, "DIALOGUE_ENDED published at finish");
    CHECK(cc_dialogue_finished(r), "runner reports finished");

    /* ── NO-BUS PARITY: detaching yields zero further events ─────────────── */
    cc_interactable_set_event_bus(scene, NULL);
    CCActor door2 = cc_actor_spawn(scene, cube, mat, 8,1,0);
    CCInteractable* d2 = cc_interactable_register(scene, door2, CC_INTERACT_DOOR, 3.0f);
    log_n = 0;
    CHECK(cc_interactable_trigger(scene, d2), "door2 triggers with no bus");
    cc_event_bus_update(bus);
    CHECK(log_n == 0, "detached bus receives nothing (behavior unchanged)");

    cc_dialogue_free_runner(r);
    cc_dialogue_free(dlg);
    cc_event_bus_destroy(bus);
    cc_shutdown(eng);

    if (failures == 0) {
        printf("EVENT INTEGRATION TEST: all checks passed (interact + dialogue publish "
               "on reserved channels; callbacks still fire; detach = silent)\n");
        return 0;
    }
    printf("EVENT INTEGRATION TEST: %d check(s) FAILED\n", failures);
    return 1;
}
