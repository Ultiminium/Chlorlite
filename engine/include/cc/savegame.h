#pragma once
/*
 * CCSaveGame — the management layer that turns the raw key-value save store
 * (cc/save.h) into a real game save system: named SLOTS, quicksave, a ring of
 * AUTOSAVES, CHECKPOINTS, per-save METADATA (so a load menu can list "Slot 2 —
 * Crypt, 1h23m, 3 days ago"), and helpers to CAPTURE/RESTORE live actor state
 * (transform + visibility) so you save the actual world, not just hand-picked
 * scalars.
 *
 * Files live under a base directory you choose; slots are addressed by index or
 * by the reserved quicksave/autosave channels. Everything is built on the
 * existing cc_save_* KV primitive, so a game can still poke arbitrary keys into
 * the same CCSaveState before writing.
 *
 *   CCSaveGame* sg = cc_savegame_open("/saves", 3, 3);  // 3 slots, 3 autosaves
 *   CCSaveState* s = cc_save_new();
 *   cc_save_set_int(s, "player.hp", 87);
 *   cc_savegame_capture_actor(s, "player", player_actor);   // world state
 *   cc_savegame_write_slot(sg, 0, s, "Crypt Level 2");      // stamps metadata
 *   cc_save_free(s);
 *   ... later:
 *   CCSaveState* r = cc_savegame_read_slot(sg, 0);
 *   int hp = cc_save_get_int(r, "player.hp", 100);
 *   cc_savegame_restore_actor(r, "player", player_actor);   // move it back
 *
 * Autosave is throttled (min seconds between) and rotates across the autosave
 * ring so you keep the last N. Metadata is stored inside the save itself under
 * reserved __meta.* keys, so listing a slot is just a cheap read.
 *
 * Pure CPU + filesystem, headless-safe.
 */
#include "cc/save.h"
#include "cc/actor.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCSaveGame CCSaveGame;

/* Metadata surfaced for a load menu (read cheaply from a slot without loading
 * the whole game). All fields are filled from the save's reserved __meta.* keys;
 * `exists` is false if the slot file is absent. */
typedef struct {
    bool     exists;
    char     label[64];       /* caller-supplied, e.g. level or area name    */
    int64_t  timestamp;       /* unix time the save was written              */
    double   playtime_seconds;/* accumulated playtime, if the game tracks it */
    int32_t  version;         /* save format/game version                    */
} CCSaveMeta;

/* ─── lifecycle ───────────────────────────────────────────────────────────── */
/* Open (creating the directory if needed) a save manager rooted at base_dir with
 * `slots` manual slots and `autosaves` autosave ring entries. Returns NULL on
 * failure (e.g. cannot create dir). */
CCSaveGame* cc_savegame_open(const char* base_dir, uint32_t slots, uint32_t autosaves);
void        cc_savegame_close(CCSaveGame* sg);

/* Stamp the game version written into future saves' metadata (default 1). */
void cc_savegame_set_version(CCSaveGame* sg, int32_t version);
/* Tell the manager the current playtime so autosaves/metadata can record it. */
void cc_savegame_set_playtime(CCSaveGame* sg, double seconds);

/* ─── manual slots ────────────────────────────────────────────────────────── */
/* Write `s` to slot `index` (0..slots-1), stamping metadata (timestamp, label,
 * version, playtime). Returns true on success. */
bool cc_savegame_write_slot(CCSaveGame* sg, uint32_t index, CCSaveState* s, const char* label);
/* Read a slot; caller frees with cc_save_free. NULL if absent/error. */
CCSaveState* cc_savegame_read_slot(CCSaveGame* sg, uint32_t index);
/* Cheap metadata peek for a load menu (does not keep the state around). */
CCSaveMeta   cc_savegame_slot_meta(CCSaveGame* sg, uint32_t index);
bool         cc_savegame_slot_exists(CCSaveGame* sg, uint32_t index);
bool         cc_savegame_delete_slot(CCSaveGame* sg, uint32_t index);

/* ─── quicksave (single reserved channel) ─────────────────────────────────── */
bool         cc_savegame_quicksave(CCSaveGame* sg, CCSaveState* s, const char* label);
CCSaveState* cc_savegame_quickload(CCSaveGame* sg);   /* NULL if none */
bool         cc_savegame_has_quicksave(CCSaveGame* sg);

/* ─── autosave (throttled ring) ───────────────────────────────────────────── */
/* Set the minimum seconds between autosaves (default 0 = unthrottled). */
void cc_savegame_set_autosave_interval(CCSaveGame* sg, double min_seconds);
/* Attempt an autosave at wall-clock `now_seconds`. Rotates to the next ring
 * entry. Skipped (returns false) if the interval hasn't elapsed since the last
 * autosave. Pass now_seconds monotonically (e.g. cc_time). */
bool cc_savegame_autosave(CCSaveGame* sg, CCSaveState* s, const char* label, double now_seconds);
/* Load the MOST RECENT autosave (by timestamp) across the ring; NULL if none. */
CCSaveState* cc_savegame_load_latest_autosave(CCSaveGame* sg);
/* Metadata of autosave ring entry i (0..autosaves-1). */
CCSaveMeta   cc_savegame_autosave_meta(CCSaveGame* sg, uint32_t i);

/* ─── live actor capture / restore ────────────────────────────────────────── */
/* Store an actor's transform (pos/rot/scale) + visibility under keys prefixed by
 * `id` (e.g. "actor.<id>.pos"). Give each persistent actor a stable string id. */
void cc_savegame_capture_actor(CCSaveState* s, const char* id, CCActor a);
/* Apply previously-captured transform + visibility back onto an actor. Returns
 * true if the actor's data was present in the save. */
bool cc_savegame_restore_actor(CCSaveState* s, const char* id, CCActor a);
/* True if the save contains captured data for `id`. */
bool cc_savegame_has_actor(const CCSaveState* s, const char* id);

#ifdef __cplusplus
}
#endif
