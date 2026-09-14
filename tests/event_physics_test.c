/* event_physics_test — proves physics contacts publish CC_EVT_CONTACT on an
 * attached event bus, carrying both body ids and a plausible impact speed, and
 * that an existing contact_event callback STILL fires (additive). With no bus,
 * behavior is unchanged. Pure logic; prints "EVENT PHYSICS TEST: all checks
 * passed" and returns 0, else aborts. */
#include "cc/physics.h"
#include "cc/event.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* bus capture */
static int      g_contact_events = 0;
static float    g_max_impact = 0.0f;
static uint64_t g_last_a = 0; static int64_t g_last_b = -1;
static void on_contact(const CCEvent* e, void* u) {
    (void)u;
    if (e->channel == CC_EVT_CONTACT) {
        g_contact_events++;
        if (e->f > g_max_impact) g_max_impact = e->f;
        g_last_a = e->sender; g_last_b = e->i;
    }
}
/* legacy contact callback — must still fire alongside the bus */
static int g_cb_hits = 0;
static void contact_cb(const CCContact* c, void* u) { (void)c;(void)u; g_cb_hits++; }

/* drop a dynamic sphere onto a static ground; return contact-event count seen */
static int run_drop(CCEventBus* bus, int with_callback) {
    CCPhysicsWorld* w = cc_physics_create();
    cc_physics_set_gravity(w, (CCVec3){0,-20.0f,0});
    if (bus) cc_phys_set_event_bus(w, bus);
    if (with_callback) cc_phys_set_contact_event(w, contact_cb, NULL);

    CCCollider ground = {.type=CC_SHAPE_PLANE, .plane={.normal={0,1,0}, .offset=0}};
    cc_body_create(w, CC_BODY_STATIC, ground, cc_body_material_default(), (CCVec3){0,0,0});

    CCCollider ball = {.type=CC_SHAPE_SPHERE, .sphere={.radius=0.5f}};
    CCBodyId b = cc_body_create(w, CC_BODY_DYNAMIC, ball, cc_body_material_default(),
                                (CCVec3){0, 6.0f, 0});
    (void)b;

    int local = 0;
    for (int step=0; step<240; step++) {
        cc_physics_step(w, 1.0f/60.0f);
        if (bus) local += (int)cc_event_bus_update(bus);  /* flush per frame */
    }
    cc_physics_destroy(w);
    return local;
}

int main(void) {
    CCEventBus* bus = cc_event_bus_create();
    cc_event_subscribe(bus, CC_EVT_CONTACT, on_contact, NULL);

    /* 1. with a bus + a legacy callback: both must observe the impact --------- */
    int delivered = run_drop(bus, /*with_callback=*/1);
    CHECK(delivered > 0, "bus delivered at least one contact event");
    CHECK(g_contact_events > 0, "CC_EVT_CONTACT published on collision");
    CHECK(g_cb_hits > 0, "legacy contact_event callback ALSO fired (additive)");
    CHECK(g_max_impact > 1.0f, "impact speed is plausible (>1 m/s from a 6m drop)");
    /* the ground is body 1 (created first), the ball is body 2 */
    CHECK(g_last_a != g_last_b + 0 /*trivially true*/, "contact carries two body ids");
    CHECK((g_last_a==1 && g_last_b==2) || (g_last_a==2 && g_last_b==1),
          "contact identifies the ground+ball body ids");

    /* 2. NO bus: must not crash and callback path still works ----------------- */
    g_cb_hits = 0;
    run_drop(NULL, /*with_callback=*/1);
    CHECK(g_cb_hits > 0, "callback still fires with no bus attached");

    /* 3. detached (bus set then NULL): no further contact events -------------- */
    g_contact_events = 0;
    {
        CCPhysicsWorld* w = cc_physics_create();
        cc_physics_set_gravity(w, (CCVec3){0,-20.0f,0});
        cc_phys_set_event_bus(w, bus);
        cc_phys_set_event_bus(w, NULL);   /* detach */
        CCCollider ground = {.type=CC_SHAPE_PLANE, .plane={.normal={0,1,0}, .offset=0}};
        cc_body_create(w, CC_BODY_STATIC, ground, cc_body_material_default(), (CCVec3){0,0,0});
        CCCollider ball = {.type=CC_SHAPE_SPHERE, .sphere={.radius=0.5f}};
        cc_body_create(w, CC_BODY_DYNAMIC, ball, cc_body_material_default(), (CCVec3){0,4,0});
        for (int s=0;s<240;s++){ cc_physics_step(w,1.0f/60.0f); cc_event_bus_update(bus); }
        cc_physics_destroy(w);
    }
    CHECK(g_contact_events == 0, "detached bus receives no contact events");

    cc_event_bus_destroy(bus);

    if (failures == 0) {
        printf("EVENT PHYSICS TEST: all checks passed (CC_EVT_CONTACT with impact speed + "
               "body ids; legacy callback still fires; detach = silent)\n");
        return 0;
    }
    printf("EVENT PHYSICS TEST: %d check(s) FAILED\n", failures);
    return 1;
}
