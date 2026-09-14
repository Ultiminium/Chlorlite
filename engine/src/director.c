/* director.c — CCDirector AI pacing/tension director. See cc/director.h.
 *
 * State: a scalar intensity (0..1) plus a phase FSM (BUILD_UP→PEAK→FADE→REST).
 * update() decays intensity, advances the phase by threshold/timeout rules, and
 * accrues a spawn budget at the phase's desired rate; should_spawn() consumes it.
 * All deterministic given inputs + seed. Bus watching adds stress from combat
 * events via a subscribed listener.
 */
#include "cc/director.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct CCDirector {
    CCDirectorConfig cfg;
    float           intensity;      /* 0..1                                     */
    CCDirectorPhase phase;
    float           phase_t;        /* seconds in current phase                 */
    float           spawn_budget;   /* accrues at spawn_rate; >=1 → allow spawn  */
    uint32_t        rng;            /* xorshift state                           */
    /* bus watching */
    CCEventBus*     bus;
    CCListenerId    listener;
};

/* ── deterministic RNG (xorshift32) ──────────────────────────────────────── */
static uint32_t rng_next(CCDirector* d) {
    uint32_t x = d->rng ? d->rng : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    d->rng = x;
    return x;
}
static float rng_unit(CCDirector* d) { return (rng_next(d) >> 8) * (1.0f/16777216.0f); } /* [0,1) */

static float clamp01(float v){ return v<0.0f?0.0f:(v>1.0f?1.0f:v); }

CCDirectorConfig cc_director_default_config(void) {
    CCDirectorConfig c;
    c.peak_threshold   = 0.85f;
    c.rest_threshold   = 0.20f;
    c.peak_duration    = 6.0f;
    c.rest_duration    = 8.0f;
    c.decay_rate       = 0.08f;
    c.build_spawn_rate = 0.25f;
    c.peak_spawn_rate  = 0.80f;
    c.min_spawn_dist   = 8.0f;
    c.max_spawn_dist   = 40.0f;
    c.seed             = 1u;
    return c;
}

CCDirector* cc_director_create_cfg(const CCDirectorConfig* cfg) {
    CCDirector* d = (CCDirector*)calloc(1, sizeof(CCDirector));
    if (!d) return NULL;
    d->cfg   = cfg ? *cfg : cc_director_default_config();
    d->phase = CC_DIR_REST;      /* start calm */
    d->rng   = d->cfg.seed ? d->cfg.seed : 1u;
    return d;
}
CCDirector* cc_director_create(void) { return cc_director_create_cfg(NULL); }

void cc_director_destroy(CCDirector* d) {
    if (!d) return;
    if (d->bus && d->listener) cc_event_unsubscribe(d->bus, d->listener);
    free(d);
}

void cc_director_reset(CCDirector* d) {
    if (!d) return;
    d->intensity = 0.0f;
    d->phase = CC_DIR_REST;
    d->phase_t = 0.0f;
    d->spawn_budget = 0.0f;
    d->rng = d->cfg.seed ? d->cfg.seed : 1u;
}

/* ── feeding stress ──────────────────────────────────────────────────────── */
void cc_director_add_stress(CCDirector* d, float amount) {
    if (!d) return;
    d->intensity = clamp01(d->intensity + amount);
}
void cc_director_set_intensity(CCDirector* d, float v) { if (d) d->intensity = clamp01(v); }
float cc_director_intensity(const CCDirector* d) { return d ? d->intensity : 0.0f; }

/* ── bus watching ────────────────────────────────────────────────────────── */
static void director_on_event(const CCEvent* e, void* ud) {
    CCDirector* d = (CCDirector*)ud;
    switch (e->channel) {
        case CC_EVT_DAMAGE:  cc_director_add_stress(d, 0.15f + 0.02f * e->f); break;
        case CC_EVT_CONTACT: cc_director_add_stress(d, 0.03f * e->f);         break;
        case CC_EVT_DEATH:   cc_director_add_stress(d, 0.30f);                break;
        default: break;
    }
}
void cc_director_watch_bus(CCDirector* d, CCEventBus* bus) {
    if (!d) return;
    if (d->bus && d->listener) { cc_event_unsubscribe(d->bus, d->listener); d->listener = 0; }
    d->bus = bus;
    if (bus) {
        /* one listener, filtered internally by channel; subscribe_any keeps it
         * simple and future-proof if more stress channels are added. */
        d->listener = cc_event_subscribe_any(bus, director_on_event, d);
    }
}

/* ── phase machine ───────────────────────────────────────────────────────── */
static void set_phase(CCDirector* d, CCDirectorPhase p) {
    d->phase = p;
    d->phase_t = 0.0f;
    d->spawn_budget = 0.0f;   /* fresh budget each phase */
}

float cc_director_spawn_rate(const CCDirector* d) {
    if (!d) return 0.0f;
    switch (d->phase) {
        case CC_DIR_BUILD_UP: return d->cfg.build_spawn_rate;
        case CC_DIR_PEAK:     return d->cfg.peak_spawn_rate;
        case CC_DIR_FADE:
        case CC_DIR_REST:
        default:              return 0.0f;   /* deliberately easing off */
    }
}

void cc_director_update(CCDirector* d, float dt) {
    if (!d || dt < 0.0f) return;
    d->phase_t += dt;

    /* intensity decays toward 0 whenever it isn't being fed; during FADE we let
     * it fall naturally (no extra suppression needed — spawn_rate is already 0). */
    d->intensity = clamp01(d->intensity - d->cfg.decay_rate * dt);

    /* accrue spawn budget at the current phase's rate */
    d->spawn_budget += cc_director_spawn_rate(d) * dt;

    /* phase transitions */
    switch (d->phase) {
        case CC_DIR_BUILD_UP:
            if (d->intensity >= d->cfg.peak_threshold) set_phase(d, CC_DIR_PEAK);
            break;
        case CC_DIR_PEAK:
            if (d->phase_t >= d->cfg.peak_duration) set_phase(d, CC_DIR_FADE);
            break;
        case CC_DIR_FADE:
            if (d->intensity <= d->cfg.rest_threshold) set_phase(d, CC_DIR_REST);
            break;
        case CC_DIR_REST:
        default:
            if (d->phase_t >= d->cfg.rest_duration) set_phase(d, CC_DIR_BUILD_UP);
            break;
    }
}

/* ── decision queries ────────────────────────────────────────────────────── */
CCDirectorPhase cc_director_phase(const CCDirector* d) { return d ? d->phase : CC_DIR_REST; }
float cc_director_phase_time(const CCDirector* d) { return d ? d->phase_t : 0.0f; }
const char* cc_director_phase_name(CCDirectorPhase p) {
    switch (p) {
        case CC_DIR_BUILD_UP: return "BUILD_UP";
        case CC_DIR_PEAK:     return "PEAK";
        case CC_DIR_FADE:     return "FADE";
        case CC_DIR_REST:     return "REST";
        default:              return "?";
    }
}

bool cc_director_should_spawn(CCDirector* d) {
    if (!d) return false;
    if (d->spawn_budget >= 1.0f) {
        d->spawn_budget -= 1.0f;
        return true;
    }
    return false;
}

/* ── spawn-point selection ───────────────────────────────────────────────── */
int32_t cc_director_pick_spawn(CCDirector* d,
                               CCVec3 player_pos, float fx, float fz,
                               const CCVec3* candidates, uint32_t count) {
    if (!d || !candidates || count == 0) return -1;

    /* normalize facing on XZ (if given) */
    float flen = sqrtf(fx*fx + fz*fz);
    bool have_facing = flen > 1e-4f;
    if (have_facing) { fx /= flen; fz /= flen; }

    int32_t best = -1;
    float   best_score = -1e30f;
    for (uint32_t i = 0; i < count; ++i) {
        float dx = candidates[i].x - player_pos.x;
        float dz = candidates[i].z - player_pos.z;
        float dist = sqrtf(dx*dx + dz*dz);
        if (dist < d->cfg.min_spawn_dist || dist > d->cfg.max_spawn_dist) continue;

        /* prefer BEHIND the player: dot of facing with dir-to-candidate; behind
         * means negative dot → higher score. If no facing, all equal on this axis. */
        float behind = 0.0f;
        if (have_facing && dist > 1e-4f) {
            float dot = (fx*dx + fz*dz) / dist;   /* -1 (behind) .. +1 (ahead) */
            behind = -dot;                        /* +1 behind .. -1 ahead     */
        }
        /* mild preference for mid-range over the extremes of the band */
        float band = 1.0f - fabsf((dist - d->cfg.min_spawn_dist) /
                     (d->cfg.max_spawn_dist - d->cfg.min_spawn_dist) - 0.4f);

        float jitter = rng_unit(d) * 0.25f;
        float score = behind * 1.0f + band * 0.3f + jitter;
        if (score > best_score) { best_score = score; best = (int32_t)i; }
    }
    return best;
}
