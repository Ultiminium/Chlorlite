#include "cc/claudecore.h"
#include "cc/ccmath.h"
#include "renderer_internal.h"
#include <qwerty/qwerty.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#ifdef _WIN32
  #include <direct.h>
  #define CC_MKDIR(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #define CC_MKDIR(p) mkdir((p), 0755)
#endif
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <sys/types.h>

struct CCEngine {
    CCEngineConfig  cfg;
    CCRenderer*     renderer;
    QContext*       qwerty;
    CCScene*        active_scene;

    uint64_t start_ns, last_frame_ns;
    double   delta;
    uint64_t frame_count;

    uint8_t prev_keys[QKEY_COUNT];
    uint8_t cur_keys[QKEY_COUNT];    /* this frame's key state, snapshotted ONCE per
                                        frame (sticky keys clear on read, so all
                                        reads must use this snapshot, not re-poll) */
    uint8_t pressed_latch[QKEY_COUNT]; /* set if key went down at all this frame */
    uint8_t prev_mouse[QMOUSE_COUNT];
    int32_t prev_mx, prev_my;
    int     mouse_init;
    float   scroll_dx, scroll_dy;

    /* gamepad state (index 0 = player 1) */
    struct {
        uint8_t connected;
        uint8_t btn[CC_GAMEPAD_BUTTON_COUNT];
        uint8_t prev_btn[CC_GAMEPAD_BUTTON_COUNT];
        float   axis[CC_GAMEPAD_AXIS_COUNT];
    } pads[CC_MAX_GAMEPADS];
    float gamepad_deadzone;

    bool     running, quit;
    CCCameraDesc stored_camera;
    void* audio_system;
    void* script_system;
    CCAAState aa;
};

/* Getter used by aa.c to reach the embedded AA state. */
CCAAState* cc_engine_aa_state(CCEngine* eng) { return eng ? &eng->aa : NULL; }

static uint64_t mono_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

CCEngine* cc_init(const CCEngineConfig* cfg) {
    CCEngine* e = calloc(1, sizeof(CCEngine));
    e->cfg = cfg ? *cfg : cc_default_config();
    e->gamepad_deadzone = 0.15f;
    cc_aa_state_init(&e->aa);
    if (!e->cfg.title)          e->cfg.title = "Chlorlite";
    if (!e->cfg.screenshot_dir) e->cfg.screenshot_dir = "/tmp/cc_screenshots";
    CC_MKDIR(e->cfg.screenshot_dir);

    e->start_ns = e->last_frame_ns = mono_ns();

    /* Input */
    QConfig qcfg = qwerty_default_config();
    qcfg.backend = e->cfg.headless ? QBACKEND_HEADLESS : QBACKEND_AUTO;
    e->qwerty = qwerty_init(&qcfg);
    if (!e->qwerty) { qcfg.backend = QBACKEND_HEADLESS; e->qwerty = qwerty_init(&qcfg); }

    /* Renderer */
    CCRendererBackend rb = e->cfg.renderer_backend;
    if (e->cfg.headless && rb == CC_RENDERER_AUTO) rb = CC_RENDERER_OSMESA;
    e->renderer = cc_renderer_create(rb, e->cfg.width, e->cfg.height,
                                     e->cfg.title, e->cfg.screenshot_dir);

    /* Route windowed key events into qwerty's event queue (event-driven input)
       instead of polling the whole keyboard every frame. No-op when headless. */
    extern void cc_renderer_wire_input(CCRenderer*, void*);
    if (e->renderer) cc_renderer_wire_input(e->renderer, e->qwerty);

    e->running = true;
    CC_INFO("Chlorlite %s | input=%s | renderer=%s",
            cc_version_string(),
            qwerty_backend_name(e->qwerty),
            e->renderer ? "OK" : "null");
    return e;
}

void cc_shutdown(CCEngine* e) {
    if (!e) return;
    cc_renderer_destroy(e->renderer);
    qwerty_shutdown(e->qwerty);
    free(e);
}

void cc_tick(CCEngine* e, double dt) {
    e->delta = dt; e->frame_count++;
    int windowed = cc_renderer_has_window(e->renderer);
    (void)windowed;
    /* NOTE: keyboard/mouse prev-state for edge detection (cc_key_pressed etc.) is
       snapshotted at END of frame via cc_input_end_frame(), NOT here. Snapshotting
       at the top of cc_tick was a bug: in the windowed run loop glfwPollEvents()
       already ran (in cc_renderer_poll_events) BEFORE cc_tick, so this captured the
       CURRENT frame's state → prev==now → cc_key_pressed never fired (only Down,
       which the game reads via level-triggered cc_key_down, worked). */
    /* Input events (keys/scroll/gamepad) are drained once per frame in
       cc_input_begin_frame — the single queue-drain point — not here. */
    extern void cc_script_tick(CCEngine*,double);
    cc_script_tick(e, dt);
    extern void cc_audio_update(CCEngine*, float);
    cc_audio_update(e, (float)dt);
    extern void cc_systems_run(CCScene*, CCEngine*, double);
    if (e->active_scene) cc_systems_run(e->active_scene, e, dt);
}

void cc_run(CCEngine* e) {
    extern bool cc_renderer_should_close(CCRenderer*);
    extern void cc_renderer_poll_events(CCRenderer*);

    /* Headless auto-drive: with no window to close, a plain cc_run() would spin
       forever and never capture anything. When running headless we advance a
       bounded number of frames and then snap a screenshot, so plugin-style games
       (cc_game_*) produce visible output with no bespoke loop. Overridable via
       env: CC_RUN_TICKS (frame count), CC_RUN_SCREENSHOT (output PNG path). */
    bool headless = e->cfg.headless;
    long auto_ticks = 0; const char* auto_shot = NULL;
    if (headless) {
        const char* t = getenv("CC_RUN_TICKS");
        auto_ticks = t ? strtol(t, NULL, 10) : 120;
        if (auto_ticks <= 0) auto_ticks = 120;
        auto_shot = getenv("CC_RUN_SCREENSHOT");
        if (!auto_shot || !*auto_shot) auto_shot = "/tmp/cc_screenshots/frame.png";
    }
    long tick = 0;
    int  split = (e->cfg.on_tick && e->cfg.on_render);   /* TPS/FPS accumulator mode */
    double fixed_dt = (e->cfg.tick_rate > 0.0) ? (1.0/e->cfg.tick_rate) : (1.0/60.0);
    double accumulator = 0.0;

    e->last_frame_ns = mono_ns();
    while (!e->quit) {
        /* window close request (windowed mode) */
        if (e->renderer && cc_renderer_should_close(e->renderer)) break;
        /* headless: stop after the bounded tick budget */
        if (headless && tick >= auto_ticks) break;

        uint64_t now = mono_ns();
        double dt;
        if (headless) {
            /* Deterministic fixed step: the headless loop runs as fast as the
               CPU allows, so wall-clock dt would be ~0 and nothing would animate.
               A fixed 1/60s step makes scripted captures reproducible. */
            dt = 1.0 / 60.0;
        } else {
            dt = (double)(now - e->last_frame_ns) * 1e-9;
            if (dt > 0.25) dt = 0.25;   /* clamp huge stalls (debugger, alt-tab) */
        }
        e->last_frame_ns = now;

        cc_renderer_poll_events(e->renderer);
        cc_input_begin_frame(e);               /* drain input once per rendered frame */

        if (split) {
            /* ── TPS loop: advance the simulation at a FIXED timestep. The
               accumulator banks real elapsed time and spends it in fixed_dt
               chunks — 0, 1, or several sim ticks per rendered frame — so the sim
               runs in real time and deterministically regardless of frame rate.
               A step cap prevents the "spiral of death" if a frame is very slow. */
            accumulator += dt;
            int steps = 0;
            while (accumulator >= fixed_dt) {
                cc_tick(e, fixed_dt);                          /* engine subsystems */
                e->cfg.on_tick(e, fixed_dt, e->cfg.userdata);  /* game simulation */
                accumulator -= fixed_dt;
                if (++steps >= 8) { accumulator = 0; break; }  /* clamp catch-up */
            }
            /* ── FPS loop: render ONCE, passing the interpolation fraction so the
               game can draw between the last two sim states for smooth motion. */
            double alpha = accumulator / fixed_dt;
            cc_frame_begin(e);
            e->cfg.on_render(e, alpha, e->cfg.userdata);
            cc_frame_end(e);
        } else {
            /* Legacy single model: logic + draw together, once per frame. */
            cc_tick(e, dt);
            cc_frame_begin(e);
            if (e->cfg.on_frame) e->cfg.on_frame(e, dt, e->cfg.userdata);
            cc_frame_end(e);
        }
        cc_input_end_frame(e);   /* roll input state for next frame's edges */

        /* optional frame cap */
        if (e->cfg.target_fps > 0.0) {
            double frame_s = 1.0 / e->cfg.target_fps;
            double elapsed = (double)(mono_ns() - now) * 1e-9;
            if (elapsed < frame_s) {
                struct timespec ts;
                double rem = frame_s - elapsed;
                ts.tv_sec = (time_t)rem;
                ts.tv_nsec = (long)((rem - ts.tv_sec) * 1e9);
                nanosleep(&ts, NULL);
            }
        }
        tick++;
    }

    /* Headless: capture the final frame so the run leaves visible output. */
    if (headless && auto_shot) {
        const char* saved = cc_screenshot(e, auto_shot);
        if (e->cfg.verbose) CC_INFO("headless run: %ld ticks → %s", tick, saved ? saved : "(null)");
    }
}

void cc_quit(CCEngine* e) { if(e) e->quit=true; }

uint32_t cc_demo_run(CCEngine* e, const char* out_dir,
                     uint32_t frames, double fixed_dt,
                     const CCInputEvent* timeline, uint32_t timeline_len) {
    if (!e || !out_dir) return 0;
    if (fixed_dt <= 0.0) fixed_dt = 1.0/60.0;
    if (!e->cfg.on_frame) {
        CC_INFO("cc_demo_run: cfg.on_frame is NULL — nothing to record");
        return 0;
    }
    char path[512];
    for (uint32_t f = 0; f < frames; f++) {
        /* deliver this frame's scripted input BEFORE begin_frame, exactly as the
           real loop (poll → begin_frame → tick → on_frame → end_frame). */
        for (uint32_t i = 0; i < timeline_len; i++)
            if (timeline && (uint32_t)timeline[i].frame == f)
                cc_input_inject_key(e, (QKey)timeline[i].key, timeline[i].down != 0);
        cc_input_begin_frame(e);
        cc_tick(e, fixed_dt);
        e->cfg.on_frame(e, fixed_dt, e->cfg.userdata);   /* game logic + draw */
        snprintf(path, sizeof(path), "%s/frame_%06u.png", out_dir, f);
        cc_screenshot(e, path);
        cc_input_end_frame(e);
    }
    if (e->cfg.verbose) CC_INFO("cc_demo_run: captured %u frames → %s", frames, out_dir);
    return frames;
}

/* Frame */
void cc_frame_begin(CCEngine* e) { if(e&&e->renderer) cc_renderer_frame_begin(e->renderer); }
void cc_frame_end(CCEngine* e)   {
    if(!e||!e->renderer) return;
    /* translate AA mode → renderer each frame (mode may change at runtime) */
    int m = (int)e->aa.mode;
    cc_renderer_set_aa(e->renderer, m, cc_aa_render_scale(e), 0);
    cc_renderer_frame_end(e->renderer);
}
const char* cc_screenshot(CCEngine* e, const char* path) {
    return (e&&e->renderer) ? cc_renderer_screenshot(e->renderer, path) : path;
}
void cc_frame_pixels(CCEngine* e, uint8_t** px, uint32_t* w, uint32_t* h) {
    if(!e||!e->renderer){ if(px)*px=NULL; if(w)*w=0; if(h)*h=0; return; }
    const uint8_t* p = cc_renderer_frame_pixels(e->renderer, w, h);
    if(px) *px = (uint8_t*)p;
}
void cc_resize(CCEngine* e, uint32_t w, uint32_t h) { if(e){ e->cfg.width=w; e->cfg.height=h; if(e->renderer) cc_renderer_resize(e->renderer,w,h); } }

/* Scene */
void      cc_scene_set_active(CCEngine* e, CCScene* s) { if(e) e->active_scene=s; }
CCScene*  cc_scene_active(CCEngine* e) { return e?e->active_scene:NULL; }

/* Timing */
double   cc_time(CCEngine* e)     { return (double)(mono_ns()-e->start_ns)*1e-9; }
double   cc_delta(CCEngine* e)    { return e?e->delta:0; }
uint64_t cc_frame_num(CCEngine* e){ return e?e->frame_count:0; }
uint64_t cc_now_ns(void)          { return mono_ns(); }
const char* cc_version_string(void) { return "0.3.1"; }

/* Input */
/* Process this frame's input by DRAINING qwerty's event queue (event-driven), not
   by scanning the whole keyboard. Cost is proportional to keys that actually
   changed this frame (usually zero) rather than O(QKEY_COUNT) every frame.
   Windowed key events reach the queue via the GLFW key callback (see renderer.c
   cc_renderer_wire_input); headless/injected events are dispatched into the same
   queue. For each KEY_DOWN we set a per-frame press latch, so a fast press+release
   in one frame still registers as cc_key_pressed. cc_key_down reads qwerty's O(1)
   mirrored level-state directly. Called once per frame before the game reads input. */
void cc_input_begin_frame(CCEngine* e) {
    if (!e) return;
    /* snapshot gamepad buttons for edge detection (before consuming this frame's) */
    for(int p=0;p<CC_MAX_GAMEPADS;p++)
        for(int b=0;b<CC_GAMEPAD_BUTTON_COUNT;b++)
            e->pads[p].prev_btn[b]=e->pads[p].btn[b];
    e->scroll_dx=e->scroll_dy=0;

    /* SINGLE queue drain for the whole frame — keys (→ press latch), scroll, and
       gamepad all routed here. Event-driven: cost scales with events that actually
       happened, not with keyboard size. */
    QEvent evs[256];
    for (;;) {
        uint32_t n = qwerty_poll(e->qwerty, evs, 256);
        for (uint32_t i=0;i<n;i++) {
            switch (evs[i].type) {
                case QEVENT_KEY_DOWN: {
                    QKey k = evs[i].key.key;
                    /* latch only a true rising edge (was up last frame); a held key
                       that re-sends DOWN (or repeated injection) must not re-fire.
                       A fast tap still latches: prev was up, so this counts. */
                    if ((unsigned)k < QKEY_COUNT && !e->prev_keys[k]) e->pressed_latch[k] = 1;
                } break;
                case QEVENT_MOUSE_SCROLL:
                    e->scroll_dx+=evs[i].mouse_scroll.dx; e->scroll_dy+=evs[i].mouse_scroll.dy;
                    break;
                case QEVENT_GAMEPAD_BUTTON_DOWN:
                case QEVENT_GAMEPAD_BUTTON_UP: {
                    uint32_t pad=evs[i].gamepad_button.gamepad_id, btn=evs[i].gamepad_button.button;
                    if(pad<CC_MAX_GAMEPADS && btn<CC_GAMEPAD_BUTTON_COUNT){
                        e->pads[pad].btn[btn]=(evs[i].type==QEVENT_GAMEPAD_BUTTON_DOWN)?1:0;
                        e->pads[pad].connected=1;
                    }
                } break;
                case QEVENT_GAMEPAD_AXIS: {
                    uint32_t pad=evs[i].gamepad_axis.gamepad_id, ax=evs[i].gamepad_axis.axis;
                    if(pad<CC_MAX_GAMEPADS && ax<CC_GAMEPAD_AXIS_COUNT){
                        e->pads[pad].axis[ax]=evs[i].gamepad_axis.value;
                        e->pads[pad].connected=1;
                    }
                } break;
                default: break;
            }
        }
        if (n < 256) break;
    }
    /* cur_keys mirrors qwerty's authoritative level state for consistent reads. */
    for (int k=0;k<QKEY_COUNT;k++)
        e->cur_keys[k] = qwerty_key_down(e->qwerty,(QKey)k) ? 1 : 0;
}

/* Roll 'current' → 'previous' for released-edge detection and clear the latch.
   Runs at END of frame. Reads qwerty's mirrored state (authoritative for both
   windowed — fed by the key callback — and headless). */
void cc_input_end_frame(CCEngine* e) {
    if (!e) return;
    for (int k=0;k<QKEY_COUNT;k++) {
        e->prev_keys[k] = qwerty_key_down(e->qwerty,(QKey)k) ? 1 : 0;
        e->pressed_latch[k] = 0;
    }
    for (int b=0;b<QMOUSE_COUNT;b++)
        e->prev_mouse[b] = qwerty_mouse_down(e->qwerty,(QMouseButton)b) ? 1 : 0;
    /* Do NOT overwrite prev_mx/prev_my in windowed mode: there, cc_mouse_delta
       manages that pair from GLFW's cursor position. Clobbering it here with
       qwerty's (different) position made each frame's delta = glfw_pos - qwerty_pos,
       a huge bogus value → the camera spun out of control. Only sync in headless. */
    if (!cc_renderer_has_window(e->renderer))
        qwerty_mouse_pos(e->qwerty,&e->prev_mx,&e->prev_my);
}

bool cc_key_down(CCEngine* e,QKey k)       { return e && e->cur_keys[k]!=0; }
bool cc_key_pressed(CCEngine* e,QKey k)    { return e && e->pressed_latch[k]!=0; }
bool cc_key_released(CCEngine* e,QKey k)   { return e && !e->cur_keys[k] && e->prev_keys[k]; }
bool cc_mouse_down(CCEngine* e,QMouseButton b){
    if (cc_renderer_has_window(e->renderer)) return cc_renderer_glfw_mouse_btn(e->renderer,(int)b)!=0;
    return qwerty_mouse_down(e->qwerty,b);
}
bool cc_mouse_pressed(CCEngine* e,QMouseButton b){ return cc_mouse_down(e,b)&&!e->prev_mouse[b]; }
bool cc_mouse_released(CCEngine* e,QMouseButton b){ return !cc_mouse_down(e,b)&&e->prev_mouse[b]; }
void cc_input_inject_mouse_button(CCEngine* e,QMouseButton b,bool down){
    int32_t x=0,y=0; cc_mouse_pos(e,&x,&y);
    qwerty_inject_mouse_button(e->qwerty,b,down,x,y);
}
QContext* cc_input_context(CCEngine* e){ return e?e->qwerty:NULL; }

/* ─── gamepad ─── */
static float gp_deadzone(CCEngine* e, float v){
    float dz=e->gamepad_deadzone;
    if(v> dz) return (v-dz)/(1.0f-dz);
    if(v<-dz) return (v+dz)/(1.0f-dz);
    return 0.0f;
}
bool cc_gamepad_connected(CCEngine* e, uint32_t pad){
    return e && pad<CC_MAX_GAMEPADS && e->pads[pad].connected;
}
bool cc_gamepad_button(CCEngine* e, uint32_t pad, CCGamepadButton b){
    return e && pad<CC_MAX_GAMEPADS && b<CC_GAMEPAD_BUTTON_COUNT && e->pads[pad].btn[b];
}
bool cc_gamepad_button_pressed(CCEngine* e, uint32_t pad, CCGamepadButton b){
    return e && pad<CC_MAX_GAMEPADS && b<CC_GAMEPAD_BUTTON_COUNT
        && e->pads[pad].btn[b] && !e->pads[pad].prev_btn[b];
}
bool cc_gamepad_button_released(CCEngine* e, uint32_t pad, CCGamepadButton b){
    return e && pad<CC_MAX_GAMEPADS && b<CC_GAMEPAD_BUTTON_COUNT
        && !e->pads[pad].btn[b] && e->pads[pad].prev_btn[b];
}
float cc_gamepad_axis(CCEngine* e, uint32_t pad, CCGamepadAxis a){
    if(!e || pad>=CC_MAX_GAMEPADS || a>=CC_GAMEPAD_AXIS_COUNT) return 0.0f;
    float v=e->pads[pad].axis[a];
    /* triggers are 0..1 and shouldn't be symmetric-deadzoned */
    if(a==CC_GAMEPAD_AXIS_LT || a==CC_GAMEPAD_AXIS_RT) return v;
    return gp_deadzone(e,v);
}
void cc_gamepad_set_deadzone(CCEngine* e, float dz){
    if(e){ if(dz<0)dz=0; if(dz>0.9f)dz=0.9f; e->gamepad_deadzone=dz; }
}
void cc_gamepad_inject_button(CCEngine* e, uint32_t pad, CCGamepadButton b, bool down){
    if(e && pad<CC_MAX_GAMEPADS && b<CC_GAMEPAD_BUTTON_COUNT){
        e->pads[pad].btn[b]=down?1:0; e->pads[pad].connected=1;
    }
}
void cc_gamepad_inject_axis(CCEngine* e, uint32_t pad, CCGamepadAxis a, float value){
    if(e && pad<CC_MAX_GAMEPADS && a<CC_GAMEPAD_AXIS_COUNT){
        if(value<-1)value=-1; if(value>1)value=1;
        e->pads[pad].axis[a]=value; e->pads[pad].connected=1;
    }
}
void cc_gamepad_inject_connected(CCEngine* e, uint32_t pad, bool connected){
    if(e && pad<CC_MAX_GAMEPADS) e->pads[pad].connected=connected?1:0;
}
void cc_input_inject(CCEngine* e,const QEvent* ev){ if(e&&ev) qwerty_inject_event(e->qwerty,ev); }
uint32_t cc_input_poll(CCEngine* e,QEvent* out,uint32_t max){
    if(!e||!out||!max) return 0;
    return qwerty_poll(e->qwerty,out,max);
}
void cc_display_size(CCEngine* e, uint32_t* w, uint32_t* h){
    extern void cc_renderer_display_size(CCRenderer*, uint32_t*, uint32_t*);
    cc_renderer_display_size(e->renderer, w, h);
}
void cc_mouse_pos(CCEngine* e,int32_t* x,int32_t* y){
    if (cc_renderer_has_window(e->renderer)) {
        double cx,cy; cc_renderer_glfw_cursor(e->renderer,&cx,&cy);
        if(x)*x=(int32_t)cx; if(y)*y=(int32_t)cy; return;
    }
    qwerty_mouse_pos(e->qwerty,x,y);
}
void cc_mouse_delta(CCEngine* e,int32_t* dx,int32_t* dy){
    int32_t cx,cy; cc_mouse_pos(e,&cx,&cy);
    if (cc_renderer_has_window(e->renderer)) {
        if (!e->mouse_init) { e->prev_mx=cx; e->prev_my=cy; e->mouse_init=1; }
        if(dx)*dx=cx-e->prev_mx; if(dy)*dy=cy-e->prev_my;
        e->prev_mx=cx; e->prev_my=cy;
        return;
    }
    if(dx)*dx=cx-e->prev_mx; if(dy)*dy=cy-e->prev_my;
}
void cc_capture_mouse(CCEngine* e, bool capture){
    if (cc_renderer_has_window(e->renderer))
        cc_renderer_glfw_capture_cursor(e->renderer, capture?1:0);
}
QMod cc_mods(CCEngine* e){ return qwerty_mods(e->qwerty); }
void cc_input_inject_key(CCEngine* e,QKey k,bool down){
    extern void qwerty_dispatch(QContext*,QEvent*);
    QEvent ev={.type=down?QEVENT_KEY_DOWN:QEVENT_KEY_UP,.key={.key=k}};
    qwerty_dispatch(e->qwerty,&ev);
}
void cc_input_inject_mouse_move(CCEngine* e,int32_t x,int32_t y){
    extern void qwerty_dispatch(QContext*,QEvent*);
    QEvent ev={.type=QEVENT_MOUSE_MOVE,.mouse_move={.x=x,.y=y}};
    qwerty_dispatch(e->qwerty,&ev);
}

/* Render pass-throughs */
CCTexture cc_texture_load(CCEngine* e,const char* p){ return cc_renderer_texture_load(e->renderer,p); }
CCTexture cc_texture_load_srgb(CCEngine* e,const char* p){ return cc_renderer_texture_load_srgb(e->renderer,p); }
CCMaterial cc_material_load_pbr(CCEngine* e,const char* dir,const CCMaterialDesc* base){ return cc_renderer_material_load_pbr(e->renderer,dir,base); }
CCTexture cc_texture_create(CCEngine* e,const CCTextureDesc* d,const void* px){ return cc_renderer_texture_create(e->renderer,d,px); }
CCTexture cc_texture_proc(CCEngine* e,const CCTextureDesc* d,void(*g)(uint8_t*,uint32_t,uint32_t,void*),void* ud){ return cc_renderer_texture_proc(e->renderer,d,g,ud); }
void cc_texture_destroy(CCEngine* e,CCTexture t){ cc_renderer_texture_destroy(e->renderer,t); }
void cc_texture_update(CCEngine* e,CCTexture t,const void* px){ cc_renderer_texture_update(e->renderer,t,px); }
CCMesh cc_mesh_create(CCEngine* e,const CCVertex* v,uint32_t nv,const uint32_t* i,uint32_t ni,CCMeshUsage u){ return cc_renderer_mesh_create(e->renderer,v,nv,i,ni,u); }

/* ── .ccmodel → renderable mesh bridge ─────────────────────────────────── */
#include "cc/ccmodel.h"
CCMesh cc_mesh_from_model(CCEngine* e, const void* model_ptr){
    const CCModel* m=(const CCModel*)model_ptr;
    if(!e||!m||m->geom.vertex_count==0||m->geom.index_count==0) return CC_NULL;
    /* CCMVertex and CCVertex share an identical 52-byte layout, but convert
       field-by-field so a future divergence can't silently corrupt data. */
    uint32_t nv=m->geom.vertex_count, ni=m->geom.index_count;
    CCVertex* v=(CCVertex*)malloc(nv*sizeof(CCVertex));
    if(!v) return CC_NULL;
    for(uint32_t k=0;k<nv;k++){
        const CCMVertex* s=&m->geom.vertices[k]; CCVertex* d=&v[k];
        d->pos[0]=s->pos[0]; d->pos[1]=s->pos[1]; d->pos[2]=s->pos[2];
        d->normal[0]=s->normal[0]; d->normal[1]=s->normal[1]; d->normal[2]=s->normal[2];
        d->uv[0]=s->uv[0]; d->uv[1]=s->uv[1];
        d->tangent[0]=s->tangent[0]; d->tangent[1]=s->tangent[1];
        d->tangent[2]=s->tangent[2]; d->tangent[3]=s->tangent[3];
        d->color[0]=s->color[0]; d->color[1]=s->color[1];
        d->color[2]=s->color[2]; d->color[3]=s->color[3];
    }
    CCMesh mesh=cc_renderer_mesh_create(e->renderer,v,nv,m->geom.indices,ni,CC_MESH_STATIC);
    free(v);
    return mesh;
}
CCMesh cc_mesh_load_ccmodel(CCEngine* e, const char* path){
    CCModel* m=ccm_load_text(path);
    if(!m){ return CC_NULL; }
    CCMesh mesh=cc_mesh_from_model(e,m);
    ccm_model_free(m);
    return mesh;
}

/* Load a glTF 2.0 model (.gltf + external .bin, OR self-contained binary .glb)
 * into a renderable mesh. Geometry only (positions/normals/uvs/indices) — for
 * materials use cc_materials_from_model on the CCModel via ccm_import_gltf. */
CCMesh cc_mesh_load_gltf(CCEngine* e, const char* path){
    if(!e||!e->renderer||!path){ return CC_NULL; }
    CCModel* m=ccm_import_gltf(path, path);
    if(!m){ fprintf(stderr,"[cc] glTF load failed: %s\n", path); return CC_NULL; }
    CCMesh mesh=cc_mesh_from_model(e,m);
    ccm_model_free(m);
    return mesh;
}

/* join base_dir + rel into out (out_sz). If rel is absolute or base is NULL/empty,
   rel is used as-is. */
static void cc_pathjoin(char* out, size_t out_sz, const char* base, const char* rel){
    if(!rel||!rel[0]){ out[0]=0; return; }
    if(!base||!base[0]||rel[0]=='/'){ snprintf(out,out_sz,"%s",rel); return; }
    size_t bl=strlen(base);
    if(base[bl-1]=='/') snprintf(out,out_sz,"%s%s",base,rel);
    else                snprintf(out,out_sz,"%s/%s",base,rel);
}

CCMaterial cc_material_from_model_slot(CCEngine* e, const void* model_ptr,
                                       uint32_t slot, const char* base_dir){
    const CCModel* m=(const CCModel*)model_ptr;
    if(!e||!m||slot>=m->matl.slot_count) return CC_NULL;
    const CCMMaterialSlot* s=&m->matl.slots[slot];
    CCMaterialDesc d={0};
    d.base_color[0]=s->base_color[0]; d.base_color[1]=s->base_color[1];
    d.base_color[2]=s->base_color[2]; d.base_color[3]=s->base_color[3]?s->base_color[3]:1.0f;
    d.roughness=s->roughness; d.metallic=s->metallic;
    d.emissive[0]=s->emissive[0]; d.emissive[1]=s->emissive[1]; d.emissive[2]=s->emissive[2];
    d.tint[0]=d.tint[1]=d.tint[2]=d.tint[3]=1.0f;
    d.alpha_cutoff=s->alpha_cutoff; d.double_sided=s->double_sided; d.alpha_blend=s->alpha_blend;
    char p[1024];
    /* albedo + emissive are COLOR → sRGB path; the rest are linear DATA maps */
    if(s->albedo_tex[0]){     cc_pathjoin(p,sizeof(p),base_dir,s->albedo_tex);     d.albedo_map=cc_texture_load_srgb(e,p); }
    if(s->emissive_tex[0]){   cc_pathjoin(p,sizeof(p),base_dir,s->emissive_tex);   d.emissive_map=cc_texture_load_srgb(e,p); }
    if(s->normal_tex[0]){     cc_pathjoin(p,sizeof(p),base_dir,s->normal_tex);     d.normal_map=cc_texture_load(e,p); }
    if(s->roughmetal_tex[0]){ cc_pathjoin(p,sizeof(p),base_dir,s->roughmetal_tex); d.roughness_metallic_map=cc_texture_load(e,p); }
    if(s->ao_tex[0]){         cc_pathjoin(p,sizeof(p),base_dir,s->ao_tex);         d.ao_map=cc_texture_load(e,p); }
    return cc_material_create(e,&d);
}

uint32_t cc_materials_from_model(CCEngine* e, const void* model_ptr,
                                 CCMaterial* out, uint32_t max, const char* base_dir){
    const CCModel* m=(const CCModel*)model_ptr;
    if(!e||!m||!out||max==0) return 0;
    if(m->matl.slot_count==0){
        /* no slots: emit one neutral default so callers always get a usable material */
        CCMaterialDesc d={0}; d.base_color[0]=d.base_color[1]=d.base_color[2]=d.base_color[3]=1.0f;
        d.roughness=0.8f; d.tint[0]=d.tint[1]=d.tint[2]=d.tint[3]=1.0f;
        out[0]=cc_material_create(e,&d);
        return 1;
    }
    uint32_t n=m->matl.slot_count<max?m->matl.slot_count:max;
    for(uint32_t i=0;i<n;i++) out[i]=cc_material_from_model_slot(e,m,i,base_dir);
    return n;
}


void cc_mesh_update(CCEngine* e,CCMesh m,const CCVertex* v,uint32_t nv,const uint32_t* i,uint32_t ni){ extern void cc_renderer_mesh_update(CCRenderer*,CCMesh,const CCVertex*,uint32_t,const uint32_t*,uint32_t); if(e&&e->renderer) cc_renderer_mesh_update(e->renderer,m,v,nv,i,ni); }
CCMesh cc_mesh_quad(CCEngine* e){ return cc_renderer_mesh_quad(e->renderer); }
void cc_mesh_destroy(CCEngine* e,CCMesh m){ cc_renderer_mesh_destroy(e->renderer,m); }
void cc_draw_mesh(CCEngine* e,CCMesh m,CCMaterial mat,const CCTransform3D* xf){ cc_renderer_draw_mesh(e->renderer,m,mat,xf); }
void cc_draw_mesh_wireframe(CCEngine* e,CCMesh m,const CCTransform3D* xf,float r,float g,float b){
    extern void cc_renderer_draw_mesh_wireframe(CCRenderer*,CCMesh,const CCTransform3D*,float,float,float);
    if(e&&e->renderer&&xf) cc_renderer_draw_mesh_wireframe(e->renderer,m,xf,r,g,b);
}
void cc_draw_bounds(CCEngine* e,const CCTransform3D* xf,float r,float g,float b){
    /* draw a unit-cube outline at the transform via the gizmo line system */
    extern void cc_gizmo_box(CCEngine*,CCVec3,CCVec3,CCVec3);
    if(!e||!xf) return;
    CCVec3 c={xf->pos[0],xf->pos[1],xf->pos[2]};
    CCVec3 half={0.5f*(xf->scale[0]?xf->scale[0]:1), 0.5f*(xf->scale[1]?xf->scale[1]:1), 0.5f*(xf->scale[2]?xf->scale[2]:1)};
    CCVec3 col={r,g,b};
    cc_gizmo_box(e,c,half,col);
}
CCTexture cc_texture_from_memory(CCEngine* e,const void* data,size_t size){
    extern CCTexture cc_renderer_texture_from_memory(CCRenderer*,const void*,size_t);
    return (e&&e->renderer&&data&&size)? cc_renderer_texture_from_memory(e->renderer,data,size) : 0;
}

/* ── Render targets ── */
CCRenderTarget cc_rt_create(CCEngine* e,uint32_t w,uint32_t h,CCPixelFmt fmt,bool depth,uint32_t msaa){
    extern CCRenderTarget cc_renderer_rt_create(CCRenderer*,uint32_t,uint32_t,CCPixelFmt,bool,uint32_t);
    return (e&&e->renderer)? cc_renderer_rt_create(e->renderer,w,h,fmt,depth,msaa) : 0;
}
void cc_rt_destroy(CCEngine* e,CCRenderTarget rt){
    extern void cc_renderer_rt_destroy(CCRenderer*,CCRenderTarget);
    if(e&&e->renderer) cc_renderer_rt_destroy(e->renderer,rt);
}
CCTexture cc_rt_color_texture(CCEngine* e,CCRenderTarget rt){
    extern CCTexture cc_renderer_rt_color_texture(CCRenderer*,CCRenderTarget);
    return (e&&e->renderer)? cc_renderer_rt_color_texture(e->renderer,rt) : 0;
}
CCTexture cc_rt_depth_texture(CCEngine* e,CCRenderTarget rt){
    extern CCTexture cc_renderer_rt_depth_texture(CCRenderer*,CCRenderTarget);
    return (e&&e->renderer)? cc_renderer_rt_depth_texture(e->renderer,rt) : 0;
}
void cc_rt_read_pixels(CCEngine* e,CCRenderTarget rt,void* out,uint32_t* w,uint32_t* h){
    extern void cc_renderer_rt_read_pixels(CCRenderer*,CCRenderTarget,void*,uint32_t*,uint32_t*);
    if(e&&e->renderer) cc_renderer_rt_read_pixels(e->renderer,rt,out,w,h);
}
void cc_rt_capture(CCEngine* e,CCRenderTarget rt){
    extern void cc_renderer_rt_capture(CCRenderer*,CCRenderTarget);
    if(e&&e->renderer) cc_renderer_rt_capture(e->renderer,rt);
}

/* ── Wavefront OBJ importer ─────────────────────────────────────────────
   Parses v / vt / vn / f (triangulating polygons via a fan). Handles
   f a/b/c, a//c, a formats. Recomputes normals if the file has none. */
CCMesh cc_mesh_load_obj(CCEngine* e, const char* path){
    if(!e||!e->renderer) return 0;
    FILE* f=fopen(path,"rb");
    if(!f){ fprintf(stderr,"[cc] OBJ open failed: %s\n",path); return 0; }
    /* dynamic pools */
    float* vp=NULL; uint32_t vpn=0,vpc=0;   /* positions xyz */
    float* vt=NULL; uint32_t vtn=0,vtc=0;   /* uv */
    float* vn=NULL; uint32_t vnn=0,vnc=0;   /* normals */
    CCVertex* verts=NULL; uint32_t vn2=0,vc2=0;
    uint32_t* idx=NULL; uint32_t in2=0,ic2=0;
    #define PUSHF(arr,n,c,a,b,cc) do{ if(n+3>c){c=c?c*2:256;arr=realloc(arr,c*sizeof(float));} arr[n++]=a;arr[n++]=b;arr[n++]=cc; }while(0)
    char line[512];
    int has_normals=0;
    while(fgets(line,sizeof(line),f)){
        if(line[0]=='v'&&line[1]==' '){ float x,y,z; if(sscanf(line+2,"%f %f %f",&x,&y,&z)==3) PUSHF(vp,vpn,vpc,x,y,z); }
        else if(line[0]=='v'&&line[1]=='t'){ float u,v2=0; sscanf(line+3,"%f %f",&u,&v2); if(vtn+2>vtc){vtc=vtc?vtc*2:256;vt=realloc(vt,vtc*sizeof(float));} vt[vtn++]=u;vt[vtn++]=v2; }
        else if(line[0]=='v'&&line[1]=='n'){ float x,y,z; if(sscanf(line+3,"%f %f %f",&x,&y,&z)==3){ PUSHF(vn,vnn,vnc,x,y,z); has_normals=1; } }
        else if(line[0]=='f'&&line[1]==' '){
            /* parse up to 8 verts of the face, each pi[/ti][/ni] */
            int pi[8],ti[8],ni[8],fn=0; char* p=line+2;
            while(*p&&fn<8){
                while(*p==' '||*p=='\t')p++;
                if(!*p||*p=='\n'||*p=='\r')break;
                int a=0,b=0,c=0; a=atoi(p);
                char* slash=strchr(p,'/');
                if(slash){ b=atoi(slash+1); char* s2=strchr(slash+1,'/'); if(s2)c=atoi(s2+1); }
                pi[fn]=a; ti[fn]=b; ni[fn]=c; fn++;
                while(*p&&*p!=' '&&*p!='\t')p++;
            }
            /* fan-triangulate; build CCVertex per corner */
            for(int t=1;t+1<fn;t++){
                int corner[3]={0,t,t+1};
                for(int k=0;k<3;k++){
                    int ci=corner[k];
                    int p1=pi[ci]; if(p1<0)p1=(int)(vpn/3)+p1+1;   /* neg = relative */
                    int t1=ti[ci]; if(t1<0)t1=(int)(vtn/2)+t1+1;
                    int n1=ni[ci]; if(n1<0)n1=(int)(vnn/3)+n1+1;
                    if(vn2>=vc2){vc2=vc2?vc2*2:512;verts=realloc(verts,vc2*sizeof(CCVertex));}
                    CCVertex* vx=&verts[vn2];
                    memset(vx,0,sizeof(CCVertex));
                    if(p1>=1&&(uint32_t)(p1*3)<=vpn){ vx->pos[0]=vp[(p1-1)*3];vx->pos[1]=vp[(p1-1)*3+1];vx->pos[2]=vp[(p1-1)*3+2]; }
                    if(t1>=1&&(uint32_t)(t1*2)<=vtn){ vx->uv[0]=vt[(t1-1)*2];vx->uv[1]=vt[(t1-1)*2+1]; }
                    if(n1>=1&&(uint32_t)(n1*3)<=vnn){ vx->normal[0]=vn[(n1-1)*3];vx->normal[1]=vn[(n1-1)*3+1];vx->normal[2]=vn[(n1-1)*3+2]; }
                    vx->tangent[0]=1;vx->tangent[3]=1;
                    vx->color[0]=vx->color[1]=vx->color[2]=vx->color[3]=255;
                    if(in2>=ic2){ic2=ic2?ic2*2:512;idx=realloc(idx,ic2*sizeof(uint32_t));}
                    idx[in2]=vn2; in2++; vn2++;
                }
            }
        }
    }
    fclose(f);
    if(vn2==0){ free(vp);free(vt);free(vn);free(verts);free(idx); fprintf(stderr,"[cc] OBJ has no faces: %s\n",path); return 0; }
    if(!has_normals){
        extern void cc_geometry_recompute_normals(CCVertex*,uint32_t,const uint32_t*,uint32_t,bool);
        cc_geometry_recompute_normals(verts,vn2,idx,in2,true);
    }
    CCMesh m=cc_renderer_mesh_create(e->renderer,verts,vn2,idx,in2,CC_MESH_STATIC);
    fprintf(stderr,"[cc] OBJ loaded %s: %u verts, %u tris\n",path,vn2,in2/3);
    free(vp);free(vt);free(vn);free(verts);free(idx);
    return m;
    #undef PUSHF
}
void cc_draw_mesh_instanced(CCEngine* e,CCMesh m,CCMaterial mat,const CCTransform3D* xforms,uint32_t count){ if(e&&e->renderer) cc_renderer_draw_mesh_instanced(e->renderer,m,mat,xforms,count); }
void cc_draw_billboards(CCEngine* e,CCTexture tex,const CCBillboard* bbs,uint32_t count,CCBillboardMode mode){ if(e&&e->renderer) cc_renderer_draw_billboards(e->renderer,tex,bbs,count,mode); }
void cc_draw_decal(CCEngine* e,CCTexture tex,const CCDecal* d){ if(e&&e->renderer) cc_renderer_submit_decal(e->renderer,tex,d); }
void cc_mesh_attach_skin(CCEngine* e,CCMesh m,const uint16_t* j,const float* w,uint32_t nv){ if(e&&e->renderer) cc_renderer_mesh_attach_skin(e->renderer,m,j,w,nv); }
void cc_set_bones(CCEngine* e,const float* mats,uint32_t n){ if(e&&e->renderer) cc_renderer_set_bones(e->renderer,mats,n); }
void cc_draw_skinned(CCEngine* e,CCMesh m,CCMaterial mat,const CCTransform3D* xf){ if(e&&e->renderer) cc_renderer_draw_skinned(e->renderer,m,mat,xf); }
void cc_draw_sprite(CCEngine* e,CCTexture t,float x,float y,float w,float h,float a,uint32_t tint){ if(e&&e->renderer) cc_renderer_draw_sprite_ex(e->renderer,t,x,y,w,h,0,0,1,1,a,tint); }
void cc_draw_rect(CCEngine* e,float x,float y,float w,float h,uint32_t fill,float border_px,uint32_t border_col){ if(e&&e->renderer) cc_renderer_draw_rect_border(e->renderer,x,y,w,h,fill,border_px,border_col); }
void cc_draw_circle(CCEngine* e,float cx,float cy,float r,uint32_t fill,float bpx,uint32_t bcol){ if(e&&e->renderer) cc_renderer_draw_circle(e->renderer,cx,cy,r,fill,bpx,bcol); }
void cc_draw_line(CCEngine* e,float x0,float y0,float x1,float y1,float w,uint32_t col){ if(e&&e->renderer) cc_renderer_draw_line(e->renderer,x0,y0,x1,y1,w,col); }
void cc_draw_triangle(CCEngine* e,float ax,float ay,float bx,float by,float cx,float cy,uint32_t col){ if(e&&e->renderer) cc_renderer_draw_triangle(e->renderer,ax,ay,bx,by,cx,cy,col); }
void cc_draw_sprite_ex(CCEngine* e,CCTexture t,float x,float y,float w,float h,float u0,float v0,float u1,float v1,float ang,uint32_t tint,int layer){ (void)layer; if(e&&e->renderer) cc_renderer_draw_sprite_ex(e->renderer,t,x,y,w,h,u0,v0,u1,v1,ang,tint); }

/* Stubs for unimplemented features */



CCMaterial cc_material_create(CCEngine* e,const CCMaterialDesc* d){ extern CCMaterial cc_renderer_material_create(CCRenderer*,const CCMaterialDesc*); return cc_renderer_material_create(e->renderer,d); }
void cc_material_destroy(CCEngine* e,CCMaterial m){ extern void cc_renderer_material_destroy(CCRenderer*,CCMaterial); cc_renderer_material_destroy(e->renderer,m); }
void cc_material_set_base_color(CCEngine* e,CCMaterial m,float r,float g,float b,float a){ extern void cc_renderer_material_set_base_color(CCRenderer*,CCMaterial,float,float,float,float); if(e&&e->renderer) cc_renderer_material_set_base_color(e->renderer,m,r,g,b,a); }
void cc_material_set_roughness(CCEngine* e,CCMaterial m,float v){ extern void cc_renderer_material_set_roughness(CCRenderer*,CCMaterial,float); if(e&&e->renderer) cc_renderer_material_set_roughness(e->renderer,m,v); }
void cc_material_set_metallic(CCEngine* e,CCMaterial m,float v){ extern void cc_renderer_material_set_metallic(CCRenderer*,CCMaterial,float); if(e&&e->renderer) cc_renderer_material_set_metallic(e->renderer,m,v); }
void cc_camera_set(CCEngine* e,const CCCameraDesc* c){
    if(!e||!e->renderer||!c) return;
    CCVec3 pos={c->pos[0],c->pos[1],c->pos[2]};
    CCVec3 tgt={c->target[0],c->target[1],c->target[2]};
    CCVec3 up={c->up[0],c->up[1],c->up[2]};
    if(up.x==0&&up.y==0&&up.z==0) up=(CCVec3){0,1,0};
    float aspect=(float)e->cfg.width/(float)e->cfg.height;
    CCMat4 view=mat4_look_at(pos,tgt,up);
    CCMat4 proj;
    if(c->ortho_size>0){ float hh=c->ortho_size,ww=hh*aspect; proj=mat4_ortho(-ww,ww,-hh,hh,c->near_plane,c->far_plane); }
    else proj=mat4_perspective((c->fov_deg>0?c->fov_deg:60.0f)*CC_DEG2RAD,aspect,c->near_plane>0?c->near_plane:0.1f,c->far_plane>0?c->far_plane:1000.0f);
    CCMat4 vp=mat4_mul(proj,view);
    cc_renderer_set_matrices(e->renderer,view.m,proj.m,vp.m,mat4_inverse(vp).m,&pos.x);
    e->stored_camera=*c;
}
void cc_camera_get(CCEngine* e,CCCameraDesc* c){ if(e&&c) *c=e->stored_camera; }
CCLightId cc_light_add(CCEngine* e,const CCLight* l){ extern CCLightId cc_renderer_light_add(CCRenderer*,const CCLight*); return cc_renderer_light_add(e->renderer,l); }
void cc_light_update(CCEngine* e,CCLightId id,const CCLight* l){ extern void cc_renderer_light_update(CCRenderer*,CCLightId,const CCLight*); cc_renderer_light_update(e->renderer,id,l); }
void cc_light_remove(CCEngine* e,CCLightId id){ extern void cc_renderer_light_remove(CCRenderer*,CCLightId); cc_renderer_light_remove(e->renderer,id); }
void cc_light_set_ambient(CCEngine* e,float r,float g,float b,float i){ extern void cc_renderer_set_ambient(CCRenderer*,float,float,float,float); cc_renderer_set_ambient(e->renderer,r,g,b,i); }
void cc_light_set_sky(CCEngine* e, CCTexture hdr_cubemap){ extern void cc_renderer_set_sky(CCRenderer*,CCTexture,bool); if(e&&e->renderer) cc_renderer_set_sky(e->renderer,hdr_cubemap,true); }
void cc_light_set_sky_colors(CCEngine* e,const float zenith[3],const float horizon[3],const float ground[3],float intensity){ extern void cc_renderer_set_sky_colors(CCRenderer*,const float*,const float*,const float*,float); if(e&&e->renderer) cc_renderer_set_sky_colors(e->renderer,zenith,horizon,ground,intensity); }
void cc_draw_text(CCEngine* e,CCFont f,const char* t,float x,float y,float s,uint32_t c){ if(e&&e->renderer) cc_renderer_draw_text(e->renderer,f,t,x,y,s,c); }
/* Word-wrap text into width w, drawing successive lines downward. */
void cc_draw_text_wrap(CCEngine* e,CCFont f,const char* text,float x,float y,float w,float s,uint32_t col){
    if(!e||!e->renderer||!text) return;
    float lh = cc_font_line_height(e,f,s); if(lh<=0) lh=s*1.3f;
    char line[512]=""; size_t ll=0;
    const char* p=text;
    while(*p){
        /* grab next word */
        const char* ws=p; while(*ws==' ') ws++;   /* leading spaces */
        const char* we=ws; while(*we && *we!=' ' && *we!='\n') we++;
        size_t wlen=(size_t)(we-ws);
        char word[256]; if(wlen>255)wlen=255; memcpy(word,ws,wlen); word[wlen]=0;
        /* candidate line */
        char cand[512]; snprintf(cand,sizeof(cand),"%s%s%s", line, ll?" ":"", word);
        if(cc_text_width(e,f,cand,s) > w && ll>0){
            cc_draw_text(e,f,line,x,y,s,col); y+=lh;
            snprintf(line,sizeof(line),"%s",word); ll=strlen(line);
        } else {
            snprintf(line,sizeof(line),"%s",cand); ll=strlen(line);
        }
        p=we;
        if(*p=='\n'){ cc_draw_text(e,f,line,x,y,s,col); y+=lh; line[0]=0; ll=0; p++; }
        else if(*p==' ') p++;
    }
    if(ll>0) cc_draw_text(e,f,line,x,y,s,col);
}
CCFont cc_font_builtin(CCEngine* e){ return e&&e->renderer ? cc_renderer_font_builtin(e->renderer) : 0; }
CCFont cc_font_load(CCEngine* e,const char* p,float px){ return e&&e->renderer ? cc_renderer_font_load(e->renderer,p,px) : 0; }
float cc_text_width(CCEngine* e,CCFont f,const char* t,float s){ return e&&e->renderer ? cc_renderer_text_width(e->renderer,f,t,s) : 0; }
float cc_font_line_height(CCEngine* e,CCFont f,float s){ return e&&e->renderer ? cc_renderer_font_line_height(e->renderer,f,s) : s; }
void cc_font_destroy(CCEngine* e,CCFont f){ (void)e;(void)f; }

/* Logging */
void cc_log(CCLogLevel level, const char* fmt, ...) {
    static const char* labels[]={"DEBUG","INFO ","WARN ","ERROR"};
    FILE* out=(level>=CC_LOG_WARN)?stderr:stdout;
    fprintf(out,"[cc:%s] ",labels[level<4?level:3]);
    va_list ap; va_start(ap,fmt); vfprintf(out,fmt,ap); va_end(ap);
    fputc('\n',out);
}

void cc_postfx_set(CCEngine* e, const CCPostFX* fx) {
    if (e && e->renderer) cc_renderer_postfx_set(e->renderer, fx);
}

/* Camera system integration */
#include "cc/camera.h"

void cc_engine_apply_camera(CCEngine* e, CCCameraRig* rig) {
    if (!e || !rig || !e->renderer) return;
    rig->cam.aspect = (float)e->cfg.width / (float)e->cfg.height;
    cc_camera_update(rig, e, 0); /* recompute matrices */
    cc_renderer_set_matrices(e->renderer,
        rig->cam.view.m, rig->cam.proj.m,
        rig->cam.view_proj.m, rig->cam.inv_view_proj.m,
        (float*)&rig->cam.position);
}

/* ── Real mesh generation ─────────────────────────────────────────────── */

static CCMesh build_mesh(CCEngine* e, CCVertex* verts, uint32_t nv, uint32_t* idx, uint32_t ni) {
    CCMesh m = cc_renderer_mesh_create(e->renderer, verts, nv, idx, ni, CC_MESH_STATIC);
    free(verts); free(idx);
    return m;
}

CCMesh cc_mesh_cube(CCEngine* e, float s) {
    float h = s*0.5f;
    /* 6 faces × 4 verts = 24 verts, 6 × 2 tris × 3 = 36 indices */
    typedef struct { float px,py,pz, nx,ny,nz, u,v; } FV;
    FV face_data[6][4] = {
        {{ h,-h,-h,0,0,-1,0,0},{ h, h,-h,0,0,-1,0,1},{-h, h,-h,0,0,-1,1,1},{-h,-h,-h,0,0,-1,1,0}}, /* -Z */
        {{-h,-h, h,0,0, 1,0,0},{ h,-h, h,0,0, 1,1,0},{ h, h, h,0,0, 1,1,1},{-h, h, h,0,0, 1,0,1}}, /* +Z */
        {{-h,-h,-h,-1,0,0,0,0},{-h,-h, h,-1,0,0,1,0},{-h, h, h,-1,0,0,1,1},{-h, h,-h,-1,0,0,0,1}}, /* -X */
        {{ h,-h, h, 1,0,0,0,0},{ h,-h,-h, 1,0,0,1,0},{ h, h,-h, 1,0,0,1,1},{ h, h, h, 1,0,0,0,1}}, /* +X */
        {{-h,-h,-h,0,-1,0,0,0},{ h,-h,-h,0,-1,0,1,0},{ h,-h, h,0,-1,0,1,1},{-h,-h, h,0,-1,0,0,1}}, /* -Y */
        {{-h, h, h,0, 1,0,0,0},{ h, h, h,0, 1,0,1,0},{ h, h,-h,0, 1,0,1,1},{-h, h,-h,0, 1,0,0,1}}, /* +Y */
    };
    CCVertex* verts = malloc(24*sizeof(CCVertex));
    uint32_t* idx = malloc(36*4);
    for (int f=0;f<6;f++) {
        for (int v=0;v<4;v++) {
            CCVertex* vt=&verts[f*4+v];
            FV* d=&face_data[f][v];
            vt->pos[0]=d->px;vt->pos[1]=d->py;vt->pos[2]=d->pz;
            vt->normal[0]=d->nx;vt->normal[1]=d->ny;vt->normal[2]=d->nz;
            vt->uv[0]=d->u;vt->uv[1]=d->v;
            vt->tangent[3]=1;vt->color[0]=vt->color[1]=vt->color[2]=vt->color[3]=255;
            
        }
        int b=f*4;
        idx[f*6+0]=b;idx[f*6+1]=b+1;idx[f*6+2]=b+2;
        idx[f*6+3]=b;idx[f*6+4]=b+2;idx[f*6+5]=b+3;
    }
    /* Compute tangents per face */
    for (int f=0;f<6;f++) {
        CCVertex* v0=&verts[f*4];CCVertex* v1=&verts[f*4+1];CCVertex* v2=&verts[f*4+2];
        float e1x=v1->pos[0]-v0->pos[0],e1y=v1->pos[1]-v0->pos[1],e1z=v1->pos[2]-v0->pos[2];
        float e2x=v2->pos[0]-v0->pos[0],e2y=v2->pos[1]-v0->pos[1],e2z=v2->pos[2]-v0->pos[2];
        float du1=v1->uv[0]-v0->uv[0],dv1=v1->uv[1]-v0->uv[1];
        float du2=v2->uv[0]-v0->uv[0],dv2=v2->uv[1]-v0->uv[1];
        float r=du1*dv2-du2*dv1; if(fabsf(r)<1e-8f)r=1e-8f; r=1/r;
        float tx=(dv2*e1x-dv1*e2x)*r,ty=(dv2*e1y-dv1*e2y)*r,tz=(dv2*e1z-dv1*e2z)*r;
        float l=sqrtf(tx*tx+ty*ty+tz*tz); if(l>1e-6f){tx/=l;ty/=l;tz/=l;}
        for (int v=0;v<4;v++){verts[f*4+v].tangent[0]=tx;verts[f*4+v].tangent[1]=ty;verts[f*4+v].tangent[2]=tz;}
    }
    return build_mesh(e, verts, 24, idx, 36);
}

CCMesh cc_mesh_sphere(CCEngine* e, float r, uint32_t slices, uint32_t stacks) {
    if (slices<3) slices=3; if (stacks<2) stacks=2;
    uint32_t nv=(slices+1)*(stacks+1);
    uint32_t ni=slices*stacks*6;
    CCVertex* verts=malloc(nv*sizeof(CCVertex));
    uint32_t* idx=malloc(ni*4);
    uint32_t vi=0;
    for (uint32_t j=0;j<=stacks;j++) {
        float phi=CC_PI*j/stacks-CC_PI*0.5f;
        for (uint32_t i=0;i<=slices;i++) {
            float theta=CC_TAU*i/slices;
            float nx=cosf(phi)*cosf(theta),ny=sinf(phi),nz=cosf(phi)*sinf(theta);
            CCVertex* v=&verts[vi++];
            v->pos[0]=nx*r;v->pos[1]=ny*r;v->pos[2]=nz*r;
            v->normal[0]=nx;v->normal[1]=ny;v->normal[2]=nz;
            v->uv[0]=(float)i/slices;v->uv[1]=(float)j/stacks;
            v->tangent[0]=-sinf(theta);v->tangent[2]=cosf(theta);v->tangent[3]=1;
            v->color[0]=v->color[1]=v->color[2]=v->color[3]=255;
            
        }
    }
    uint32_t ii=0;
    for (uint32_t j=0;j<stacks;j++) for (uint32_t i=0;i<slices;i++) {
        uint32_t a=j*(slices+1)+i,b=a+1,c=a+slices+1,d=c+1;
        idx[ii++]=a;idx[ii++]=c;idx[ii++]=b;
        idx[ii++]=b;idx[ii++]=c;idx[ii++]=d;
    }
    return build_mesh(e,verts,nv,idx,ni);
}

CCMesh cc_mesh_plane(CCEngine* e, float w, float h, uint32_t divs) {
    if (divs<1) divs=1;
    uint32_t nv=(divs+1)*(divs+1), ni=divs*divs*6;
    CCVertex* verts=malloc(nv*sizeof(CCVertex));
    uint32_t* idx=malloc(ni*4);
    uint32_t vi=0;
    for (uint32_t j=0;j<=divs;j++) for (uint32_t i=0;i<=divs;i++) {
        CCVertex* v=&verts[vi++];
        v->pos[0]=(i/(float)divs-0.5f)*w;v->pos[1]=0;v->pos[2]=(j/(float)divs-0.5f)*h;
        v->normal[0]=0;v->normal[1]=1;v->normal[2]=0;
        v->uv[0]=(float)i/divs;v->uv[1]=(float)j/divs;
        v->tangent[0]=1;v->tangent[3]=1;
        v->color[0]=v->color[1]=v->color[2]=v->color[3]=255;
        
    }
    uint32_t ii=0;
    for (uint32_t j=0;j<divs;j++) for (uint32_t i=0;i<divs;i++) {
        uint32_t a=j*(divs+1)+i,b=a+1,c=a+divs+1,d=c+1;
        idx[ii++]=a;idx[ii++]=b;idx[ii++]=c;
        idx[ii++]=b;idx[ii++]=d;idx[ii++]=c;
    }
    return build_mesh(e,verts,nv,idx,ni);
}

/* Helper: set a vertex with white color + a tangent placeholder. */
static inline void gv_set(CCVertex* v, float px,float py,float pz,
                          float nx,float ny,float nz, float u,float vv) {
    v->pos[0]=px;v->pos[1]=py;v->pos[2]=pz;
    v->normal[0]=nx;v->normal[1]=ny;v->normal[2]=nz;
    v->uv[0]=u;v->uv[1]=vv;
    v->tangent[0]=1;v->tangent[1]=0;v->tangent[2]=0;v->tangent[3]=1;
    v->color[0]=v->color[1]=v->color[2]=v->color[3]=255;
}

/* Cylinder along Y, centered at origin. Side ring + two caps. */
CCMesh cc_mesh_cylinder(CCEngine* e, float r, float h, uint32_t segs) {
    if (segs<3) segs=3;
    float hh=h*0.5f;
    /* side: (segs+1)*2 verts; each cap: center + (segs+1) rim */
    uint32_t side_nv=(segs+1)*2;
    uint32_t cap_nv =(segs+2);
    uint32_t nv=side_nv + cap_nv*2;
    uint32_t ni=segs*6 /*side*/ + segs*3*2 /*caps*/;
    CCVertex* V=malloc(nv*sizeof(CCVertex));
    uint32_t* I=malloc(ni*4);
    uint32_t vi=0, ii=0;
    /* side wall */
    for (uint32_t i=0;i<=segs;i++){
        float t=(float)i/segs, a=t*CC_TAU;
        float nx=cosf(a), nz=sinf(a);
        gv_set(&V[vi++], nx*r,-hh,nz*r, nx,0,nz, t,0);
        gv_set(&V[vi++], nx*r, hh,nz*r, nx,0,nz, t,1);
    }
    for (uint32_t i=0;i<segs;i++){
        uint32_t a=i*2, b=a+1, c=a+2, d=a+3;
        I[ii++]=a;I[ii++]=c;I[ii++]=b; I[ii++]=b;I[ii++]=c;I[ii++]=d;
    }
    /* caps */
    for (int cap=0;cap<2;cap++){
        float y = cap? hh : -hh; float ny = cap? 1.0f : -1.0f;
        uint32_t center=vi;
        gv_set(&V[vi++], 0,y,0, 0,ny,0, 0.5f,0.5f);
        uint32_t rim0=vi;
        for (uint32_t i=0;i<=segs;i++){
            float t=(float)i/segs, a=t*CC_TAU;
            float nx=cosf(a), nz=sinf(a);
            gv_set(&V[vi++], nx*r,y,nz*r, 0,ny,0, 0.5f+0.5f*nx,0.5f+0.5f*nz);
        }
        for (uint32_t i=0;i<segs;i++){
            uint32_t r1=rim0+i, r2=rim0+i+1;
            if (cap){ I[ii++]=center;I[ii++]=r1;I[ii++]=r2; }
            else    { I[ii++]=center;I[ii++]=r2;I[ii++]=r1; }
        }
    }
    cc_geometry_recompute_tangents(V,nv,I,ni);
    return build_mesh(e,V,nv,I,ni);
}

/* Cone along Y: apex at +h/2, circular base at -h/2. */
CCMesh cc_mesh_cone(CCEngine* e, float r, float h, uint32_t segs) {
    if (segs<3) segs=3;
    float hh=h*0.5f;
    /* side: per-seg (apex + 2 base) as flat tris for correct normals;
       base: center + rim */
    uint32_t side_nv=segs*3;
    uint32_t base_nv=segs+2;
    uint32_t nv=side_nv+base_nv;
    uint32_t ni=segs*3 + segs*3;
    CCVertex* V=malloc(nv*sizeof(CCVertex));
    uint32_t* I=malloc(ni*4);
    uint32_t vi=0, ii=0;
    float slant=sqrtf(h*h+r*r); float ny_side = r/slant; float ry = h/slant;
    for (uint32_t i=0;i<segs;i++){
        float a0=(float)i/segs*CC_TAU, a1=(float)(i+1)/segs*CC_TAU, am=(a0+a1)*0.5f;
        float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1),cm=cosf(am),sm=sinf(am);
        /* face normal points outward + up */
        uint32_t base=vi;
        gv_set(&V[vi++], 0,hh,0,        cm*ry,ny_side,sm*ry, ((float)i+0.5f)/segs,1);
        gv_set(&V[vi++], c0*r,-hh,s0*r, c0*ry,ny_side,s0*ry, (float)i/segs,0);
        gv_set(&V[vi++], c1*r,-hh,s1*r, c1*ry,ny_side,s1*ry, (float)(i+1)/segs,0);
        I[ii++]=base;I[ii++]=base+1;I[ii++]=base+2;
    }
    uint32_t center=vi;
    gv_set(&V[vi++], 0,-hh,0, 0,-1,0, 0.5f,0.5f);
    uint32_t rim0=vi;
    for (uint32_t i=0;i<=segs;i++){
        float a=(float)i/segs*CC_TAU, nx=cosf(a),nz=sinf(a);
        gv_set(&V[vi++], nx*r,-hh,nz*r, 0,-1,0, 0.5f+0.5f*nx,0.5f+0.5f*nz);
    }
    for (uint32_t i=0;i<segs;i++){ I[ii++]=center;I[ii++]=rim0+i+1;I[ii++]=rim0+i; }
    cc_geometry_recompute_tangents(V,nv,I,ni);
    return build_mesh(e,V,nv,I,ni);
}

/* Capsule along Y: cylinder body of height h capped by two hemispheres of
 * radius r. Total height = h + 2r. `segs` = radial segments; ring count scales. */
CCMesh cc_mesh_capsule(CCEngine* e, float r, float h, uint32_t segs) {
    if (segs<3) segs=3;
    uint32_t rings=segs/2; if (rings<2) rings=2;   /* per hemisphere */
    float hh=h*0.5f;
    /* rows: top hemi (rings+1) + bottom hemi (rings+1); columns = segs+1 */
    uint32_t rows = (rings+1)*2;
    uint32_t cols = segs+1;
    uint32_t nv = rows*cols;
    uint32_t ni = (rows-1)*segs*6;
    CCVertex* V=malloc(nv*sizeof(CCVertex));
    uint32_t* I=malloc(ni*4);
    uint32_t vi=0;
    /* top hemisphere: phi 0..pi/2, center offset +hh */
    for (uint32_t j=0;j<=rings;j++){
        float phi=(CC_PI*0.5f)*j/rings;              /* 0 at pole → pi/2 at equator */
        float sy=cosf(phi), sr=sinf(phi);
        for (uint32_t i=0;i<cols;i++){
            float a=(float)i/segs*CC_TAU, nx=sr*cosf(a), nz=sr*sinf(a), ny=sy;
            gv_set(&V[vi++], nx*r, hh+ny*r, nz*r, nx,ny,nz, (float)i/segs, 1.0f-0.25f*j/rings);
        }
    }
    /* bottom hemisphere: equator → bottom pole, center offset -hh */
    for (uint32_t j=0;j<=rings;j++){
        float phi=(CC_PI*0.5f)*(j/(float)rings)+CC_PI*0.5f; /* pi/2 → pi */
        float sy=cosf(phi), sr=sinf(phi);
        for (uint32_t i=0;i<cols;i++){
            float a=(float)i/segs*CC_TAU, nx=sr*cosf(a), nz=sr*sinf(a), ny=sy;
            gv_set(&V[vi++], nx*r, -hh+ny*r, nz*r, nx,ny,nz, (float)i/segs, 0.25f-0.25f*j/rings);
        }
    }
    uint32_t ii=0;
    for (uint32_t j=0;j<rows-1;j++) for (uint32_t i=0;i<segs;i++){
        uint32_t a=j*cols+i,b=a+1,c=a+cols,d=c+1;
        I[ii++]=a;I[ii++]=c;I[ii++]=b; I[ii++]=b;I[ii++]=c;I[ii++]=d;
    }
    cc_geometry_recompute_tangents(V,nv,I,ni);
    return build_mesh(e,V,nv,I,ni);
}

/* Torus in the XZ plane. r_major = ring center radius, r_minor = tube radius. */
CCMesh cc_mesh_torus(CCEngine* e, float r_major, float r_minor, uint32_t segs) {
    if (segs<3) segs=3;
    uint32_t sides=segs, rings=segs;
    uint32_t cols=sides+1, rows=rings+1;
    uint32_t nv=rows*cols, ni=rings*sides*6;
    CCVertex* V=malloc(nv*sizeof(CCVertex));
    uint32_t* I=malloc(ni*4);
    uint32_t vi=0;
    for (uint32_t j=0;j<=rings;j++){
        float u=(float)j/rings*CC_TAU, cu=cosf(u), su=sinf(u);
        for (uint32_t i=0;i<=sides;i++){
            float v=(float)i/sides*CC_TAU, cv=cosf(v), sv=sinf(v);
            float cx=cu*r_major, cz=su*r_major;               /* ring center */
            float px=cu*(r_major+cv*r_minor);
            float pz=su*(r_major+cv*r_minor);
            float py=sv*r_minor;
            float nx=px-cx, nz=pz-cz, ny=py;                  /* toward center → normal */
            float nl=sqrtf(nx*nx+ny*ny+nz*nz); if(nl>1e-6f){nx/=nl;ny/=nl;nz/=nl;}
            gv_set(&V[vi++], px,py,pz, nx,ny,nz, (float)j/rings,(float)i/sides);
        }
    }
    uint32_t ii=0;
    for (uint32_t j=0;j<rings;j++) for (uint32_t i=0;i<sides;i++){
        uint32_t a=j*cols+i,b=a+1,c=a+cols,d=c+1;
        I[ii++]=a;I[ii++]=c;I[ii++]=b; I[ii++]=b;I[ii++]=c;I[ii++]=d;
    }
    cc_geometry_recompute_tangents(V,nv,I,ni);
    return build_mesh(e,V,nv,I,ni);
}

/* ── Geometry toolkit (CPU, headless-safe) ───────────────────────────── */

void cc_geometry_bounds(const CCVertex* verts, uint32_t nv,
                        float out_min[3], float out_max[3]) {
    if (!verts||!nv) return;
    float mn[3]={verts[0].pos[0],verts[0].pos[1],verts[0].pos[2]};
    float mx[3]={mn[0],mn[1],mn[2]};
    for (uint32_t i=1;i<nv;i++) for(int k=0;k<3;k++){
        float p=verts[i].pos[k];
        if(p<mn[k])mn[k]=p; if(p>mx[k])mx[k]=p;
    }
    if(out_min){out_min[0]=mn[0];out_min[1]=mn[1];out_min[2]=mn[2];}
    if(out_max){out_max[0]=mx[0];out_max[1]=mx[1];out_max[2]=mx[2];}
}

void cc_geometry_recompute_normals(CCVertex* verts, uint32_t nv,
                                   const uint32_t* idx, uint32_t ni, bool smooth) {
    if (!verts||!idx||ni<3) return;
    if (smooth) {
        /* zero, accumulate area-weighted face normals, normalize. Shared by
           position so co-located verts (seams) get identical smoothing. */
        for (uint32_t i=0;i<nv;i++){verts[i].normal[0]=verts[i].normal[1]=verts[i].normal[2]=0;}
        for (uint32_t t=0;t+2<ni;t+=3){
            uint32_t ia=idx[t],ib=idx[t+1],ic=idx[t+2];
            CCVec3 a={verts[ia].pos[0],verts[ia].pos[1],verts[ia].pos[2]};
            CCVec3 b={verts[ib].pos[0],verts[ib].pos[1],verts[ib].pos[2]};
            CCVec3 c={verts[ic].pos[0],verts[ic].pos[1],verts[ic].pos[2]};
            CCVec3 fn=vec3_cross(vec3_sub(b,a),vec3_sub(c,a)); /* len ∝ 2*area */
            verts[ia].normal[0]+=fn.x;verts[ia].normal[1]+=fn.y;verts[ia].normal[2]+=fn.z;
            verts[ib].normal[0]+=fn.x;verts[ib].normal[1]+=fn.y;verts[ib].normal[2]+=fn.z;
            verts[ic].normal[0]+=fn.x;verts[ic].normal[1]+=fn.y;verts[ic].normal[2]+=fn.z;
        }
        for (uint32_t i=0;i<nv;i++){
            CCVec3 n=vec3_norm((CCVec3){verts[i].normal[0],verts[i].normal[1],verts[i].normal[2]});
            verts[i].normal[0]=n.x;verts[i].normal[1]=n.y;verts[i].normal[2]=n.z;
        }
    } else {
        /* flat: each vertex takes its (last) face normal */
        for (uint32_t t=0;t+2<ni;t+=3){
            uint32_t ia=idx[t],ib=idx[t+1],ic=idx[t+2];
            CCVec3 a={verts[ia].pos[0],verts[ia].pos[1],verts[ia].pos[2]};
            CCVec3 b={verts[ib].pos[0],verts[ib].pos[1],verts[ib].pos[2]};
            CCVec3 c={verts[ic].pos[0],verts[ic].pos[1],verts[ic].pos[2]};
            CCVec3 n=vec3_norm(vec3_cross(vec3_sub(b,a),vec3_sub(c,a)));
            for(uint32_t k=0;k<3;k++){uint32_t vi2=idx[t+k];verts[vi2].normal[0]=n.x;verts[vi2].normal[1]=n.y;verts[vi2].normal[2]=n.z;}
        }
    }
}

void cc_geometry_recompute_tangents(CCVertex* verts, uint32_t nv,
                                    const uint32_t* idx, uint32_t ni) {
    if (!verts||!idx||ni<3) return;
    CCVec3* tan=calloc(nv,sizeof(CCVec3));
    CCVec3* bit=calloc(nv,sizeof(CCVec3));
    if(!tan||!bit){free(tan);free(bit);return;}
    for (uint32_t t=0;t+2<ni;t+=3){
        uint32_t i0=idx[t],i1=idx[t+1],i2=idx[t+2];
        CCVertex *p0=&verts[i0],*p1=&verts[i1],*p2=&verts[i2];
        float e1x=p1->pos[0]-p0->pos[0],e1y=p1->pos[1]-p0->pos[1],e1z=p1->pos[2]-p0->pos[2];
        float e2x=p2->pos[0]-p0->pos[0],e2y=p2->pos[1]-p0->pos[1],e2z=p2->pos[2]-p0->pos[2];
        float du1=p1->uv[0]-p0->uv[0],dv1=p1->uv[1]-p0->uv[1];
        float du2=p2->uv[0]-p0->uv[0],dv2=p2->uv[1]-p0->uv[1];
        float d=du1*dv2-du2*dv1; float r=(fabsf(d)<1e-8f)?0.0f:1.0f/d;
        CCVec3 T={(dv2*e1x-dv1*e2x)*r,(dv2*e1y-dv1*e2y)*r,(dv2*e1z-dv1*e2z)*r};
        CCVec3 B={(du1*e2x-du2*e1x)*r,(du1*e2y-du2*e1y)*r,(du1*e2z-du2*e1z)*r};
        uint32_t ix[3]={i0,i1,i2};
        for(int k=0;k<3;k++){tan[ix[k]]=vec3_add(tan[ix[k]],T);bit[ix[k]]=vec3_add(bit[ix[k]],B);}
    }
    for (uint32_t i=0;i<nv;i++){
        CCVec3 n={verts[i].normal[0],verts[i].normal[1],verts[i].normal[2]};
        CCVec3 t=tan[i];
        /* Gram-Schmidt orthogonalize t against n */
        CCVec3 to=vec3_sub(t,vec3_scale(n,vec3_dot(n,t)));
        float tl=vec3_len(to);
        if(tl>1e-6f) to=vec3_scale(to,1.0f/tl);
        else { /* degenerate UV: pick any perpendicular */
            to = fabsf(n.y)<0.99f ? vec3_norm(vec3_cross((CCVec3){0,1,0},n))
                                  : vec3_norm(vec3_cross((CCVec3){1,0,0},n));
        }
        float hand = (vec3_dot(vec3_cross(n,to),bit[i])<0.0f)?-1.0f:1.0f;
        verts[i].tangent[0]=to.x;verts[i].tangent[1]=to.y;verts[i].tangent[2]=to.z;verts[i].tangent[3]=hand;
    }
    free(tan);free(bit);
}

uint32_t cc_geometry_weld(CCVertex* verts, uint32_t* nv_inout,
                          uint32_t* idx, uint32_t ni, float epsilon) {
    if(!verts||!nv_inout||!idx) return nv_inout?*nv_inout:0;
    uint32_t nv=*nv_inout;
    if(nv==0) return 0;
    float eps2=epsilon*epsilon;
    uint32_t* remap=malloc(nv*sizeof(uint32_t));
    uint32_t out=0;
    /* O(n^2) but fine for procedural meshes; positions only. */
    for (uint32_t i=0;i<nv;i++){
        uint32_t found=UINT32_MAX;
        for (uint32_t j=0;j<out;j++){
            float dx=verts[i].pos[0]-verts[j].pos[0];
            float dy=verts[i].pos[1]-verts[j].pos[1];
            float dz=verts[i].pos[2]-verts[j].pos[2];
            if(dx*dx+dy*dy+dz*dz<=eps2){found=j;break;}
        }
        if(found==UINT32_MAX){ verts[out]=verts[i]; remap[i]=out; out++; }
        else remap[i]=found;
    }
    for (uint32_t i=0;i<ni;i++) idx[i]=remap[idx[i]];
    free(remap);
    *nv_inout=out;
    return out;
}

void cc_geometry_flip(CCVertex* verts, uint32_t nv, uint32_t* idx, uint32_t ni) {
    if(verts) for(uint32_t i=0;i<nv;i++){
        verts[i].normal[0]=-verts[i].normal[0];
        verts[i].normal[1]=-verts[i].normal[1];
        verts[i].normal[2]=-verts[i].normal[2];
    }
    if(idx) for(uint32_t t=0;t+2<ni;t+=3){ uint32_t tmp=idx[t+1];idx[t+1]=idx[t+2];idx[t+2]=tmp; }
}

void cc_mouse_scroll(CCEngine* e, float* dx, float* dy) {
    if (dx) *dx = e->scroll_dx;
    if (dy) *dy = e->scroll_dy;
}

/* Public camera matrix upload */
void cc_upload_camera_matrices(CCEngine* e,
                                const float* view, const float* proj,
                                const float* view_proj, const float* inv_vp,
                                const float* cam_pos_xyz) {
    if (!e || !e->renderer) return;
    cc_renderer_set_matrices(e->renderer, view, proj, view_proj, inv_vp, cam_pos_xyz);
}

CCRenderer* cc_engine_renderer(CCEngine* e) { return e ? e->renderer : NULL; }
QContext* cc_engine_qwerty(CCEngine* e) { return e ? e->qwerty : NULL; }


/* ─── Custom shader API (user GLSL injection) ────────────────────────── */
CCShader cc_shader_load_src(CCEngine* e, const char* vs, const char* fs){ return e&&e->renderer?cc_renderer_shader_create(e->renderer,vs,fs):0; }
CCShader cc_shader_load(CCEngine* e, const char* vp, const char* fp){
    if(!e||!e->renderer||!vp||!fp) return 0;
    FILE* f=fopen(vp,"rb"); if(!f) return 0; fseek(f,0,SEEK_END); long vsz=ftell(f); fseek(f,0,SEEK_SET);
    char* vsrc=malloc(vsz+1); if(fread(vsrc,1,vsz,f)!=(size_t)vsz){fclose(f);free(vsrc);return 0;} vsrc[vsz]=0; fclose(f);
    f=fopen(fp,"rb"); if(!f){free(vsrc);return 0;} fseek(f,0,SEEK_END); long fsz=ftell(f); fseek(f,0,SEEK_SET);
    char* fsrc=malloc(fsz+1); if(fread(fsrc,1,fsz,f)!=(size_t)fsz){fclose(f);free(vsrc);free(fsrc);return 0;} fsrc[fsz]=0; fclose(f);
    CCShader s=cc_renderer_shader_create(e->renderer,vsrc,fsrc); free(vsrc); free(fsrc); return s;
}
CCShader cc_shader_load_spirv(CCEngine* e, const void* v, size_t vs, const void* f, size_t fs){ (void)e;(void)v;(void)vs;(void)f;(void)fs; return 0; }
void cc_shader_set_int(CCEngine* e, CCShader s, const char* n, int v){ if(e&&e->renderer) cc_renderer_shader_set_int(e->renderer,s,n,v); }
void cc_shader_set_float(CCEngine* e, CCShader s, const char* n, float v){ if(e&&e->renderer) cc_renderer_shader_set_float(e->renderer,s,n,v); }
void cc_shader_set_vec3(CCEngine* e, CCShader s, const char* n, float x,float y,float z){ if(e&&e->renderer) cc_renderer_shader_set_vec3(e->renderer,s,n,x,y,z); }
void cc_shader_set_vec4(CCEngine* e, CCShader s, const char* n, float x,float y,float z,float w){ if(e&&e->renderer) cc_renderer_shader_set_vec4(e->renderer,s,n,x,y,z,w); }
void cc_shader_set_mat4(CCEngine* e, CCShader s, const char* n, const float* m){ if(e&&e->renderer) cc_renderer_shader_set_mat4(e->renderer,s,n,m); }
void cc_shader_destroy(CCEngine* e, CCShader s){ if(e&&e->renderer) cc_renderer_shader_destroy(e->renderer,s); }
void cc_post_custom(CCEngine* e, CCShader s){ if(e&&e->renderer) cc_renderer_custom_fullscreen_pass(e->renderer,s); }

/* Audio system ownership (defined here, used by audio.c) */
struct CCAudioSystem;
struct CCAudioSystem* cc_engine_audio_get(CCEngine* e) { return e ? (struct CCAudioSystem*)e->audio_system : NULL; }
void cc_engine_audio_set(CCEngine* e, struct CCAudioSystem* s) { if(e) e->audio_system=(void*)s; }
struct CCScriptSystem;
struct CCScriptSystem* cc_engine_script_get(CCEngine* e) { return e ? (struct CCScriptSystem*)e->script_system : NULL; }
void cc_engine_script_set(CCEngine* e, struct CCScriptSystem* s) { if(e) e->script_system=(void*)s; }

/* ─── Asset path resolution (bundle-relative) ────────────────────────────
   Resolves a relative asset path against the directory containing the running
   executable, so a shipped bundle (exe + assets/) finds its files regardless of
   the working directory it's launched from. Returns a pointer to a static
   buffer (not thread-safe; copy if needed). */
#ifdef _WIN32
  #include <windows.h>
#endif
const char* cc_asset_path(const char* rel) {
    static char buf[1024];
    static char exedir[900] = {0};
    if (!exedir[0]) {
#ifdef _WIN32
        char p[900]; DWORD n = GetModuleFileNameA(NULL, p, sizeof(p));
        if (n > 0) { for (int i=(int)n-1;i>=0;i--){ if(p[i]=='\\'||p[i]=='/'){p[i]=0;break;} } snprintf(exedir,sizeof(exedir),"%s",p); }
#else
        char p[900]; ssize_t n = readlink("/proc/self/exe", p, sizeof(p)-1);
        if (n > 0) { p[n]=0; for (int i=(int)n-1;i>=0;i--){ if(p[i]=='/'){p[i]=0;break;} } snprintf(exedir,sizeof(exedir),"%s",p); }
#endif
        else snprintf(exedir, sizeof(exedir), ".");
    }
    snprintf(buf, sizeof(buf), "%s/%s", exedir, rel ? rel : "");
    return buf;
}

/* Resolve a bare asset NAME to a usable path, working in BOTH contexts:
   1. RUNTIME (shipped game): <exedir>/assets/<name> — the bundled folder.
   2. DEV (running from the CC skill): assets-dev/<type>/<name>, where <type> is
      derived from the extension (fonts/models/textures/anims). This is the
      in-development asset library.
   A game just calls cc_asset("player.gltf"); the same call finds the dev asset
   while building and the bundled asset when shipped. The bundler greps sources for
   cc_asset("...") and copies exactly those from assets-dev/ into the shipped
   assets/ — so only USED assets ship. Returns a static buffer (copy if you keep). */
static int asset_ieq(const char* a, const char* b){
    for(;*a&&*b;a++,b++){ char ca=*a,cb=*b; if(ca>='A'&&ca<='Z')ca+=32; if(cb>='A'&&cb<='Z')cb+=32; if(ca!=cb)return 0; }
    return *a==*b;
}
static const char* asset_dev_subdir(const char* name){
    const char* dot=NULL; for(const char* p=name;*p;p++) if(*p=='.')dot=p;
    const char* e = dot?dot+1:"";
    #define IEQ(a,b) asset_ieq((a),(b))
    if(IEQ(e,"ttf")||IEQ(e,"otf")) return "fonts";
    if(IEQ(e,"gltf")||IEQ(e,"glb")||IEQ(e,"obj")||IEQ(e,"ccmodel")) return "models";
    if(IEQ(e,"png")||IEQ(e,"jpg")||IEQ(e,"jpeg")||IEQ(e,"tga")||IEQ(e,"bmp")) return "textures";
    if(IEQ(e,"ccanim")||IEQ(e,"anim")) return "anims";
    #undef IEQ
    return ""; /* unknown → assets-dev root */
}
const char* cc_asset(const char* name){
    static char buf[1024];
    if(!name||!*name){ buf[0]=0; return buf; }
    /* 1. shipped: next to the exe under assets/ */
    const char* rt = cc_asset_path("assets/");
    static char rtpath[1024]; snprintf(rtpath,sizeof(rtpath),"%s%s",rt,name);
    FILE* f=fopen(rtpath,"rb"); if(f){ fclose(f); snprintf(buf,sizeof(buf),"%s",rtpath); return buf; }
    /* 2. dev library: assets-dev/<type>/<name> (searched from cwd upward a bit) */
    const char* sub=asset_dev_subdir(name);
    const char* prefixes[]={ "assets-dev", "../assets-dev", "../../assets-dev", NULL };
    for(int i=0;prefixes[i];i++){
        if(sub[0]) snprintf(buf,sizeof(buf),"%s/%s/%s",prefixes[i],sub,name);
        else       snprintf(buf,sizeof(buf),"%s/%s",prefixes[i],name);
        FILE* d=fopen(buf,"rb"); if(d){ fclose(d); return buf; }
    }
    /* 3. not found: return the runtime path anyway (loader will report the miss) */
    snprintf(buf,sizeof(buf),"%s",rtpath); return buf;
}
void cc_light_set_shadow_softness(CCEngine* e, float softness){ extern void cc_renderer_set_shadow_softness(CCRenderer*,float); if(e&&e->renderer) cc_renderer_set_shadow_softness(e->renderer,softness); }
