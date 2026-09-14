#pragma once
/*
 * CCConsole — CVars (named runtime variables) + a command console. The backbone
 * of a debuggable engine: expose gameplay/engine knobs by NAME so they can be
 * tuned, inspected, and scripted at runtime without recompiling, and register
 * COMMANDS that a developer types (or a script/keybind fires) to make things
 * happen — spawn, noclip, reload, give, teleport.
 *
 * CVARS are typed named values (float / int / bool / string) with an optional
 * description and flags. Set/get by name from C, from the Python bridge, from a
 * config file, or by typing "name value" at the console. A cheap way to make any
 * tuning knob live:
 *
 *     cc_cvar_register_float(con, "ai_aggression", 0.5f, "0..1 guard aggression");
 *     ... anywhere: float a = cc_cvar_get_float(con, "ai_aggression");
 *     ... at runtime: cc_console_exec(con, "ai_aggression 0.9");   // now live
 *
 * COMMANDS are named callbacks that receive the raw argument string (already
 * split is up to you, or use cc_console_argc/argv helpers passed in). They can
 * print back into the console log via the provided printer.
 *
 *     cc_command_register(con, "spawn", spawn_cmd, game, "spawn <type> [x y z]");
 *     cc_console_exec(con, "spawn zombie 3 0 5");
 *
 * cc_console_exec parses one line: if the first token names a COMMAND it runs it;
 * else if it names a CVAR, "name" prints its value and "name value" sets it; else
 * it reports "unknown". A rolling LOG captures command output and any cc_console_
 * print so a UI (or a test) can read results back. Pure CPU, headless-safe.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCConsole CCConsole;

typedef enum {
    CC_CVAR_FLOAT = 0,
    CC_CVAR_INT,
    CC_CVAR_BOOL,
    CC_CVAR_STRING,
} CCCvarType;

enum {                       /* cvar flags (bitmask) */
    CC_CVAR_NONE     = 0,
    CC_CVAR_READONLY = 1<<0, /* cannot be set via the console (still get) */
    CC_CVAR_CHEAT    = 1<<1, /* only settable when cheats are enabled     */
};

/* A command callback. `argc`/`argv` are the parsed tokens (argv[0] = command
 * name). `print` writes a line into the console log (may be NULL-safe). `ud` is
 * the userdata registered with the command. */
typedef void (*CCCommandFn)(CCConsole* con, int argc, char** argv, void* ud);

/* ─── lifecycle ───────────────────────────────────────────────────────────── */
CCConsole* cc_console_create(void);
void       cc_console_destroy(CCConsole* con);

/* ─── CVar registration ───────────────────────────────────────────────────── */
void cc_cvar_register_float (CCConsole* con, const char* name, float v,       const char* desc);
void cc_cvar_register_int   (CCConsole* con, const char* name, int v,         const char* desc);
void cc_cvar_register_bool  (CCConsole* con, const char* name, bool v,        const char* desc);
void cc_cvar_register_string(CCConsole* con, const char* name, const char* v, const char* desc);
/* OR-in flags on an existing cvar (READONLY/CHEAT). */
void cc_cvar_set_flags(CCConsole* con, const char* name, uint32_t flags);

/* ─── CVar get/set (by name; missing → the provided default / no-op) ──────── */
float       cc_cvar_get_float (CCConsole* con, const char* name);
int         cc_cvar_get_int   (CCConsole* con, const char* name);
bool        cc_cvar_get_bool  (CCConsole* con, const char* name);
const char* cc_cvar_get_string(CCConsole* con, const char* name);   /* "" if none/other type */
/* Programmatic set (ignores READONLY/CHEAT — that gating is console-only). Returns
 * true if the cvar exists. Cross-type sets coerce (e.g. set_float on an int cvar). */
bool cc_cvar_set_float (CCConsole* con, const char* name, float v);
bool cc_cvar_set_int   (CCConsole* con, const char* name, int v);
bool cc_cvar_set_bool  (CCConsole* con, const char* name, bool v);
bool cc_cvar_set_string(CCConsole* con, const char* name, const char* v);
bool cc_cvar_exists(CCConsole* con, const char* name);

/* ─── Commands ────────────────────────────────────────────────────────────── */
void cc_command_register(CCConsole* con, const char* name, CCCommandFn fn,
                         void* userdata, const char* help);

/* ─── Execution ───────────────────────────────────────────────────────────── */
/* Parse+run one console line. Whitespace-tokenized. Returns true if it named a
 * known command or cvar (false → "unknown", still logged). Output (command
 * prints, cvar values, errors) goes to the rolling log. */
bool cc_console_exec(CCConsole* con, const char* line);
/* Enable/disable cheat-flagged cvars/commands (default off). */
void cc_console_set_cheats(CCConsole* con, bool enabled);
bool cc_console_cheats(const CCConsole* con);

/* ─── Log (command output; readable by a UI or a test) ────────────────────── */
/* Append a line to the log (printf-style). Commands call this to report. */
void        cc_console_print(CCConsole* con, const char* fmt, ...);
uint32_t    cc_console_log_count(const CCConsole* con);
/* Line i (0 = oldest kept). NULL if out of range. */
const char* cc_console_log_line(const CCConsole* con, uint32_t i);
/* The most recently appended line (convenience), or NULL if empty. */
const char* cc_console_log_last(const CCConsole* con);
void        cc_console_log_clear(CCConsole* con);

/* ─── Config files ────────────────────────────────────────────────────────── */
/* Run every line of a file through cc_console_exec (comments start with # or //,
 * blank lines skipped). Great for a settings.cfg / autoexec. Returns lines run. */
int  cc_console_exec_file(CCConsole* con, const char* path);
/* Write all non-readonly cvars as "name value" lines to a file (reloadable via
 * exec_file). Returns cvars written, or -1 on error. */
int  cc_console_save_cvars(CCConsole* con, const char* path);

/* ─── Introspection (for autocomplete / help UIs) ─────────────────────────── */
uint32_t    cc_console_cvar_count(const CCConsole* con);
const char* cc_console_cvar_name(const CCConsole* con, uint32_t i);   /* NULL if oob */
uint32_t    cc_console_command_count(const CCConsole* con);
const char* cc_console_command_name(const CCConsole* con, uint32_t i);

#ifdef __cplusplus
}
#endif
