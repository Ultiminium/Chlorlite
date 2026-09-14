#pragma once
/*
 * CCTweens — value tweening + timers/scheduling. Domain-general "juice" and
 * logic-timing infrastructure used by UI, camera moves, fades, spawn cadence,
 * cooldowns, and sequenced actions.
 *
 * A TWEEN animates a float from `from` to `to` over `duration` seconds along an
 * easing curve, calling a setter each frame with the current value (and an
 * optional on-complete). A TIMER calls a callback after a delay (once) or every
 * interval (repeating). One CCTweens manager owns them; call cc_tweens_update(dt)
 * once per frame.
 *
 *   CCTweens* tw = cc_tweens_create();
 *   // fade a value 0->1 over 0.5s, ease-out, writing into `alpha`
 *   cc_tween_to(tw, &alpha, 0.0f, 1.0f, 0.5f, CC_EASE_OUT, NULL, NULL);
 *   // spawn every 2 seconds
 *   cc_timer_every(tw, 2.0f, spawn_cb, userdata);
 *   ... each frame: cc_tweens_update(tw, dt);
 *
 * Tweens/timers are handle-identified so they can be cancelled. Completed
 * one-shots are auto-removed.
 */
#include <stdint.h>
#include <stdbool.h>
#include "cc/camera.h"   /* reuse the existing CCCamEase easing enum */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCTweens CCTweens;
typedef uint32_t CCTweenId;   /* 0 = invalid */
typedef uint32_t CCTimerId;   /* 0 = invalid */

/* Easing shares the engine's single ease vocabulary (cc/camera.h CCCamEase):
 * CC_EASE_LINEAR, CC_EASE_SMOOTH (smoothstep), CC_EASE_IN, CC_EASE_OUT,
 * CC_EASE_IN_OUT, CC_EASE_CUBIC. */
typedef CCCamEase CCEaseType;

/* optional callbacks */
typedef void (*CCTweenSetFn) (float value, void* userdata);  /* if target ptr is NULL */
typedef void (*CCTweenDoneFn)(void* userdata);
typedef void (*CCTimerFn)    (void* userdata);

CCTweens* cc_tweens_create(void);
void      cc_tweens_destroy(CCTweens* tw);
void      cc_tweens_update(CCTweens* tw, float dt);
void      cc_tweens_clear(CCTweens* tw);            /* cancel everything */
uint32_t  cc_tweens_active_count(const CCTweens* tw); /* tweens + timers alive */

/* ─── tweens ────────────────────────────────────────────────────────────
 * Animate *target from `from` to `to`. If `target` is NULL you must instead use
 * cc_tween_value with a setter callback. `on_done` (nullable) fires at the end. */
CCTweenId cc_tween_to(CCTweens* tw, float* target, float from, float to,
                      float duration, CCEaseType ease,
                      CCTweenDoneFn on_done, void* userdata);
/* Same, but delivers the value via a setter callback each frame (no target ptr). */
CCTweenId cc_tween_value(CCTweens* tw, float from, float to, float duration,
                         CCEaseType ease, CCTweenSetFn set, CCTweenDoneFn on_done, void* userdata);
void      cc_tween_cancel(CCTweens* tw, CCTweenId id);
bool      cc_tween_active(const CCTweens* tw, CCTweenId id);

/* ─── timers ────────────────────────────────────────────────────────────── */
/* Fire `fn` once after `delay` seconds. */
CCTimerId cc_timer_after(CCTweens* tw, float delay, CCTimerFn fn, void* userdata);
/* Fire `fn` every `interval` seconds, forever (until cancelled). */
CCTimerId cc_timer_every(CCTweens* tw, float interval, CCTimerFn fn, void* userdata);
void      cc_timer_cancel(CCTweens* tw, CCTimerId id);
bool      cc_timer_active(const CCTweens* tw, CCTimerId id);

/* evaluate an easing curve directly (t in 0..1) — handy standalone */
float cc_ease(CCEaseType type, float t);

#ifdef __cplusplus
}
#endif
