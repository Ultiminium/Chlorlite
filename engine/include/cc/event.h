#pragma once
/*
 * CCEventBus — a decoupled publish/subscribe message bus. Domain-general
 * connective tissue: it lets one system announce that something happened
 * ("door opened", "player took 12 damage", "item picked up", "dialogue node
 * entered") without knowing or naming who reacts. Any number of listeners
 * subscribe to a channel and receive events; publishers never hold a pointer
 * to a subscriber. This replaces the engine's scattered single-callback hooks
 * (each subsystem otherwise invents its own one-slot "on_X" function pointer)
 * with one many-to-many primitive that any game can build its logic on.
 *
 * A CHANNEL is just a small integer id (an event "type"). You reserve the low
 * range for your own game events; the engine ships a set of well-known channels
 * (CC_EVT_*) that its own subsystems publish on, so gameplay can react to engine
 * activity with zero coupling.
 *
 * An EVENT carries: its channel, a 64-bit `sender` tag (caller's choice — an
 * actor id, a pointer cast to uintptr_t, or 0), two general-purpose scalars
 * (`i` int64 and `f` float — enough for "amount", "index", "id", "value"
 * without allocating), and an opaque `data`/`data_size` blob for anything
 * larger. The blob is COPIED into the bus at publish time, so the publisher's
 * pointer need not outlive the call.
 *
 *   CCEventBus* bus = cc_event_bus_create();
 *   // subscribe: react to any door opening
 *   CCListenerId l = cc_event_subscribe(bus, CC_EVT_DOOR_OPENED, on_door, ui);
 *   // publish: a door announces itself (sender = door actor id, i = door index)
 *   cc_event_emit_i(bus, CC_EVT_DOOR_OPENED, door_actor, door_index);
 *   ... each frame: cc_event_bus_update(bus);   // flushes the queued events
 *   ...
 *   cc_event_unsubscribe(bus, l);
 *
 * DELIVERY MODEL. Publishing ENQUEUES by default: events are buffered and then
 * delivered in FIFO order when you call cc_event_bus_update() once per frame.
 * Deferred delivery is what makes the bus safe — a listener may publish more
 * events, subscribe, or unsubscribe (even itself) while events are being
 * dispatched, without corrupting iteration or re-entering. Events published
 * during dispatch are delivered on the NEXT update, never recursively. If you
 * need synchronous fan-out, cc_event_emit_now() delivers immediately to all
 * current listeners (still safe against subscribe/unsubscribe during dispatch).
 *
 * Listeners are handle-identified (CCListenerId, 0 = invalid) so they can be
 * cancelled. Everything is pure CPU, allocation-light, and headless-safe.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCEventBus CCEventBus;
typedef uint32_t CCListenerId;   /* 0 = invalid */

/* An event channel is any uint32. Games use their own ids from CC_EVT_USER up.
 * The engine reserves [0, CC_EVT_USER) for well-known channels its subsystems
 * publish on — subscribe to these to react to engine activity for free. */
enum {
    CC_EVT_NONE = 0,

    /* interactables (cc/interact.h) */
    CC_EVT_DOOR_OPENED,      /* sender=actor, i=is_open(1/0)                    */
    CC_EVT_DOOR_CLOSED,      /* sender=actor                                    */
    CC_EVT_SWITCH_TOGGLED,   /* sender=actor, i=is_on(1/0)                      */
    CC_EVT_LEVER_PULLED,     /* sender=actor, i=is_on(1/0)                      */
    CC_EVT_ITEM_PICKED_UP,   /* sender=actor                                    */
    CC_EVT_INTERACT_LOCKED,  /* sender=actor — tried to use a locked thing      */
    CC_EVT_INTERACT_USED,    /* sender=actor, i=on?1:0 — a custom interactable fired */

    /* gameplay-ish generic (published by games, defined here for a shared vocab)*/
    CC_EVT_DAMAGE,           /* sender=attacker, i=target id, f=amount          */
    CC_EVT_HEAL,             /* sender=source,   i=target id, f=amount          */
    CC_EVT_DEATH,            /* sender=entity id                                */
    CC_EVT_SPAWN,            /* sender=entity id                                */

    /* physics (cc/physics.h) — a neutral collision fact; games map it to damage */
    CC_EVT_CONTACT,          /* sender=body a, i=body b, f=impact speed (|v·n|)  */

    /* audio input (cc/mic.h) */
    CC_EVT_MIC_LEVEL,        /* i=voice_active(1/0), f=loudness 0..1            */

    /* spatial sound (cc/soundfield.h) — a positional noise for hearing AI */
    CC_EVT_SOUND,            /* sender packs XZ (see cc/soundfield.h), f=loudness */

    /* dialogue (cc/dialogue.h) */
    CC_EVT_DIALOGUE_STARTED, /* sender=convo id                                 */
    CC_EVT_DIALOGUE_NODE,    /* sender=convo id, i=node index                   */
    CC_EVT_DIALOGUE_ENDED,   /* sender=convo id                                 */

    CC_EVT_USER = 1024       /* first id free for game-defined channels          */
};

/* The event delivered to listeners. Fields you don't use stay 0. */
typedef struct CCEvent {
    uint32_t    channel;     /* which channel this was published on              */
    uint64_t    sender;      /* caller-chosen tag (actor id, ptr, 0…)            */
    int64_t     i;           /* general scalar: amount, index, id…              */
    float       f;           /* general scalar: value, fraction…                */
    const void* data;        /* optional payload (bus-owned copy, valid during   */
    uint32_t    data_size;   /*   the callback only); NULL/0 if none             */
} CCEvent;

/* A listener. `e` is valid only for the duration of the call. */
typedef void (*CCEventFn)(const CCEvent* e, void* userdata);

/* ─── lifecycle ─────────────────────────────────────────────────────────── */
CCEventBus* cc_event_bus_create(void);
void        cc_event_bus_destroy(CCEventBus* bus);
/* Deliver all events queued since the last update, in FIFO order. Call once per
 * frame. Events published by listeners during this call are queued for NEXT
 * frame (never recursed). Returns the number of events delivered. */
uint32_t    cc_event_bus_update(CCEventBus* bus);
/* Drop everything: all listeners AND any queued-but-undelivered events. */
void        cc_event_bus_clear(CCEventBus* bus);
/* Drop only queued events, keep subscriptions. */
void        cc_event_bus_drain(CCEventBus* bus);

/* ─── subscribing ───────────────────────────────────────────────────────── */
/* Listen on one channel. Returns a handle (0 only if bus is NULL). The same fn
 * may subscribe to several channels (distinct handles). */
CCListenerId cc_event_subscribe(CCEventBus* bus, uint32_t channel,
                                CCEventFn fn, void* userdata);
/* Listen on EVERY channel (a monitor/logger). */
CCListenerId cc_event_subscribe_any(CCEventBus* bus, CCEventFn fn, void* userdata);
/* Cancel a subscription. Safe to call from inside a listener (including on
 * itself). Unknown/stale ids are ignored. */
void         cc_event_unsubscribe(CCEventBus* bus, CCListenerId id);
/* How many live listeners (for tests/introspection). */
uint32_t     cc_event_listener_count(const CCEventBus* bus);
/* How many events are queued awaiting the next update. */
uint32_t     cc_event_queued_count(const CCEventBus* bus);

/* ─── publishing (deferred: delivered on next cc_event_bus_update) ──────────
 * Return true if the event was accepted (false only if bus is NULL or the queue
 * is exhausted). The *_i / *_f / *_if helpers are conveniences for the common
 * "just a scalar" cases so callers don't build a CCEvent by hand. */
bool cc_event_emit    (CCEventBus* bus, const CCEvent* e);
bool cc_event_emit_i  (CCEventBus* bus, uint32_t channel, uint64_t sender, int64_t i);
bool cc_event_emit_f  (CCEventBus* bus, uint32_t channel, uint64_t sender, float f);
bool cc_event_emit_if (CCEventBus* bus, uint32_t channel, uint64_t sender, int64_t i, float f);
/* With a payload blob: the bus COPIES `data_size` bytes, so `data` may be a
 * stack buffer. Pass NULL/0 for no payload. */
bool cc_event_emit_data(CCEventBus* bus, uint32_t channel, uint64_t sender,
                        int64_t i, float f, const void* data, uint32_t data_size);

/* ─── publishing (synchronous: delivered before returning) ──────────────────
 * Fan out to all CURRENT listeners immediately. Safe against listeners that
 * subscribe/unsubscribe during the call. Use when a reaction must happen this
 * instant (e.g. a veto/aggregation pattern); prefer the deferred emit otherwise.
 * Returns the number of listeners notified. */
uint32_t cc_event_emit_now(CCEventBus* bus, const CCEvent* e);

#ifdef __cplusplus
}
#endif
