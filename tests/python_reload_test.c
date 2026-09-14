/* python_reload_test — verifies .py hot-reload: watch a script, change it on
 * disk, poll, and confirm the new definitions are live. Built only with CC_PY=1.
 * Pure logic; prints "PYTHON RELOAD TEST: all checks passed" / returns 0. */
#include "cc/scripting.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CC_SCRIPTING_PYTHON
int main(void){ printf("PYTHON RELOAD TEST: skipped (built without CC_SCRIPTING_PYTHON)\n"); return 0; }
#else
#include <sys/stat.h>
#include <utime.h>

typedef struct CCEngine CCEngine;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

static void write_file(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}
/* bump a file's mtime forward so poll detects the change without a real sleep */
static void bump_mtime(const char* path, long secs) {
    struct stat st; if (stat(path, &st) != 0) return;
    struct utimbuf ut; ut.actime = st.st_atime; ut.modtime = st.st_mtime + secs;
    utime(path, &ut);
}

int main(void) {
    CHECK(cc_python_init(NULL), "python init");
    const char* path = "/tmp/cc_hot.py";

    /* v1: a script that stores a tuning value in the shared var store */
    write_file(path, "import cc\ncc.set_var('speed', 1.0)\n");
    CHECK(cc_python_watch(NULL, path), "watch registers + execs the script once");
    CHECK(cc_python_exec(NULL, "import cc\nassert cc.get_var('speed')==1.0\n"),
          "v1 value is live after watch");

    /* no change yet → poll reloads nothing */
    CHECK(cc_python_poll_reloads(NULL) == 0, "poll with no change reloads 0");

    /* edit the script on disk (v2) and bump its mtime */
    write_file(path, "import cc\ncc.set_var('speed', 5.0)\n");
    bump_mtime(path, 5);
    int n = cc_python_poll_reloads(NULL);
    CHECK(n == 1, "poll detects the edit and reloads 1 file");
    CHECK(cc_python_exec(NULL, "import cc\nassert cc.get_var('speed')==5.0\n"),
          "v2 value is live after hot-reload");

    /* a second poll with no further change → 0 */
    CHECK(cc_python_poll_reloads(NULL) == 0, "poll after reload reloads 0 (mtime updated)");

    /* reloading a FUNCTION definition updates behavior in place -------------- */
    write_file(path,
        "def damage(x):\n"
        "    return int(x) * 2\n");
    bump_mtime(path, 10);
    cc_python_poll_reloads(NULL);
    /* the script ran at module scope (__main__); its damage() is now defined there.
       Re-exec uses PyRun_SimpleString which runs in __main__, so call it there. */
    CHECK(cc_python_exec(NULL, "assert damage(21) == 42\n"),
          "reloaded function definition is callable with new behavior");

    write_file(path,
        "def damage(x):\n"
        "    return int(x) + 100\n");   /* changed behavior */
    bump_mtime(path, 15);
    int r = cc_python_poll_reloads(NULL);
    CHECK(r == 1, "second edit reloads");
    CHECK(cc_python_exec(NULL, "assert damage(21) == 121\n"),
          "function behavior updated live after edit");

    /* force reload-all re-execs regardless of mtime ------------------------- */
    CHECK(cc_python_reload_all(NULL) >= 1, "reload_all re-execs watched files");

    /* a vanished file doesn't crash poll ----------------------------------- */
    remove(path);
    CHECK(cc_python_poll_reloads(NULL) == 0, "missing watched file → poll reloads 0, no crash");

    cc_python_shutdown();

    if (failures == 0) {
        printf("PYTHON RELOAD TEST: all checks passed (watch+exec, mtime change detection, "
               "live value + function reload, reload_all, missing-file safety)\n");
        return 0;
    }
    printf("PYTHON RELOAD TEST: %d check(s) FAILED\n", failures);
    return 1;
}
#endif
