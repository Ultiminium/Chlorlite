#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCEngine CCEngine;
typedef uint32_t CCSoundId;
#define CC_SOUND_NULL 0

/* ─── Sound loading ──────────────────────────────────────────────────── */
CCSoundId cc_sound_load(CCEngine* eng, const char* path);  /* .wav, .ogg, .mp3 */

/* ─── Procedural audio generation ────────────────────────────────────── */
typedef enum CCWaveform {
    CC_WAVE_SINE,
    CC_WAVE_SQUARE,
    CC_WAVE_SAWTOOTH,
    CC_WAVE_TRIANGLE,
    CC_WAVE_NOISE,
    CC_WAVE_CUSTOM,     /* user-provided generator */
} CCWaveform;

typedef struct CCProceduralToneDesc {
    CCWaveform waveform;
    float      frequency;       /* Hz */
    float      duration;        /* seconds */
    float      amplitude;       /* 0.0 - 1.0 */
    /* Envelope */
    float      attack;          /* seconds */
    float      decay;
    float      sustain;         /* level 0-1 */
    float      release;
    /* Modulation */
    float      vibrato_rate;    /* Hz, 0 = none */
    float      vibrato_depth;
    float      tremolo_rate;    /* Hz, 0 = none */
    float      tremolo_depth;
    /* Custom generator — called if waveform == CC_WAVE_CUSTOM */
    float (*generator)(float t, float freq, void* userdata);
    void* generator_userdata;
} CCProceduralToneDesc;

CCSoundId cc_sound_procedural_tone(CCEngine* eng, const CCProceduralToneDesc* desc);

/* Generate a sound from a full PCM callback (stereo float -1..1) */
typedef void (*CCPCMGeneratorFn)(float* out_stereo, uint32_t frames,
                                  uint32_t sample_rate, void* userdata);
CCSoundId cc_sound_procedural_pcm(CCEngine* eng, CCPCMGeneratorFn gen,
                                   void* userdata, float duration_seconds);

/* Synth shortcuts */
CCSoundId cc_sound_beep(CCEngine* eng, float freq, float dur);
CCSoundId cc_sound_noise(CCEngine* eng, float dur, float cutoff_hz);
CCSoundId cc_sound_footstep(CCEngine* eng);      /* generated footstep */
CCSoundId cc_sound_explosion(CCEngine* eng);     /* generated explosion */
CCSoundId cc_sound_laser(CCEngine* eng, float start_freq, float end_freq, float dur);

/* ─── Playback ───────────────────────────────────────────────────────── */
typedef uint32_t CCSoundInstance;
#define CC_SOUND_INSTANCE_NULL 0

CCSoundInstance cc_audio_play(CCEngine* eng, CCSoundId id,
                               float volume, float pitch, bool loop);
CCSoundInstance cc_audio_play_3d(CCEngine* eng, CCSoundId id,
                                  float x, float y, float z,
                                  float volume, float pitch, bool loop);
void            cc_audio_stop(CCEngine* eng, CCSoundInstance inst);
void            cc_audio_stop_all(CCEngine* eng);
bool            cc_audio_playing(CCEngine* eng, CCSoundInstance inst);

/* ─── Master control ─────────────────────────────────────────────────── */
void  cc_audio_set_master_volume(CCEngine* eng, float vol);
float cc_audio_master_volume(CCEngine* eng);
void  cc_audio_set_listener(CCEngine* eng, float x, float y, float z,
                              float fwd_x, float fwd_y, float fwd_z);

/* ─── Per-frame update (drives fades + music crossfades) ─────────────────
 * Call once per frame. It is also invoked automatically from cc_tick, so most
 * games never need to call it directly; call it manually if you drive the
 * audio system outside the normal engine loop. */
void cc_audio_update(CCEngine* eng, float dt);
const char* cc_audio_device_name(CCEngine* eng);
int cc_audio_is_online(CCEngine* eng);

/* ─── Volume fades (per instance) ────────────────────────────────────────
 * Ramp an instance's own gain over `seconds`. fade_out stops the instance when
 * it reaches zero. Gain is combined multiplicatively with bus + master. */
void cc_audio_fade_to (CCEngine* eng, CCSoundInstance inst, float target_vol, float seconds);
void cc_audio_fade_in (CCEngine* eng, CCSoundInstance inst, float seconds);
void cc_audio_fade_out(CCEngine* eng, CCSoundInstance inst, float seconds);

/* ─── Pause / resume ─────────────────────────────────────────────────── */
void cc_audio_pause (CCEngine* eng, CCSoundInstance inst);
void cc_audio_resume(CCEngine* eng, CCSoundInstance inst);
void cc_audio_pause_all (CCEngine* eng);
void cc_audio_resume_all(CCEngine* eng);
bool cc_audio_paused(CCEngine* eng, CCSoundInstance inst);

/* ─── Mixer buses (categories: music / sfx / ui / ...) ───────────────────
 * A bus is a named volume group. Assign an instance to a bus, then control the
 * whole group's level at once. Bus 0 is the default "master-child" bus. */
typedef uint32_t CCAudioBus;
CCAudioBus cc_audio_bus(CCEngine* eng, const char* name);          /* get/create by name */
void       cc_audio_set_bus_volume(CCEngine* eng, CCAudioBus bus, float vol);
float      cc_audio_bus_volume(CCEngine* eng, CCAudioBus bus);
void       cc_audio_instance_set_bus(CCEngine* eng, CCSoundInstance inst, CCAudioBus bus);

/* Play routed to a bus in one call. */
CCSoundInstance cc_audio_play_bus(CCEngine* eng, CCSoundId id, CCAudioBus bus,
                                   float volume, float pitch, bool loop);

/* ─── Music layer (single active track w/ crossfade) ─────────────────────
 * Starts `id` looping on the built-in "music" bus and crossfades from whatever
 * music was playing over `fade_seconds`. Returns the new music instance. */
CCSoundInstance cc_audio_play_music(CCEngine* eng, CCSoundId id, float volume, float fade_seconds);
void            cc_audio_stop_music(CCEngine* eng, float fade_seconds);

/* ─── 3D distance model ──────────────────────────────────────────────────
 * Controls positional attenuation. ref = distance at which gain is 1.0; max =
 * distance beyond which it stops attenuating; rolloff scales the falloff. */
void cc_audio_set_rolloff(CCEngine* eng, float ref_distance, float max_distance, float rolloff);

/* ─── Cleanup ────────────────────────────────────────────────────────── */
void cc_sound_destroy(CCEngine* eng, CCSoundId id);

#ifdef __cplusplus
}
#endif
