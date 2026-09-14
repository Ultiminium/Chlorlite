/* event_test — exercises the CCEventBus contract. Pure logic (no renderer):
 * prints "EVENT TEST: all checks passed" and returns 0 on success, else aborts
 * with a message and returns 1. Mirrors the style of save_test/dialogue_test. */
#include "cc/event.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* ── shared counters for listeners ──────────────────────────────────────── */
static int   g_door_hits, g_any_hits, g_dmg_hits;
static int64_t g_last_i;
static float   g_last_f;
static uint64_t g_last_sender;
static char    g_blob[64];
static uint32_t g_blob_size;

static void on_door(const CCEvent* e, void* u) {
    (void)u; g_door_hits++; g_last_i = e->i; g_last_sender = e->sender;
}
static void on_any(const CCEvent* e, void* u) { (void)e; (void)u; g_any_hits++; }
static void on_damage(const CCEvent* e, void* u) {
    (void)u; g_dmg_hits++; g_last_f = e->f; g_last_i = e->i;
    if (e->data && e->data_size) {
        g_blob_size = e->data_size;
        memcpy(g_blob, e->data, e->data_size < sizeof(g_blob) ? e->data_size : sizeof(g_blob));
    }
}

/* ── re-entrancy: a listener that publishes another event ───────────────── */
static CCEventBus* g_bus;
static int g_chain_a, g_chain_b;
static void chain_a(const CCEvent* e, void* u) {
    (void)e; (void)u; g_chain_a++;
    /* publish B while A is being dispatched — must NOT be delivered this frame */
    cc_event_emit_i(g_bus, CC_EVT_USER + 1, 0, 99);
}
static void chain_b(const CCEvent* e, void* u) { (void)e; (void)u; g_chain_b++; }

/* ── self-unsubscribe during dispatch ───────────────────────────────────── */
static CCListenerId g_self_id;
static int g_self_hits;
static void self_kill(const CCEvent* e, void* u) {
    (void)e; (void)u; g_self_hits++;
    cc_event_unsubscribe(g_bus, g_self_id);   /* remove myself mid-dispatch */
}

int main(void) {
    CCEventBus* bus = cc_event_bus_create();
    g_bus = bus;
    CHECK(bus != NULL, "bus created");

    /* 1. basic deferred pub/sub + channel filtering ----------------------- */
    CCListenerId ld = cc_event_subscribe(bus, CC_EVT_DOOR_OPENED, on_door, NULL);
    CCListenerId la = cc_event_subscribe_any(bus, on_any, NULL);
    CHECK(ld && la, "subscribe returns nonzero handles");
    CHECK(cc_event_listener_count(bus) == 2, "listener count = 2");

    cc_event_emit_i(bus, CC_EVT_DOOR_OPENED, 0xABCD, 1);
    cc_event_emit_i(bus, CC_EVT_SWITCH_TOGGLED, 0, 0);  /* nobody on this chan but 'any' */
    CHECK(cc_event_queued_count(bus) == 2, "two events queued");
    CHECK(g_door_hits == 0, "nothing delivered before update (deferred)");

    uint32_t n = cc_event_bus_update(bus);
    CHECK(n == 2, "update delivered 2 events");
    CHECK(g_door_hits == 1, "door listener got exactly its channel");
    CHECK(g_last_i == 1 && g_last_sender == 0xABCD, "door event payload correct");
    CHECK(g_any_hits == 2, "wildcard listener got both events");
    CHECK(cc_event_queued_count(bus) == 0, "queue empty after flush");

    /* 2. re-entrant publish rolls to the NEXT frame ----------------------- */
    cc_event_subscribe(bus, CC_EVT_USER + 0, chain_a, NULL);
    cc_event_subscribe(bus, CC_EVT_USER + 1, chain_b, NULL);
    g_any_hits = 0;
    cc_event_emit_i(bus, CC_EVT_USER + 0, 0, 0);
    cc_event_bus_update(bus);                 /* delivers A; A publishes B */
    CHECK(g_chain_a == 1, "chain A delivered this frame");
    CHECK(g_chain_b == 0, "chain B NOT delivered same frame (deferred re-entry)");
    CHECK(cc_event_queued_count(bus) == 1, "B is queued for next frame");
    cc_event_bus_update(bus);                 /* now B */
    CHECK(g_chain_b == 1, "chain B delivered next frame");

    /* 3. synchronous emit_now delivers immediately ------------------------ */
    g_door_hits = 0;
    CCEvent ev = {0}; ev.channel = CC_EVT_DOOR_OPENED; ev.sender = 7; ev.i = 5;
    uint32_t got = cc_event_emit_now(bus, &ev);
    CHECK(g_door_hits == 1, "emit_now delivered synchronously");
    CHECK(got >= 1, "emit_now returned notified count");
    CHECK(g_last_sender == 7 && g_last_i == 5, "emit_now payload correct");

    /* 4. payload blob is copied (stack buffer safe) ----------------------- */
    cc_event_subscribe(bus, CC_EVT_DAMAGE, on_damage, NULL);
    {
        char tmp[16]; memcpy(tmp, "hit:crit", 9);
        cc_event_emit_data(bus, CC_EVT_DAMAGE, 100, 42, 12.5f, tmp, 9);
        memset(tmp, 0, sizeof(tmp));          /* clobber the source after emit */
    }
    cc_event_bus_update(bus);
    CHECK(g_dmg_hits == 1, "damage listener fired");
    CHECK(g_last_f == 12.5f && g_last_i == 42, "damage scalars correct");
    CHECK(g_blob_size == 9 && memcmp(g_blob, "hit:crit", 9) == 0,
          "payload blob copied correctly despite source clobber");

    /* 5. unsubscribe stops delivery + stale handle is rejected ------------ */
    cc_event_unsubscribe(bus, ld);
    g_door_hits = 0;
    cc_event_emit_i(bus, CC_EVT_DOOR_OPENED, 0, 0);
    cc_event_bus_update(bus);
    CHECK(g_door_hits == 0, "unsubscribed listener no longer fires");
    cc_event_unsubscribe(bus, ld);            /* double-unsub: must be a no-op */
    CHECK(1, "double unsubscribe did not crash");

    /* 6. self-unsubscribe during dispatch --------------------------------- */
    g_self_id = cc_event_subscribe(bus, CC_EVT_USER + 5, self_kill, NULL);
    cc_event_emit_i(bus, CC_EVT_USER + 5, 0, 0);
    cc_event_bus_update(bus);                 /* fires once, then removes itself */
    cc_event_emit_i(bus, CC_EVT_USER + 5, 0, 0);
    cc_event_bus_update(bus);                 /* must NOT fire again */
    CHECK(g_self_hits == 1, "self-unsubscribe during dispatch fires exactly once");

    /* 7. clear drops everything ------------------------------------------- */
    cc_event_subscribe(bus, CC_EVT_HEAL, on_any, NULL);
    cc_event_emit_i(bus, CC_EVT_HEAL, 0, 0);
    cc_event_bus_clear(bus);
    CHECK(cc_event_listener_count(bus) == 0, "clear removed all listeners");
    CHECK(cc_event_queued_count(bus) == 0, "clear drained the queue");
    g_any_hits = 0;
    cc_event_bus_update(bus);
    CHECK(g_any_hits == 0, "nothing fires after clear");

    /* 8. NULL-safety ------------------------------------------------------ */
    CHECK(cc_event_subscribe(NULL, 0, on_any, NULL) == 0, "subscribe(NULL) = 0");
    CHECK(cc_event_emit_i(NULL, 0, 0, 0) == false, "emit(NULL) = false");
    CHECK(cc_event_bus_update(NULL) == 0, "update(NULL) = 0");
    cc_event_unsubscribe(NULL, 1);            /* must not crash */
    cc_event_bus_destroy(NULL);               /* must not crash */

    cc_event_bus_destroy(bus);

    if (failures == 0) {
        printf("EVENT TEST: all checks passed (deferred + sync delivery, wildcard, "
               "re-entrancy defer, payload copy, safe unsub-in-dispatch, clear)\n");
        return 0;
    }
    printf("EVENT TEST: %d check(s) FAILED\n", failures);
    return 1;
}
