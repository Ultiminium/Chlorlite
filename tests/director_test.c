/* director_test — exercises the CCDirector pacing FSM, spawn budget, bus
 * watching, and spawn-point selection. Pure logic; prints "DIRECTOR TEST: all
 * checks passed" and returns 0, else aborts. */
#include "cc/director.h"
#include "cc/event.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

int main(void) {
    /* 1. intensity clamps + decays --------------------------------------- */
    CCDirector* d = cc_director_create();
    CHECK(d != NULL, "director created");
    CHECK(cc_director_phase(d) == CC_DIR_REST, "starts in REST");
    cc_director_add_stress(d, 2.0f);
    CHECK(fabsf(cc_director_intensity(d) - 1.0f) < 1e-6f, "intensity clamps to 1.0");
    cc_director_add_stress(d, -5.0f);
    CHECK(cc_director_intensity(d) == 0.0f, "intensity clamps to 0.0");

    /* decay: set 0.5, update 1s with decay 0.08 -> ~0.42 */
    cc_director_set_intensity(d, 0.5f);
    /* jump out of REST first so we're not gated; force BUILD_UP via reset+config */
    cc_director_update(d, 1.0f);
    CHECK(cc_director_intensity(d) < 0.5f, "intensity decays over time");

    /* 2. full pacing cycle: REST → BUILD_UP → PEAK → FADE → REST ---------- */
    CCDirectorConfig cfg = cc_director_default_config();
    cfg.rest_duration = 2.0f; cfg.peak_duration = 2.0f;
    cfg.decay_rate = 0.05f;
    CCDirector* p = cc_director_create_cfg(&cfg);

    /* REST for rest_duration → BUILD_UP */
    for (int i=0;i<3;i++) cc_director_update(p, 1.0f);   /* 3s > 2s rest */
    CHECK(cc_director_phase(p) == CC_DIR_BUILD_UP, "REST times out into BUILD_UP");

    /* pump stress up past peak_threshold (0.85) → PEAK */
    for (int i=0;i<30 && cc_director_phase(p)==CC_DIR_BUILD_UP;i++) {
        cc_director_add_stress(p, 0.2f);
        cc_director_update(p, 0.1f);
    }
    CHECK(cc_director_phase(p) == CC_DIR_PEAK, "BUILD_UP reaches PEAK when intensity crosses threshold");

    /* hold PEAK for peak_duration → FADE (stop feeding stress) */
    for (int i=0;i<3;i++) cc_director_update(p, 1.0f);   /* 3s > 2s peak */
    CHECK(cc_director_phase(p) == CC_DIR_FADE, "PEAK times out into FADE");
    CHECK(cc_director_spawn_rate(p) == 0.0f, "spawn rate is 0 during FADE");

    /* in FADE, intensity decays until rest_threshold → REST */
    for (int i=0;i<200 && cc_director_phase(p)==CC_DIR_FADE;i++) cc_director_update(p, 0.1f);
    CHECK(cc_director_phase(p) == CC_DIR_REST, "FADE falls into REST once calm");

    /* 3. spawn budget: BUILD_UP accrues, should_spawn consumes ----------- */
    CCDirectorConfig sc = cc_director_default_config();
    sc.rest_duration = 0.0f;       /* leave REST immediately */
    sc.build_spawn_rate = 2.0f;    /* 2 spawns/sec */
    CCDirector* sp = cc_director_create_cfg(&sc);
    cc_director_update(sp, 0.01f);  /* REST(0s) → BUILD_UP */
    CHECK(cc_director_phase(sp) == CC_DIR_BUILD_UP, "zero rest_duration enters BUILD_UP");
    int spawns = 0;
    for (int i=0;i<100;i++) {           /* 100 * 0.1s = 10s at 2/s ≈ 20 spawns */
        cc_director_add_stress(sp, 0.001f); /* trickle so we stay in BUILD_UP a while */
        cc_director_update(sp, 0.1f);
        if (cc_director_should_spawn(sp)) spawns++;
        if (cc_director_phase(sp) != CC_DIR_BUILD_UP) break;
    }
    CHECK(spawns > 0, "should_spawn fires during BUILD_UP");

    /* FADE/REST never spawn */
    CCDirector* q = cc_director_create();  /* starts REST */
    int rest_spawns = 0;
    for (int i=0;i<50;i++){ cc_director_update(q,0.05f); if(cc_director_should_spawn(q)) rest_spawns++; }
    CHECK(rest_spawns == 0, "no spawns during REST");

    /* 4. bus watching raises intensity from combat events ---------------- */
    CCEventBus* bus = cc_event_bus_create();
    CCDirector* w = cc_director_create();
    cc_director_watch_bus(w, bus);
    CHECK(cc_director_intensity(w) == 0.0f, "watcher starts calm");
    cc_event_emit_if(bus, CC_EVT_DAMAGE, 0, 5 /*target*/, 10.0f /*amount*/);
    cc_event_emit_i(bus, CC_EVT_DEATH, 42, 0);
    cc_event_bus_update(bus);   /* deliver → director listener raises intensity */
    CHECK(cc_director_intensity(w) > 0.0f, "damage + death events raised intensity");
    float after_events = cc_director_intensity(w);
    /* unwatch: further events do nothing */
    cc_director_watch_bus(w, NULL);
    cc_event_emit_i(bus, CC_EVT_DEATH, 1, 0);
    cc_event_bus_update(bus);
    CHECK(fabsf(cc_director_intensity(w) - after_events) < 1e-6f, "unwatched director ignores events");
    /* destroying a still-watching director must unsubscribe cleanly (no UAF) */
    CCDirector* w2 = cc_director_create();
    cc_director_watch_bus(w2, bus);
    cc_director_destroy(w2);
    cc_event_emit_i(bus, CC_EVT_DEATH, 1, 0);
    cc_event_bus_update(bus);   /* must not touch freed w2 */
    CHECK(1, "destroying a watching director unsubscribed cleanly");

    /* 5. spawn-point selection: distance gate + behind-player preference -- */
    CCVec3 player = {0,0,0};
    /* facing +Z. candidates: one ahead(+Z), one behind(-Z), one too close, one too far */
    CCVec3 cands[4] = {
        {0,0, 20},   /* ahead, in range           */
        {0,0,-20},   /* behind, in range → best    */
        {0,0,  2},   /* too close (<8)             */
        {0,0,200},   /* too far (>40)              */
    };
    int32_t pick = cc_director_pick_spawn(w, player, 0.0f, 1.0f, cands, 4);
    CHECK(pick == 1, "picks the behind-player in-range candidate");

    CCVec3 near_only[1] = { {0,0,3} };
    CHECK(cc_director_pick_spawn(w, player, 0,1, near_only, 1) == -1,
          "returns -1 when all candidates fail the distance gate");

    /* determinism: same seed + inputs → same pick */
    CCDirector* a1 = cc_director_create();
    CCDirector* a2 = cc_director_create();
    CCVec3 many[3] = { {0,0,-15}, {0,0,-25}, {0,0,-35} };
    CHECK(cc_director_pick_spawn(a1, player, 0,1, many, 3) ==
          cc_director_pick_spawn(a2, player, 0,1, many, 3),
          "same seed → deterministic spawn pick");

    /* 6. NULL safety ----------------------------------------------------- */
    CHECK(cc_director_intensity(NULL) == 0.0f, "intensity(NULL)=0");
    cc_director_update(NULL, 0.1f);
    CHECK(!cc_director_should_spawn(NULL), "should_spawn(NULL)=false");
    CHECK(cc_director_pick_spawn(NULL, player, 0,1, cands, 4) == -1, "pick_spawn(NULL)=-1");
    cc_director_destroy(NULL);

    cc_director_destroy(d); cc_director_destroy(p); cc_director_destroy(sp);
    cc_director_destroy(q); cc_director_destroy(w);
    cc_director_destroy(a1); cc_director_destroy(a2);
    cc_event_bus_destroy(bus);

    if (failures == 0) {
        printf("DIRECTOR TEST: all checks passed (pacing FSM REST/BUILD_UP/PEAK/FADE, "
               "spawn budget, bus-driven intensity, behind-player spawn pick, determinism)\n");
        return 0;
    }
    printf("DIRECTOR TEST: %d check(s) FAILED\n", failures);
    return 1;
}
