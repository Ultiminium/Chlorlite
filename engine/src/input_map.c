/*
 * input_map.c — Chlorlite action binding system implementation
 */
#include "cc/input_map.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct {
    char     name[CC_INPUT_ACTION_NAME_LEN];
    CCAction actions[CC_INPUT_MAX_ACTIONS];
    uint32_t action_count;
    CCAxis   axes[CC_INPUT_MAX_AXES];
    uint32_t axis_count;
} CCInputContext;

struct CCInputMap {
    QContext*       q;
    CCInputContext  contexts[CC_INPUT_MAX_CONTEXTS];
    uint32_t        context_count;
    uint32_t        active_context;
    /* key capture for rebinding UI */
    bool            capturing;
    QKey            captured_key;
};

/* ─── Helpers ────────────────────────────────────────────────────────── */

static CCInputContext* active_ctx(CCInputMap* im) {
    return &im->contexts[im->active_context];
}

static CCAction* find_action(CCInputContext* c, const char* name) {
    for (uint32_t i=0;i<c->action_count;i++)
        if (strcmp(c->actions[i].name, name)==0) return &c->actions[i];
    return NULL;
}

static CCAction* get_or_create_action(CCInputContext* c, const char* name) {
    CCAction* a = find_action(c, name);
    if (a) return a;
    if (c->action_count >= CC_INPUT_MAX_ACTIONS) return NULL;
    a = &c->actions[c->action_count++];
    memset(a, 0, sizeof(*a));
    strncpy(a->name, name, CC_INPUT_ACTION_NAME_LEN-1);
    return a;
}

static CCAxis* find_axis(CCInputContext* c, const char* name) {
    for (uint32_t i=0;i<c->axis_count;i++)
        if (strcmp(c->axes[i].name, name)==0) return &c->axes[i];
    return NULL;
}

static CCAxis* get_or_create_axis(CCInputContext* c, const char* name) {
    CCAxis* a = find_axis(c, name);
    if (a) return a;
    if (c->axis_count >= CC_INPUT_MAX_AXES) return NULL;
    a = &c->axes[c->axis_count++];
    memset(a, 0, sizeof(*a));
    strncpy(a->name, name, CC_INPUT_ACTION_NAME_LEN-1);
    a->gamepad_axis = -1;
    return a;
}

static bool binding_is_down(CCInputMap* im, const CCBinding* b) {
    switch (b->source) {
        case CC_SRC_KEY:            return qwerty_key_down(im->q, (QKey)b->code);
        case CC_SRC_MOUSE_BUTTON:   return qwerty_mouse_down(im->q, (QMouseButton)b->code);
        default:                    return false;
    }
}

/* ─── Lifecycle ──────────────────────────────────────────────────────── */

CCInputMap* cc_input_map_create(QContext* q) {
    CCInputMap* im = calloc(1, sizeof(CCInputMap));
    im->q = q;
    /* default context */
    strncpy(im->contexts[0].name, "default", CC_INPUT_ACTION_NAME_LEN-1);
    im->context_count = 1;
    im->active_context = 0;
    return im;
}

void cc_input_map_destroy(CCInputMap* im) { free(im); }

void cc_input_map_update(CCInputMap* im, float dt) {
    /* Drain the Qwerty event queue so callbacks/char input flow, and so the
       poll ring doesn't overflow. Instant-state queries read live state. */
    QEvent evs[256];
    uint32_t n;
    do {
        n = qwerty_poll(im->q, evs, 256);
        if (im->capturing) {
            for (uint32_t i=0;i<n;i++) {
                if (evs[i].type == QEVENT_KEY_DOWN && evs[i].key.key != QKEY_NONE) {
                    im->captured_key = evs[i].key.key;
                    im->capturing = false;
                    break;
                }
            }
        }
    } while (n == 256);

    CCInputContext* c = active_ctx(im);

    /* Advance action edge state */
    for (uint32_t i=0;i<c->action_count;i++) {
        CCAction* a = &c->actions[i];
        a->held_prev = a->held;
        bool down = false;
        for (uint32_t b=0;b<a->binding_count;b++)
            if (binding_is_down(im, &a->bindings[b])) { down = true; break; }
        a->held = down;
    }

    /* Update axes */
    for (uint32_t i=0;i<c->axis_count;i++) {
        CCAxis* ax = &c->axes[i];
        float v = 0.0f;
        for (uint32_t b=0;b<ax->positive_count;b++)
            if (binding_is_down(im, &ax->positive[b])) { v += 1.0f; break; }
        for (uint32_t b=0;b<ax->negative_count;b++)
            if (binding_is_down(im, &ax->negative[b])) { v -= 1.0f; break; }
        ax->value = v;
        if (ax->smoothing > 0.0001f) {
            float alpha = 1.0f - expf(-dt / ax->smoothing);
            ax->smoothed += (v - ax->smoothed) * alpha;
        } else {
            ax->smoothed = v;
        }
    }
}

/* ─── Action binding ─────────────────────────────────────────────────── */

void cc_input_bind_action(CCInputMap* im, const char* action, QKey key) {
    CCAction* a = get_or_create_action(active_ctx(im), action);
    if (!a || a->binding_count >= CC_INPUT_MAX_BINDINGS) return;
    a->bindings[a->binding_count++] = (CCBinding){ CC_SRC_KEY, (uint32_t)key };
}

void cc_input_bind_action_mouse(CCInputMap* im, const char* action, QMouseButton btn) {
    CCAction* a = get_or_create_action(active_ctx(im), action);
    if (!a || a->binding_count >= CC_INPUT_MAX_BINDINGS) return;
    a->bindings[a->binding_count++] = (CCBinding){ CC_SRC_MOUSE_BUTTON, (uint32_t)btn };
}

void cc_input_unbind_action(CCInputMap* im, const char* action) {
    CCAction* a = find_action(active_ctx(im), action);
    if (a) a->binding_count = 0;
}

void cc_input_rebind_action(CCInputMap* im, const char* action, uint32_t idx, QKey new_key) {
    CCAction* a = find_action(active_ctx(im), action);
    if (!a) return;
    if (idx >= a->binding_count) {
        if (a->binding_count < CC_INPUT_MAX_BINDINGS) idx = a->binding_count++;
        else return;
    }
    a->bindings[idx] = (CCBinding){ CC_SRC_KEY, (uint32_t)new_key };
}

/* ─── Action query ───────────────────────────────────────────────────── */

bool cc_input_action_held(CCInputMap* im, const char* action) {
    CCAction* a = find_action(active_ctx(im), action);
    return a ? a->held : false;
}
bool cc_input_action_pressed(CCInputMap* im, const char* action) {
    CCAction* a = find_action(active_ctx(im), action);
    return a ? (a->held && !a->held_prev) : false;
}
bool cc_input_action_released(CCInputMap* im, const char* action) {
    CCAction* a = find_action(active_ctx(im), action);
    return a ? (!a->held && a->held_prev) : false;
}

/* ─── Axis ───────────────────────────────────────────────────────────── */

void cc_input_bind_axis(CCInputMap* im, const char* axis, QKey pos, QKey neg) {
    CCAxis* a = get_or_create_axis(active_ctx(im), axis);
    if (!a) return;
    if (a->positive_count < CC_INPUT_MAX_BINDINGS)
        a->positive[a->positive_count++] = (CCBinding){ CC_SRC_KEY, (uint32_t)pos };
    if (a->negative_count < CC_INPUT_MAX_BINDINGS)
        a->negative[a->negative_count++] = (CCBinding){ CC_SRC_KEY, (uint32_t)neg };
}

void cc_input_axis_smoothing(CCInputMap* im, const char* axis, float smoothing) {
    CCAxis* a = find_axis(active_ctx(im), axis);
    if (a) a->smoothing = smoothing;
}

float cc_input_axis(CCInputMap* im, const char* axis) {
    CCAxis* a = find_axis(active_ctx(im), axis);
    return a ? a->smoothed : 0.0f;
}
float cc_input_axis_raw(CCInputMap* im, const char* axis) {
    CCAxis* a = find_axis(active_ctx(im), axis);
    return a ? a->value : 0.0f;
}

/* ─── Contexts ───────────────────────────────────────────────────────── */

uint32_t cc_input_context_create(CCInputMap* im, const char* name) {
    if (im->context_count >= CC_INPUT_MAX_CONTEXTS) return 0;
    uint32_t idx = im->context_count++;
    memset(&im->contexts[idx], 0, sizeof(CCInputContext));
    strncpy(im->contexts[idx].name, name, CC_INPUT_ACTION_NAME_LEN-1);
    return idx;
}

void cc_input_context_activate(CCInputMap* im, const char* name) {
    for (uint32_t i=0;i<im->context_count;i++)
        if (strcmp(im->contexts[i].name, name)==0) { im->active_context = i; return; }
}

const char* cc_input_active_context(CCInputMap* im) {
    return im->contexts[im->active_context].name;
}

/* ─── Introspection ──────────────────────────────────────────────────── */

uint32_t cc_input_action_count(CCInputMap* im) {
    return active_ctx(im)->action_count;
}
const char* cc_input_action_name(CCInputMap* im, uint32_t idx) {
    CCInputContext* c = active_ctx(im);
    return idx < c->action_count ? c->actions[idx].name : "";
}
const char* cc_input_action_binding_str(CCInputMap* im, const char* action) {
    static char buf[128];
    buf[0] = 0;
    CCAction* a = find_action(active_ctx(im), action);
    if (!a) return buf;
    for (uint32_t i=0;i<a->binding_count;i++) {
        if (i) strncat(buf, ", ", sizeof(buf)-strlen(buf)-1);
        if (a->bindings[i].source == CC_SRC_KEY)
            strncat(buf, qwerty_key_name((QKey)a->bindings[i].code), sizeof(buf)-strlen(buf)-1);
        else if (a->bindings[i].source == CC_SRC_MOUSE_BUTTON) {
            char m[16]; snprintf(m,sizeof(m),"Mouse%u",a->bindings[i].code);
            strncat(buf, m, sizeof(buf)-strlen(buf)-1);
        }
    }
    return buf;
}

void cc_input_begin_capture(CCInputMap* im) {
    im->capturing = true;
    im->captured_key = QKEY_NONE;
}
QKey cc_input_capture_key(CCInputMap* im) {
    QKey k = im->captured_key;
    im->captured_key = QKEY_NONE;
    return k;
}

/* ─── Save / load ────────────────────────────────────────────────────── */

bool cc_input_save_bindings(CCInputMap* im, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "# Chlorlite input bindings\n");
    for (uint32_t ci=0; ci<im->context_count; ci++) {
        CCInputContext* c = &im->contexts[ci];
        fprintf(f, "[context:%s]\n", c->name);
        for (uint32_t i=0;i<c->action_count;i++) {
            CCAction* a = &c->actions[i];
            for (uint32_t b=0;b<a->binding_count;b++)
                fprintf(f, "action %s %d %u\n", a->name, a->bindings[b].source, a->bindings[b].code);
        }
        for (uint32_t i=0;i<c->axis_count;i++) {
            CCAxis* ax = &c->axes[i];
            for (uint32_t b=0;b<ax->positive_count;b++)
                fprintf(f, "axis+ %s %u\n", ax->name, ax->positive[b].code);
            for (uint32_t b=0;b<ax->negative_count;b++)
                fprintf(f, "axis- %s %u\n", ax->name, ax->negative[b].code);
        }
    }
    fclose(f);
    return true;
}

bool cc_input_load_bindings(CCInputMap* im, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[256];
    uint32_t cur_ctx = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#' || line[0]=='\n') continue;
        if (strncmp(line, "[context:", 9)==0) {
            char nm[CC_INPUT_ACTION_NAME_LEN];
            if (sscanf(line, "[context:%47[^]]", nm)==1) {
                cur_ctx = 0;
                for (uint32_t i=0;i<im->context_count;i++)
                    if (strcmp(im->contexts[i].name,nm)==0){cur_ctx=i;break;}
            }
            continue;
        }
        char kind[16], name[CC_INPUT_ACTION_NAME_LEN];
        int src; unsigned code;
        if (sscanf(line, "action %47s %d %u", name, &src, &code)==3) {
            CCAction* a = get_or_create_action(&im->contexts[cur_ctx], name);
            if (a && a->binding_count<CC_INPUT_MAX_BINDINGS)
                a->bindings[a->binding_count++] = (CCBinding){ (CCInputSource)src, code };
        } else if (sscanf(line, "axis%15s %47s %u", kind, name, &code)==3) {
            /* handled loosely; kind holds "+ name" split — simplified */
        }
    }
    fclose(f);
    return true;
}
