/* mic_test — exercises CCMic capture-analysis via the SYNTHETIC feed path (no
 * hardware needed, fully headless). Feeds sine bursts + silence and verifies
 * level/loudness/peak/VAD and the CC_EVT_MIC_LEVEL bus event. Prints
 * "MIC TEST: all checks passed" and returns 0, else aborts. */
#include "cc/mic.h"
#include "cc/event.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

#define SR 16000

/* fill `buf` with `n` samples of a sine at `freq` Hz, amplitude `amp` (0..1) */
static void gen_sine(int16_t* buf, uint32_t n, float freq, float amp, uint32_t* phase) {
    for (uint32_t i = 0; i < n; ++i) {
        float t = (float)(*phase + i) / (float)SR;
        buf[i] = (int16_t)(sinf(2.0f*3.14159265f*freq*t) * amp * 32767.0f);
    }
    *phase += n;
}

/* bus capture */
static int   g_mic_events = 0;
static float g_last_loud = -1.0f;
static int   g_last_voice = -1;
static void on_mic(const CCEvent* e, void* u) {
    (void)u;
    if (e->channel == CC_EVT_MIC_LEVEL) {
        g_mic_events++; g_last_loud = e->f; g_last_voice = (int)e->i;
    }
}

int main(void) {
    CCMic* m = cc_mic_open(SR);
    CHECK(m != NULL, "mic opened");
    CHECK(cc_mic_sample_rate(m) == SR, "sample rate reported");
    /* In the headless sandbox there is no capture device (or it fails to open);
     * either way synthetic feed must work. We don't assert has_device's value. */

    /* 1. silence → near-zero level, no voice --------------------------------- */
    int16_t sil[1600] = {0};
    cc_mic_feed(m, sil, 1600);
    cc_mic_update(m, 0.1f);
    CHECK(cc_mic_level(m) < 0.01f, "silence gives ~0 level");
    CHECK(!cc_mic_voice_active(m), "silence is not voice-active");

    /* 2. loud sine burst → high level + peak, voice becomes active ----------- */
    uint32_t phase = 0;
    int16_t tone[1600];
    /* feed several blocks of a loud 300 Hz tone so smoothed loudness climbs */
    for (int blk = 0; blk < 8; ++blk) {
        gen_sine(tone, 1600, 300.0f, 0.8f, &phase);
        cc_mic_feed(m, tone, 1600);
        cc_mic_update(m, 0.1f);
    }
    CHECK(cc_mic_level(m) > 0.3f, "loud tone gives substantial RMS level");
    CHECK(cc_mic_peak(m) > 0.5f, "loud tone gives high peak");
    CHECK(cc_mic_loudness(m) > 0.10f, "smoothed loudness rises above VAD open");
    CHECK(cc_mic_voice_active(m), "loud tone activates voice detection");
    /* RMS of a full-amp sine is ~0.707*amp; 0.8 amp → ~0.57. sanity band. */
    CHECK(cc_mic_level(m) > 0.4f && cc_mic_level(m) < 0.75f,
          "RMS in the expected band for a 0.8-amp sine");

    /* 3. return to silence → loudness decays, voice eventually closes -------- */
    for (int i = 0; i < 30; ++i) cc_mic_update(m, 0.1f);   /* 3s of no input */
    CHECK(cc_mic_loudness(m) < 0.05f, "loudness decays back down in silence");
    CHECK(!cc_mic_voice_active(m), "voice closes after silence (hysteresis)");

    /* 4. gain scales the analysed level ------------------------------------- */
    CCMic* g = cc_mic_open(SR);
    cc_mic_set_gain(g, 4.0f);
    int16_t quiet[1600];
    uint32_t ph2 = 0;
    gen_sine(quiet, 1600, 300.0f, 0.1f, &ph2);   /* quiet 0.1-amp tone */
    cc_mic_feed(g, quiet, 1600);
    cc_mic_update(g, 0.1f);
    float amplified = cc_mic_level(g);
    CCMic* g1 = cc_mic_open(SR);                 /* same tone, gain 1 */
    uint32_t ph3 = 0; gen_sine(quiet, 1600, 300.0f, 0.1f, &ph3);
    cc_mic_feed(g1, quiet, 1600);
    cc_mic_update(g1, 0.1f);
    CHECK(amplified > cc_mic_level(g1) * 2.0f, "gain multiplies the analysed level");

    /* 5. VAD threshold tuning ----------------------------------------------- */
    CCMic* v = cc_mic_open(SR);
    cc_mic_set_vad(v, 0.5f, 0.4f);   /* require loud to open */
    uint32_t ph4 = 0; int16_t mid[1600];
    for (int blk=0; blk<8; ++blk){ gen_sine(mid, 1600, 300.0f, 0.3f, &ph4);
        cc_mic_feed(v, mid, 1600); cc_mic_update(v, 0.1f); }
    CHECK(!cc_mic_voice_active(v), "moderate tone stays below a high VAD open threshold");

    /* 6. bus event: CC_EVT_MIC_LEVEL emitted with loudness + voice flag ------ */
    CCEventBus* bus = cc_event_bus_create();
    cc_event_subscribe(bus, CC_EVT_MIC_LEVEL, on_mic, NULL);
    CCMic* mb = cc_mic_open(SR);
    cc_mic_watch_bus(mb, bus);
    uint32_t ph5 = 0; int16_t loud[1600];
    for (int blk=0; blk<5; ++blk){ gen_sine(loud, 1600, 300.0f, 0.8f, &ph5);
        cc_mic_feed(mb, loud, 1600); cc_mic_update(mb, 0.1f); cc_event_bus_update(bus); }
    CHECK(g_mic_events > 0, "CC_EVT_MIC_LEVEL published while audio present");
    CHECK(g_last_loud > 0.10f, "mic event carries loudness");
    CHECK(g_last_voice == 1, "mic event flags voice active during loud tone");

    /* 7. NULL safety -------------------------------------------------------- */
    CHECK(cc_mic_level(NULL) == 0.0f, "level(NULL)=0");
    CHECK(!cc_mic_voice_active(NULL), "voice_active(NULL)=false");
    CHECK(cc_mic_update(NULL, 0.1f) == 0, "update(NULL)=0");
    cc_mic_feed(NULL, sil, 1600);
    cc_mic_close(NULL);

    cc_mic_close(m); cc_mic_close(g); cc_mic_close(g1);
    cc_mic_close(v); cc_mic_close(mb);
    cc_event_bus_destroy(bus);

    if (failures == 0) {
        printf("MIC TEST: all checks passed (synthetic capture, RMS/peak/loudness, "
               "hysteresis VAD, gain, threshold tuning, CC_EVT_MIC_LEVEL bus event)\n");
        return 0;
    }
    printf("MIC TEST: %d check(s) FAILED\n", failures);
    return 1;
}
