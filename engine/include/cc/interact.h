#pragma once
/*
 * CCInteractable — the MECHANISM for "things the player can act on." This is a
 * game ENGINE, not a fixed menu of object types: the engine owns detection
 * (proximity + facing), focus tracking, the interaction prompt, range, a
 * re-trigger cooldown, and optional event emission. YOU own the behavior. If you
 * want a gravity-inverting rune, a mirror that writes in blood, a valve that
 * floods a room, or a terminal that runs a minigame, you define it — the engine
 * does not need to know what it is.
 *
 * THE OPEN WAY (define your own behavior):
 *   CCInteractDesc d = {0};
 *   d.range = 2.5f;
 *   d.prompt = "Read";
 *   d.state = my_rune;                         // your data, you own it
 *   d.on_interact = rune_used;                 // your function
 *   d.on_focus    = rune_glow_on;              // optional
 *   d.on_unfocus  = rune_glow_off;             // optional
 *   d.on_update   = rune_pulse;                // optional, per-frame
 *   CCInteractable* it = cc_interactable_create(world, actor, &d);
 *   ... your callbacks do WHATEVER you want. The engine just tells you WHEN.
 *
 * The loop a game runs:
 *   1. create interactables (custom, or a convenience built-in below)
 *   2. each frame:  cc_interactable_update(world, dt);   // runs on_update, anims
 *      find the focused target near the player:
 *        CCInteractable* it = cc_interactable_query(world, px,py,pz, fx,fz);
 *        if (it) show cc_interactable_prompt(it)          // "[E] Read"
 *   3. on the use key:  cc_interactable_trigger(world, it);  // fires on_interact
 *
 * CONVENIENCE BUILT-INS (optional, NOT special): cc_interactable_register with a
 * CCInteractType gives you a ready-made door/switch/lever/pickup — but these are
 * just prewired uses of the SAME generic mechanism, provided so simple cases are
 * one call. They are examples, not the engine's idea of what an interactable can
 * be. Anything they do, your own callbacks can do (and more).
 *
 * A `void* state` rides on every interactable for your data; toggle/enabled/
 * cooldown are engine-managed conveniences you may use or ignore. An optional
 * event bus publishes generic CC_EVT_* on trigger for decoupled listeners.
 */
#include "cc/actor.h"
#include "cc/ecs.h"
#include "cc/event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Convenience built-in behaviors. CC_INTERACT_CUSTOM means "no built-in — my
 * callbacks are the behavior"; the others are prewired examples (see above). */
typedef enum {
    CC_INTERACT_CUSTOM = -1, /* pure custom: only your callbacks run            */
    CC_INTERACT_USE = 0,     /* generic: fire callback only                     */
    CC_INTERACT_DOOR,        /* swings open/closed                              */
    CC_INTERACT_SWITCH,      /* toggles on/off                                  */
    CC_INTERACT_LEVER,       /* latching toggle                                 */
    CC_INTERACT_PICKUP,      /* collected then hidden                           */
} CCInteractType;

typedef struct CCInteractable CCInteractable;

/* callback fired on a successful trigger. `on` = new state (open/on/collected).
 * userdata is whatever was passed at register time. */
typedef void (*CCInteractFn)(CCInteractable* it, bool on, void* userdata);

/* ─── the generic mechanism: define your OWN interactable ─────────────────── */
/* Fired when the player triggers this interactable. Do whatever you want. */
typedef void (*CCInteractActionFn)(CCInteractable* it, void* state);
/* Fired when this becomes / stops being the focused target (for glow, hover
 * SFX, prompt changes). Optional. */
typedef void (*CCInteractFocusFn)(CCInteractable* it, void* state);
/* Fired every frame from cc_interactable_update while registered (custom
 * animation, pulsing, timers). Optional. */
typedef void (*CCInteractUpdateFn)(CCInteractable* it, float dt, void* state);

/* Descriptor for a custom interactable. Everything is optional except that a
 * meaningful interactable supplies at least on_interact. Zero-initialize it. */
typedef struct {
    float               range;        /* trigger radius (world units; default 2) */
    const char*         prompt;       /* verb shown, e.g. "Read", "Pull" (opt)   */
    void*               state;        /* YOUR data pointer, passed to callbacks   */
    float               cooldown;     /* min seconds between triggers (0 = none)  */
    CCInteractActionFn  on_interact;  /* the behavior — fires on trigger          */
    CCInteractFocusFn   on_focus;     /* became the focused target (opt)          */
    CCInteractFocusFn   on_unfocus;   /* stopped being focused (opt)              */
    CCInteractUpdateFn  on_update;    /* per-frame while registered (opt)         */
} CCInteractDesc;

/* Create a fully custom interactable on `world` around `actor`. The engine
 * handles detection/focus/prompt/range/cooldown/events; your callbacks are the
 * behavior. Returns a handle owned by the world. */
CCInteractable* cc_interactable_create(CCScene* world, CCActor actor, const CCInteractDesc* desc);

/* Get/replace your state pointer at any time. */
void* cc_interactable_state(const CCInteractable* it);
void  cc_interactable_set_state(CCInteractable* it, void* state);
/* Enable/disable: a disabled interactable is skipped by query + trigger (does
 * not lose its state). Enabled by default. */
void  cc_interactable_set_enabled(CCInteractable* it, bool enabled);
bool  cc_interactable_enabled(const CCInteractable* it);
/* A generic toggle bit the engine tracks for you (built-ins use it as open/on);
 * custom interactables may use it however they like, or ignore it. */
void  cc_interactable_set_on(CCInteractable* it, bool on);

/* ─── registry lives on the ECS world (CCScene) ──────────────────────────── */
/* Register `actor` as an interactable of `type`, triggerable within `range`
 * world units. Returns a handle (owned by the world; freed with the world via
 * cc_interactable_clear or when you stop using it). */
CCInteractable* cc_interactable_register(CCScene* world, CCActor actor,
                                         CCInteractType type, float range);
void cc_interactable_set_callback(CCInteractable* it, CCInteractFn fn, void* userdata);
void cc_interactable_set_prompt(CCInteractable* it, const char* verb); /* "Open", "Pull"… */
void cc_interactable_set_locked(CCInteractable* it, bool locked);      /* doors/switches */

/* Find the best interactable within range of point (px,py,pz). If a facing
 * direction (fx,fz) is given (non-zero), targets roughly in front are preferred.
 * Returns NULL if none in range. */
CCInteractable* cc_interactable_query(CCScene* world, float px, float py, float pz,
                                      float fx, float fz);

/* Trigger an interactable, running its built-in behavior + callback. Returns
 * true if it actually fired (false if locked, already collected, or animating). */
bool cc_interactable_trigger(CCScene* world, CCInteractable* it);

/* Advance built-in animations (door swing, switch slide). Call once per frame
 * with dt seconds. */
void cc_interactable_update(CCScene* world, float dt);

/* OPTIONAL: attach an event bus to this world's interactables. When set, every
 * successful trigger ALSO publishes on the reserved CC_EVT_* channels, with the
 * event `sender` set to the interactable's actor id:
 *   DOOR   → CC_EVT_DOOR_OPENED (i=1) or CC_EVT_DOOR_CLOSED (i=0)
 *   SWITCH → CC_EVT_SWITCH_TOGGLED (i = on?1:0)
 *   LEVER  → CC_EVT_LEVER_PULLED   (i = 1)
 *   PICKUP → CC_EVT_ITEM_PICKED_UP
 * A blocked trigger on a LOCKED interactable publishes CC_EVT_INTERACT_LOCKED.
 * This is purely additive — the per-interactable callback still fires as before,
 * and with no bus attached behavior is unchanged. Pass NULL to detach. */
void cc_interactable_set_event_bus(CCScene* world, CCEventBus* bus);

/* state queries */
bool        cc_interactable_is_on(const CCInteractable* it);      /* open / on / collected */
bool        cc_interactable_is_locked(const CCInteractable* it);
CCActor     cc_interactable_actor(const CCInteractable* it);
const char* cc_interactable_prompt(const CCInteractable* it);     /* e.g. "Open" */
CCInteractType cc_interactable_type(const CCInteractable* it);

/* free all interactables registered on this world */
void cc_interactable_clear(CCScene* world);

#ifdef __cplusplus
}
#endif
