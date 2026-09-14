#pragma once
/*
 * CCAudioFX — spatial audio shaping: OCCLUSION (walls muffle sound) and REVERB
 * ZONES (spaces color sound). This is what makes 3D audio feel like it lives in
 * the level: a growl from behind a wall is dampened and quiet; footsteps in a
 * cathedral ring; the same footsteps in a closet are dead. The engine already
 * does positional attenuation (distance rolloff, cc/audio.h); this adds the two
 * things distance alone can't express — geometry between listener and source,
 * and the acoustic character of the space the listener is in.
 *
 * OCCLUSION reuses the pathfinding/sound grid (cc/ai.h CCGrid) the same way
 * cc/soundfield.h does: sound is occluded by BLOCKED cells on the straight line
 * from source to listener. A clear line = no occlusion (factor 1). A line that
 * clips wall cells = muffled: the more wall crossed, the lower the factor and
 * the stronger the low-pass "muffle" (high frequencies die first through walls).
 * This is a fast line-march, not a full path solve — occlusion is a perceptual
 * "is there stuff in the way", distinct from soundfield's "how does noise travel
 * to reach a hunting AI" (which needs the around-corners path).
 *
 * REVERB ZONES are axis-aligned boxes (on XZ, any Y) each carrying a reverb
 * preset (wet mix, decay time, damping). The listener is "in" the innermost zone
 * that contains it; queries return that zone's params (0/dry if none). A game
 * feeds the results into its mixer — e.g. set an instance's volume to
 * base * occlusion, and drive a reverb send / EFX slot from the zone wetness.
 *
 *   CCAudioFX* fx = cc_audiofx_create();
 *   cc_audiofx_set_grid(fx, level_grid);                 // for occlusion
 *   cc_audiofx_add_zone(fx, cathedral_box, cc_reverb_hall());
 *   ... each frame:
 *     cc_audiofx_set_listener(fx, cam.x, cam.z);
 *     CCOcclusion o = cc_audiofx_occlusion(fx, src.x, src.z);
 *     cc_audio_instance_set_volume(eng, inst, base * o.volume);   // muffle vol
 *     CCReverbParams r = cc_audiofx_reverb_here(fx);              // space color
 *     // apply o.lowpass and r.wet to your DSP / OpenAL EFX send
 *
 * Pure CPU, deterministic, headless-safe.
 */
#include "cc/ccmath.h"
#include "cc/ai.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCAudioFX CCAudioFX;

/* Occlusion result for a source, as heard from the current listener. */
typedef struct {
    float volume;    /* 0..1 gain multiplier (1 = clear line of sound)          */
    float lowpass;   /* 0..1 muffle amount (0 = bright/clear, 1 = fully muffled) */
    bool  occluded;  /* true if any wall lies between source and listener        */
} CCOcclusion;

/* Reverb parameters for a space (drive a reverb send / OpenAL EFX slot). */
typedef struct {
    float wet;       /* 0..1 wet/dry mix                                         */
    float decay;     /* reverb time in seconds (RT60-ish)                        */
    float damping;   /* 0..1 high-frequency absorption (1 = very damped)         */
} CCReverbParams;

/* An axis-aligned reverb zone on the XZ plane (any Y). Listener inside → its
 * reverb applies; overlapping zones resolve to the SMALLEST (most specific). */
typedef struct {
    float min_x, min_z, max_x, max_z;
} CCReverbBox;

/* Presets (tweak the returned struct freely). */
CCReverbParams cc_reverb_none(void);   /* dry: wet 0                            */
CCReverbParams cc_reverb_room(void);   /* small room                           */
CCReverbParams cc_reverb_hall(void);   /* large hall / cathedral               */
CCReverbParams cc_reverb_cave(void);   /* long, dark, damped                   */

/* ─── lifecycle ───────────────────────────────────────────────────────────── */
CCAudioFX* cc_audiofx_create(void);
void       cc_audiofx_destroy(CCAudioFX* fx);

/* Occlusion needs a grid whose blocked cells are walls. Optional: with no grid,
 * occlusion always returns clear (volume 1). Does not own the grid. */
void cc_audiofx_set_grid(CCAudioFX* fx, CCGrid* grid);
/* Tuning: how much a fully-occluded source is attenuated (min_volume, default
 * 0.25) and how strongly wall thickness muffles (per_cell, default 0.35). */
void cc_audiofx_set_occlusion(CCAudioFX* fx, float min_volume, float per_cell);

/* ─── reverb zones ────────────────────────────────────────────────────────── */
/* Add a zone; returns its index (or -1 if the table is full). */
int  cc_audiofx_add_zone(CCAudioFX* fx, CCReverbBox box, CCReverbParams params);
void cc_audiofx_clear_zones(CCAudioFX* fx);
uint32_t cc_audiofx_zone_count(const CCAudioFX* fx);

/* ─── per-frame ───────────────────────────────────────────────────────────── */
/* Set the listener XZ (usually the camera / player head). */
void cc_audiofx_set_listener(CCAudioFX* fx, float x, float z);

/* Occlusion of a source at world XZ, heard from the current listener. */
CCOcclusion cc_audiofx_occlusion(const CCAudioFX* fx, float sx, float sz);
/* Reverb of the space the LISTENER currently occupies (innermost zone; dry if
 * none). */
CCReverbParams cc_audiofx_reverb_here(const CCAudioFX* fx);
/* Reverb of the space a given world XZ occupies (for non-listener queries). */
CCReverbParams cc_audiofx_reverb_at(const CCAudioFX* fx, float x, float z);

#ifdef __cplusplus
}
#endif
