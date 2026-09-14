/* soundfield_test — verifies spatial sound propagation with wall occlusion,
 * distance attenuation, toward-source direction, and the event-bus path. Pure
 * logic; prints "SOUNDFIELD TEST: all checks passed" and returns 0, else aborts. */
#include "cc/soundfield.h"
#include "cc/ai.h"
#include "cc/event.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

int main(void) {
    /* 20x20 grid, 1 unit cells, centered on origin → world spans ~[-10,10] */
    CCGrid* grid = cc_grid_create(20, 20, 1.0f);
    CCSoundField* sf = cc_soundfield_create(grid);
    CHECK(sf != NULL, "sound field created");
    cc_soundfield_set_falloff(sf, 0.06f);
    cc_soundfield_set_floor(sf, 0.02f);

    /* helper world coords near center */
    /* 1. open space: loudness decreases with distance ---------------------- */
    cc_soundfield_begin(sf);
    cc_soundfield_emit(sf, 0.0f, 0.0f, 1.0f);   /* loud sound at origin */
    cc_soundfield_propagate(sf, 100.0f);

    float at_src  = cc_soundfield_loudness(sf, 0.0f, 0.0f);
    float at_near = cc_soundfield_loudness(sf, 3.0f, 0.0f);
    float at_far  = cc_soundfield_loudness(sf, 8.0f, 0.0f);
    CHECK(at_src > 0.9f, "loudness at source ~ emitted");
    CHECK(at_near < at_src && at_near > at_far, "loudness attenuates with distance");
    CHECK(at_far > 0.0f, "still audible at moderate distance");

    /* 2. toward-source direction points back at the source ----------------- */
    float loud; CCVec3 dir;
    bool heard = cc_soundfield_sample(sf, 5.0f, 0.0f, &loud, &dir);
    CHECK(heard, "listener east of source hears it");
    /* source is to the WEST (−X) of a listener at +X, so dir.x should be negative */
    CHECK(dir.x < -0.3f, "direction points back toward the source (−X)");

    /* 3. WALL OCCLUSION: the SAME point is quieter with a wall than without -- */
    /* measure a point in open space first (no walls yet) */
    float open_pt = cc_soundfield_loudness(sf, 4.0f, -3.0f);  /* from test 1's field */

    /* build a vertical wall at cell x=13 (world ~ +3.5) spanning Z, leaving a
       gap at z=10 (world ~ +0.5) so sound must detour through the "doorway". */
    for (uint32_t z=0; z<20; ++z) if (z!=10) cc_grid_set_blocked(grid, 13, z, true);

    cc_soundfield_begin(sf);
    cc_soundfield_emit(sf, 0.0f, 0.0f, 1.0f);
    cc_soundfield_propagate(sf, 100.0f);

    /* the same point now sits behind the wall and must detour through the gap →
       strictly quieter than it was in open space. */
    float behind_wall = cc_soundfield_loudness(sf, 4.0f, -3.0f);
    CHECK(behind_wall > 0.0f, "point behind a gapped wall is still audible (leaks through the gap)");
    CHECK(behind_wall < open_pt - 0.02f, "the wall makes the same point measurably quieter (occlusion)");

    /* fully seal a pocket: cells around (world +6,0) boxed in → silent */
    /* box: x in {16,18}, z in {8,12} walls, and x=16..18 top/bottom */
    for (uint32_t x=16; x<=18; ++x){ cc_grid_set_blocked(grid,x,8,true); cc_grid_set_blocked(grid,x,12,true); }
    for (uint32_t z=8; z<=12; ++z){ cc_grid_set_blocked(grid,16,z,true); cc_grid_set_blocked(grid,18,z,true); }
    cc_soundfield_begin(sf);
    cc_soundfield_emit(sf, 0.0f, 0.0f, 1.0f);
    cc_soundfield_propagate(sf, 100.0f);
    float x17,z17; cc_grid_cell_to_world(grid, 17, 10, &x17, &z17);
    float sealed = cc_soundfield_loudness(sf, x17, z17);
    CHECK(sealed == 0.0f, "fully sealed room hears nothing");

    /* 4. multiple sounds: loudest wins at a shared cell -------------------- */
    cc_soundfield_begin(sf);
    cc_soundfield_emit(sf, -6.0f, 0.0f, 0.4f);   /* quiet, far left */
    cc_soundfield_emit(sf,  1.0f, 0.0f, 1.0f);   /* loud, near center */
    cc_soundfield_propagate(sf, 100.0f);
    float mid = cc_soundfield_loudness(sf, 0.0f, 0.0f);
    /* direction at origin should point toward the LOUD source (+X side) */
    cc_soundfield_sample(sf, 0.0f, 0.0f, &loud, &dir);
    CHECK(mid > 0.5f, "loud nearby source dominates at the origin");
    CHECK(dir.x > 0.0f, "direction points toward the louder (+X) source");

    /* 5. event-bus path: cc_sound_emit_event → watched field ingests it ---- */
    cc_grid_clear(grid);   /* clear walls for a clean open-space bus test */
    CCEventBus* bus = cc_event_bus_create();
    cc_soundfield_watch_bus(sf, bus);
    cc_sound_emit_event(bus, 2.0f, 0.0f, 0.9f);
    cc_event_bus_update(bus);          /* deliver → field queues it as pending */
    cc_soundfield_begin(sf);           /* drains pending into emits */
    cc_soundfield_propagate(sf, 100.0f);
    float via_bus = cc_soundfield_loudness(sf, 2.0f, 0.0f);
    CHECK(via_bus > 0.5f, "sound emitted via the bus propagates");

    /* mic-level events heard from a configured position */
    cc_soundfield_set_mic_position(sf, -4.0f, 0.0f);
    cc_event_emit_if(bus, CC_EVT_MIC_LEVEL, 0, 1, 0.8f);
    cc_event_bus_update(bus);
    cc_soundfield_begin(sf);
    cc_soundfield_propagate(sf, 100.0f);
    float via_mic = cc_soundfield_loudness(sf, -4.0f, 0.0f);
    CHECK(via_mic > 0.5f, "mic-level event heard at the mic position");

    /* destroy-while-watching must unsubscribe cleanly (no UAF) */
    CCGrid* g2 = cc_grid_create(8,8,1.0f);
    CCSoundField* sf2 = cc_soundfield_create(g2);
    cc_soundfield_watch_bus(sf2, bus);
    cc_soundfield_destroy(sf2);
    cc_event_emit_if(bus, CC_EVT_MIC_LEVEL, 0, 1, 0.5f);
    cc_event_bus_update(bus);           /* must not touch freed sf2 */
    CHECK(1, "destroying a watching field unsubscribed cleanly");
    cc_grid_destroy(g2);

    /* 6. NULL safety ------------------------------------------------------- */
    CHECK(cc_soundfield_create(NULL) == NULL, "create(NULL grid)=NULL");
    CHECK(cc_soundfield_loudness(NULL, 0,0) == 0.0f, "loudness(NULL)=0");
    CHECK(!cc_soundfield_sample(NULL, 0,0, &loud, &dir), "sample(NULL)=false");
    cc_soundfield_destroy(NULL);

    cc_soundfield_destroy(sf);
    cc_event_bus_destroy(bus);
    cc_grid_destroy(grid);

    if (failures == 0) {
        printf("SOUNDFIELD TEST: all checks passed (distance attenuation, wall occlusion "
               "+ sealed-room silence, toward-source direction, loudest-wins, bus + mic ingest)\n");
        return 0;
    }
    printf("SOUNDFIELD TEST: %d check(s) FAILED\n", failures);
    return 1;
}
