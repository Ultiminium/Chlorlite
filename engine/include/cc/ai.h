#pragma once
/*
 * cc/ai.h — Chlorlite AI tier
 *
 * Three independent, headless-safe (pure CPU) building blocks:
 *
 *   1. Grid pathfinding — A* over a 2D occupancy grid. Returns a list of cell
 *      waypoints from start to goal, 4- or 8-connected, with diagonal-corner
 *      cutting disallowed. Cost + heuristic are octile by default.
 *
 *   2. Steering behaviors — the classic Reynolds set operating in the XZ ground
 *      plane (Y ignored), returning a steering force you add to an agent's
 *      velocity: seek, flee, arrive, wander, and separation (flocking). These
 *      compose — sum several forces, clamp to max_force, integrate.
 *
 *   3. Behavior FSM — a tiny general-purpose finite state machine for agent
 *      decision logic (distinct from the ANIMATION state machine cc_asm_*):
 *      named states with enter/update/exit callbacks and guarded transitions.
 *
 * All three are usable standalone; a typical enemy uses the FSM to pick a state
 * ("patrol"/"chase"), pathfinding to plan a route, and steering to move along it
 * while avoiding neighbours.
 */

#include "cc/ccmath.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════════
 * 1. GRID PATHFINDING (A*)
 * ════════════════════════════════════════════════════════════════════ */

typedef struct CCGrid CCGrid;

/* Create a w×h occupancy grid, all cells initially walkable. cell_size is the
 * world size of one cell, used only by the world<->cell helpers below. */
CCGrid* cc_grid_create(uint32_t w, uint32_t h, float cell_size);
void    cc_grid_destroy(CCGrid* g);
void    cc_grid_set_blocked(CCGrid* g, uint32_t x, uint32_t y, bool blocked);
bool    cc_grid_is_blocked(const CCGrid* g, uint32_t x, uint32_t y);
void    cc_grid_clear(CCGrid* g);               /* all walkable */
uint32_t cc_grid_width(const CCGrid* g);
uint32_t cc_grid_height(const CCGrid* g);

/* World<->cell mapping (grid is centered on the origin in XZ). */
void   cc_grid_world_to_cell(const CCGrid* g, float wx, float wz, int32_t* cx, int32_t* cy);
void   cc_grid_cell_to_world(const CCGrid* g, uint32_t cx, uint32_t cy, float* wx, float* wz);

typedef struct { int32_t x, y; } CCCell;

/* Find a path from (sx,sy) to (gx,gy). Writes up to `max_out` waypoints
 * (inclusive of both endpoints) into out_cells and returns the count, or 0 if
 * unreachable / bad input. If `diagonal`, uses 8-connectivity (no corner
 * cutting through blocked cells). */
uint32_t cc_astar(const CCGrid* g, int32_t sx, int32_t sy, int32_t gx, int32_t gy,
                  bool diagonal, CCCell* out_cells, uint32_t max_out);

/* ══════════════════════════════════════════════════════════════════════
 * 2. STEERING BEHAVIORS (XZ plane)
 * ════════════════════════════════════════════════════════════════════ */

typedef struct {
    CCVec3 position;      /* XZ used, Y carried through */
    CCVec3 velocity;
    float  max_speed;
    float  max_force;     /* steering force clamp */
    float  radius;        /* for separation / arrival */
    /* wander internal state */
    float  _wander_angle;
} CCAgent;

CCAgent cc_agent_make(CCVec3 pos, float max_speed, float max_force);

/* Each returns a steering force (XZ). Add them, clamp to max_force, then call
 * cc_agent_integrate. */
CCVec3 cc_steer_seek  (const CCAgent* a, CCVec3 target);
CCVec3 cc_steer_flee  (const CCAgent* a, CCVec3 threat);
CCVec3 cc_steer_arrive(const CCAgent* a, CCVec3 target, float slow_radius);
CCVec3 cc_steer_wander(CCAgent* a, float jitter, float radius, float distance, float dt);
/* Separation: push away from neighbours within `sep_radius`. `others` is an
 * array of agents (may include self; self is skipped by pointer identity). */
CCVec3 cc_steer_separation(const CCAgent* a, const CCAgent* others, uint32_t count, float sep_radius);

/* Follow a cell path in world space: seeks the current waypoint, advancing when
 * within `arrive_radius`. `*wp_index` is the caller-held cursor (start at 0).
 * Returns the steering force; sets *done=true when the last waypoint is reached. */
CCVec3 cc_steer_path_follow(CCAgent* a, const CCGrid* g, const CCCell* path,
                            uint32_t path_len, uint32_t* wp_index,
                            float arrive_radius, bool* done);

/* Apply a summed steering force and advance the agent by dt. Clamps to
 * max_speed. Returns the new position for convenience. */
CCVec3 cc_agent_integrate(CCAgent* a, CCVec3 steering, float dt);

/* ══════════════════════════════════════════════════════════════════════
 * 3. BEHAVIOR FSM (agent decision logic; NOT the anim state machine)
 * ════════════════════════════════════════════════════════════════════ */

typedef struct CCBrain CCBrain;

/* State callbacks receive the brain, the user context (your agent/game data),
 * and dt. enter/exit may be NULL. update may be NULL. */
typedef void  (*CCStateFn)(CCBrain* b, void* ctx, float dt);
/* A transition guard: return true to take the transition. */
typedef bool  (*CCGuardFn)(CCBrain* b, void* ctx);

CCBrain* cc_brain_create(void* ctx);
void     cc_brain_destroy(CCBrain* b);

/* Register a state; returns its id. */
uint32_t cc_brain_add_state(CCBrain* b, const char* name,
                            CCStateFn on_enter, CCStateFn on_update, CCStateFn on_exit);
/* Add a guarded transition from→to. Evaluated each tick while `from` is active;
 * the first satisfied guard wins. */
void     cc_brain_add_transition(CCBrain* b, uint32_t from, uint32_t to, CCGuardFn guard);
/* Global (any-state) transition: taken from whatever state is active. */
void     cc_brain_add_any_transition(CCBrain* b, uint32_t to, CCGuardFn guard);

void        cc_brain_set_state(CCBrain* b, uint32_t state);   /* force (fires exit/enter) */
uint32_t    cc_brain_current(const CCBrain* b);
const char* cc_brain_current_name(const CCBrain* b);
float       cc_brain_time_in_state(const CCBrain* b);

/* Advance: evaluate transitions, then run the active state's update. */
void cc_brain_tick(CCBrain* b, float dt);

#ifdef __cplusplus
}
#endif
