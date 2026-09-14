/* camera_custom_test — proves camera controllers are a MECHANISM, not a fixed
 * menu: it installs a fully custom controller the engine doesn't ship (a "spiral
 * cam" with developer-owned state) and checks the engine drives it every update
 * while the developer owns the motion. Also confirms built-in controllers still
 * work. Pure logic (no rendering needed). */
#include "cc/claudecore.h"
#include "cc/camera.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* a camera behavior the ENGINE knows nothing about: spiral around a point,
 * rising, with the developer's own tunables + call counter. */
typedef struct {
    float angle;      /* accumulated */
    float radius;
    float rise;
    float height;
    int   updates;    /* how many times the engine called us */
} SpiralCam;

static void spiral_controller(CCCameraRig* rig, CCEngine* eng, float dt, void* state) {
    (void)eng;
    SpiralCam* s = (SpiralCam*)state;
    s->updates++;
    s->angle  += dt * 1.5f;      /* spin */
    s->height += dt * s->rise;   /* climb */
    CCVec3 pos = {
        cosf(s->angle) * s->radius,
        s->height,
        sinf(s->angle) * s->radius
    };
    cc_cam_set_position(rig, pos);
    CCVec3 center = {0, 0, 0};
    CCVec3 up = {0, 1, 0};
    cc_cam_look_at(rig, center, up);
}

int main(void) {
    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 64; cfg.height = 64; cfg.verbose = false;
    CCEngine* e = cc_init(&cfg);
    CHECK(e != NULL, "engine created");
    if (!e) { printf("CAMERA CUSTOM TEST: init failed\n"); return 1; }

    CCCameraRig* rig = cc_camera_create();
    CHECK(rig != NULL, "camera rig created");

    /* ── install a fully custom controller ──────────────────────────────── */
    SpiralCam spiral = { .angle = 0, .radius = 10.0f, .rise = 2.0f, .height = 0 };
    cc_cam_use_custom(rig, spiral_controller, &spiral);

    /* run several frames; the engine must call OUR controller each time */
    CCVec3 first = {0}, last = {0};
    for (int i = 0; i < 30; i++) {
        cc_camera_update(rig, e, 1.0f/30.0f);
        if (i == 0) first = rig->cam.position;
    }
    last = rig->cam.position;

    CHECK(spiral.updates == 30, "engine called the custom controller every frame");
    CHECK(spiral.height > 1.5f, "developer's rise logic ran (camera climbed ~2u/s)");
    /* camera should have moved along the developer-defined spiral */
    float moved = sqrtf((last.x-first.x)*(last.x-first.x) +
                        (last.y-first.y)*(last.y-first.y) +
                        (last.z-first.z)*(last.z-first.z));
    CHECK(moved > 1.0f, "camera position followed the custom spiral path");
    /* radius stays ~10 in XZ (developer's constraint) */
    float r_xz = sqrtf(last.x*last.x + last.z*last.z);
    CHECK(fabsf(r_xz - 10.0f) < 0.5f, "custom controller held its own radius invariant");

    /* ── detaching the custom controller stops it ───────────────────────── */
    int before = spiral.updates;
    cc_cam_use_custom(rig, NULL, NULL);   /* detach */
    cc_camera_update(rig, e, 1.0f/30.0f);
    CHECK(spiral.updates == before, "detached custom controller no longer called");

    /* ── built-in controllers still work (backward compat) ──────────────── */
    CCVec3 tgt = {0, 0, 0};
    cc_cam_use_orbit(rig, tgt, 8.0f, 0.0f, 20.0f);
    cc_camera_update(rig, e, 1.0f/30.0f);
    CCVec3 orbit_pos = rig->cam.position;
    float orbit_r = sqrtf(orbit_pos.x*orbit_pos.x + orbit_pos.z*orbit_pos.z);
    CHECK(orbit_r > 1.0f, "built-in orbit controller still positions the camera");

    /* swapping back to a custom controller works too */
    SpiralCam spiral2 = { .angle = 1.0f, .radius = 5.0f, .rise = 0, .height = 3.0f };
    cc_cam_use_custom(rig, spiral_controller, &spiral2);
    cc_camera_update(rig, e, 1.0f/30.0f);
    CHECK(spiral2.updates == 1, "can swap back to a (different) custom controller");

    cc_camera_destroy(rig);
    cc_shutdown(e);

    if (failures == 0) {
        printf("CAMERA CUSTOM TEST: all checks passed (developer-defined camera controller: "
               "engine drives it each frame, developer owns the motion; detach + built-ins work)\n");
        return 0;
    }
    printf("CAMERA CUSTOM TEST: %d check(s) FAILED\n", failures);
    return 1;
}
