#pragma once
/*
 * CCParticles — a CPU particle system for smoke, fire, sparks, dust, rain, magic,
 * explosions, confetti… domain-general "juice" for any game. An EMITTER holds a
 * spawn configuration (rates, lifetime, velocity/size/color ranges, gravity,
 * drag) and a pool of live particles. Each frame you update it with dt and draw
 * it; particles render as camera-facing billboards (reusing cc_draw_billboards),
 * optionally textured, and fade/shrink over their lifetime.
 *
 *   CCParticles* p = cc_particles_create(2000);      // pool capacity
 *   CCEmitterDesc d = cc_emitter_smoke();            // a preset
 *   d.position = (CCVec3){0, 1, 0};
 *   cc_particles_config(p, &d);
 *   ... each frame: cc_particles_update(p, dt); cc_particles_draw(p, eng, tex);
 *   // one-shot burst (explosion, pickup sparkle):
 *   cc_particles_burst(p, 200);
 *
 * Continuous emission uses desc.rate (particles/sec); set rate=0 and call
 * cc_particles_burst for one-shots. Presets give sensible starting points you
 * can tweak.
 */
#include "cc/ccmath.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCEngine CCEngine;
typedef struct CCParticles CCParticles;

typedef struct CCEmitterDesc {
    CCVec3 position;         /* emitter origin */
    CCVec3 spawn_extent;     /* box half-size around origin particles spawn in */

    float  rate;             /* particles/sec (0 = burst-only) */

    /* initial velocity = base_velocity + random in [-vel_spread, +vel_spread] */
    CCVec3 base_velocity;
    CCVec3 vel_spread;

    CCVec3 gravity;          /* accel applied each second (e.g. {0,-9.8,0}) */
    float  drag;             /* velocity *= (1 - drag*dt); 0 = none */

    float  life_min, life_max;      /* seconds */
    float  size_start, size_end;    /* world units; interpolated over life */
    float  size_jitter;             /* 0..1 random size variation */

    float  color_start[4];   /* linear RGBA at birth */
    float  color_end[4];     /* linear RGBA at death (usually alpha→0) */

    uint64_t seed;
} CCEmitterDesc;

/* ─── presets (tweak after fetching) ─────────────────────────────────────── */
CCEmitterDesc cc_emitter_smoke(void);
CCEmitterDesc cc_emitter_fire(void);
CCEmitterDesc cc_emitter_sparks(void);
CCEmitterDesc cc_emitter_rain(void);
CCEmitterDesc cc_emitter_magic(void);

/* ─── lifecycle ─────────────────────────────────────────────────────────── */
CCParticles* cc_particles_create(uint32_t capacity);
void         cc_particles_destroy(CCParticles* p);
void         cc_particles_config(CCParticles* p, const CCEmitterDesc* desc);
void         cc_particles_set_position(CCParticles* p, float x, float y, float z);
void         cc_particles_set_emitting(CCParticles* p, bool on);  /* pause/resume continuous */

/* spawn `count` particles immediately (explosions, bursts). */
void         cc_particles_burst(CCParticles* p, uint32_t count);

/* advance simulation by dt seconds (spawns per rate, integrates, ages out). */
void         cc_particles_update(CCParticles* p, float dt);

/* draw live particles as billboards. tex may be CC_NULL for solid quads. */
void         cc_particles_draw(CCParticles* p, CCEngine* eng, uint32_t tex);

uint32_t     cc_particles_alive(const CCParticles* p);

#ifdef __cplusplus
}
#endif
