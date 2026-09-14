/*
 * scripting_py.c — Chlorlite embedded Python scripting bridge.
 *
 * Implements cc_python_init / cc_python_exec / cc_python_exec_file /
 * cc_python_call from cc/scripting.h using a real embedded CPython interpreter.
 * Compiled only when CC_SCRIPTING_PYTHON is defined (link -lpython3.x); when it
 * isn't, this translation unit is empty so the engine builds without Python.
 *
 * A `cc` module is injected into the interpreter so scripts can talk back to the
 * engine without any per-game glue:
 *     import cc
 *     cc.log("hello from python")           # → engine log
 *     t = cc.time()                          # → seconds since start (float)
 *     cc.emit(channel, i, f)                 # → publish on the attached event bus
 *     cc.set_var("score", 10); cc.get_var("score")   # shared K/V with C
 *
 * Design: one interpreter per process (the standard embedding model). The active
 * CCEngine* and an optional CCEventBus* are held in file-static state so the
 * `cc` module methods can reach them; cc_python_bind_bus() wires the bus. All
 * entry points are no-ops returning false if the interpreter failed to init, so
 * callers never crash on a Python-less build or a broken script.
 */
#include "cc/scripting.h"

#ifdef CC_SCRIPTING_PYTHON

#include "cc/claudecore.h"
#include "cc/event.h"
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>
#include <string.h>

/* ── engine-side state reachable from the cc module ──────────────────────── */
static CCEngine*   g_py_engine = NULL;
static CCEventBus* g_py_bus    = NULL;
static bool        g_py_ready  = false;

/* a tiny shared string→double var store so C and Python can exchange values
 * without a full binding layer (handy for "score", tuning knobs, flags). */
#define PY_MAX_VARS 128
static struct { char key[64]; double val; bool used; } g_vars[PY_MAX_VARS];

static double* var_slot(const char* key, bool create) {
    for (int i = 0; i < PY_MAX_VARS; i++)
        if (g_vars[i].used && strcmp(g_vars[i].key, key) == 0) return &g_vars[i].val;
    if (!create) return NULL;
    for (int i = 0; i < PY_MAX_VARS; i++)
        if (!g_vars[i].used) {
            g_vars[i].used = true;
            snprintf(g_vars[i].key, sizeof(g_vars[i].key), "%s", key);
            g_vars[i].val = 0.0;
            return &g_vars[i].val;
        }
    return NULL;
}

/* ── the `cc` module methods ─────────────────────────────────────────────── */
static PyObject* py_cc_log(PyObject* self, PyObject* args) {
    (void)self;
    const char* msg;
    if (!PyArg_ParseTuple(args, "s", &msg)) return NULL;
    cc_log(CC_LOG_INFO, "[py] %s", msg);
    Py_RETURN_NONE;
}
static PyObject* py_cc_time(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    double t = g_py_engine ? cc_time(g_py_engine) : 0.0;
    return PyFloat_FromDouble(t);
}
static PyObject* py_cc_emit(PyObject* self, PyObject* args) {
    (void)self;
    unsigned int channel; long long i = 0; double f = 0.0;
    /* cc.emit(channel, i=0, f=0.0) */
    if (!PyArg_ParseTuple(args, "I|Ld", &channel, &i, &f)) return NULL;
    bool ok = false;
    if (g_py_bus) ok = cc_event_emit_if(g_py_bus, (uint32_t)channel, 0, (int64_t)i, (float)f);
    return PyBool_FromLong(ok ? 1 : 0);
}
static PyObject* py_cc_set_var(PyObject* self, PyObject* args) {
    (void)self;
    const char* key; double val;
    if (!PyArg_ParseTuple(args, "sd", &key, &val)) return NULL;
    double* slot = var_slot(key, true);
    if (slot) *slot = val;
    Py_RETURN_NONE;
}
static PyObject* py_cc_get_var(PyObject* self, PyObject* args) {
    (void)self;
    const char* key; double dflt = 0.0;
    if (!PyArg_ParseTuple(args, "s|d", &key, &dflt)) return NULL;
    double* slot = var_slot(key, false);
    return PyFloat_FromDouble(slot ? *slot : dflt);
}

static PyMethodDef cc_methods[] = {
    {"log",     py_cc_log,     METH_VARARGS, "cc.log(msg): write to the engine log"},
    {"time",    py_cc_time,    METH_NOARGS,  "cc.time() -> seconds since start"},
    {"emit",    py_cc_emit,    METH_VARARGS, "cc.emit(channel, i=0, f=0.0) -> bool: publish on the event bus"},
    {"set_var", py_cc_set_var, METH_VARARGS, "cc.set_var(key, value): store a shared float"},
    {"get_var", py_cc_get_var, METH_VARARGS, "cc.get_var(key, default=0.0) -> float"},
    {NULL, NULL, 0, NULL}
};
static PyModuleDef cc_module = {
    PyModuleDef_HEAD_INIT, "cc", "Chlorlite engine bridge", -1, cc_methods,
    NULL, NULL, NULL, NULL
};
static PyObject* PyInit_cc(void) { return PyModule_Create(&cc_module); }

/* C setter used by the engine to wire the event bus scripts publish on. Declared
 * in a private extern (see cc_python_bind_bus prototype below). */
void cc_python_bind_bus(CCEventBus* bus) { g_py_bus = bus; }

/* ── public API (cc/scripting.h) ─────────────────────────────────────────── */
bool cc_python_init(CCEngine* eng) {
    if (g_py_ready) { g_py_engine = eng; return true; }
    g_py_engine = eng;
    /* register the built-in `cc` module before Py_Initialize so `import cc` works */
    if (PyImport_AppendInittab("cc", PyInit_cc) != 0) {
        CC_ERROR("cc_python_init: failed to register cc module");
        return false;
    }
    Py_Initialize();
    if (!Py_IsInitialized()) {
        CC_ERROR("cc_python_init: Py_Initialize failed");
        return false;
    }
    g_py_ready = true;
    CC_INFO("cc_python_init: embedded Python %s ready", Py_GetVersion());
    return true;
}

bool cc_python_exec(CCEngine* eng, const char* code) {
    (void)eng;
    if (!g_py_ready || !code) return false;
    int r = PyRun_SimpleString(code);   /* 0 on success, -1 on (printed) error */
    if (r != 0) {
        PyErr_Clear();   /* clear the indicator so the next exec starts clean */
        CC_ERROR("cc_python_exec: script raised an exception");
        return false;
    }
    return true;
}

bool cc_python_exec_file(CCEngine* eng, const char* path) {
    (void)eng;
    if (!g_py_ready || !path) return false;
    FILE* f = fopen(path, "rb");
    if (!f) { CC_ERROR("cc_python_exec_file: cannot open %s", path); return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[rd] = 0;
    bool ok = cc_python_exec(eng, buf);
    free(buf);
    return ok;
}

/* Call module.fn(*argv) where each argv element is a NUL-terminated C string
 * passed as a Python str. Returns a heap-allocated C string of str(result) that
 * the caller must free(), or NULL on error. (Keeps the ABI void*-simple per the
 * header; string in/out covers the common "call a gameplay hook" case without a
 * full type-marshalling layer.) */
void* cc_python_call(CCEngine* eng, const char* module, const char* fn,
                     int argc, void** argv) {
    (void)eng;
    if (!g_py_ready || !module || !fn) return NULL;
    PyObject* mod = PyImport_ImportModule(module);
    if (!mod) { PyErr_Print(); PyErr_Clear(); return NULL; }
    PyObject* func = PyObject_GetAttrString(mod, fn);
    if (!func || !PyCallable_Check(func)) {
        PyErr_Clear();
        CC_ERROR("cc_python_call: %s.%s not callable", module, fn);
        Py_XDECREF(func); Py_DECREF(mod); return NULL;
    }
    PyObject* args = PyTuple_New(argc);
    for (int i = 0; i < argc; i++) {
        const char* s = argv ? (const char*)argv[i] : "";
        PyTuple_SetItem(args, i, PyUnicode_FromString(s ? s : ""));
    }
    PyObject* res = PyObject_CallObject(func, args);
    Py_DECREF(args); Py_DECREF(func); Py_DECREF(mod);
    if (!res) { PyErr_Print(); PyErr_Clear(); return NULL; }
    PyObject* str = PyObject_Str(res);
    Py_DECREF(res);
    if (!str) return NULL;
    const char* utf = PyUnicode_AsUTF8(str);
    char* out = utf ? strdup(utf) : NULL;
    Py_DECREF(str);
    return out;
}

/* ── hot-reload: watch .py files, re-exec on change ──────────────────────── */
#include <sys/stat.h>
#define PY_MAX_WATCH 64
static struct { char path[512]; long mtime; bool used; } g_watch[PY_MAX_WATCH];

static long file_mtime(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_mtime;
}

bool cc_python_watch(CCEngine* eng, const char* path) {
    (void)eng;
    if (!g_py_ready || !path) return false;
    /* already watched? update nothing, just succeed */
    for (int i = 0; i < PY_MAX_WATCH; i++)
        if (g_watch[i].used && strcmp(g_watch[i].path, path) == 0) return true;
    for (int i = 0; i < PY_MAX_WATCH; i++) {
        if (!g_watch[i].used) {
            g_watch[i].used = true;
            snprintf(g_watch[i].path, sizeof(g_watch[i].path), "%s", path);
            g_watch[i].mtime = file_mtime(path);   /* baseline; -1 if missing now */
            /* exec it once on registration so its definitions are live */
            cc_python_exec_file(eng, path);
            return true;
        }
    }
    CC_WARN("cc_python_watch: watch table full (max %d)", PY_MAX_WATCH);
    return false;
}

int cc_python_poll_reloads(CCEngine* eng) {
    if (!g_py_ready) return 0;
    int reloaded = 0;
    for (int i = 0; i < PY_MAX_WATCH; i++) {
        if (!g_watch[i].used) continue;
        long m = file_mtime(g_watch[i].path);
        if (m < 0) continue;                 /* file vanished; keep last-good defs */
        if (m != g_watch[i].mtime) {
            g_watch[i].mtime = m;
            if (cc_python_exec_file(eng, g_watch[i].path)) {
                reloaded++;
                CC_INFO("cc_python: reloaded %s", g_watch[i].path);
            }
            /* on failure the file stays watched; next good save reloads it */
        }
    }
    return reloaded;
}

int cc_python_reload_all(CCEngine* eng) {
    if (!g_py_ready) return 0;
    int n = 0;
    for (int i = 0; i < PY_MAX_WATCH; i++) {
        if (!g_watch[i].used) continue;
        g_watch[i].mtime = file_mtime(g_watch[i].path);
        if (cc_python_exec_file(eng, g_watch[i].path)) n++;
    }
    return n;
}

void cc_python_shutdown(void) {
    if (g_py_ready) { Py_Finalize(); g_py_ready = false; }
    g_py_engine = NULL; g_py_bus = NULL;
    memset(g_watch, 0, sizeof(g_watch));
}

#endif /* CC_SCRIPTING_PYTHON */
