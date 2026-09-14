/* python_test — exercises the embedded Python scripting bridge (cc/scripting.h)
 * and the injected `cc` module. Built only with CC_PY=1 (-DCC_SCRIPTING_PYTHON).
 * Pure logic; prints "PYTHON TEST: all checks passed" / returns 0, else aborts. */
#include "cc/scripting.h"
#include "cc/event.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef CC_SCRIPTING_PYTHON
int main(void){ printf("PYTHON TEST: skipped (built without CC_SCRIPTING_PYTHON)\n"); return 0; }
#else

typedef struct CCEngine CCEngine;   /* opaque; the bridge doesn't deref it here */

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* listen for events Python emits on the bus */
static int   g_evt_hits = 0;
static int64_t g_evt_i = 0;
static float   g_evt_f = 0;
static void on_evt(const CCEvent* e, void* u){ (void)u; g_evt_hits++; g_evt_i=e->i; g_evt_f=e->f; }

int main(void) {
    /* 1. init the interpreter (no real engine needed — bridge tolerates NULL) -- */
    bool ok = cc_python_init(NULL);
    CHECK(ok, "cc_python_init succeeded");
    if (!ok) { printf("PYTHON TEST: init failed\n"); return 1; }

    /* 2. exec simple code -------------------------------------------------- */
    CHECK(cc_python_exec(NULL, "x = 2 + 3\n"), "exec basic statement");
    CHECK(cc_python_exec(NULL, "import cc\ncc.log('hello from embedded python')\n"),
          "import cc module + cc.log");

    /* 3. syntax / runtime errors are reported as false, not a crash -------- */
    CHECK(!cc_python_exec(NULL, "this is not valid python $$$"), "syntax error → false");
    CHECK(!cc_python_exec(NULL, "raise ValueError('boom')"), "runtime exception → false");

    /* 4. shared var store: Python sets, C reads (and vice versa) ----------- */
    CHECK(cc_python_exec(NULL, "import cc\ncc.set_var('score', 42.0)\n"), "python set_var");
    /* read it back through a python expression that logs it, and via a call below */
    CHECK(cc_python_exec(NULL,
        "import cc\n"
        "assert abs(cc.get_var('score') - 42.0) < 1e-6, 'score mismatch'\n"),
        "python get_var round-trips its own set_var");

    /* 5. cc.emit publishes on a bound event bus --------------------------- */
    CCEventBus* bus = cc_event_bus_create();
    cc_event_subscribe(bus, 1234, on_evt, NULL);
    cc_python_bind_bus(bus);
    CHECK(cc_python_exec(NULL, "import cc\nok = cc.emit(1234, 7, 1.5)\nassert ok\n"),
          "cc.emit returns True on a bound bus");
    cc_event_bus_update(bus);   /* deliver */
    CHECK(g_evt_hits == 1, "C listener received the python-emitted event");
    CHECK(g_evt_i == 7 && g_evt_f == 1.5f, "python-emitted event carried i=7, f=1.5");

    /* 6. cc.time() is callable and returns a float ------------------------ */
    CHECK(cc_python_exec(NULL, "import cc\nt = cc.time()\nassert isinstance(t, float)\n"),
          "cc.time() returns a float");

    /* 7. cc_python_call: define a module fn, call it, get str(result) ------ */
    CHECK(cc_python_exec(NULL,
        "import sys, types\n"
        "m = types.ModuleType('gamehooks')\n"
        "def greet(name):\n"
        "    return 'hi ' + name\n"
        "m.greet = greet\n"
        "sys.modules['gamehooks'] = m\n"),
        "defined a callable module fn");
    void* argv[1]; argv[0] = (void*)"claude";
    char* res = (char*)cc_python_call(NULL, "gamehooks", "greet", 1, argv);
    CHECK(res != NULL, "cc_python_call returned a result");
    CHECK(res && strcmp(res, "hi claude") == 0, "cc_python_call passed the arg and returned str(result)");
    free(res);

    /* calling a missing fn returns NULL, no crash */
    void* bad = cc_python_call(NULL, "gamehooks", "does_not_exist", 0, NULL);
    CHECK(bad == NULL, "calling a missing fn returns NULL");

    /* 8. exec a script FILE ----------------------------------------------- */
    const char* pth = "/tmp/cc_py_test_script.py";
    FILE* f = fopen(pth, "wb");
    if (f) { fputs("import cc\ncc.set_var('from_file', 99.0)\n", f); fclose(f); }
    CHECK(cc_python_exec_file(NULL, pth), "exec_file ran the script");
    CHECK(cc_python_exec(NULL, "import cc\nassert cc.get_var('from_file') == 99.0\n"),
          "script file's side effect is visible");
    CHECK(!cc_python_exec_file(NULL, "/tmp/does_not_exist_py_xyz.py"),
          "exec_file on a missing file → false");

    cc_event_bus_destroy(bus);
    cc_python_shutdown();

    if (failures == 0) {
        printf("PYTHON TEST: all checks passed (init, exec, error handling, cc module "
               "log/time/emit/vars, cc_python_call, exec_file)\n");
        return 0;
    }
    printf("PYTHON TEST: %d check(s) FAILED\n", failures);
    return 1;
}
#endif
