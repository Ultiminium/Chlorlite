/* loop_test — verifies the TPS/FPS fixed-timestep accumulator logic in isolation.
 *
 * The real cc_run loop is driven by wall-clock time and a live window, which can't
 * run deterministically headless. So this test reimplements the SAME accumulator
 * math cc_run uses and asserts the properties that matter:
 *   - sim advances at a FIXED rate independent of frame rate (N ticks for T seconds)
 *   - a slow frame produces multiple catch-up ticks; a fast frame may produce zero
 *   - the interpolation alpha stays in [0,1)
 *   - the step cap prevents unbounded catch-up (spiral of death)
 *
 * This guards the architecture: simulation time == real time regardless of FPS.
 * Prints "LOOP TEST: all checks passed" / returns 0.
 */
#include <stdio.h>
#include <math.h>

static int failures = 0;
#define CHECK(c,msg) do{ if(!(c)){ printf("  FAIL: %s (%s:%d)\n",msg,__FILE__,__LINE__); failures++; } }while(0)

/* mirrors cc_run's accumulator exactly */
typedef struct { double fixed_dt, accumulator; long total_ticks; } Loop;
static int loop_frame(Loop* L, double frame_dt) {
    L->accumulator += frame_dt;
    int steps = 0;
    while (L->accumulator >= L->fixed_dt) {
        L->total_ticks++;
        L->accumulator -= L->fixed_dt;
        if (++steps >= 8) { L->accumulator = 0; break; }   /* spiral-of-death clamp */
    }
    return steps;
}
static double loop_alpha(const Loop* L){ return L->accumulator / L->fixed_dt; }

int main(void) {
    const double TPS = 60.0, FDT = 1.0/TPS;

    /* 1. FIXED RATE regardless of FPS: simulate 2 seconds of real time at three
       very different frame rates; the sim must tick ~120 times in every case. */
    double fps_rates[] = { 30.0, 60.0, 144.0, 1000.0 };
    for (int r=0; r<4; r++) {
        Loop L = { .fixed_dt=FDT, .accumulator=0, .total_ticks=0 };
        double fdt = 1.0/fps_rates[r];
        double t=0; while (t < 2.0) { loop_frame(&L, fdt); t += fdt; }
        /* allow ±1 tick for boundary rounding */
        char m[96]; snprintf(m,sizeof(m),"~120 ticks in 2s at %gfps (got %ld)", fps_rates[r], L.total_ticks);
        CHECK(L.total_ticks >= 119 && L.total_ticks <= 121, m);
    }

    /* 2. A SLOW frame produces multiple catch-up ticks. One 0.1s frame = 6 ticks. */
    { Loop L={.fixed_dt=FDT,.accumulator=0,.total_ticks=0};
      int s = loop_frame(&L, 0.1);
      CHECK(s==6, "0.1s frame → 6 fixed ticks (catch-up)"); }

    /* 3. A FAST frame may produce ZERO ticks (accumulator not yet full). */
    { Loop L={.fixed_dt=FDT,.accumulator=0,.total_ticks=0};
      int s = loop_frame(&L, 0.001);            /* 1ms << 16.6ms */
      CHECK(s==0, "1ms frame → 0 ticks, time banked");
      CHECK(loop_alpha(&L) > 0 && loop_alpha(&L) < 1, "alpha in (0,1) after partial accumulation"); }

    /* 4. Interpolation alpha always in [0,1). */
    { Loop L={.fixed_dt=FDT,.accumulator=0,.total_ticks=0};
      double t=0; int ok=1;
      while(t<1.0){ loop_frame(&L, 1.0/90.0); double a=loop_alpha(&L); if(a<0||a>=1.0) ok=0; t+=1.0/90.0; }
      CHECK(ok, "alpha stays in [0,1) across a 90fps second"); }

    /* 5. SPIRAL-OF-DEATH clamp: a huge stall can't produce unbounded ticks. */
    { Loop L={.fixed_dt=FDT,.accumulator=0,.total_ticks=0};
      int s = loop_frame(&L, 100.0);            /* 100s stall */
      CHECK(s==8, "giant stall clamped to 8 ticks");
      CHECK(L.accumulator==0, "accumulator reset after clamp (no banked backlog)"); }

    if (failures==0)
        printf("LOOP TEST: all checks passed (fixed sim rate vs FPS, catch-up ticks, zero-tick fast frames, alpha in [0,1), spiral-of-death clamp)\n");
    else
        printf("LOOP TEST: %d FAILURES\n", failures);
    return failures?1:0;
}
