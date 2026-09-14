#pragma once
/*
 * cc/timeline.h — a generic, genre-neutral TIMELINE mechanism.
 *
 * A timeline is "time advancing through tagged spans and points." That single
 * mechanism is:
 *   - a fighting-game MOVE:  startup → active(hitbox on) → recovery, + cancel window
 *   - a shooter's WEAPON cycle: windup → fire(recoil, muzzle) → chamber → ready
 *   - a tank's RELOAD:       eject → load → seat → ready
 *   - an ability CAST:       charge → release(spawn projectile) → cooldown
 *   - an animation's NOTIFIES: footstep@0.2s, sheathe@0.6s
 *
 * The engine owns the MECHANISM (advance time; know which phase is current; know
 * which windows are open; fire enter/exit/marker events at the right instant). The
 * GAME owns the POLICY (what a phase or window or marker MEANS — activate a hitbox,
 * apply recoil, allow a cancel, spawn a shell). Nothing here is combat-specific: a
 * "window" is just a named span of time; a "marker" is just a named instant.
 *
 * It is EVENT-DRIVEN by design: you get a callback the frame a phase/window/marker
 * boundary is crossed — you do NOT scan "is it frame 6 yet?" every frame across
 * every actor. The timeline announces its own key moments. (This matches the
 * engine's loop principle: event-driven for change, per-frame only for continuous
 * values. Advancing time is continuous; the boundaries are events.)
 *
 * Timelines interoperate with the anim state machine rather than duplicating it:
 * an anim TRIGGER drives which move you're in; a TIMELINE drives the windows within
 * that move. Drive the timeline with the same fixed_dt as your sim (on_tick).
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCTimeline CCTimeline;

/* What kind of boundary was crossed, delivered to your callback. */
typedef enum {
    CC_TL_PHASE_ENTER,   /* entered a phase (payload: phase id/name)      */
    CC_TL_PHASE_EXIT,    /* left a phase                                  */
    CC_TL_WINDOW_OPEN,   /* a tagged window began                         */
    CC_TL_WINDOW_CLOSE,  /* a tagged window ended                         */
    CC_TL_MARKER,        /* an instantaneous point was passed             */
    CC_TL_FINISHED,      /* the timeline reached its end (non-looping)    */
} CCTimelineEventType;

/* One boundary event. `name`/`id` identify the phase/window/marker; the game maps
 * those to meaning. `t` is the timeline time at which it fired. */
typedef struct {
    CCTimelineEventType type;
    const char*         name;   /* the tag you gave the phase/window/marker */
    uint32_t            id;     /* the index you gave it (fast switch)       */
    float               t;      /* timeline time of the boundary             */
    void*               user;   /* the timeline's userdata                   */
} CCTimelineEvent;

typedef void (*CCTimelineCallback)(const CCTimelineEvent* ev, void* user);

/* ── build ──────────────────────────────────────────────────────────────
   Timelines are cheap value-ish objects. Create, add spans/points, then play. */
CCTimeline* cc_timeline_create(float duration, bool loop);
void        cc_timeline_destroy(CCTimeline* tl);
void        cc_timeline_set_callback(CCTimeline* tl, CCTimelineCallback cb, void* user);

/* A PHASE is a contiguous span; phases are expected to tile the timeline (at most
   one current at a time). Add in order. id is yours to switch on. */
void cc_timeline_add_phase(CCTimeline* tl, uint32_t id, const char* name,
                           float start, float end);
/* A WINDOW is a tagged span that MAY overlap phases and other windows (hitbox
   active, cancel-allowed, i-frames, armor). */
void cc_timeline_add_window(CCTimeline* tl, uint32_t id, const char* name,
                            float start, float end);
/* A MARKER is an instant (spawn fx, footstep, play sound). */
void cc_timeline_add_marker(CCTimeline* tl, uint32_t id, const char* name, float t);

/* ── run ────────────────────────────────────────────────────────────────
   Advance by dt (use your sim's fixed_dt). Fires all boundaries crossed this step
   via the callback, in time order. Returns false once a non-looping timeline has
   finished. Safe to call after finish (no-op). */
bool  cc_timeline_advance(CCTimeline* tl, float dt);
void  cc_timeline_play(CCTimeline* tl);     /* (re)start from 0, playing */
void  cc_timeline_stop(CCTimeline* tl);     /* pause; keeps time */
void  cc_timeline_reset(CCTimeline* tl);    /* back to 0, stopped */
void  cc_timeline_seek(CCTimeline* tl, float t);  /* jump; does NOT fire boundaries */

/* ── query (for the per-frame "what's true right now") ──────────────────
   These are the level-state reads: the callback tells you when a boundary is
   crossed; these tell you the current state at any moment (e.g. "is the hitbox
   window open right now" during collision resolution). */
float       cc_timeline_time(const CCTimeline* tl);
bool        cc_timeline_playing(const CCTimeline* tl);
bool        cc_timeline_finished(const CCTimeline* tl);
uint32_t    cc_timeline_current_phase(const CCTimeline* tl);   /* id, or CC_TL_NONE */
const char* cc_timeline_current_phase_name(const CCTimeline* tl);
bool        cc_timeline_window_open(const CCTimeline* tl, uint32_t window_id);
bool        cc_timeline_window_open_name(const CCTimeline* tl, const char* name);

#define CC_TL_NONE 0xFFFFFFFFu

#ifdef __cplusplus
}
#endif
