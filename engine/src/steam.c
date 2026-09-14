/*
 * steam.c — Steamworks bridge implementation
 *
 * The default build has no Steamworks SDK, so this compiles as a safe no-op
 * layer that also provides a LOCAL fallback for cloud saves (writes to disk)
 * so save/load works identically in development. When built with -DCC_USE_STEAM
 * and linked against steam_api, the real Steamworks calls are used instead.
 *
 * The real-SDK path is guarded and calls the C flat API (steam_api_flat.h);
 * we keep it in an #ifdef so the default build never needs the SDK present.
 */
#include "cc/steam.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef CC_USE_STEAM
#include "steam/steam_api_flat.h"
static bool g_steam_ok = false;
static ISteamUserStats*  g_stats   = NULL;
static ISteamRemoteStorage* g_cloud = NULL;
static ISteamFriends*    g_friends = NULL;
static ISteamUser*       g_user    = NULL;
#endif

static uint32_t g_app_id = 0;

bool cc_steam_init(uint32_t app_id) {
    g_app_id = app_id;
#ifdef CC_USE_STEAM
    if (!SteamAPI_Init()) { fprintf(stderr, "[cc.steam] SteamAPI_Init failed (Steam not running?)\n"); return false; }
    g_stats   = SteamAPI_SteamUserStats_v012();
    g_cloud   = SteamAPI_SteamRemoteStorage_v016();
    g_friends = SteamAPI_SteamFriends_v017();
    g_user    = SteamAPI_SteamUser_v023();
    if (g_stats) SteamAPI_ISteamUserStats_RequestCurrentStats(g_stats);
    g_steam_ok = true;
    return true;
#else
    (void)app_id;
    return false;   /* no SDK linked — game continues without Steam */
#endif
}

void cc_steam_shutdown(void) {
#ifdef CC_USE_STEAM
    if (g_steam_ok) { SteamAPI_Shutdown(); g_steam_ok = false; }
#endif
}

void cc_steam_run_callbacks(void) {
#ifdef CC_USE_STEAM
    if (g_steam_ok) SteamAPI_RunCallbacks();
#endif
}

bool cc_steam_available(void) {
#ifdef CC_USE_STEAM
    return g_steam_ok;
#else
    return false;
#endif
}

/* ─── Achievements ───────────────────────────────────────────────────── */
bool cc_steam_unlock_achievement(const char* name) {
#ifdef CC_USE_STEAM
    if (!g_steam_ok || !g_stats) return false;
    SteamAPI_ISteamUserStats_SetAchievement(g_stats, name);
    SteamAPI_ISteamUserStats_StoreStats(g_stats);
    return true;
#else
    (void)name; return false;
#endif
}
bool cc_steam_clear_achievement(const char* name) {
#ifdef CC_USE_STEAM
    if (!g_steam_ok || !g_stats) return false;
    SteamAPI_ISteamUserStats_ClearAchievement(g_stats, name);
    return true;
#else
    (void)name; return false;
#endif
}
bool cc_steam_achievement_unlocked(const char* name) {
#ifdef CC_USE_STEAM
    if (!g_steam_ok || !g_stats) return false;
    bool got = false;
    SteamAPI_ISteamUserStats_GetAchievement(g_stats, name, &got);
    return got;
#else
    (void)name; return false;
#endif
}

/* ─── Stats ──────────────────────────────────────────────────────────── */
bool cc_steam_set_stat_int(const char* n, int32_t v) {
#ifdef CC_USE_STEAM
    if (!g_steam_ok||!g_stats) return false;
    return SteamAPI_ISteamUserStats_SetStatInt32(g_stats, n, v);
#else
    (void)n;(void)v; return false;
#endif
}
bool cc_steam_set_stat_float(const char* n, float v) {
#ifdef CC_USE_STEAM
    if (!g_steam_ok||!g_stats) return false;
    return SteamAPI_ISteamUserStats_SetStatFloat(g_stats, n, v);
#else
    (void)n;(void)v; return false;
#endif
}
int32_t cc_steam_get_stat_int(const char* n) {
#ifdef CC_USE_STEAM
    int32_t v=0; if (g_steam_ok&&g_stats) SteamAPI_ISteamUserStats_GetStatInt32(g_stats,n,&v); return v;
#else
    (void)n; return 0;
#endif
}
float cc_steam_get_stat_float(const char* n) {
#ifdef CC_USE_STEAM
    float v=0; if (g_steam_ok&&g_stats) SteamAPI_ISteamUserStats_GetStatFloat(g_stats,n,&v); return v;
#else
    (void)n; return 0.0f;
#endif
}
bool cc_steam_store_stats(void) {
#ifdef CC_USE_STEAM
    if (!g_steam_ok||!g_stats) return false;
    return SteamAPI_ISteamUserStats_StoreStats(g_stats);
#else
    return false;
#endif
}

/* ─── Cloud saves (local-disk fallback when no SDK) ──────────────────── */
static void local_cloud_path(const char* file, char* out, size_t n) {
    const char* home = getenv("HOME");
    snprintf(out, n, "%s/.claudecore_cloud_%u_%s", home?home:"/tmp", g_app_id, file);
}
bool cc_steam_cloud_write(const char* file, const void* data, uint32_t size) {
#ifdef CC_USE_STEAM
    if (g_steam_ok && g_cloud)
        return SteamAPI_ISteamRemoteStorage_FileWrite(g_cloud, file, data, (int)size);
#endif
    /* fallback: local disk so cloud saves work in dev */
    char path[1024]; local_cloud_path(file, path, sizeof(path));
    FILE* f = fopen(path, "wb"); if (!f) return false;
    size_t w = fwrite(data, 1, size, f); fclose(f);
    return w == size;
}
uint32_t cc_steam_cloud_read(const char* file, void* out, uint32_t max_size) {
#ifdef CC_USE_STEAM
    if (g_steam_ok && g_cloud) {
        int32_t sz = SteamAPI_ISteamRemoteStorage_GetFileSize(g_cloud, file);
        if (sz > (int32_t)max_size) sz = max_size;
        if (sz > 0) return SteamAPI_ISteamRemoteStorage_FileRead(g_cloud, file, out, sz);
        return 0;
    }
#endif
    char path[1024]; local_cloud_path(file, path, sizeof(path));
    FILE* f = fopen(path, "rb"); if (!f) return 0;
    uint32_t r = (uint32_t)fread(out, 1, max_size, f); fclose(f);
    return r;
}
bool cc_steam_cloud_exists(const char* file) {
#ifdef CC_USE_STEAM
    if (g_steam_ok && g_cloud) return SteamAPI_ISteamRemoteStorage_FileExists(g_cloud, file);
#endif
    char path[1024]; local_cloud_path(file, path, sizeof(path));
    FILE* f = fopen(path, "rb"); if (f) { fclose(f); return true; } return false;
}

/* ─── Rich presence / user ───────────────────────────────────────────── */
void cc_steam_set_rich_presence(const char* key, const char* value) {
#ifdef CC_USE_STEAM
    if (g_steam_ok && g_friends) SteamAPI_ISteamFriends_SetRichPresence(g_friends, key, value);
#else
    (void)key;(void)value;
#endif
}
const char* cc_steam_username(void) {
#ifdef CC_USE_STEAM
    if (g_steam_ok && g_friends) return SteamAPI_ISteamFriends_GetPersonaName(g_friends);
#endif
    return "Player";
}
uint64_t cc_steam_user_id(void) {
#ifdef CC_USE_STEAM
    if (g_steam_ok && g_user) return SteamAPI_ISteamUser_GetSteamID(g_user);
#endif
    return 0;
}
