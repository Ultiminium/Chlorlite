/* mic.c — CCMic microphone capture + analysis. See cc/mic.h.
 *
 * Sources: a real OpenAL capture device (behind CC_USE_AUDIO) and/or a
 * synthetic ring buffer fed via cc_mic_feed(). cc_mic_update() drains both into
 * one analysis pass computing RMS level, peak, an attack/decay-smoothed
 * loudness, and a hysteresis voice-activity flag. Optionally emits
 * CC_EVT_MIC_LEVEL on an attached bus.
 *
 * The OpenAL path uses its OWN capture device (alcCaptureOpenDevice), separate
 * from the playback device the audio mixer owns — capture and playback are
 * independent in OpenAL, so this does not disturb cc/audio.h.
 */
#include "cc/mic.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef CC_USE_AUDIO
  #include <AL/al.h>
  #include <AL/alc.h>
#endif

#define MIC_RING_CAP  16384   /* synthetic sample ring (mono int16)             */
#define MIC_BLOCK_MAX 4096    /* max samples analysed per update                */

struct CCMic {
    uint32_t sample_rate;
    float    gain;

    /* synthetic ring buffer */
    int16_t  ring[MIC_RING_CAP];
    uint32_t head, tail, ring_count;

    /* analysis outputs */
    float    level;      /* instantaneous RMS 0..1                              */
    float    loudness;   /* smoothed 0..1                                       */
    float    peak;       /* recent peak 0..1                                    */
    bool     voice;      /* VAD state (with hysteresis)                         */

    /* tuning */
    float    attack_s, decay_s;
    float    vad_open, vad_close;

    /* bus */
    CCEventBus* bus;

    bool     has_device;
#ifdef CC_USE_AUDIO
    ALCdevice* capture;   /* NULL unless a real device opened                   */
#endif
};

static float clamp01(float v){ return v<0.0f?0.0f:(v>1.0f?1.0f:v); }

CCMic* cc_mic_open(uint32_t sample_rate) {
    CCMic* m = (CCMic*)calloc(1, sizeof(CCMic));
    if (!m) return NULL;
    m->sample_rate = sample_rate ? sample_rate : 16000;
    m->gain      = 1.0f;
    m->attack_s  = 0.05f;
    m->decay_s   = 0.30f;
    m->vad_open  = 0.10f;
    m->vad_close = 0.05f;
    m->has_device = false;

#ifdef CC_USE_AUDIO
    /* Try to open a real capture device; failure is fine → synthetic mode. */
    m->capture = alcCaptureOpenDevice(NULL, m->sample_rate, AL_FORMAT_MONO16,
                                      (ALCsizei)(m->sample_rate)); /* ~1s buffer */
    if (m->capture) {
        alcCaptureStart(m->capture);
        m->has_device = true;
    }
#endif
    return m;
}

void cc_mic_close(CCMic* m) {
    if (!m) return;
    if (m->bus) cc_mic_watch_bus(m, NULL);
#ifdef CC_USE_AUDIO
    if (m->capture) {
        alcCaptureStop(m->capture);
        alcCaptureCloseDevice(m->capture);
    }
#endif
    free(m);
}

bool     cc_mic_has_device(const CCMic* m) { return m ? m->has_device : false; }
uint32_t cc_mic_sample_rate(const CCMic* m) { return m ? m->sample_rate : 0; }

void cc_mic_feed(CCMic* m, const int16_t* samples, uint32_t count) {
    if (!m || !samples || count == 0) return;
    for (uint32_t i = 0; i < count; ++i) {
        if (m->ring_count == MIC_RING_CAP) {  /* full: drop oldest */
            m->tail = (m->tail + 1) % MIC_RING_CAP;
            m->ring_count--;
        }
        m->ring[m->head] = samples[i];
        m->head = (m->head + 1) % MIC_RING_CAP;
        m->ring_count++;
    }
}

/* pull up to `max` samples from ring into out; return count */
static uint32_t ring_drain(CCMic* m, int16_t* out, uint32_t max) {
    uint32_t n = 0;
    while (n < max && m->ring_count > 0) {
        out[n++] = m->ring[m->tail];
        m->tail = (m->tail + 1) % MIC_RING_CAP;
        m->ring_count--;
    }
    return n;
}

void cc_mic_set_smoothing(CCMic* m, float attack_s, float decay_s) {
    if (!m) return;
    m->attack_s = attack_s > 0.0f ? attack_s : 0.0f;
    m->decay_s  = decay_s  > 0.0f ? decay_s  : 0.0f;
}
void cc_mic_set_vad(CCMic* m, float open_thresh, float close_thresh) {
    if (!m) return;
    m->vad_open  = clamp01(open_thresh);
    m->vad_close = clamp01(close_thresh);
    if (m->vad_close > m->vad_open) m->vad_close = m->vad_open; /* keep open>=close */
}
void cc_mic_set_gain(CCMic* m, float gain) { if (m) m->gain = gain < 0.0f ? 0.0f : gain; }

uint32_t cc_mic_update(CCMic* m, float dt) {
    if (!m) return 0;
    int16_t block[MIC_BLOCK_MAX];
    uint32_t n = 0;

#ifdef CC_USE_AUDIO
    if (m->capture) {
        ALCint avail = 0;
        alcGetIntegerv(m->capture, ALC_CAPTURE_SAMPLES, 1, &avail);
        if (avail > 0) {
            uint32_t want = (avail > MIC_BLOCK_MAX) ? MIC_BLOCK_MAX : (uint32_t)avail;
            alcCaptureSamples(m->capture, block, (ALCsizei)want);
            n = want;
        }
    }
#endif
    /* append synthetic/fed samples after any device samples (up to the block) */
    if (n < MIC_BLOCK_MAX)
        n += ring_drain(m, block + n, MIC_BLOCK_MAX - n);

    if (n == 0) {
        /* no new audio: decay loudness toward 0 so silence relaxes the signal */
        if (m->decay_s > 0.0f) {
            float k = dt / m->decay_s;
            if (k > 1.0f) k = 1.0f;
            m->loudness += (0.0f - m->loudness) * k;
        } else {
            m->loudness = 0.0f;
        }
        m->level = 0.0f; m->peak = 0.0f;
        /* VAD close check on the relaxed loudness */
        if (m->voice && m->loudness < m->vad_close) m->voice = false;
        return 0;
    }

    /* analysis: RMS + peak over the block, with gain */
    double sumsq = 0.0;
    float  pk = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float s = (block[i] * (1.0f/32768.0f)) * m->gain;
        if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
        sumsq += (double)s * s;
        float a = s < 0.0f ? -s : s;
        if (a > pk) pk = a;
    }
    float rms = (float)sqrt(sumsq / (double)n);
    m->level = clamp01(rms);
    m->peak  = clamp01(pk);

    /* attack/decay smoothing toward the instantaneous level */
    float target = m->level;
    float tc = (target > m->loudness) ? m->attack_s : m->decay_s;
    if (tc > 0.0f) {
        float k = dt / tc;
        if (k > 1.0f) k = 1.0f;
        m->loudness += (target - m->loudness) * k;
    } else {
        m->loudness = target;
    }
    m->loudness = clamp01(m->loudness);

    /* hysteresis VAD */
    if (!m->voice && m->loudness >= m->vad_open)  m->voice = true;
    else if (m->voice && m->loudness < m->vad_close) m->voice = false;

    /* optional bus event */
    if (m->bus)
        cc_event_emit_if(m->bus, CC_EVT_MIC_LEVEL, 0,
                         m->voice ? 1 : 0, m->loudness);
    return n;
}

float cc_mic_level(const CCMic* m)    { return m ? m->level : 0.0f; }
float cc_mic_loudness(const CCMic* m) { return m ? m->loudness : 0.0f; }
float cc_mic_peak(const CCMic* m)     { return m ? m->peak : 0.0f; }
bool  cc_mic_voice_active(const CCMic* m) { return m ? m->voice : false; }

void cc_mic_watch_bus(CCMic* m, CCEventBus* bus) { if (m) m->bus = bus; }
