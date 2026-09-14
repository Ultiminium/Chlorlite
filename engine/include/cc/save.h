#pragma once
/*
 * CCSaveState — game save/load as a typed key-value store.
 *
 * A save file is a set of named values a game writes and reads back: player
 * stats, world flags, quest progress, inventory counts, positions, the current
 * level name, a timestamp — whatever the game needs. Values are typed (int,
 * float, bool, string, vec3, blob) and addressed by string key, optionally
 * grouped by a section prefix ("player.hp", "quest.intro.done"). Serializes to a
 * human-readable, hand-editable text .ccsave file (same spirit as .ccmodel /
 * .cclist). This is the foundation the rest of the gameplay layer builds on
 * (checkpoints, quest state, persistence).
 *
 *   CCSaveState* s = cc_save_new();
 *   cc_save_set_int(s, "player.hp", 87);
 *   cc_save_set_str(s, "level", "crypt_02.cclist");
 *   cc_save_set_vec3(s, "player.pos", x, y, z);
 *   cc_save_write(s, "slot1.ccsave");
 *   ...
 *   CCSaveState* s = cc_save_read("slot1.ccsave");
 *   int hp = cc_save_get_int(s, "player.hp", 100);   // default if absent
 *   cc_save_free(s);
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCSaveState CCSaveState;

/* lifecycle */
CCSaveState* cc_save_new(void);
void         cc_save_free(CCSaveState* s);

/* file I/O (text .ccsave). read returns NULL on failure. */
bool         cc_save_write(const CCSaveState* s, const char* path);
CCSaveState* cc_save_read(const char* path);

/* setters (overwrite if the key already exists) */
void cc_save_set_int (CCSaveState* s, const char* key, int64_t v);
void cc_save_set_float(CCSaveState* s, const char* key, float v);
void cc_save_set_bool(CCSaveState* s, const char* key, bool v);
void cc_save_set_str (CCSaveState* s, const char* key, const char* v);
void cc_save_set_vec3(CCSaveState* s, const char* key, float x, float y, float z);

/* getters (return the default if the key is missing or the wrong type) */
int64_t     cc_save_get_int  (const CCSaveState* s, const char* key, int64_t def);
float       cc_save_get_float(const CCSaveState* s, const char* key, float def);
bool        cc_save_get_bool (const CCSaveState* s, const char* key, bool def);
const char* cc_save_get_str  (const CCSaveState* s, const char* key, const char* def);
void        cc_save_get_vec3 (const CCSaveState* s, const char* key, float* x, float* y, float* z);

/* introspection */
bool     cc_save_has(const CCSaveState* s, const char* key);
void     cc_save_remove(CCSaveState* s, const char* key);
uint32_t cc_save_count(const CCSaveState* s);
/* enumerate keys (returns key at index, or NULL if out of range) */
const char* cc_save_key_at(const CCSaveState* s, uint32_t index);

#ifdef __cplusplus
}
#endif
