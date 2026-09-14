#pragma once
/*
 * CCSoundField — spatial sound propagation for "hearing" AI. This is the piece
 * that makes noise MATTER in a horror/stealth game: a dropped object, a gunshot,
 * a footstep, or the player's real microphone loudness becomes a SOUND EVENT at
 * a world position, and enemies that are close enough (through the actual level
 * geometry, not through walls) hear it and move to investigate.
 *
 * PROPAGATION MODEL. Straight-line distance is wrong for occlusion: a scream on
 * the far side of a solid wall should be muffled or inaudible even if it's only
 * two metres away as the crow flies. So propagation travels through the WALKABLE
 * space of a CCGrid (cc/ai.h) — the same occupancy grid used for pathfinding.
 * The perceived loudness at a listener is the emitted loudness attenuated by the
 * PATH distance sound must travel around obstacles (a flood fill / BFS over open
 * cells from the source). A fully sealed room hears nothing; a room with a
 * doorway hears sound leaking through the door, quieter the longer the detour.
 *
 * Because the field already knows the propagation path, it also tells a hearing
 * agent WHICH WAY to move toward a sound (the first step of the least-cost path
 * back to the source) — so "investigate the noise" needs no extra pathfinding.
 *
 *   CCSoundField* sf = cc_soundfield_create(grid);
 *   ... each frame:
 *     cc_soundfield_begin(sf);                       // clear last frame's sounds
 *     cc_soundfield_emit(sf, noise_x, noise_z, loud);// 1+ sounds this frame
 *     cc_soundfield_emit(sf, cc_mic_x, cc_mic_z, cc_mic_loudness(mic));
 *     cc_soundfield_propagate(sf, hearing_radius);   // flood fill
 *     // per enemy:
 *     float heard; CCVec3 toward;
 *     if (cc_soundfield_sample(sf, ex, ez, &heard, &toward) && heard > enemy_threshold)
 *         move_agent_toward(enemy, toward);          // investigate
 *
 * Optionally the field can auto-ingest sounds from the event bus (CC_EVT_SOUND,
 * and CC_EVT_MIC_LEVEL as a sound at a configured position) so gameplay code
 * just emits events. Pure CPU, deterministic, headless-safe.
 */
#include "cc/ccmath.h"
#include "cc/ai.h"
#include "cc/event.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCSoundField CCSoundField;

/* Create a sound field over an existing occupancy grid. The grid's blocked
 * cells are walls sound cannot pass through; walkable cells conduct it. The
 * field does not own the grid. */
CCSoundField* cc_soundfield_create(CCGrid* grid);
void          cc_soundfield_destroy(CCSoundField* sf);

/* ─── per-frame usage ─────────────────────────────────────────────────────── */
/* Clear all sounds emitted last frame (call once at the top of the frame). */
void cc_soundfield_begin(CCSoundField* sf);
/* Emit a sound of `loudness` (0..1) at world XZ (wx,wz). Multiple emits stack —
 * the field keeps the LOUDEST contribution at each cell. Out-of-grid or blocked
 * source cells snap to the nearest usable cell. */
void cc_soundfield_emit(CCSoundField* sf, float wx, float wz, float loudness);
/* Flood-fill perceived loudness outward from every emitted sound, attenuating
 * with propagation (path) distance. `falloff` is loudness lost per world unit of
 * travel (default via cc_soundfield_set_falloff). Sounds below the audible floor
 * stop spreading. Call once after all emits, before sampling. */
void cc_soundfield_propagate(CCSoundField* sf, float max_travel);

/* ─── querying (per listener) ─────────────────────────────────────────────── */
/* Perceived loudness at world XZ (ex,ez), 0 if nothing audible there. */
float cc_soundfield_loudness(const CCSoundField* sf, float ex, float ez);
/* Full query: writes perceived loudness to *out_loud and a unit XZ direction
 * toward the sound source (first step of the least-cost path) to *out_dir.
 * Returns true if any sound is audible at the listener. out_* may be NULL. */
bool  cc_soundfield_sample(const CCSoundField* sf, float ex, float ez,
                           float* out_loud, CCVec3* out_dir);

/* ─── tuning ──────────────────────────────────────────────────────────────── */
/* Loudness lost per world unit of propagation (default 0.06). Higher = sound
 * dies faster with distance. */
void cc_soundfield_set_falloff(CCSoundField* sf, float per_unit);
/* Loudness below this is treated as inaudible / not propagated (default 0.02). */
void cc_soundfield_set_floor(CCSoundField* sf, float floor);

/* ─── event bus (optional) ────────────────────────────────────────────────── */
/* When watching, cc_soundfield_begin also drains sounds the bus delivered since
 * the last frame: CC_EVT_SOUND (a positional sound) and, if a mic position is
 * set, CC_EVT_MIC_LEVEL (treated as a sound at that position with f=loudness).
 * You still pump the bus as usual. Pass NULL to stop watching. */
void cc_soundfield_watch_bus(CCSoundField* sf, CCEventBus* bus);
/* Where mic-level events are heard from (e.g. the player's head). */
void cc_soundfield_set_mic_position(CCSoundField* sf, float wx, float wz);

/* Helper: publish a positional sound on the bus for a watching sound field to
 * ingest next frame. Encodes XZ (metres, ×8 fixed-point) into the event sender.
 * loudness is 0..1. Equivalent to cc_soundfield_emit but decoupled via the bus. */
static inline void cc_sound_emit_event(CCEventBus* bus, float wx, float wz, float loudness) {
    int32_t xi = (int32_t)(wx * 8.0f);
    int32_t zi = (int32_t)(wz * 8.0f);
    uint64_t packed = ((uint64_t)(uint32_t)xi) | (((uint64_t)(uint32_t)zi) << 32);
    CCEvent e; e.channel = CC_EVT_SOUND; e.sender = packed;
    e.i = 0; e.f = loudness; e.data = 0; e.data_size = 0;
    cc_event_emit(bus, &e);
}

#ifdef __cplusplus
}
#endif
