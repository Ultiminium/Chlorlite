#pragma once
/*
 * cc/steam.h — Chlorlite Steamworks bridge
 *
 * Steam's SDK is not redistributable, so this is a thin, optional bridge:
 *   - Build WITHOUT the SDK  → every call safely no-ops, cc_steam_available()
 *                              returns false. Your game builds and runs fine.
 *   - Build WITH the SDK     → define CC_USE_STEAM and link steam_api; calls
 *                              route to real Steamworks (achievements, cloud
 *                              saves, rich presence, overlay).
 *
 * To ship on Steam: drop the Steamworks SDK in third_party/steam/, build with
 * -DCC_USE_STEAM, and ship your steam_appid.txt. The game code is identical
 * either way — that's the point.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize Steam. Returns false if the SDK isn't linked or Steam isn't
   running — the game continues normally either way. */
bool cc_steam_init(uint32_t app_id);
void cc_steam_shutdown(void);
void cc_steam_run_callbacks(void);   /* call once per frame */
bool cc_steam_available(void);       /* is a real Steam session active? */

/* ─── Achievements ───────────────────────────────────────────────────── */
bool cc_steam_unlock_achievement(const char* api_name);
bool cc_steam_clear_achievement(const char* api_name);
bool cc_steam_achievement_unlocked(const char* api_name);

/* ─── Stats ──────────────────────────────────────────────────────────── */
bool  cc_steam_set_stat_int(const char* name, int32_t value);
bool  cc_steam_set_stat_float(const char* name, float value);
int32_t cc_steam_get_stat_int(const char* name);
float cc_steam_get_stat_float(const char* name);
bool  cc_steam_store_stats(void);

/* ─── Cloud saves ────────────────────────────────────────────────────── */
bool     cc_steam_cloud_write(const char* file, const void* data, uint32_t size);
uint32_t cc_steam_cloud_read(const char* file, void* out, uint32_t max_size);
bool     cc_steam_cloud_exists(const char* file);

/* ─── Rich presence / user ───────────────────────────────────────────── */
void        cc_steam_set_rich_presence(const char* key, const char* value);
const char* cc_steam_username(void);
uint64_t    cc_steam_user_id(void);

#ifdef __cplusplus
}
#endif
