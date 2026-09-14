#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCEngine CCEngine;
typedef uint64_t CCEntityId;

/* ─── Script registration (C/C++) ────────────────────────────────────── */
/*
 * Register a script type by name.
 * Lifecycle callbacks are called by the ECS each tick.
 * Use this to attach behavior to entities in C or C++.
 */
typedef struct CCScriptDef {
    const char* name;
    size_t      instance_size;
    void (*on_init)   (void* inst, CCEngine* eng, CCEntityId entity);
    void (*on_update) (void* inst, CCEngine* eng, CCEntityId entity, double dt);
    void (*on_destroy)(void* inst, CCEngine* eng, CCEntityId entity);
} CCScriptDef;

void cc_script_register(CCEngine* eng, const CCScriptDef* def);
void cc_script_attach(CCEngine* eng, CCEntityId entity, const char* script_name);
void cc_script_detach(CCEngine* eng, CCEntityId entity, const char* script_name);

/* ─── Python scripting ───────────────────────────────────────────────── */
#ifdef CC_SCRIPTING_PYTHON
typedef struct CCEventBus CCEventBus;
bool cc_python_init(CCEngine* eng);
bool cc_python_exec_file(CCEngine* eng, const char* path);
bool cc_python_exec(CCEngine* eng, const char* code);
void* cc_python_call(CCEngine* eng, const char* module, const char* fn,
                     int argc, void** argv);
/* Wire the event bus that scripts publish on via cc.emit(...). Optional. */
void cc_python_bind_bus(CCEventBus* bus);
/* Finalize the interpreter (safe to call once at shutdown; idempotent). */
void cc_python_shutdown(void);

/* ─── Python hot-reload ──────────────────────────────────────────────────
 * Watch a .py file for edits and re-execute it when it changes on disk, so
 * gameplay logic updates live without restarting. Re-running a script redefines
 * its functions in place (the usual Python module-reload effect). Register each
 * file once with cc_python_watch, then call cc_python_poll_reloads() each frame
 * (cheap: a stat() per watched file); it re-execs any that changed and returns
 * the number reloaded. cc_python_reload_all forces a re-exec of all watched
 * files regardless of mtime. */
bool cc_python_watch(CCEngine* eng, const char* path);
int  cc_python_poll_reloads(CCEngine* eng);
int  cc_python_reload_all(CCEngine* eng);
#endif

/* ─── Rust FFI bridge ────────────────────────────────────────────────── */
/*
 * Load a compiled Rust shared library and call cc_rust_entry() inside it.
 * The Rust side uses the cc_rust_sdk crate to register scripts.
 */
bool cc_rust_load(CCEngine* eng, const char* dylib_path);
void cc_rust_unload(CCEngine* eng, const char* dylib_path);

/* ─── Script hot-reload ──────────────────────────────────────────────── */
/* Watch script files and reload changed ones at runtime */
void cc_script_watch(CCEngine* eng, const char* dir);
void cc_script_reload_all(CCEngine* eng);

#ifdef __cplusplus
}
#endif
