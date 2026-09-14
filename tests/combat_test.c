/* combat_test — verifies the game-feel layer's behavior with real assertions.
 * These check the STRUCTURE of feel (what fires, timing, direction, scaling); the
 * subjective "does it feel good" is the human-in-the-loop tuning pass on top. */
#include "cc/claudecore.h"
#include "cc/combat.h"
#include "cc/event.h"
#include <stdio.h>
#include <math.h>

static int failures=0;
#define CHECK(c,msg) do{ if(!(c)){ printf("  FAIL: %s (%s:%d)\n",msg,__FILE__,__LINE__); failures++; } }while(0)
#define NEAR(a,b,eps) (fabsf((float)((a)-(b))) <= (eps))

/* count CC_EVT_DAMAGE events + capture last amount */
static int g_dmg_count=0; static float g_dmg_amt=0; static int64_t g_dmg_victim=0;
static void on_damage(const CCEvent* e, void* ud){ (void)ud; g_dmg_count++; g_dmg_amt=e->f; g_dmg_victim=e->i; }

/* a custom on_hit policy: drives its own reaction via mechanism primitives */
void test_policy(CCCombat* cb, const CCHit* h, void* user){
    (void)user;
    cc_combat_set_hitstun(cb, h->victim, 0.5f);   /* fixed 0.5s, ignoring the profile */
}

int main(void){
    CCEngineConfig cfg=cc_sandbox_config(); cfg.verbose=false;
    CCEngine* e=cc_init(&cfg);
    CCCombat* cb=cc_combat_create(e);
    CHECK(cb!=NULL,"combat create");

    const float DT=1.0f/60.0f;

    /* ── HITSTOP: a hit freezes the sim, then time resumes ──────────────── */
    CCHit h={ .attacker=1, .victim=2, .damage=10, .strength=1.0f, .dir_x=1, .dir_z=0 };
    CHECK(!cc_combat_hitstop_active(cb), "no hitstop before any hit");
    CHECK(cc_combat_begin_frame(cb,DT)==DT, "dt passes through normally when idle");

    cc_combat_register_hit(cb,&h);
    CHECK(cc_combat_hitstop_active(cb), "hitstop active right after a hit");
    /* default hitstop_base=0.06s → ~4 frozen frames at 60fps. During freeze, dt→0. */
    float first = cc_combat_begin_frame(cb,DT);
    CHECK(first==0.0f, "sim dt is frozen (0) during hitstop");
    /* burn the rest of the freeze */
    int frozen_frames=1;
    while (cc_combat_hitstop_active(cb)) { cc_combat_begin_frame(cb,DT); frozen_frames++; if(frozen_frames>100)break; }
    CHECK(frozen_frames>=3 && frozen_frames<=5, "hitstop lasts ~0.06s (3-5 frames at 60fps)");
    CHECK(cc_combat_begin_frame(cb,DT)==DT, "dt resumes to normal after hitstop ends");

    /* ── KNOCKBACK: direction points along the hit dir, magnitude scales ── */
    float kx,ky,kz;
    cc_combat_last_knockback(cb,&kx,&ky,&kz);
    CHECK(kx>0 && NEAR(kz,0,0.001f), "knockback points along +x hit direction");
    CHECK(ky>0, "knockback has upward pop for readability");
    float mag1=sqrtf(kx*kx+kz*kz);

    CCHit strong=h; strong.strength=2.0f; strong.victim=3;
    cc_combat_register_hit(cb,&strong);
    cc_combat_last_knockback(cb,&kx,&ky,&kz);
    float mag2=sqrtf(kx*kx+kz*kz);
    CHECK(mag2>mag1, "stronger hit → larger knockback");

    /* diagonal direction is normalized before scaling */
    CCHit diag=h; diag.dir_x=3; diag.dir_z=4; diag.victim=4; diag.strength=1.0f;
    /* clear hitstop so it doesn't matter here */
    while(cc_combat_hitstop_active(cb)) cc_combat_begin_frame(cb,DT);
    cc_combat_register_hit(cb,&diag);
    cc_combat_last_knockback(cb,&kx,&ky,&kz);
    CHECK(NEAR(kx/kz, 3.0f/4.0f, 0.01f), "knockback preserves the hit direction ratio (normalized)");

    /* ── HITSTUN: victim is stunned, and it decays to zero over time ────── */
    while(cc_combat_hitstop_active(cb)) cc_combat_begin_frame(cb,DT);
    CCHit hs={ .attacker=1, .victim=42, .damage=5, .strength=1.0f, .dir_x=1 };
    cc_combat_register_hit(cb,&hs);
    CHECK(cc_combat_in_hitstun(cb,42), "victim in hitstun right after hit");
    float stun0=cc_combat_hitstun_remaining(cb,42);
    CHECK(stun0>0, "hitstun has positive duration");
    /* clear the hitstop from THIS hit first (freeze doesn't decay stun), then decay */
    while(cc_combat_hitstop_active(cb)) cc_combat_begin_frame(cb,DT);
    float prev=cc_combat_hitstun_remaining(cb,42);
    cc_combat_begin_frame(cb,DT);
    CHECK(cc_combat_hitstun_remaining(cb,42) < prev, "hitstun decays over frames");
    int guard=0; while(cc_combat_in_hitstun(cb,42) && guard++<200) cc_combat_begin_frame(cb,DT);
    CHECK(!cc_combat_in_hitstun(cb,42), "hitstun ends after its duration");
    CHECK(!cc_combat_in_hitstun(cb,999), "an un-hit entity is never in hitstun");

    /* ── TUNABILITY: the profile changes the feel ───────────────────────── */
    CCFeelProfile* p=cc_combat_profile(cb);
    p->hitstop_base=0.0f;   /* disable hitstop */
    CCHit noh={ .attacker=1,.victim=7,.damage=1,.strength=1.0f,.dir_x=1 };
    cc_combat_register_hit(cb,&noh);
    CHECK(!cc_combat_hitstop_active(cb), "profile is tunable: hitstop_base=0 → no freeze");

    /* ── DAMAGE EVENT: emitted on a supplied bus ────────────────────────── */
    CCEventBus* bus=cc_event_bus_create();
    cc_event_subscribe(bus, CC_EVT_DAMAGE, on_damage, NULL);
    cc_combat_set_event_bus(cb,bus);
    CCHit dh={ .attacker=1,.victim=88,.damage=17.5f,.strength=1.0f,.dir_x=1 };
    cc_combat_register_hit(cb,&dh);
    cc_event_bus_update(bus);   /* flush FIFO */
    CHECK(g_dmg_count==1, "CC_EVT_DAMAGE emitted once per hit");
    CHECK(NEAR(g_dmg_amt,17.5f,0.001f), "damage amount carried in event");
    CHECK(g_dmg_victim==88, "victim id carried in event");

    /* ── MODULARITY: nothing is mandatory ───────────────────────────────── */
    /* (a) enable_none → a hit fires NO built-in reaction */
    { CCCombat* c2=cc_combat_create(e);
      cc_combat_set_enable(c2, cc_feel_enable_none());
      CCHit q={ .attacker=1,.victim=5,.damage=9,.strength=2.0f,.dir_x=1 };
      cc_combat_register_hit(c2,&q);
      CHECK(!cc_combat_hitstop_active(c2), "enable_none: no hitstop");
      CHECK(!cc_combat_in_hitstun(c2,5),   "enable_none: no hitstun");
      float x,y,z; cc_combat_last_knockback(c2,&x,&y,&z);
      CHECK(x==0&&y==0&&z==0,               "enable_none: no knockback");
      cc_combat_destroy(c2); }

    /* (b) realistic-sim case: knockback WITHOUT the upward pop */
    { CCCombat* c3=cc_combat_create(e);
      CCFeelEnable en=cc_feel_enable_all(); en.knockback_up=false; en.hitstop=false;
      cc_combat_set_enable(c3,en);
      CCHit q={ .attacker=1,.victim=6,.damage=9,.strength=1.0f,.dir_x=1,.dir_z=0 };
      cc_combat_register_hit(c3,&q);
      float x,y,z; cc_combat_last_knockback(c3,&x,&y,&z);
      CHECK(x>0 && y==0.0f, "knockback_up=off: horizontal knockback, no vertical pop");
      CHECK(!cc_combat_hitstop_active(c3), "hitstop=off: realistic sim never freezes");
      cc_combat_destroy(c3); }

    /* (c) custom policy: enable_none + on_hit drives the ENTIRE reaction */
    { CCCombat* c4=cc_combat_create(e);
      cc_combat_set_enable(c4, cc_feel_enable_none());
      /* policy: apply a fixed custom hitstun of 0.5s regardless of profile */
      cc_combat_set_on_hit(c4, NULL, NULL);   /* set below via a real fn */
      extern void test_policy(CCCombat*, const CCHit*, void*);
      cc_combat_set_on_hit(c4, test_policy, NULL);
      CCHit q={ .attacker=1,.victim=77,.damage=3,.strength=1.0f,.dir_x=1 };
      cc_combat_register_hit(c4,&q);
      CHECK(cc_combat_in_hitstun(c4,77), "custom on_hit policy drove its own hitstun");
      CHECK(NEAR(cc_combat_hitstun_remaining(c4,77),0.5f,0.001f), "policy set exactly 0.5s stun");
      cc_combat_destroy(c4); }

    cc_event_bus_destroy(bus);
    cc_combat_destroy(cb);
    cc_shutdown(e);

    if(failures==0) printf("COMBAT TEST: all checks passed (hitstop freeze+duration, knockback dir+scale+normalize, hitstun decay, tunable profile, damage event)\n");
    else printf("COMBAT TEST: %d FAILURES\n", failures);
    return failures?1:0;
}
