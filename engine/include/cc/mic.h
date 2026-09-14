#pragma once
/*
 * CCMic — microphone / audio input capture + analysis. Built for the horror
 * concept where the game (and its AI director) REACTS TO REAL-WORLD SOUND: the
 * player's mic level drives "the monster hears you", a loud noise draws a
 * stalker, sustained speech raises tension. Capture is only half of it — the
 * useful signal for gameplay is the ANALYSIS: instantaneous level (RMS),
 * smoothed loudness, peak, and a simple energy-gated voice-activity flag.
 *
 * TWO SOURCES, ONE API.
 *   - REAL capture via OpenAL (alcCaptureOpenDevice…), used on a machine with a
 *     microphone. Enabled when the engine is built with audio (CC_USE_AUDIO).
 *   - SYNTHETIC / INJECTED samples: you push PCM frames yourself with
 *     cc_mic_feed(). This path needs no hardware and works everywhere —
 *     including Claude's headless sandbox and CI — so mic-driven gameplay is
 *     fully testable without a device. It's also how you'd feed a recorded WAV,
 *     a network stream, or a procedural test signal.
 * The analysis (level/RMS/peak/VAD) is identical for both, so game code written
 * against a synthetic source behaves the same on a real mic.
 *
 * FRAMES. Audio arrives as mono 16-bit signed PCM at a fixed sample rate. You
 * call cc_mic_update() once per frame; it pulls whatever the source has ready,
 * updates the running analysis, and (optionally) publishes a CC_EVT_MIC_LEVEL
 * event on an attached bus so the director can consume loudness with zero
 * coupling.
 *
 *   CCMic* mic = cc_mic_open(16000);          // 16 kHz mono
 *   cc_mic_watch_bus(mic, bus);               // optional: emit CC_EVT_MIC_LEVEL
 *   ... each frame:
 *     cc_mic_update(mic, dt);
 *     float loud = cc_mic_loudness(mic);      // smoothed 0..1
 *     if (cc_mic_voice_active(mic)) director_add_stress(dir, loud*dt);
 *
 * With no hardware you instead push samples before update():
 *   cc_mic_feed(mic, pcm, n);                 // synthetic/recorded frames
 *
 * Pure CPU for the analysis + synthetic path; the OpenAL path is behind
 * CC_USE_AUDIO and degrades to "no device" cleanly when unavailable.
 */
#include "cc/event.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCMic CCMic;

/* Open an input source at `sample_rate` Hz, mono 16-bit. If real capture is
 * available it's used; otherwise the mic still opens in SYNTHETIC mode and you
 * drive it with cc_mic_feed(). Returns NULL only on allocation failure. */
CCMic* cc_mic_open(uint32_t sample_rate);
void   cc_mic_close(CCMic* m);

/* Is a real hardware capture device backing this mic? false = synthetic-only
 * (feed samples yourself). Lets a game show "no microphone detected". */
bool     cc_mic_has_device(const CCMic* m);
uint32_t cc_mic_sample_rate(const CCMic* m);

/* Push `count` mono int16 samples into the source (synthetic path). Safe to mix
 * with a real device too (the fed samples are analysed alongside). */
void cc_mic_feed(CCMic* m, const int16_t* samples, uint32_t count);

/* Pull ready samples (from the device and/or fed buffer), fold them into the
 * running analysis, and advance smoothing by dt. Call once per frame. Returns
 * the number of samples consumed this call. */
uint32_t cc_mic_update(CCMic* m, float dt);

/* ─── analysis (updated by cc_mic_update) ─────────────────────────────────── */
/* Instantaneous RMS of the most recent block, 0..1 (1 = full-scale). */
float cc_mic_level(const CCMic* m);
/* Smoothed loudness 0..1 — attack/decay-filtered level, better for gameplay
 * (avoids single-frame spikes). Tunable via cc_mic_set_smoothing. */
float cc_mic_loudness(const CCMic* m);
/* Peak absolute sample of the most recent block, 0..1. */
float cc_mic_peak(const CCMic* m);
/* Energy-gated voice-activity flag: true while loudness exceeds the open
 * threshold, staying true until it falls below the (lower) close threshold —
 * hysteresis so it doesn't chatter. Tune with cc_mic_set_vad. */
bool  cc_mic_voice_active(const CCMic* m);

/* ─── tuning ──────────────────────────────────────────────────────────────── */
/* Attack/decay time-constants (seconds) for cc_mic_loudness smoothing. Small
 * attack = snappy rise; larger decay = slower fall. Defaults 0.05 / 0.3. */
void cc_mic_set_smoothing(CCMic* m, float attack_s, float decay_s);
/* Voice-activity open/close thresholds on loudness (0..1), with hysteresis
 * (open > close). Defaults 0.10 / 0.05. */
void cc_mic_set_vad(CCMic* m, float open_thresh, float close_thresh);
/* Input gain multiplier applied to samples before analysis (default 1.0). */
void cc_mic_set_gain(CCMic* m, float gain);

/* ─── event bus (optional) ────────────────────────────────────────────────── */
/* When watching, cc_mic_update publishes CC_EVT_MIC_LEVEL each frame that has
 * new audio, with f = loudness (0..1) and i = voice_active (1/0). The director
 * or any listener can consume it. Pass NULL to stop. */
void cc_mic_watch_bus(CCMic* m, CCEventBus* bus);

#ifdef __cplusplus
}
#endif
