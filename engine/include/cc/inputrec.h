#pragma once
/*
 * CCInputRec — input recording & deterministic playback. Capture a stream of
 * input events with timestamps, then replay them exactly — for demos, automated
 * playtests, reproducible bug reports, and regression tests. Built on the
 * existing QEvent + cc_input_inject plumbing.
 *
 * RECORD (each frame, after polling input):
 *   CCInputRec* rec = cc_inputrec_create();
 *   cc_inputrec_start_recording(rec);
 *   ... each frame:
 *       QEvent evs[64]; uint32_t n = cc_input_poll(eng, evs, 64);
 *       cc_inputrec_capture(rec, t_seconds, evs, n);   // feed polled events
 *   cc_inputrec_save(rec, "demo.ccrec");
 *
 * PLAYBACK (each frame):
 *   CCInputRec* rec = cc_inputrec_load("demo.ccrec");
 *   cc_inputrec_start_playback(rec);
 *   ... each frame:
 *       cc_inputrec_play(rec, eng, t_seconds);   // re-injects due events
 *       if (cc_inputrec_finished(rec)) ...
 *
 * The recording stores events with their capture time (seconds from record
 * start), so playback re-injects each event when playback time reaches it —
 * frame-rate independent. Binary .ccrec format (QEvent is POD).
 */
#include "cc/claudecore.h"
#include <qwerty/qwerty.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCInputRec CCInputRec;

CCInputRec* cc_inputrec_create(void);
void        cc_inputrec_destroy(CCInputRec* rec);
void        cc_inputrec_clear(CCInputRec* rec);

/* ─── recording ─── */
void     cc_inputrec_start_recording(CCInputRec* rec);
/* feed the events polled this frame, tagged with the current time (seconds). */
void     cc_inputrec_capture(CCInputRec* rec, double t_seconds,
                             const QEvent* events, uint32_t count);
uint32_t cc_inputrec_event_count(const CCInputRec* rec);
double   cc_inputrec_duration(const CCInputRec* rec);   /* seconds */

/* ─── playback ─── */
void cc_inputrec_start_playback(CCInputRec* rec);
/* re-inject every recorded event whose timestamp is <= t_seconds since the last
 * play() call. Call once per frame with the current playback time. */
void cc_inputrec_play(CCInputRec* rec, CCEngine* eng, double t_seconds);
bool cc_inputrec_finished(const CCInputRec* rec);   /* all events played? */

/* ─── file I/O (binary .ccrec) ─── */
bool        cc_inputrec_save(const CCInputRec* rec, const char* path);
CCInputRec* cc_inputrec_load(const char* path);

#ifdef __cplusplus
}
#endif
