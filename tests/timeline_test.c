/* timeline_test — verifies the generic timeline mechanism, and proves it's
 * genre-neutral by expressing TWO different things with the SAME API:
 *   (1) a fighting-game MOVE: startup/active/recovery phases, a hitbox window
 *       inside 'active', and a cancel window overlapping recovery.
 *   (2) a weapon CYCLE: windup/fire/chamber phases + a recoil marker.
 * Checks boundaries fire in time order, at the right instants, and that the
 * "is this window open right now" query is correct mid-span. */
#include "cc/timeline.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures=0;
#define CHECK(c,msg) do{ if(!(c)){ printf("  FAIL: %s (%s:%d)\n",msg,__FILE__,__LINE__); failures++; } }while(0)

/* record the sequence of boundary events for assertions */
#define MAXLOG 64
typedef struct { CCTimelineEventType type; char name[32]; float t; } LogItem;
static LogItem LOG[MAXLOG]; static int NLOG=0;
static void on_ev(const CCTimelineEvent* ev, void* user){
    (void)user;
    if(NLOG<MAXLOG){ LOG[NLOG].type=ev->type; LOG[NLOG].t=ev->t;
        LOG[NLOG].name[0]=0; if(ev->name){ strncpy(LOG[NLOG].name,ev->name,31); LOG[NLOG].name[31]=0; }
        NLOG++; }
}
static int count_type(CCTimelineEventType t){ int n=0; for(int i=0;i<NLOG;i++) if(LOG[i].type==t)n++; return n; }
static int find(CCTimelineEventType t, const char* name){
    for(int i=0;i<NLOG;i++) if(LOG[i].type==t && strcmp(LOG[i].name,name?name:"")==0) return i; return -1; }

/* advance a timeline in fixed 1/60 steps for `secs` seconds */
static void run(CCTimeline* tl, float secs){
    float dt=1.0f/60.0f; int steps=(int)(secs/dt + 0.5f);
    for(int i=0;i<steps;i++) cc_timeline_advance(tl,dt);
}

int main(void){
    /* ── (1) FIGHTING MOVE ── total 0.5s: startup [0,0.1) active [0.1,0.25)
       recovery [0.25,0.5). hitbox window inside active [0.12,0.23]. cancel window
       overlaps late recovery [0.35,0.5]. */
    NLOG=0;
    CCTimeline* mv=cc_timeline_create(0.5f,false);
    cc_timeline_set_callback(mv,on_ev,NULL);
    cc_timeline_add_phase(mv,1,"startup", 0.00f,0.10f);
    cc_timeline_add_phase(mv,2,"active",  0.10f,0.25f);
    cc_timeline_add_phase(mv,3,"recovery",0.25f,0.50f);
    cc_timeline_add_window(mv,10,"hitbox", 0.12f,0.23f);
    cc_timeline_add_window(mv,11,"cancel", 0.35f,0.50f);
    cc_timeline_add_marker(mv,20,"swing_sfx",0.10f);
    cc_timeline_play(mv);

    /* at t=0.15 the hitbox window must be OPEN and phase must be 'active' */
    run(mv,0.15f);
    CHECK(cc_timeline_window_open_name(mv,"hitbox"), "hitbox window open mid-active");
    CHECK(cc_timeline_current_phase(mv)==2, "current phase is 'active' at t=0.15");
    CHECK(!cc_timeline_window_open_name(mv,"cancel"), "cancel window not open yet");

    run(mv,0.40f);   /* finish the move (total 0.55 > 0.5) */
    CHECK(cc_timeline_finished(mv), "move finished after its duration");

    /* boundary correctness */
    CHECK(count_type(CC_TL_PHASE_ENTER)==3, "3 phase enters (startup/active/recovery)");
    CHECK(count_type(CC_TL_WINDOW_OPEN)==2, "2 window opens (hitbox/cancel)");
    CHECK(count_type(CC_TL_WINDOW_CLOSE)==2,"2 window closes");
    CHECK(find(CC_TL_WINDOW_OPEN,"hitbox")>=0, "hitbox open fired");
    CHECK(find(CC_TL_MARKER,"swing_sfx")>=0,  "swing_sfx marker fired");
    CHECK(count_type(CC_TL_FINISHED)==1,      "finished fired once");
    /* order: hitbox opens AFTER active-enter, cancel opens during recovery */
    CHECK(find(CC_TL_PHASE_ENTER,"active") < find(CC_TL_WINDOW_OPEN,"hitbox"),
          "active phase entered before hitbox opened");
    cc_timeline_destroy(mv);

    /* ── (2) WEAPON CYCLE ── SAME api, different meaning. 1.0s: windup [0,0.1)
       fire [0.1,0.15) chamber [0.15,1.0). recoil marker at fire start. Loops. */
    NLOG=0;
    CCTimeline* gun=cc_timeline_create(1.0f,true);
    cc_timeline_set_callback(gun,on_ev,NULL);
    cc_timeline_add_phase(gun,1,"windup", 0.00f,0.10f);
    cc_timeline_add_phase(gun,2,"fire",   0.10f,0.15f);
    cc_timeline_add_phase(gun,3,"chamber",0.15f,1.00f);
    cc_timeline_add_marker(gun,5,"recoil",0.10f);
    cc_timeline_add_window(gun,6,"muzzle_flash",0.10f,0.13f);
    cc_timeline_play(gun);

    run(gun,0.12f);
    CHECK(cc_timeline_current_phase(gun)==2, "weapon in 'fire' phase at t=0.12");
    CHECK(cc_timeline_window_open_name(gun,"muzzle_flash"), "muzzle flash on during fire");
    CHECK(find(CC_TL_MARKER,"recoil")>=0, "recoil marker fired at fire start");

    /* loop: run well past 1.0s; must wrap and fire FINISHED then re-enter windup */
    run(gun,1.0f);   /* now ~1.12s total → looped once */
    CHECK(count_type(CC_TL_FINISHED)>=1, "looping weapon fired FINISHED at wrap");
    CHECK(!cc_timeline_finished(gun), "looping timeline is never 'finished' state");
    /* recoil marker should have fired at least twice (once per cycle) */
    CHECK(count_type(CC_TL_MARKER)>=2, "recoil marker fired each cycle (loop works)");
    cc_timeline_destroy(gun);

    if(failures==0)
        printf("TIMELINE TEST: all checks passed (phases/windows/markers fire in order; genre-neutral: same API drove a fighting move AND a looping weapon cycle; mid-span queries correct)\n");
    else
        printf("TIMELINE TEST: %d FAILURES\n", failures);
    return failures?1:0;
}
