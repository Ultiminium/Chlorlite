/* console.c — CCConsole: CVars + command console. See cc/console.h.
 * Pure CPU, headless-safe. Fixed-capacity tables (games register a bounded set).
 */
#include "cc/console.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define CON_MAX_CVARS    256
#define CON_MAX_COMMANDS 128
#define CON_MAX_ARGS     32
#define CON_LOG_LINES    256
#define CON_LINE_LEN     256
#define CON_STR_LEN      128
#define CON_NAME_LEN     64

typedef struct {
    char       name[CON_NAME_LEN];
    char       desc[96];
    CCCvarType type;
    uint32_t   flags;
    /* value union kept as all three so cross-type get/set can coerce cheaply */
    double     num;                  /* float/int/bool live here                */
    char       str[CON_STR_LEN];     /* string cvars                            */
    bool       used;
} Cvar;

typedef struct {
    char        name[CON_NAME_LEN];
    char        help[96];
    CCCommandFn fn;
    void*       ud;
    bool        used;
} Command;

struct CCConsole {
    Cvar     cvars[CON_MAX_CVARS];
    uint32_t cvar_count;
    Command  cmds[CON_MAX_COMMANDS];
    uint32_t cmd_count;

    char     log[CON_LOG_LINES][CON_LINE_LEN];
    uint32_t log_head;   /* index of next write (ring)                          */
    uint32_t log_size;   /* number of valid lines (<= CON_LOG_LINES)            */

    bool     cheats;
};

/* ── lifecycle ───────────────────────────────────────────────────────────── */
CCConsole* cc_console_create(void) { return (CCConsole*)calloc(1, sizeof(CCConsole)); }
void cc_console_destroy(CCConsole* c) { free(c); }

/* ── log (ring buffer) ───────────────────────────────────────────────────── */
void cc_console_print(CCConsole* c, const char* fmt, ...) {
    if (!c || !fmt) return;
    char* dst = c->log[c->log_head];
    va_list ap; va_start(ap, fmt);
    vsnprintf(dst, CON_LINE_LEN, fmt, ap);
    va_end(ap);
    c->log_head = (c->log_head + 1) % CON_LOG_LINES;
    if (c->log_size < CON_LOG_LINES) c->log_size++;
}
uint32_t cc_console_log_count(const CCConsole* c) { return c ? c->log_size : 0; }
const char* cc_console_log_line(const CCConsole* c, uint32_t i) {
    if (!c || i >= c->log_size) return NULL;
    /* oldest kept line is at (head - size) mod N */
    uint32_t start = (c->log_head + CON_LOG_LINES - c->log_size) % CON_LOG_LINES;
    return c->log[(start + i) % CON_LOG_LINES];
}
const char* cc_console_log_last(const CCConsole* c) {
    if (!c || c->log_size == 0) return NULL;
    return c->log[(c->log_head + CON_LOG_LINES - 1) % CON_LOG_LINES];
}
void cc_console_log_clear(CCConsole* c) { if (c) { c->log_head = 0; c->log_size = 0; } }

/* ── cvar table ──────────────────────────────────────────────────────────── */
static Cvar* cvar_find(CCConsole* c, const char* name) {
    if (!c || !name) return NULL;
    for (uint32_t i = 0; i < c->cvar_count; i++)
        if (c->cvars[i].used && strcmp(c->cvars[i].name, name) == 0) return &c->cvars[i];
    return NULL;
}
static Cvar* cvar_alloc(CCConsole* c, const char* name, CCCvarType t, const char* desc) {
    Cvar* v = cvar_find(c, name);
    if (v) return v;                 /* re-register updates in place */
    if (c->cvar_count >= CON_MAX_CVARS) return NULL;
    v = &c->cvars[c->cvar_count++];
    memset(v, 0, sizeof(*v));
    v->used = true; v->type = t;
    snprintf(v->name, sizeof(v->name), "%s", name);
    if (desc) snprintf(v->desc, sizeof(v->desc), "%s", desc);
    return v;
}

void cc_cvar_register_float(CCConsole* c, const char* n, float v, const char* d) {
    Cvar* cv = cvar_alloc(c, n, CC_CVAR_FLOAT, d); if (cv) cv->num = v;
}
void cc_cvar_register_int(CCConsole* c, const char* n, int v, const char* d) {
    Cvar* cv = cvar_alloc(c, n, CC_CVAR_INT, d); if (cv) cv->num = v;
}
void cc_cvar_register_bool(CCConsole* c, const char* n, bool v, const char* d) {
    Cvar* cv = cvar_alloc(c, n, CC_CVAR_BOOL, d); if (cv) cv->num = v ? 1 : 0;
}
void cc_cvar_register_string(CCConsole* c, const char* n, const char* v, const char* d) {
    Cvar* cv = cvar_alloc(c, n, CC_CVAR_STRING, d);
    if (cv) snprintf(cv->str, sizeof(cv->str), "%s", v ? v : "");
}
void cc_cvar_set_flags(CCConsole* c, const char* n, uint32_t flags) {
    Cvar* v = cvar_find(c, n); if (v) v->flags |= flags;
}

float cc_cvar_get_float(CCConsole* c, const char* n) {
    Cvar* v = cvar_find(c, n);
    if (!v) return 0.0f;
    if (v->type == CC_CVAR_STRING) return (float)atof(v->str);
    return (float)v->num;
}
int cc_cvar_get_int(CCConsole* c, const char* n) {
    Cvar* v = cvar_find(c, n);
    if (!v) return 0;
    if (v->type == CC_CVAR_STRING) return atoi(v->str);
    return (int)v->num;
}
bool cc_cvar_get_bool(CCConsole* c, const char* n) {
    Cvar* v = cvar_find(c, n);
    if (!v) return false;
    if (v->type == CC_CVAR_STRING) return v->str[0] && strcmp(v->str,"0") && strcmp(v->str,"false");
    return v->num != 0.0;
}
const char* cc_cvar_get_string(CCConsole* c, const char* n) {
    Cvar* v = cvar_find(c, n);
    return (v && v->type == CC_CVAR_STRING) ? v->str : "";
}

bool cc_cvar_set_float(CCConsole* c, const char* n, float x) {
    Cvar* v = cvar_find(c, n); if (!v) return false;
    if (v->type == CC_CVAR_STRING) snprintf(v->str, sizeof(v->str), "%g", x);
    else if (v->type == CC_CVAR_INT)  v->num = (double)(int)x;
    else if (v->type == CC_CVAR_BOOL) v->num = (x != 0.0f) ? 1 : 0;
    else v->num = x;
    return true;
}
bool cc_cvar_set_int(CCConsole* c, const char* n, int x)  { return cc_cvar_set_float(c, n, (float)x); }
bool cc_cvar_set_bool(CCConsole* c, const char* n, bool x){ return cc_cvar_set_float(c, n, x ? 1.0f : 0.0f); }
bool cc_cvar_set_string(CCConsole* c, const char* n, const char* s) {
    Cvar* v = cvar_find(c, n); if (!v) return false;
    if (v->type == CC_CVAR_STRING) snprintf(v->str, sizeof(v->str), "%s", s ? s : "");
    else v->num = s ? atof(s) : 0.0;
    return true;
}
bool cc_cvar_exists(CCConsole* c, const char* n) { return cvar_find(c, n) != NULL; }

/* ── commands ────────────────────────────────────────────────────────────── */
static Command* cmd_find(CCConsole* c, const char* name) {
    for (uint32_t i = 0; i < c->cmd_count; i++)
        if (c->cmds[i].used && strcmp(c->cmds[i].name, name) == 0) return &c->cmds[i];
    return NULL;
}
void cc_command_register(CCConsole* c, const char* name, CCCommandFn fn, void* ud, const char* help) {
    if (!c || !name || !fn) return;
    Command* cmd = cmd_find(c, name);
    if (!cmd) {
        if (c->cmd_count >= CON_MAX_COMMANDS) return;
        cmd = &c->cmds[c->cmd_count++];
        memset(cmd, 0, sizeof(*cmd));
        cmd->used = true;
        snprintf(cmd->name, sizeof(cmd->name), "%s", name);
    }
    cmd->fn = fn; cmd->ud = ud;
    if (help) snprintf(cmd->help, sizeof(cmd->help), "%s", help);
}

/* ── set a cvar from a console token, honoring READONLY/CHEAT gating ──────── */
static void cvar_console_set(CCConsole* c, Cvar* v, const char* val) {
    if (v->flags & CC_CVAR_READONLY) { cc_console_print(c, "%s is read-only", v->name); return; }
    if ((v->flags & CC_CVAR_CHEAT) && !c->cheats) { cc_console_print(c, "%s requires cheats", v->name); return; }
    switch (v->type) {
        case CC_CVAR_STRING: snprintf(v->str, sizeof(v->str), "%s", val); break;
        case CC_CVAR_BOOL:
            v->num = (!strcmp(val,"1")||!strcmp(val,"true")||!strcmp(val,"on")) ? 1 : 0; break;
        case CC_CVAR_INT:   v->num = (double)atoi(val); break;
        case CC_CVAR_FLOAT: default: v->num = atof(val); break;
    }
}
static void cvar_print(CCConsole* c, Cvar* v) {
    switch (v->type) {
        case CC_CVAR_STRING: cc_console_print(c, "%s = \"%s\"", v->name, v->str); break;
        case CC_CVAR_BOOL:   cc_console_print(c, "%s = %s", v->name, v->num!=0?"true":"false"); break;
        case CC_CVAR_INT:    cc_console_print(c, "%s = %d", v->name, (int)v->num); break;
        case CC_CVAR_FLOAT: default: cc_console_print(c, "%s = %g", v->name, v->num); break;
    }
}

/* ── exec: tokenize one line, dispatch to command or cvar ─────────────────── */
bool cc_console_exec(CCConsole* c, const char* line) {
    if (!c || !line) return false;
    char buf[CON_LINE_LEN];
    snprintf(buf, sizeof(buf), "%s", line);

    /* tokenize on whitespace (simple; no quotes — string cvars take one token or
     * the remainder is joined below for set) */
    char* argv[CON_MAX_ARGS]; int argc = 0;
    char* p = buf;
    while (*p && argc < CON_MAX_ARGS) {
        while (*p==' '||*p=='\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p!=' ' && *p!='\t') p++;
        if (*p) *p++ = 0;
    }
    if (argc == 0) return false;

    /* command? */
    Command* cmd = cmd_find(c, argv[0]);
    if (cmd) {
        cmd->fn(c, argc, argv, cmd->ud);
        return true;
    }
    /* cvar? */
    Cvar* v = cvar_find(c, argv[0]);
    if (v) {
        if (argc == 1) { cvar_print(c, v); }
        else {
            /* for string cvars, join the rest of the line so values with spaces
             * work; for numeric, argv[1] is enough */
            if (v->type == CC_CVAR_STRING) {
                char joined[CON_STR_LEN]; size_t o = 0;
                for (int i = 1; i < argc && o < sizeof(joined)-1; i++)
                    o += snprintf(joined+o, sizeof(joined)-o, "%s%s", i>1?" ":"", argv[i]);
                cvar_console_set(c, v, joined);
            } else {
                cvar_console_set(c, v, argv[1]);
            }
        }
        return true;
    }
    cc_console_print(c, "unknown command or cvar: %s", argv[0]);
    return false;
}

void cc_console_set_cheats(CCConsole* c, bool e) { if (c) c->cheats = e; }
bool cc_console_cheats(const CCConsole* c) { return c ? c->cheats : false; }

/* ── config files ────────────────────────────────────────────────────────── */
int cc_console_exec_file(CCConsole* c, const char* path) {
    if (!c || !path) return 0;
    FILE* f = fopen(path, "r");
    if (!f) { cc_console_print(c, "cannot open %s", path); return 0; }
    char line[CON_LINE_LEN];
    int run = 0;
    while (fgets(line, sizeof(line), f)) {
        /* strip trailing newline + comments (# or //) */
        char* nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        char* hash = strchr(line, '#'); if (hash) *hash = 0;
        char* sl = strstr(line, "//"); if (sl) *sl = 0;
        /* skip blank */
        char* s = line; while (*s==' '||*s=='\t') s++;
        if (!*s) continue;
        cc_console_exec(c, s);
        run++;
    }
    fclose(f);
    return run;
}
int cc_console_save_cvars(CCConsole* c, const char* path) {
    if (!c || !path) return -1;
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    int n = 0;
    for (uint32_t i = 0; i < c->cvar_count; i++) {
        Cvar* v = &c->cvars[i];
        if (!v->used || (v->flags & CC_CVAR_READONLY)) continue;
        switch (v->type) {
            case CC_CVAR_STRING: fprintf(f, "%s %s\n", v->name, v->str); break;
            case CC_CVAR_BOOL:   fprintf(f, "%s %d\n", v->name, v->num!=0?1:0); break;
            case CC_CVAR_INT:    fprintf(f, "%s %d\n", v->name, (int)v->num); break;
            case CC_CVAR_FLOAT: default: fprintf(f, "%s %g\n", v->name, v->num); break;
        }
        n++;
    }
    fclose(f);
    return n;
}

/* ── introspection ───────────────────────────────────────────────────────── */
uint32_t cc_console_cvar_count(const CCConsole* c) { return c ? c->cvar_count : 0; }
const char* cc_console_cvar_name(const CCConsole* c, uint32_t i) {
    if (!c || i >= c->cvar_count || !c->cvars[i].used) return NULL;
    return c->cvars[i].name;
}
uint32_t cc_console_command_count(const CCConsole* c) { return c ? c->cmd_count : 0; }
const char* cc_console_command_name(const CCConsole* c, uint32_t i) {
    if (!c || i >= c->cmd_count || !c->cmds[i].used) return NULL;
    return c->cmds[i].name;
}
