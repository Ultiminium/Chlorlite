/* inputrec_test — input recording & deterministic playback. Builds a recording
 * of key/mouse events at known times, round-trips it through a .ccrec file, then
 * replays it into a fresh engine and asserts the injected events reproduce the
 * expected input state at the right playback times. */
#include "cc/claudecore.h"
#include "cc/inputrec.h"
#include "cc/input.h"
#include <qwerty/qwerty.h>
#include <stdio.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)

static QEvent key_ev(QEventType type, QKey k){
    QEvent e; memset(&e,0,sizeof(e));
    e.type=type; e.key.key=k; return e;
}

int main(void){
    /* ── build a recording by capturing scripted events at set times ── */
    CCInputRec* rec=cc_inputrec_create();
    cc_inputrec_start_recording(rec);

    /* t=0.0: press SPACE */
    QEvent e1=key_ev(QEVENT_KEY_DOWN, QKEY_SPACE);
    cc_inputrec_capture(rec, 0.0, &e1, 1);
    /* t=0.5: press A */
    QEvent e2=key_ev(QEVENT_KEY_DOWN, QKEY_A);
    cc_inputrec_capture(rec, 0.5, &e2, 1);
    /* t=1.0: release SPACE */
    QEvent e3=key_ev(QEVENT_KEY_UP, QKEY_SPACE);
    cc_inputrec_capture(rec, 1.0, &e3, 1);
    /* t=1.5: release A */
    QEvent e4=key_ev(QEVENT_KEY_UP, QKEY_A);
    cc_inputrec_capture(rec, 1.5, &e4, 1);

    CHECK(cc_inputrec_event_count(rec)==4,"4 events captured");
    CHECK(cc_inputrec_duration(rec)>1.49 && cc_inputrec_duration(rec)<1.51,"duration ~1.5s");

    /* ── save + load round-trip ── */
    CHECK(cc_inputrec_save(rec,"/tmp/demo.ccrec"),"save");
    CCInputRec* loaded=cc_inputrec_load("/tmp/demo.ccrec");
    CHECK(loaded!=NULL,"load");
    CHECK(loaded && cc_inputrec_event_count(loaded)==4,"loaded event count matches");
    cc_inputrec_destroy(rec);

    /* ── replay into a fresh engine ── */
    CCEngineConfig cfg=cc_sandbox_config(); cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e){ printf("init fail\n"); return 1; }
    cc_inputrec_start_playback(loaded);

    /* helper: advance playback to time t, then tick to process injected events */
    /* at t=0.1: SPACE down, A up */
    cc_inputrec_play(loaded, e, 0.1); cc_tick(e,0.016);
    CHECK(cc_key_down(e,QKEY_SPACE),"t=0.1 SPACE held");
    CHECK(!cc_key_down(e,QKEY_A),"t=0.1 A not yet");
    CHECK(!cc_inputrec_finished(loaded),"not finished at 0.1");

    /* at t=0.6: A also down */
    cc_inputrec_play(loaded, e, 0.6); cc_tick(e,0.016);
    CHECK(cc_key_down(e,QKEY_SPACE),"t=0.6 SPACE still held");
    CHECK(cc_key_down(e,QKEY_A),"t=0.6 A now held");

    /* at t=1.1: SPACE released, A still down */
    cc_inputrec_play(loaded, e, 1.1); cc_tick(e,0.016);
    CHECK(!cc_key_down(e,QKEY_SPACE),"t=1.1 SPACE released");
    CHECK(cc_key_down(e,QKEY_A),"t=1.1 A still held");

    /* at t=1.6: both released, playback finished */
    cc_inputrec_play(loaded, e, 1.6); cc_tick(e,0.016);
    CHECK(!cc_key_down(e,QKEY_A),"t=1.6 A released");
    CHECK(cc_inputrec_finished(loaded),"playback finished at end");

    cc_inputrec_destroy(loaded);
    cc_shutdown(e);
    if(fails){ printf("INPUTREC TEST: %d FAILURE(S)\n",fails); return 2; }
    printf("INPUTREC TEST: all checks passed (record, save/load, deterministic replay)\n");
    return 0;
}
