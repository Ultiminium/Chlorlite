/* audiofx_test — verifies spatial audio shaping: wall occlusion (volume +
 * lowpass grow with wall thickness, clear line = no occlusion) and reverb zones
 * (listener inside a box gets its reverb; overlapping → smallest wins; dry
 * outside). Pure logic; prints "AUDIOFX TEST: all checks passed" / returns 0. */
#include "cc/audiofx.h"
#include "cc/ai.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

int main(void) {
    CCAudioFX* fx = cc_audiofx_create();
    CHECK(fx != NULL, "audiofx created");

    /* ── occlusion ──────────────────────────────────────────────────────── */
    /* 20x20 grid, 1u cells, centered → world ~[-10,10]. Wall column at cell
       x=10 (world ~0) with a gap, listener on one side, source on the other. */
    CCGrid* grid = cc_grid_create(20, 20, 1.0f);
    cc_audiofx_set_grid(fx, grid);

    /* no walls yet: clear line = no occlusion */
    cc_audiofx_set_listener(fx, -5.0f, 0.0f);
    CCOcclusion clear = cc_audiofx_occlusion(fx, 5.0f, 0.0f);
    CHECK(!clear.occluded, "clear line: not occluded");
    CHECK(clear.volume > 0.99f && clear.lowpass < 0.01f, "clear line: full volume, no lowpass");

    /* build a solid wall column at cell x=10 (spans all z) */
    for (uint32_t z = 0; z < 20; ++z) cc_grid_set_blocked(grid, 10, z, true);

    /* now source (+5,0) and listener (-5,0) are on opposite sides of the wall */
    CCOcclusion occ = cc_audiofx_occlusion(fx, 5.0f, 0.0f);
    CHECK(occ.occluded, "wall between source and listener → occluded");
    CHECK(occ.volume < 0.99f, "occlusion reduces volume");
    CHECK(occ.lowpass > 0.0f, "occlusion adds lowpass (muffle)");

    /* thicker wall → more muffle. Add a second + third wall column adjacent. */
    for (uint32_t z = 0; z < 20; ++z) { cc_grid_set_blocked(grid, 9, z, true); cc_grid_set_blocked(grid, 11, z, true); }
    CCOcclusion thick = cc_audiofx_occlusion(fx, 5.0f, 0.0f);
    CHECK(thick.lowpass >= occ.lowpass, "thicker wall → at least as much muffle");
    CHECK(thick.volume <= occ.volume + 1e-6f, "thicker wall → not louder");

    /* volume never drops below the configured floor */
    cc_audiofx_set_occlusion(fx, 0.3f, 0.9f);   /* min_volume 0.3, strong per-cell */
    CCOcclusion floored = cc_audiofx_occlusion(fx, 5.0f, 0.0f);
    CHECK(floored.volume >= 0.3f - 1e-6f, "occluded volume respects the min floor");

    /* a source on the SAME side (no wall between) is clear again */
    cc_audiofx_set_listener(fx, 5.0f, 0.0f);
    CCOcclusion same = cc_audiofx_occlusion(fx, 6.0f, 0.0f);
    CHECK(!same.occluded, "same side of wall: not occluded");

    /* with no grid attached, occlusion is always clear */
    CCAudioFX* nofx = cc_audiofx_create();
    CCOcclusion ng = cc_audiofx_occlusion(nofx, 100.0f, 100.0f);
    CHECK(!ng.occluded && ng.volume > 0.99f, "no grid → never occluded");
    cc_audiofx_destroy(nofx);

    /* ── reverb zones ───────────────────────────────────────────────────── */
    CHECK(cc_audiofx_zone_count(fx) == 0, "no zones initially");

    /* a big hall containing a small closet (overlapping) */
    CCReverbBox hall   = { -10, -10, 10, 10 };
    CCReverbBox closet = {  6,  6,  9,  9 };
    int zi_hall = cc_audiofx_add_zone(fx, hall, cc_reverb_hall());
    int zi_clos = cc_audiofx_add_zone(fx, closet, cc_reverb_room());
    CHECK(zi_hall >= 0 && zi_clos >= 0, "added two zones");
    CHECK(cc_audiofx_zone_count(fx) == 2, "zone count = 2");

    /* listener in the hall but outside the closet → hall reverb (wet high) */
    CCReverbParams inhall = cc_audiofx_reverb_at(fx, 0.0f, 0.0f);
    CHECK(inhall.wet > 0.4f, "inside hall → hall wetness");
    CHECK(inhall.decay > 1.5f, "inside hall → long decay");

    /* listener inside BOTH → the smaller (closet/room) wins */
    CCReverbParams incloset = cc_audiofx_reverb_at(fx, 7.5f, 7.5f);
    CCReverbParams room = cc_reverb_room();
    CHECK(fabsf(incloset.wet - room.wet) < 1e-4f, "overlap resolves to the smallest zone (room)");
    CHECK(incloset.decay < inhall.decay, "closet decay shorter than hall");

    /* listener outside all zones → dry */
    CCReverbParams dry = cc_audiofx_reverb_at(fx, 50.0f, 50.0f);
    CHECK(dry.wet == 0.0f, "outside all zones → dry (wet 0)");

    /* reverb_here uses the listener position */
    cc_audiofx_set_listener(fx, 0.0f, 0.0f);
    CCReverbParams here = cc_audiofx_reverb_here(fx);
    CHECK(here.wet > 0.4f, "reverb_here reflects listener position (in hall)");

    cc_audiofx_clear_zones(fx);
    CHECK(cc_audiofx_zone_count(fx) == 0, "clear_zones empties the table");
    CHECK(cc_audiofx_reverb_here(fx).wet == 0.0f, "after clear → dry");

    /* ── NULL safety ────────────────────────────────────────────────────── */
    CCOcclusion no = cc_audiofx_occlusion(NULL, 0, 0);
    CHECK(no.volume > 0.99f && !no.occluded, "occlusion(NULL) → clear");
    CHECK(cc_audiofx_reverb_here(NULL).wet == 0.0f, "reverb_here(NULL) → dry");
    cc_audiofx_destroy(NULL);

    cc_audiofx_destroy(fx);
    cc_grid_destroy(grid);

    if (failures == 0) {
        printf("AUDIOFX TEST: all checks passed (wall occlusion volume+lowpass by "
               "thickness, min floor, reverb zones w/ smallest-wins overlap, dry outside)\n");
        return 0;
    }
    printf("AUDIOFX TEST: %d check(s) FAILED\n", failures);
    return 1;
}
