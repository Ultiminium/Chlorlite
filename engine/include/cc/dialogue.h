#pragma once
/*
 * CCDialogue — branching conversations / scripted text events.
 *
 * A dialogue is a graph of nodes. Each node has a speaker + a line of text, and
 * then either:
 *   - a list of player CHOICES, each with text and a target node, or
 *   - an auto-advance to a "next" node (linear line), or
 *   - nothing (end of conversation).
 * Nodes can carry an ACTION string (e.g. "set quest.intro.done" / "give key") the
 * game interprets via a callback, and choices can be GATED by a required flag so
 * options appear only when unlocked. Loaded from a hand-editable text .ccdlg
 * file (same spirit as .ccmodel/.cclist/.ccsave). A runtime CCDialogueRunner
 * walks the graph: the game reads the current line + choices, and calls advance
 * or choose to move on.
 *
 *   CCDialogue* d = cc_dialogue_load("intro.ccdlg");
 *   CCDialogueRunner* r = cc_dialogue_start(d, "root");
 *   while (!cc_dialogue_finished(r)) {
 *       printf("%s: %s\n", cc_dialogue_speaker(r), cc_dialogue_text(r));
 *       int n = cc_dialogue_choice_count(r);
 *       if (n == 0) cc_dialogue_advance(r);          // linear line
 *       else        cc_dialogue_choose(r, pick);     // player picked option `pick`
 *   }
 *
 * Flag gating + actions use a callback the game supplies (typically backed by a
 * CCSaveState): is a flag set? and please apply this action string.
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCDialogue       CCDialogue;
typedef struct CCDialogueRunner CCDialogueRunner;

/* Game-supplied hooks. flag_query returns whether a named flag/condition holds
 * (for choice gating). action_apply is called when a node's action fires.
 * Either may be NULL (then gating passes and actions are ignored). */
typedef struct CCDialogueHooks {
    bool (*flag_query)(const char* flag, void* ud);
    void (*action_apply)(const char* action, void* ud);
    void* userdata;
} CCDialogueHooks;

/* ─── load / free ────────────────────────────────────────────────────────
 * Text .ccdlg grammar ('#' comments; blocks separated by 'node <id>'):
 *   ccdlg 1
 *   node root
 *     speaker Old Man
 *     text Welcome, traveler. You shouldn't be here.
 *     action set met_old_man          # optional, fires when node is entered
 *     choice Why not? -> warning       # choice <text> -> <target node id>
 *     choice [has_key] Open the gate -> gate   # [flag] gates the choice
 *     goto warning                     # OR: auto-advance (no choices)
 *   node warning
 *     speaker Old Man
 *     text The dark takes those who linger.
 *     end                              # explicit end (no next / choices)
 */
#include "cc/event.h"

CCDialogue* cc_dialogue_load(const char* path);
CCDialogue* cc_dialogue_parse(const char* text);   /* from an in-memory string */
void        cc_dialogue_free(CCDialogue* d);
uint32_t    cc_dialogue_node_count(const CCDialogue* d);

/* ─── runtime ────────────────────────────────────────────────────────────── */
CCDialogueRunner* cc_dialogue_start(CCDialogue* d, const char* start_node);
void              cc_dialogue_set_hooks(CCDialogueRunner* r, const CCDialogueHooks* hooks);
/* OPTIONAL: attach an event bus. When set, the runner publishes (deferred) on
 * the reserved channels with `sender` = your `convo_id`:
 *   CC_EVT_DIALOGUE_STARTED once, CC_EVT_DIALOGUE_NODE (i=node index) on each
 *   node entered, CC_EVT_DIALOGUE_ENDED when the conversation finishes.
 * Purely additive; hooks still fire as before. Call right after start (the
 * STARTED + first NODE are emitted at attach time so nothing is missed). */
void              cc_dialogue_set_event_bus(CCDialogueRunner* r, CCEventBus* bus, uint64_t convo_id);
void              cc_dialogue_free_runner(CCDialogueRunner* r);

/* current node */
const char* cc_dialogue_speaker(const CCDialogueRunner* r);
const char* cc_dialogue_text(const CCDialogueRunner* r);
bool        cc_dialogue_finished(const CCDialogueRunner* r);

/* choices at the current node (only those whose gate passes are visible) */
uint32_t    cc_dialogue_choice_count(const CCDialogueRunner* r);
const char* cc_dialogue_choice_text(const CCDialogueRunner* r, uint32_t i);

/* advance a linear node (no choices) to its goto/next; no-op if there are
 * choices. Returns false if the conversation ended. */
bool cc_dialogue_advance(CCDialogueRunner* r);
/* pick visible choice `i`, moving to its target. Returns false if invalid. */
bool cc_dialogue_choose(CCDialogueRunner* r, uint32_t i);

#ifdef __cplusplus
}
#endif
