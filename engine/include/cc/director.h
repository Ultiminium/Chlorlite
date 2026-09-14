#pragma once
/*
 * CCDirector — an AI pacing director for dynamic difficulty and tension curves.
 * This is the horror/action "director" (cf. Left 4 Dead's AI Director and
 * Alien: Isolation's stalker): a GLOBAL orchestrator that decides WHEN to apply
 * pressure and when to relieve it, so encounters form deliberate peaks-and-
 * valleys instead of constant noise. It is distinct from cc/ai.h, which is
 * per-agent (pathfinding/steering/FSM); the director sits above agents and tells
 * the game "spawn now", "back off", "go quiet".
 *
 * MODEL. The director tracks a single 0..1 INTENSITY value approximating how
 * stressed the player currently is. Intensity RISES from stress you report
 * (taking damage, a threat in view, a chase) and DECAYS over time when the
 * player is left alone. The director walks a pacing cycle driven by that value:
 *
 *     BUILD_UP  — intensity climbing toward a peak; pressure allowed to grow
 *        ↓ (intensity reaches peak_threshold)
 *     PEAK      — sustained maximum pressure for up to peak_duration
 *        ↓ (peak times out)
 *     FADE      — deliberately back off; spawns suppressed so intensity falls
 *        ↓ (intensity drops below rest_threshold)
 *     REST      — a guaranteed quiet lull of rest_duration (breathing room)
 *        ↓ (rest times out)
 *     BUILD_UP  — cycle repeats
 *
 * You feed it stress and time; it tells you what phase you're in and whether it
 * currently WANTS an encounter. A typical loop:
 *
 *     CCDirector* dir = cc_director_create();
 *     // optionally: react to engine events automatically
 *     cc_director_watch_bus(dir, bus);   // DAMAGE/CONTACT/DEATH raise intensity
 *     ...
 *     each frame:
 *       cc_director_add_stress(dir, threat_visible ? 0.4f*dt : 0.0f);
 *       cc_director_update(dir, dt);
 *       if (cc_director_should_spawn(dir))
 *           spawn_enemy_at( cc_director_pick_spawn(dir, candidates, n) );
 *
 * The director does NOT spawn anything itself or know your entities — it makes
 * the pacing DECISION and (optionally) helps pick a spawn point from candidates
 * you supply (favouring out-of-view, not-too-close positions). Everything is
 * pure CPU, deterministic given the same inputs + seed, and headless-safe.
 */
#include "cc/ccmath.h"
#include "cc/event.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCDirector CCDirector;

/* Pacing phases (see the cycle diagram above). */
typedef enum {
    CC_DIR_BUILD_UP = 0,
    CC_DIR_PEAK,
    CC_DIR_FADE,
    CC_DIR_REST,
} CCDirectorPhase;

/* Tunables. cc_director_default_config() fills sensible values; override any. */
typedef struct {
    float peak_threshold;   /* intensity that ends BUILD_UP → PEAK (def 0.85)   */
    float rest_threshold;   /* intensity that ends FADE → REST     (def 0.20)   */
    float peak_duration;    /* max seconds held at PEAK             (def 6)      */
    float rest_duration;    /* guaranteed quiet seconds at REST     (def 8)      */
    float decay_rate;       /* intensity lost per second when unpressured (0.08) */
    float build_spawn_rate; /* desired spawns/sec during BUILD_UP  (def 0.25)   */
    float peak_spawn_rate;  /* desired spawns/sec during PEAK      (def 0.8)     */
    float min_spawn_dist;   /* reject spawn candidates closer than this (def 8)  */
    float max_spawn_dist;   /* reject spawn candidates farther than this (def 40)*/
    uint32_t seed;          /* RNG seed for spawn jitter/selection (def 1)       */
} CCDirectorConfig;

CCDirectorConfig cc_director_default_config(void);

/* ─── lifecycle ───────────────────────────────────────────────────────────── */
CCDirector* cc_director_create(void);                       /* default config   */
CCDirector* cc_director_create_cfg(const CCDirectorConfig* cfg);
void        cc_director_destroy(CCDirector* d);
/* Advance pacing by dt: applies decay, updates intensity, runs the phase state
 * machine, and accrues the spawn budget. Call once per frame. */
void        cc_director_update(CCDirector* d, float dt);
void        cc_director_reset(CCDirector* d);               /* back to REST, i=0 */

/* ─── feeding stress ──────────────────────────────────────────────────────── */
/* Add to intensity now (clamped 0..1). Use for discrete jolts (took a hit) or
 * per-frame pressure (threat_visible ? rate*dt : 0). Negative values relieve. */
void  cc_director_add_stress(CCDirector* d, float amount);
/* Directly set intensity 0..1 (rarely needed; add_stress is usual). */
void  cc_director_set_intensity(CCDirector* d, float v);
float cc_director_intensity(const CCDirector* d);           /* current 0..1     */

/* OPTIONAL: auto-raise intensity from engine events. When watching, the director
 * subscribes to the bus and adds stress on CC_EVT_DAMAGE (by f=amount, scaled),
 * CC_EVT_CONTACT (by impact speed, scaled), and CC_EVT_DEATH (a spike). You must
 * still pump the bus (cc_event_bus_update) as usual. Pass NULL to stop watching. */
void  cc_director_watch_bus(CCDirector* d, CCEventBus* bus);

/* ─── querying the decision ───────────────────────────────────────────────── */
CCDirectorPhase cc_director_phase(const CCDirector* d);
const char*     cc_director_phase_name(CCDirectorPhase p);  /* "BUILD_UP"… */
float           cc_director_phase_time(const CCDirector* d);/* secs in phase     */
/* True when the pacing budget says an encounter should start THIS frame. Reading
 * it CONSUMES one unit of spawn budget (so poll it once per frame and act on it).
 * Always false during FADE/REST (the director is deliberately easing off). */
bool            cc_director_should_spawn(CCDirector* d);
/* The desired spawns/sec for the current phase (0 during FADE/REST). */
float           cc_director_spawn_rate(const CCDirector* d);

/* ─── spawn-point selection (optional helper) ─────────────────────────────── */
/* Given the player position + facing (fx,fz on the XZ plane) and an array of
 * candidate spawn points, pick the best index: within [min,max]_spawn_dist,
 * preferring points BEHIND / out of the player's facing cone (so threats appear
 * off-screen), with seeded jitter to avoid repetition. Returns -1 if none pass
 * the distance gate. Does not modify the director except advancing its RNG. */
int32_t cc_director_pick_spawn(CCDirector* d,
                               CCVec3 player_pos, float fx, float fz,
                               const CCVec3* candidates, uint32_t count);

#ifdef __cplusplus
}
#endif
