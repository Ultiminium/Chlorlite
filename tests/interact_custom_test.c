/* interact_custom_test — proves the interactable system is a MECHANISM, not a
 * fixed set of types: it defines a fully custom interactable the engine has no
 * concept of (a "gravity rune" with developer-owned state + behavior) and checks
 * that the engine drives the developer's callbacks (interact/focus/unfocus/
 * update) and manages detection/range/cooldown/enable without knowing what the
 * thing does. Uses a headless engine for actors. */
#include "cc/claudecore.h"
#include "cc/interact.h"
#include "cc/event.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* ── a completely game-specific interactable the ENGINE knows nothing about ── */
typedef struct {
    float gravity;       /* the developer's own world state          */
    int   times_used;
    int   focus_events;
    int   unfocus_events;
    float pulse;         /* advanced by on_update                    */
} GravityRune;

static void rune_interact(CCInteractable* it, void* state) {
    (void)it;
    GravityRune* g = (GravityRune*)state;
    g->gravity = -g->gravity;   /* invert gravity — pure game behavior */
    g->times_used++;
}
static void rune_focus(CCInteractable* it, void* state) {
    (void)it; ((GravityRune*)state)->focus_events++;
}
static void rune_unfocus(CCInteractable* it, void* state) {
    (void)it; ((GravityRune*)state)->unfocus_events++;
}
static void rune_update(CCInteractable* it, float dt, void* state) {
    (void)it; ((GravityRune*)state)->pulse += dt;
}

/* a bus listener to confirm the generic CC_EVT_INTERACT_USED fires */
static int g_used_events = 0;
static void on_used(const CCEvent* e, void* u){ (void)e;(void)u; g_used_events++; }

int main(void) {
    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 64; cfg.height = 64; cfg.verbose = false;
    CCEngine* e = cc_init(&cfg);
    CHECK(e != NULL, "engine created");
    if (!e) { printf("INTERACT CUSTOM TEST: init failed\n"); return 1; }
    CCScene* world = cc_scene_create(e, "w");
    CCMesh cube = cc_mesh_cube(e, 1.0f);
    CCMaterialDesc md = {.base_color={1,1,1,1}, .tint={1,1,1,1}};
    CCMaterial mat = cc_material_create(e, &md);

    CCEventBus* bus = cc_event_bus_create();
    cc_event_subscribe(bus, CC_EVT_INTERACT_USED, on_used, NULL);
    cc_interactable_set_event_bus(world, bus);

    /* place the rune actor and define a fully custom interactable for it */
    CCActor rune_actor = cc_actor_spawn(world, cube, mat, 5.0f, 0.0f, 0.0f);
    GravityRune rune = { .gravity = 9.8f };

    CCInteractDesc d = {0};
    d.range       = 3.0f;
    d.prompt      = "Touch the rune";
    d.state       = &rune;
    d.cooldown    = 0.5f;
    d.on_interact = rune_interact;
    d.on_focus    = rune_focus;
    d.on_unfocus  = rune_unfocus;
    d.on_update   = rune_update;
    CCInteractable* it = cc_interactable_create(world, rune_actor, &d);
    CHECK(it != NULL, "custom interactable created");
    CHECK(cc_interactable_state(it) == &rune, "state pointer round-trips");
    CHECK(strcmp(cc_interactable_prompt(it), "Touch the rune") == 0, "custom prompt set");

    /* ── detection: out of range vs in range ────────────────────────────── */
    /* player far away (at origin, rune at x=5, range 3) → no focus target */
    CCInteractable* q = cc_interactable_query(world, 0,0,0, 0,0);
    CHECK(q == NULL, "rune not detected out of range");
    CHECK(rune.focus_events == 0, "no focus event while out of range");

    /* player steps within range (x=3, rune at x=5, dist 2 < 3) */
    q = cc_interactable_query(world, 3.0f,0,0, 0,0);
    CHECK(q == it, "rune detected in range");
    CHECK(rune.focus_events == 1, "on_focus fired when it became the target");

    /* ── trigger runs the DEVELOPER'S behavior (invert gravity) ─────────── */
    float g_before = rune.gravity;
    CHECK(cc_interactable_trigger(world, it), "trigger fired");
    CHECK(rune.gravity == -g_before, "developer behavior ran: gravity inverted");
    CHECK(rune.times_used == 1, "on_interact counted one use");
    cc_event_bus_update(bus);   /* deferred bus: deliver queued events */
    CHECK(g_used_events == 1, "generic CC_EVT_INTERACT_USED published on the bus");

    /* ── cooldown: immediate re-trigger blocked, then allowed after time ── */
    CHECK(!cc_interactable_trigger(world, it), "re-trigger blocked during cooldown");
    CHECK(rune.times_used == 1, "no extra use during cooldown");
    for (int i = 0; i < 40; i++) cc_interactable_update(world, 1.0f/60.0f);  /* ~0.66s */
    CHECK(cc_interactable_trigger(world, it), "trigger allowed after cooldown elapsed");
    CHECK(rune.times_used == 2, "second use registered");
    CHECK(fabsf(rune.gravity - g_before) < 1e-4f, "gravity inverted back to original");

    /* ── on_update ran each frame while registered ──────────────────────── */
    CHECK(rune.pulse > 0.5f, "on_update advanced the rune's pulse over time");

    /* ── focus leaves when the player walks away → on_unfocus ───────────── */
    cc_interactable_query(world, 100.0f,0,0, 0,0);   /* far → best is NULL */
    CHECK(rune.unfocus_events == 1, "on_unfocus fired when focus left");

    /* ── enable/disable gating ──────────────────────────────────────────── */
    cc_interactable_set_enabled(it, false);
    CHECK(!cc_interactable_enabled(it), "disabled reported");
    CHECK(cc_interactable_query(world, 3.0f,0,0, 0,0) == NULL, "disabled rune not detected");
    CHECK(!cc_interactable_trigger(world, it), "disabled rune cannot be triggered");
    cc_interactable_set_enabled(it, true);
    CHECK(cc_interactable_query(world, 3.0f,0,0, 0,0) == it, "re-enabled rune detected again");

    /* ── the built-ins still work (backward compat) alongside custom ────── */
    CCActor door_actor = cc_actor_spawn(world, cube, mat, 3.0f, 0.0f, 0.0f);
    CCInteractable* door = cc_interactable_register(world, door_actor, CC_INTERACT_DOOR, 3.0f);
    CHECK(door != NULL, "built-in door still registers");
    CHECK(cc_interactable_trigger(world, door), "built-in door still triggers");
    CHECK(cc_interactable_is_on(door), "door opened via built-in behavior");

    cc_interactable_clear(world);
    cc_event_bus_destroy(bus);
    cc_scene_destroy(world);
    cc_shutdown(e);

    if (failures == 0) {
        printf("INTERACT CUSTOM TEST: all checks passed (developer-defined interactable: "
               "engine drives interact/focus/unfocus/update + range/cooldown/enable, "
               "behavior is the developer's; built-ins still work)\n");
        return 0;
    }
    printf("INTERACT CUSTOM TEST: %d check(s) FAILED\n", failures);
    return 1;
}
