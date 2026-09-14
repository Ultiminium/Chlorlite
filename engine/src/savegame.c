/* savegame.c — CCSaveGame: slots, quicksave, autosave ring, checkpoints,
 * metadata, and live actor capture/restore. Built on the cc_save_* KV store
 * (cc/save.h). See cc/savegame.h. Pure CPU + filesystem, headless-safe.
 */
#include "cc/savegame.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#if defined(_WIN32)
  #include <direct.h>
  #define SG_MKDIR(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #define SG_MKDIR(p) mkdir((p), 0755)
#endif

/* reserved metadata keys stored inside each save */
#define MK_LABEL    "__meta.label"
#define MK_TIME     "__meta.timestamp"
#define MK_PLAYTIME "__meta.playtime"
#define MK_VERSION  "__meta.version"
#define MK_SEQ      "__meta.seq"     /* monotonic write counter (tie-breaks time) */

struct CCSaveGame {
    char     dir[512];
    uint32_t slots;
    uint32_t autosaves;
    int32_t  version;
    double   playtime;
    double   autosave_interval;
    double   last_autosave_time;   /* wall-clock of last successful autosave    */
    bool     have_last_autosave;
    uint32_t autosave_cursor;      /* next ring slot to write                   */
    uint64_t write_seq;            /* monotonic, stamped into every save        */
};

/* ── path helpers ────────────────────────────────────────────────────────── */
static void slot_path(const CCSaveGame* sg, char* out, size_t n, uint32_t i) {
    snprintf(out, n, "%.400s/slot_%u.ccsave", sg->dir, i);
}
static void quick_path(const CCSaveGame* sg, char* out, size_t n) {
    snprintf(out, n, "%.400s/quicksave.ccsave", sg->dir);
}
static void auto_path(const CCSaveGame* sg, char* out, size_t n, uint32_t i) {
    snprintf(out, n, "%.400s/autosave_%u.ccsave", sg->dir, i);
}

/* ── lifecycle ───────────────────────────────────────────────────────────── */
CCSaveGame* cc_savegame_open(const char* base_dir, uint32_t slots, uint32_t autosaves) {
    if (!base_dir) return NULL;
    SG_MKDIR(base_dir);   /* ignore EEXIST */
    CCSaveGame* sg = (CCSaveGame*)calloc(1, sizeof(CCSaveGame));
    if (!sg) return NULL;
    snprintf(sg->dir, sizeof(sg->dir), "%.400s", base_dir);
    sg->slots = slots;
    sg->autosaves = autosaves ? autosaves : 1;
    sg->version = 1;
    return sg;
}
void cc_savegame_close(CCSaveGame* sg) { free(sg); }

void cc_savegame_set_version(CCSaveGame* sg, int32_t v) { if (sg) sg->version = v; }
void cc_savegame_set_playtime(CCSaveGame* sg, double s) { if (sg) sg->playtime = s; }

/* stamp reserved metadata into a save just before writing */
static void stamp_meta(CCSaveGame* sg, CCSaveState* s, const char* label) {
    cc_save_set_str  (s, MK_LABEL, label ? label : "");
    cc_save_set_int  (s, MK_TIME, (int64_t)time(NULL));
    cc_save_set_float(s, MK_PLAYTIME, (float)sg->playtime);
    cc_save_set_int  (s, MK_VERSION, sg->version);
    cc_save_set_int  (s, MK_SEQ, (int64_t)(++sg->write_seq));
}

/* read metadata out of a file cheaply */
static CCSaveMeta read_meta_path(const char* path) {
    CCSaveMeta m; memset(&m, 0, sizeof(m));
    CCSaveState* s = cc_save_read(path);
    if (!s) return m;
    m.exists = true;
    snprintf(m.label, sizeof(m.label), "%s", cc_save_get_str(s, MK_LABEL, ""));
    m.timestamp = cc_save_get_int(s, MK_TIME, 0);
    m.playtime_seconds = cc_save_get_float(s, MK_PLAYTIME, 0.0f);
    m.version = (int32_t)cc_save_get_int(s, MK_VERSION, 1);
    cc_save_free(s);
    return m;
}

/* ── manual slots ────────────────────────────────────────────────────────── */
bool cc_savegame_write_slot(CCSaveGame* sg, uint32_t index, CCSaveState* s, const char* label) {
    if (!sg || !s || index >= sg->slots) return false;
    stamp_meta(sg, s, label);
    char p[512]; slot_path(sg, p, sizeof(p), index);
    return cc_save_write(s, p);
}
CCSaveState* cc_savegame_read_slot(CCSaveGame* sg, uint32_t index) {
    if (!sg || index >= sg->slots) return NULL;
    char p[512]; slot_path(sg, p, sizeof(p), index);
    return cc_save_read(p);
}
CCSaveMeta cc_savegame_slot_meta(CCSaveGame* sg, uint32_t index) {
    CCSaveMeta m; memset(&m, 0, sizeof(m));
    if (!sg || index >= sg->slots) return m;
    char p[512]; slot_path(sg, p, sizeof(p), index);
    return read_meta_path(p);
}
bool cc_savegame_slot_exists(CCSaveGame* sg, uint32_t index) {
    return cc_savegame_slot_meta(sg, index).exists;
}
bool cc_savegame_delete_slot(CCSaveGame* sg, uint32_t index) {
    if (!sg || index >= sg->slots) return false;
    char p[512]; slot_path(sg, p, sizeof(p), index);
    return remove(p) == 0;
}

/* ── quicksave ───────────────────────────────────────────────────────────── */
bool cc_savegame_quicksave(CCSaveGame* sg, CCSaveState* s, const char* label) {
    if (!sg || !s) return false;
    stamp_meta(sg, s, label ? label : "Quicksave");
    char p[512]; quick_path(sg, p, sizeof(p));
    return cc_save_write(s, p);
}
CCSaveState* cc_savegame_quickload(CCSaveGame* sg) {
    if (!sg) return NULL;
    char p[512]; quick_path(sg, p, sizeof(p));
    return cc_save_read(p);
}
bool cc_savegame_has_quicksave(CCSaveGame* sg) {
    if (!sg) return false;
    char p[512]; quick_path(sg, p, sizeof(p));
    return read_meta_path(p).exists;
}

/* ── autosave ring ───────────────────────────────────────────────────────── */
void cc_savegame_set_autosave_interval(CCSaveGame* sg, double s) {
    if (sg) sg->autosave_interval = s < 0 ? 0 : s;
}
bool cc_savegame_autosave(CCSaveGame* sg, CCSaveState* s, const char* label, double now) {
    if (!sg || !s) return false;
    if (sg->have_last_autosave &&
        (now - sg->last_autosave_time) < sg->autosave_interval) {
        return false;   /* throttled */
    }
    stamp_meta(sg, s, label ? label : "Autosave");
    char p[512]; auto_path(sg, p, sizeof(p), sg->autosave_cursor);
    bool ok = cc_save_write(s, p);
    if (ok) {
        sg->autosave_cursor = (sg->autosave_cursor + 1) % sg->autosaves;
        sg->last_autosave_time = now;
        sg->have_last_autosave = true;
    }
    return ok;
}
CCSaveState* cc_savegame_load_latest_autosave(CCSaveGame* sg) {
    if (!sg) return NULL;
    int64_t best_seq = -1; int best_i = -1;
    for (uint32_t i = 0; i < sg->autosaves; i++) {
        char p[512]; auto_path(sg, p, sizeof(p), i);
        CCSaveState* st = cc_save_read(p);
        if (!st) continue;
        int64_t seq = cc_save_get_int(st, MK_SEQ, cc_save_get_int(st, MK_TIME, 0));
        cc_save_free(st);
        if (seq > best_seq) { best_seq = seq; best_i = (int)i; }
    }
    if (best_i < 0) return NULL;
    char p[512]; auto_path(sg, p, sizeof(p), (uint32_t)best_i);
    return cc_save_read(p);
}
CCSaveMeta cc_savegame_autosave_meta(CCSaveGame* sg, uint32_t i) {
    CCSaveMeta m; memset(&m, 0, sizeof(m));
    if (!sg || i >= sg->autosaves) return m;
    char p[512]; auto_path(sg, p, sizeof(p), i);
    return read_meta_path(p);
}

/* ── live actor capture / restore ────────────────────────────────────────── */
/* quaternion (xyzw) → Euler degrees (matches cc_actor_set_rotation input) */
static void quat_to_euler_deg(const float q[4], float* rx, float* ry, float* rz) {
    float x=q[0], y=q[1], z=q[2], w=q[3];
    /* ZYX-ish; good enough for round-tripping actor rotations */
    float sinr = 2.0f*(w*x + y*z), cosr = 1.0f - 2.0f*(x*x + y*y);
    float roll = atan2f(sinr, cosr);
    float sinp = 2.0f*(w*y - z*x);
    float pitch = fabsf(sinp) >= 1.0f ? copysignf(3.14159265f/2.0f, sinp) : asinf(sinp);
    float siny = 2.0f*(w*z + x*y), cosy = 1.0f - 2.0f*(y*y + z*z);
    float yaw = atan2f(siny, cosy);
    const float R2D = 57.29577951f;
    if (rx) *rx = roll*R2D; if (ry) *ry = pitch*R2D; if (rz) *rz = yaw*R2D;
}

static void akey(char* out, size_t n, const char* id, const char* field) {
    snprintf(out, n, "actor.%s.%s", id, field);
}

void cc_savegame_capture_actor(CCSaveState* s, const char* id, CCActor a) {
    if (!s || !id) return;
    CCTransform3D t;
    if (!cc_actor_get_transform(a, &t)) return;
    char k[128];
    akey(k, sizeof(k), id, "pos");   cc_save_set_vec3(s, k, t.pos[0], t.pos[1], t.pos[2]);
    akey(k, sizeof(k), id, "scale"); cc_save_set_vec3(s, k, t.scale[0], t.scale[1], t.scale[2]);
    /* store quaternion as 4 floats via two vec3-ish keys is wasteful; pack as
     * a string "x y z w" to keep the KV store simple */
    char qs[96]; snprintf(qs, sizeof(qs), "%.6f %.6f %.6f %.6f", t.rot[0],t.rot[1],t.rot[2],t.rot[3]);
    akey(k, sizeof(k), id, "rot"); cc_save_set_str(s, k, qs);
    akey(k, sizeof(k), id, "vis"); cc_save_set_bool(s, k, cc_actor_visible(a));
}

bool cc_savegame_has_actor(const CCSaveState* s, const char* id) {
    if (!s || !id) return false;
    char k[128]; akey(k, sizeof(k), id, "pos");
    return cc_save_has(s, k);
}

bool cc_savegame_restore_actor(CCSaveState* s, const char* id, CCActor a) {
    if (!s || !id) return false;
    char k[128];
    akey(k, sizeof(k), id, "pos");
    if (!cc_save_has(s, k)) return false;
    float px,py,pz; cc_save_get_vec3(s, k, &px,&py,&pz);
    cc_actor_set_position(a, px, py, pz);
    akey(k, sizeof(k), id, "scale");
    float sx,sy,sz; cc_save_get_vec3(s, k, &sx,&sy,&sz);
    cc_actor_set_scale(a, sx, sy, sz);
    akey(k, sizeof(k), id, "rot");
    const char* qs = cc_save_get_str(s, k, "0 0 0 1");
    float q[4] = {0,0,0,1};
    sscanf(qs, "%f %f %f %f", &q[0],&q[1],&q[2],&q[3]);
    float rx,ry,rz; quat_to_euler_deg(q, &rx,&ry,&rz);
    cc_actor_set_rotation(a, rx, ry, rz);
    akey(k, sizeof(k), id, "vis");
    cc_actor_set_visible(a, cc_save_get_bool(s, k, true));
    return true;
}
