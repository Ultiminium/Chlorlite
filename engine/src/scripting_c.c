/*
 * scripting_c.c — Chlorlite C/C++/Rust scripting bridge
 *
 * A real component-script system: register named script types with lifecycle
 * callbacks (init/update/destroy), attach instances to entities, and drive them
 * each tick via cc_script_tick(). Rust integration is a genuine dlopen bridge
 * (a compiled cdylib exposing cc_rust_entry(CCEngine*) registers scripts through
 * the same C API). Hot-reload re-dlopens changed shared libraries.
 */
#include "cc/scripting.h"
#include "cc/claudecore.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
  #include <windows.h>
  #define CC_DLOPEN(p)     ((void*)LoadLibraryA(p))
  #define CC_DLSYM(h,n)    ((void*)GetProcAddress((HMODULE)(h),(n)))
  #define CC_DLCLOSE(h)    FreeLibrary((HMODULE)(h))
  static const char* CC_DLERROR(void){ return "LoadLibrary failed"; }
#else
  #include <dlfcn.h>
  #define CC_DLOPEN(p)     dlopen((p), RTLD_NOW|RTLD_LOCAL)
  #define CC_DLSYM(h,n)    dlsym((h),(n))
  #define CC_DLCLOSE(h)    dlclose(h)
  #define CC_DLERROR()     dlerror()
#endif

#define CC_MAX_SCRIPT_TYPES 128
#define CC_MAX_SCRIPT_INSTANCES 4096
#define CC_MAX_DYLIBS 32

typedef struct {
    CCScriptDef def;
    char        name[64];
    bool        used;
} ScriptType;

typedef struct {
    uint32_t   type_index;
    CCEntityId entity;
    void*      instance;   /* heap block of def.instance_size */
    bool       active;
} ScriptInstance;

typedef struct {
    char   path[512];
    void*  handle;         /* dlopen handle */
    bool   used;
} LoadedDylib;

typedef struct CCScriptSystem {
    ScriptType     types[CC_MAX_SCRIPT_TYPES];
    uint32_t       type_count;
    ScriptInstance instances[CC_MAX_SCRIPT_INSTANCES];
    uint32_t       instance_count;
    LoadedDylib    dylibs[CC_MAX_DYLIBS];
    char           watch_dir[512];
} CCScriptSystem;

extern CCScriptSystem* cc_engine_script_get(CCEngine* eng);
extern void            cc_engine_script_set(CCEngine* eng, CCScriptSystem* s);

static CCScriptSystem* script_sys(CCEngine* eng) {
    CCScriptSystem* s = cc_engine_script_get(eng);
    if (s) return s;
    s = calloc(1, sizeof(CCScriptSystem));
    cc_engine_script_set(eng, s);
    return s;
}

static int find_type(CCScriptSystem* s, const char* name) {
    for (uint32_t i = 0; i < s->type_count; i++)
        if (s->types[i].used && strcmp(s->types[i].name, name) == 0) return (int)i;
    return -1;
}

/* ─── Registration ───────────────────────────────────────────────────── */
void cc_script_register(CCEngine* eng, const CCScriptDef* def) {
    if (!def || !def->name) return;
    CCScriptSystem* s = script_sys(eng);
    int existing = find_type(s, def->name);
    if (existing >= 0) {           /* re-register = hot reload of behavior */
        s->types[existing].def = *def;
        return;
    }
    if (s->type_count >= CC_MAX_SCRIPT_TYPES) { CC_ERROR("script registry full"); return; }
    ScriptType* t = &s->types[s->type_count++];
    t->def = *def;
    strncpy(t->name, def->name, sizeof(t->name)-1);
    t->used = true;
    CC_INFO("script registered: %s (%zu bytes)", def->name, def->instance_size);
}

void cc_script_attach(CCEngine* eng, CCEntityId entity, const char* name) {
    CCScriptSystem* s = script_sys(eng);
    int ti = find_type(s, name);
    if (ti < 0) { CC_WARN("cc_script_attach: unknown script '%s'", name); return; }
    if (s->instance_count >= CC_MAX_SCRIPT_INSTANCES) { CC_ERROR("script instances full"); return; }
    ScriptType* t = &s->types[ti];
    /* find a free slot (reuse inactive) */
    ScriptInstance* si = NULL;
    for (uint32_t i = 0; i < s->instance_count; i++)
        if (!s->instances[i].active) { si = &s->instances[i]; break; }
    if (!si) si = &s->instances[s->instance_count++];
    si->type_index = (uint32_t)ti;
    si->entity = entity;
    si->instance = t->def.instance_size ? calloc(1, t->def.instance_size) : NULL;
    si->active = true;
    if (t->def.on_init) t->def.on_init(si->instance, eng, entity);
}

void cc_script_detach(CCEngine* eng, CCEntityId entity, const char* name) {
    CCScriptSystem* s = script_sys(eng);
    int ti = find_type(s, name);
    for (uint32_t i = 0; i < s->instance_count; i++) {
        ScriptInstance* si = &s->instances[i];
        if (si->active && si->entity == entity && (ti < 0 || si->type_index == (uint32_t)ti)) {
            ScriptType* t = &s->types[si->type_index];
            if (t->def.on_destroy) t->def.on_destroy(si->instance, eng, entity);
            free(si->instance);
            si->instance = NULL;
            si->active = false;
        }
    }
}

/* ─── Tick — called each frame by the engine ─────────────────────────── */
void cc_script_tick(CCEngine* eng, double dt) {
    CCScriptSystem* s = cc_engine_script_get(eng);
    if (!s) return;
    for (uint32_t i = 0; i < s->instance_count; i++) {
        ScriptInstance* si = &s->instances[i];
        if (!si->active) continue;
        ScriptType* t = &s->types[si->type_index];
        if (t->def.on_update) t->def.on_update(si->instance, eng, si->entity, dt);
    }
}

/* ─── Rust FFI bridge (real dlopen) ──────────────────────────────────── */
bool cc_rust_load(CCEngine* eng, const char* dylib_path) {
    CCScriptSystem* s = script_sys(eng);
    void* h = CC_DLOPEN(dylib_path);
    if (!h) { CC_ERROR("cc_rust_load: %s", CC_DLERROR()); return false; }
    /* the cdylib must export cc_rust_entry(CCEngine*) */
    typedef void (*rust_entry_fn)(CCEngine*);
    rust_entry_fn entry = (rust_entry_fn)CC_DLSYM(h, "cc_rust_entry");
    if (!entry) { CC_ERROR("cc_rust_load: no cc_rust_entry in %s", dylib_path); CC_DLCLOSE(h); return false; }
    /* record the handle */
    for (int i = 0; i < CC_MAX_DYLIBS; i++) {
        if (!s->dylibs[i].used) {
            s->dylibs[i].used = true;
            s->dylibs[i].handle = h;
            strncpy(s->dylibs[i].path, dylib_path, sizeof(s->dylibs[i].path)-1);
            break;
        }
    }
    entry(eng);   /* Rust registers its scripts through cc_script_register */
    CC_INFO("cc_rust_load: loaded %s", dylib_path);
    return true;
}

void cc_rust_unload(CCEngine* eng, const char* dylib_path) {
    CCScriptSystem* s = script_sys(eng);
    for (int i = 0; i < CC_MAX_DYLIBS; i++) {
        if (s->dylibs[i].used && strcmp(s->dylibs[i].path, dylib_path) == 0) {
            CC_DLCLOSE(s->dylibs[i].handle);
            s->dylibs[i].used = false;
            return;
        }
    }
}

/* ─── Hot reload ─────────────────────────────────────────────────────── */
void cc_script_watch(CCEngine* eng, const char* dir) {
    CCScriptSystem* s = script_sys(eng);
    strncpy(s->watch_dir, dir ? dir : "", sizeof(s->watch_dir)-1);
}

void cc_script_reload_all(CCEngine* eng) {
    CCScriptSystem* s = script_sys(eng);
    /* re-dlopen every loaded dylib; re-running cc_rust_entry re-registers
       (which updates behavior in place via cc_script_register). */
    for (int i = 0; i < CC_MAX_DYLIBS; i++) {
        if (!s->dylibs[i].used) continue;
        void* nh = CC_DLOPEN(s->dylibs[i].path);
        if (!nh) { CC_WARN("reload: %s", CC_DLERROR()); continue; }
        typedef void (*rust_entry_fn)(CCEngine*);
        rust_entry_fn entry = (rust_entry_fn)CC_DLSYM(nh, "cc_rust_entry");
        if (entry) entry(eng);
        CC_DLCLOSE(s->dylibs[i].handle);
        s->dylibs[i].handle = nh;
    }
    CC_INFO("cc_script_reload_all: reloaded %s", s->watch_dir);
}
