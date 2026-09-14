/* input_edge_test — verifies the edge-triggered input contract in the HEADLESS
 * path, including the FAST-TAP case (a press+release within one frame must still
 * register), which is the class of bug a human hit playing the windowed game.
 *
 * Frame flow mirrors cc_run: inject events → cc_input_begin_frame (snapshot + latch
 * edges) → read cc_key_pressed/down → cc_input_end_frame (roll to prev). The press
 * LATCH is what makes a fast tap survive: begin() sets it on the down transition,
 * and it stays set through the read even if the key is already up again.
 *
 * LIMITATION: cannot exercise the real windowed GLFW path (no display in CI). That
 * path (sticky keys + the same begin/end latch) still needs a manual smoke test.
 */
#include "cc/claudecore.h"
#include "cc/input.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

static void begin(CCEngine* e){ cc_tick(e, 0.016); cc_input_begin_frame(e); }
static void end(CCEngine* e){ cc_input_end_frame(e); }

int main(void) {
    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 64; cfg.height = 64; cfg.verbose = false;
    CCEngine* e = cc_init(&cfg);
    CHECK(e != NULL, "engine init");

    /* baseline: key up */
    cc_input_inject_key(e, QKEY_SPACE, false); begin(e);
    CHECK(!cc_key_pressed(e, QKEY_SPACE), "no press when key starts up");
    CHECK(!cc_key_down(e, QKEY_SPACE),    "not down when up");
    end(e);

    /* rising edge fires pressed once */
    cc_input_inject_key(e, QKEY_SPACE, true); begin(e);
    CHECK(cc_key_pressed(e, QKEY_SPACE), "rising edge fires cc_key_pressed once");
    CHECK(cc_key_down(e, QKEY_SPACE),    "cc_key_down true while held");
    end(e);

    /* held: must NOT re-fire */
    cc_input_inject_key(e, QKEY_SPACE, true); begin(e);
    CHECK(!cc_key_pressed(e, QKEY_SPACE), "held key does not re-fire pressed");
    CHECK(cc_key_down(e, QKEY_SPACE),     "still down while held");
    end(e);

    /* falling edge fires released once */
    cc_input_inject_key(e, QKEY_SPACE, false); begin(e);
    CHECK(cc_key_released(e, QKEY_SPACE), "falling edge fires cc_key_released once");
    CHECK(!cc_key_down(e, QKEY_SPACE),    "not down after release");
    end(e);

    /* FAST TAP: down AND up within one frame must still register as pressed. */
    cc_input_inject_key(e, QKEY_Z, false); begin(e); end(e);   /* Z settled up */
    cc_input_inject_key(e, QKEY_Z, true);      /* down... */
    begin(e);                                   /* begin() latches the rising edge */
    cc_input_inject_key(e, QKEY_Z, false);     /* ...up again, same frame */
    CHECK(cc_key_pressed(e, QKEY_Z), "fast tap (down+up in one frame) still registers as pressed");
    end(e);

    /* multi-key independence: Z/X/Space each rising the same frame each fire */
    cc_input_inject_key(e, QKEY_Z, true);
    cc_input_inject_key(e, QKEY_X, true);
    cc_input_inject_key(e, QKEY_SPACE, true);
    begin(e);
    CHECK(cc_key_pressed(e, QKEY_Z),     "Z rising edge");
    CHECK(cc_key_pressed(e, QKEY_X),     "X rising edge (independent)");
    CHECK(cc_key_pressed(e, QKEY_SPACE), "Space rising edge (independent)");
    end(e);

    cc_shutdown(e);

    if (failures == 0)
        printf("INPUT EDGE TEST: all checks passed (rising once, held no re-fire, release once, FAST TAP registers, multi-key independence — headless; windowed GLFW still needs a display smoke test)\n");
    else
        printf("INPUT EDGE TEST: %d FAILURES\n", failures);
    return failures ? 1 : 0;
}
