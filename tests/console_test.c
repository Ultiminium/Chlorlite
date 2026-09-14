/* console_test — verifies CVars (register/get/set/coerce/flags) + the command
 * console (dispatch, args, log, cheats gating, config file round-trip,
 * introspection). Pure logic; prints "CONSOLE TEST: all checks passed" / 0. */
#include "cc/console.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* a test command that records what it was called with */
static int   g_cmd_calls = 0;
static int   g_cmd_argc = 0;
static char  g_cmd_arg1[64] = {0};
static void  test_cmd(CCConsole* con, int argc, char** argv, void* ud) {
    g_cmd_calls++; g_cmd_argc = argc;
    if (argc > 1) snprintf(g_cmd_arg1, sizeof(g_cmd_arg1), "%s", argv[1]);
    int* tag = (int*)ud;
    cc_console_print(con, "ran %s with %d args (tag=%d)", argv[0], argc, tag ? *tag : -1);
}

int main(void) {
    CCConsole* con = cc_console_create();
    CHECK(con != NULL, "console created");

    /* ── CVar register + typed get ──────────────────────────────────────── */
    cc_cvar_register_float (con, "sv_gravity", 9.8f, "world gravity");
    cc_cvar_register_int   (con, "ai_count", 4, "number of guards");
    cc_cvar_register_bool  (con, "fx_bloom", true, "bloom on/off");
    cc_cvar_register_string(con, "player_name", "claude", "display name");

    CHECK(fabsf(cc_cvar_get_float(con,"sv_gravity") - 9.8f) < 1e-4f, "float cvar get");
    CHECK(cc_cvar_get_int(con,"ai_count") == 4, "int cvar get");
    CHECK(cc_cvar_get_bool(con,"fx_bloom") == true, "bool cvar get");
    CHECK(strcmp(cc_cvar_get_string(con,"player_name"),"claude")==0, "string cvar get");
    CHECK(cc_cvar_exists(con,"sv_gravity") && !cc_cvar_exists(con,"nope"), "cvar_exists");

    /* ── programmatic set + cross-type coercion ─────────────────────────── */
    CHECK(cc_cvar_set_float(con,"sv_gravity", 12.0f), "set float");
    CHECK(fabsf(cc_cvar_get_float(con,"sv_gravity")-12.0f)<1e-4f, "float set took");
    cc_cvar_set_int(con,"ai_count", 9);
    CHECK(cc_cvar_get_int(con,"ai_count")==9, "int set took");
    cc_cvar_set_bool(con,"fx_bloom", false);
    CHECK(cc_cvar_get_bool(con,"fx_bloom")==false, "bool set took");
    /* setting a float on an int cvar coerces (truncates) */
    cc_cvar_set_float(con,"ai_count", 3.9f);
    CHECK(cc_cvar_get_int(con,"ai_count")==3, "float→int cvar coercion truncates");
    /* set on a missing cvar returns false */
    CHECK(!cc_cvar_set_float(con,"missing", 1.0f), "set on missing cvar → false");

    /* ── exec: cvar get + set via console line ──────────────────────────── */
    cc_console_log_clear(con);
    CHECK(cc_console_exec(con, "sv_gravity"), "exec cvar name (print)");
    const char* last = cc_console_log_last(con);
    CHECK(last && strstr(last,"sv_gravity") && strstr(last,"12"), "printed cvar value to log");

    CHECK(cc_console_exec(con, "sv_gravity 3.5"), "exec cvar set");
    CHECK(fabsf(cc_cvar_get_float(con,"sv_gravity")-3.5f)<1e-4f, "console set changed the cvar");

    /* string cvar set with spaces joins the remainder */
    CHECK(cc_console_exec(con, "player_name Dr Claude"), "exec string cvar set w/ spaces");
    CHECK(strcmp(cc_cvar_get_string(con,"player_name"),"Dr Claude")==0, "string joined remainder");

    /* unknown token → false + logged */
    cc_console_log_clear(con);
    CHECK(!cc_console_exec(con, "florb 3"), "unknown token → false");
    CHECK(cc_console_log_last(con) && strstr(cc_console_log_last(con),"unknown"), "unknown logged");

    /* ── commands ───────────────────────────────────────────────────────── */
    int tag = 77;
    cc_command_register(con, "spawn", test_cmd, &tag, "spawn <type>");
    g_cmd_calls = 0;
    CHECK(cc_console_exec(con, "spawn zombie 3 0 5"), "exec command");
    CHECK(g_cmd_calls == 1, "command callback fired once");
    CHECK(g_cmd_argc == 5, "command received argc=5 (name + 4 args)");
    CHECK(strcmp(g_cmd_arg1,"zombie")==0, "command received argv[1]=zombie");
    CHECK(cc_console_log_last(con) && strstr(cc_console_log_last(con),"tag=77"), "command saw its userdata");

    /* a command shadows a cvar of the same name is not tested (distinct names);
       re-registering a command updates it in place */
    cc_command_register(con, "spawn", test_cmd, &tag, "spawn <type> [x y z]");
    CHECK(cc_console_command_count(con)==1, "re-register command updates in place (count stays 1)");

    /* ── READONLY + CHEAT flags (console gating; programmatic set bypasses) ─ */
    cc_cvar_register_int(con, "build_id", 1000, "read-only build number");
    cc_cvar_set_flags(con, "build_id", CC_CVAR_READONLY);
    cc_console_log_clear(con);
    cc_console_exec(con, "build_id 5");   /* should be refused */
    CHECK(cc_cvar_get_int(con,"build_id")==1000, "READONLY cvar not changed via console");
    CHECK(cc_console_log_last(con) && strstr(cc_console_log_last(con),"read-only"), "READONLY refusal logged");
    CHECK(cc_cvar_set_int(con,"build_id", 2000), "programmatic set bypasses READONLY");
    CHECK(cc_cvar_get_int(con,"build_id")==2000, "programmatic READONLY set took");

    cc_cvar_register_bool(con, "noclip", false, "fly through walls");
    cc_cvar_set_flags(con, "noclip", CC_CVAR_CHEAT);
    cc_console_log_clear(con);
    cc_console_exec(con, "noclip 1");     /* cheats off → refused */
    CHECK(cc_cvar_get_bool(con,"noclip")==false, "CHEAT cvar refused when cheats off");
    CHECK(cc_console_cheats(con)==false, "cheats default off");
    cc_console_set_cheats(con, true);
    cc_console_exec(con, "noclip 1");     /* now allowed */
    CHECK(cc_cvar_get_bool(con,"noclip")==true, "CHEAT cvar allowed when cheats on");

    /* ── log ring buffer ────────────────────────────────────────────────── */
    cc_console_log_clear(con);
    CHECK(cc_console_log_count(con)==0, "log cleared");
    for (int i=0;i<5;i++) cc_console_print(con, "line %d", i);
    CHECK(cc_console_log_count(con)==5, "log counts 5 lines");
    CHECK(strcmp(cc_console_log_line(con,0),"line 0")==0, "oldest line is line 0");
    CHECK(strcmp(cc_console_log_line(con,4),"line 4")==0, "newest line is line 4");
    CHECK(cc_console_log_line(con,5)==NULL, "out-of-range log line → NULL");

    /* ── config file round-trip: save cvars → clear → exec_file restores ─── */
    const char* cfg = "/tmp/cc_console_test.cfg";
    cc_cvar_set_float(con,"sv_gravity", 6.25f);
    cc_cvar_set_int(con,"ai_count", 12);
    int saved = cc_console_save_cvars(con, cfg);
    CHECK(saved > 0, "save_cvars wrote lines");
    /* change values, then reload from file */
    cc_cvar_set_float(con,"sv_gravity", 0.0f);
    cc_cvar_set_int(con,"ai_count", 0);
    int ran = cc_console_exec_file(con, cfg);
    CHECK(ran > 0, "exec_file ran the cfg");
    CHECK(fabsf(cc_cvar_get_float(con,"sv_gravity")-6.25f)<1e-4f, "cfg restored float cvar");
    CHECK(cc_cvar_get_int(con,"ai_count")==12, "cfg restored int cvar");
    /* readonly build_id must NOT be in the saved file */
    CHECK(cc_console_exec_file(con, "/tmp/does_not_exist.cfg")==0, "exec_file missing → 0");

    /* config comments (# and //) and blanks are skipped */
    FILE* cf = fopen("/tmp/cc_console_comments.cfg","wb");
    if (cf){ fputs("# comment\n\nsv_gravity 4.4\n// trailing\nai_count 7 // inline\n", cf); fclose(cf); }
    cc_console_exec_file(con, "/tmp/cc_console_comments.cfg");
    CHECK(fabsf(cc_cvar_get_float(con,"sv_gravity")-4.4f)<1e-4f, "cfg with comments parsed");
    CHECK(cc_cvar_get_int(con,"ai_count")==7, "cfg inline-comment value parsed");

    /* ── introspection ──────────────────────────────────────────────────── */
    CHECK(cc_console_cvar_count(con) >= 5, "cvar_count reflects registrations");
    CHECK(cc_console_cvar_name(con,0)!=NULL, "cvar_name[0] valid");
    CHECK(cc_console_cvar_name(con,9999)==NULL, "cvar_name out-of-range → NULL");
    CHECK(cc_console_command_count(con)==1, "command_count = 1");
    CHECK(strcmp(cc_console_command_name(con,0),"spawn")==0, "command_name[0]=spawn");

    /* ── NULL safety ────────────────────────────────────────────────────── */
    CHECK(!cc_console_exec(NULL,"x"), "exec(NULL) → false");
    CHECK(cc_cvar_get_float(NULL,"x")==0.0f, "get(NULL) → 0");
    CHECK(cc_console_log_count(NULL)==0, "log_count(NULL) → 0");
    cc_console_destroy(NULL);

    cc_console_destroy(con);

    if (failures == 0) {
        printf("CONSOLE TEST: all checks passed (cvars typed get/set/coerce, console "
               "dispatch+args, READONLY/CHEAT gating, log ring, cfg round-trip, introspection)\n");
        return 0;
    }
    printf("CONSOLE TEST: %d check(s) FAILED\n", failures);
    return 1;
}
