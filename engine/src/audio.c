/*
 * audio.c — Chlorlite audio: procedural synthesis + OpenAL 3D playback
 *
 * Two layers:
 *   1. Synthesis — generates 16-bit PCM in memory (waveforms, ADSR envelopes,
 *      vibrato/tremolo, and composed SFX: footstep/explosion/laser). Fully
 *      device-independent and testable anywhere.
 *   2. Playback — uploads PCM to OpenAL buffers and plays them (2D + 3D
 *      positional). Initializes when an audio device is present; degrades
 *      gracefully to a no-op (sounds still "generate", play is silent) when no
 *      device exists (e.g. headless CI) so games never crash.
 */
#include "cc/audio.h"
#include "cc/claudecore.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifdef CC_USE_AUDIO
#include <AL/al.h>
#include <AL/alc.h>
#endif
#ifdef CC_USE_SNDFILE
#include <sndfile.h>
#endif

#ifndef CC_PI
#define CC_PI 3.14159265358979323846f
#endif
#define CC_AUDIO_SR 44100
#define CC_MAX_SOUNDS    1024
#define CC_MAX_INSTANCES 256

/* ─── PCM sound (device-independent) ─────────────────────────────────── */
typedef struct {
    int16_t* pcm;          /* interleaved */
    uint32_t frames;
    uint32_t channels;     /* 1 = mono (positional), 2 = stereo */
    uint32_t sample_rate;
    bool     valid;
#ifdef CC_USE_AUDIO
    ALuint   al_buffer;    /* 0 if not uploaded */
#endif
} CCSound;

typedef struct {
    bool     active;
    bool     loop;
    bool     paused;
    double   end_time;    /* wall-clock time this instance finishes (non-loop) */
    double   duration;    /* sound length in seconds */
    double   pause_started;/* wall-clock when paused (to shift end_time on resume) */
    float    base_volume; /* the instance's own gain (pre bus/master) */
    uint32_t bus;         /* mixer bus index */
    /* volume fade */
    float    fade_from, fade_to, fade_t, fade_dur; /* fade_dur<=0 = no fade */
    bool     stop_on_fade_end;
#ifdef CC_USE_AUDIO
    ALuint   al_source;
    bool     has_al;      /* true if a real AL source backs this instance */
#endif
} CCSoundInst;

#define CC_MAX_BUSES 16
typedef struct { char name[24]; float volume; bool used; } CCAudioBusRec;

typedef struct CCAudioSystem {
    CCSound     sounds[CC_MAX_SOUNDS];
    uint32_t    sound_count;
    CCSoundInst instances[CC_MAX_INSTANCES];
    float       master_volume;
    bool        device_ok;
    CCAudioBusRec buses[CC_MAX_BUSES];
    uint32_t    music_bus;            /* built-in bus index for music */
    CCSoundInstance current_music;    /* active music instance (0 = none) */
    float       roll_ref, roll_max, roll_factor;
#ifdef CC_USE_AUDIO
    ALCdevice*  device;
    ALCcontext* context;
#endif
} CCAudioSystem;

/* engine.c owns a void* audio pointer; we lazily create the system. */
extern CCAudioSystem* cc_engine_audio_get(CCEngine* eng);
extern void           cc_engine_audio_set(CCEngine* eng, CCAudioSystem* sys);

static CCAudioSystem* audio_sys(CCEngine* eng) {
    CCAudioSystem* s = cc_engine_audio_get(eng);
    if (s) return s;
    s = calloc(1, sizeof(CCAudioSystem));
    s->master_volume = 1.0f;
    s->sound_count = 1; /* 0 = null */
    /* bus 0 = default; create the built-in "music" bus at index 1 */
    s->buses[0]=(CCAudioBusRec){"master",1.0f,true};
    s->buses[1]=(CCAudioBusRec){"music",1.0f,true};
    s->music_bus=1;
    s->roll_ref=1.0f; s->roll_max=100.0f; s->roll_factor=1.0f;
#ifdef CC_USE_AUDIO
    /* Open the user's default output device. On a real machine this is their
       speakers. If no default device exists, fall back to OpenAL's null device
       so the mixing pipeline still runs (sources play, state advances) — audio
       is processed either way, it's just inaudible without hardware. A real
       audio stack behaves the same: no speaker doesn't mean no playback. */
    s->device = alcOpenDevice(NULL);
    if (!s->device) s->device = alcOpenDevice("Null Output");
    if (s->device) {
        s->context = alcCreateContext(s->device, NULL);
        if (s->context) { alcMakeContextCurrent(s->context); s->device_ok = true; }
    }
    if (s->device_ok)
        CC_INFO("audio: online (device=%s)", alcGetString(s->device, ALC_DEVICE_SPECIFIER));
    else
        CC_INFO("audio: no AL context — playback simulated (state advances, silent)");
#else
    CC_INFO("audio: built without OpenAL — synthesis only");
#endif
    cc_engine_audio_set(eng, s);
    return s;
}

/* ─── Synthesis core ─────────────────────────────────────────────────── */

static float waveform_sample(CCWaveform wf, float phase /* 0..1 */) {
    switch (wf) {
        case CC_WAVE_SINE:     return sinf(phase * 2.0f * CC_PI);
        case CC_WAVE_SQUARE:   return phase < 0.5f ? 1.0f : -1.0f;
        case CC_WAVE_SAWTOOTH: return 2.0f * phase - 1.0f;
        case CC_WAVE_TRIANGLE: return phase < 0.5f ? (4.0f*phase - 1.0f) : (3.0f - 4.0f*phase);
        case CC_WAVE_NOISE:    return (float)rand() / RAND_MAX * 2.0f - 1.0f;
        default:               return 0.0f;
    }
}

static float adsr_env(float t, float dur, float a, float d, float s, float rel) {
    if (t < a && a > 0)            return t / a;                  /* attack */
    if (t < a + d && d > 0)        return 1.0f - (1.0f - s) * (t - a) / d; /* decay */
    float rel_start = dur - rel;
    if (t < rel_start)             return s;                     /* sustain */
    if (rel > 0)                   return s * (1.0f - (t - rel_start) / rel); /* release */
    return 0.0f;
}

static CCSound* alloc_sound(CCAudioSystem* s, CCSoundId* out_id) {
    for (uint32_t i = 1; i < CC_MAX_SOUNDS; i++) {
        if (!s->sounds[i].valid) { *out_id = i; return &s->sounds[i]; }
    }
    *out_id = CC_SOUND_NULL;
    return NULL;
}

#ifdef CC_USE_AUDIO
static void upload_to_al(CCSound* snd) {
    if (!snd->al_buffer) alGenBuffers(1, &snd->al_buffer);
    ALenum fmt = snd->channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    alBufferData(snd->al_buffer, fmt, snd->pcm,
                 (ALsizei)(snd->frames * snd->channels * sizeof(int16_t)),
                 (ALsizei)snd->sample_rate);
}
#endif

static CCSoundId finalize_sound(CCAudioSystem* s, CCSound* snd, CCSoundId id) {
    snd->sample_rate = CC_AUDIO_SR;
    snd->valid = true;
#ifdef CC_USE_AUDIO
    if (s->device_ok) upload_to_al(snd);
#else
    (void)s;
#endif
    return id;
}

/* ─── Procedural tone ────────────────────────────────────────────────── */
CCSoundId cc_sound_procedural_tone(CCEngine* eng, const CCProceduralToneDesc* d) {
    if (!d || d->duration <= 0) return CC_SOUND_NULL;
    CCAudioSystem* s = audio_sys(eng);
    CCSoundId id; CCSound* snd = alloc_sound(s, &id);
    if (!snd) return CC_SOUND_NULL;

    uint32_t frames = (uint32_t)(d->duration * CC_AUDIO_SR);
    snd->pcm = malloc(frames * sizeof(int16_t));
    snd->frames = frames;
    snd->channels = 1;
    float amp = d->amplitude > 0 ? d->amplitude : 0.8f;
    float phase = 0.0f;

    for (uint32_t i = 0; i < frames; i++) {
        float t = (float)i / CC_AUDIO_SR;
        /* vibrato modulates frequency */
        float freq = d->frequency;
        if (d->vibrato_rate > 0)
            freq += d->vibrato_depth * sinf(t * 2.0f*CC_PI * d->vibrato_rate);
        phase += freq / CC_AUDIO_SR;
        if (phase >= 1.0f) phase -= 1.0f;

        float sample;
        if (d->waveform == CC_WAVE_CUSTOM && d->generator)
            sample = d->generator(t, freq, d->generator_userdata);
        else
            sample = waveform_sample(d->waveform, phase);

        /* envelope */
        float env = adsr_env(t, d->duration, d->attack, d->decay,
                             d->sustain > 0 ? d->sustain : 1.0f, d->release);
        /* tremolo modulates amplitude */
        float trem = 1.0f;
        if (d->tremolo_rate > 0)
            trem = 1.0f - d->tremolo_depth * 0.5f * (1.0f + sinf(t*2.0f*CC_PI*d->tremolo_rate));

        float v = sample * amp * env * trem;
        if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
        snd->pcm[i] = (int16_t)(v * 32767.0f);
    }
    return finalize_sound(s, snd, id);
}

/* ─── PCM callback generator (stereo) ────────────────────────────────── */
CCSoundId cc_sound_procedural_pcm(CCEngine* eng, CCPCMGeneratorFn gen,
                                   void* userdata, float duration_seconds) {
    if (!gen || duration_seconds <= 0) return CC_SOUND_NULL;
    CCAudioSystem* s = audio_sys(eng);
    CCSoundId id; CCSound* snd = alloc_sound(s, &id);
    if (!snd) return CC_SOUND_NULL;

    uint32_t frames = (uint32_t)(duration_seconds * CC_AUDIO_SR);
    float* fbuf = malloc(frames * 2 * sizeof(float));
    memset(fbuf, 0, frames * 2 * sizeof(float));
    gen(fbuf, frames, CC_AUDIO_SR, userdata);

    snd->pcm = malloc(frames * 2 * sizeof(int16_t));
    snd->frames = frames;
    snd->channels = 2;
    for (uint32_t i = 0; i < frames*2; i++) {
        float v = fbuf[i]; if (v>1) v=1; if (v<-1) v=-1;
        snd->pcm[i] = (int16_t)(v * 32767.0f);
    }
    free(fbuf);
    return finalize_sound(s, snd, id);
}

/* ─── Synth shortcuts ────────────────────────────────────────────────── */
CCSoundId cc_sound_beep(CCEngine* eng, float freq, float dur) {
    CCProceduralToneDesc d = {0};
    d.waveform = CC_WAVE_SINE; d.frequency = freq; d.duration = dur;
    d.amplitude = 0.7f; d.attack = 0.005f; d.decay = 0.02f; d.sustain = 0.8f; d.release = 0.05f;
    return cc_sound_procedural_tone(eng, &d);
}

CCSoundId cc_sound_noise(CCEngine* eng, float dur, float cutoff_hz) {
    CCAudioSystem* s = audio_sys(eng);
    CCSoundId id; CCSound* snd = alloc_sound(s, &id);
    if (!snd) return CC_SOUND_NULL;
    uint32_t frames = (uint32_t)(dur * CC_AUDIO_SR);
    snd->pcm = malloc(frames * sizeof(int16_t));
    snd->frames = frames; snd->channels = 1;
    /* white noise → one-pole low-pass at cutoff */
    float rc = 1.0f / (2.0f*CC_PI*(cutoff_hz > 0 ? cutoff_hz : 8000.0f));
    float dt = 1.0f / CC_AUDIO_SR;
    float alpha = dt / (rc + dt);
    float prev = 0.0f;
    for (uint32_t i = 0; i < frames; i++) {
        float white = (float)rand()/RAND_MAX*2.0f - 1.0f;
        prev = prev + alpha * (white - prev);
        float env = adsr_env((float)i/CC_AUDIO_SR, dur, 0.01f, 0.05f, 0.7f, 0.1f);
        float v = prev * env * 0.8f;
        snd->pcm[i] = (int16_t)(v*32767.0f);
    }
    return finalize_sound(s, snd, id);
}

CCSoundId cc_sound_footstep(CCEngine* eng) {
    /* short filtered noise burst with fast decay */
    return cc_sound_noise(eng, 0.12f, 2000.0f);
}

CCSoundId cc_sound_explosion(CCEngine* eng) {
    CCAudioSystem* s = audio_sys(eng);
    CCSoundId id; CCSound* snd = alloc_sound(s, &id);
    if (!snd) return CC_SOUND_NULL;
    float dur = 1.2f;
    uint32_t frames = (uint32_t)(dur * CC_AUDIO_SR);
    snd->pcm = malloc(frames * sizeof(int16_t));
    snd->frames = frames; snd->channels = 1;
    /* low rumble (descending sine) + noise, long exponential decay */
    float prev = 0.0f, alpha = 0.02f;
    for (uint32_t i = 0; i < frames; i++) {
        float t = (float)i / CC_AUDIO_SR;
        float freq = 90.0f * expf(-t*2.0f);            /* pitch drops */
        float rumble = sinf(t * 2.0f*CC_PI * freq);
        float white = (float)rand()/RAND_MAX*2.0f - 1.0f;
        prev = prev + alpha * (white - prev);          /* low-passed noise */
        float env = expf(-t * 3.5f);                   /* exp decay */
        float v = (rumble*0.6f + prev*0.7f) * env;
        if (v>1)v=1; if(v<-1)v=-1;
        snd->pcm[i] = (int16_t)(v*32767.0f);
    }
    return finalize_sound(s, snd, id);
}

CCSoundId cc_sound_laser(CCEngine* eng, float start_freq, float end_freq, float dur) {
    CCAudioSystem* s = audio_sys(eng);
    CCSoundId id; CCSound* snd = alloc_sound(s, &id);
    if (!snd) return CC_SOUND_NULL;
    uint32_t frames = (uint32_t)(dur * CC_AUDIO_SR);
    snd->pcm = malloc(frames * sizeof(int16_t));
    snd->frames = frames; snd->channels = 1;
    float phase = 0.0f;
    for (uint32_t i = 0; i < frames; i++) {
        float t = (float)i / CC_AUDIO_SR;
        float k = t / dur;
        float freq = start_freq + (end_freq - start_freq) * k;  /* sweep */
        phase += freq / CC_AUDIO_SR; if (phase>=1) phase-=1;
        float saw = 2.0f*phase - 1.0f;
        float env = adsr_env(t, dur, 0.005f, 0.0f, 1.0f, dur*0.5f);
        float v = saw * env * 0.6f;
        snd->pcm[i] = (int16_t)(v*32767.0f);
    }
    return finalize_sound(s, snd, id);
}

/* ─── WAV/OGG loading via libsndfile ─────────────────────────────────── */
CCSoundId cc_sound_load(CCEngine* eng, const char* path) {
    CCAudioSystem* s = audio_sys(eng);
#ifdef CC_USE_SNDFILE
    SF_INFO info = {0};
    SNDFILE* f = sf_open(path, SFM_READ, &info);
    if (!f) { fprintf(stderr, "[cc] sound load fail: %s\n", path); return CC_SOUND_NULL; }
    CCSoundId id; CCSound* snd = alloc_sound(s, &id);
    if (!snd) { sf_close(f); return CC_SOUND_NULL; }
    uint32_t total = (uint32_t)(info.frames * info.channels);
    snd->pcm = malloc(total * sizeof(int16_t));
    sf_readf_short(f, snd->pcm, info.frames);
    snd->frames = (uint32_t)info.frames;
    snd->channels = (uint32_t)info.channels;
    snd->sample_rate = (uint32_t)info.samplerate;
    snd->valid = true;
    sf_close(f);
    if (s->device_ok) upload_to_al(snd);
    return id;
#else
    (void)path; (void)s;
    fprintf(stderr, "[cc] sound load: built without libsndfile\n");
    return CC_SOUND_NULL;
#endif
}

/* ─── Playback ───────────────────────────────────────────────────────── */
static double sound_duration(const CCSound* snd) {
    uint32_t sr = snd->sample_rate ? snd->sample_rate : CC_AUDIO_SR;
    return snd->frames > 0 ? (double)snd->frames / (double)sr : 0.0;
}

static CCSoundInstance start_source(CCAudioSystem* s, CCSound* snd,
                                     float vol, float pitch, bool loop,
                                     bool positional, float x, float y, float z) {
    double now = (double)cc_now_ns() * 1e-9;
    double dur = sound_duration(snd);
    if (pitch > 0) dur /= pitch;   /* pitch shifts playback length */

    for (uint32_t i = 1; i < CC_MAX_INSTANCES; i++) {
        /* reap finished instances (AL state if backed, else the timer) */
        if (s->instances[i].active) {
#ifdef CC_USE_AUDIO
            if (s->instances[i].has_al) {
                ALint st; alGetSourcei(s->instances[i].al_source, AL_SOURCE_STATE, &st);
                if (st != AL_PLAYING) { alDeleteSources(1,&s->instances[i].al_source); s->instances[i].active=false; s->instances[i].has_al=false; }
            } else
#endif
            if (!s->instances[i].loop && now >= s->instances[i].end_time) {
                s->instances[i].active = false;
            }
        }
        if (!s->instances[i].active) {
            CCSoundInst* inst = &s->instances[i];
            memset(inst, 0, sizeof(*inst));
            inst->active   = true;
            inst->loop     = loop;
            inst->duration = dur;
            inst->end_time = now + dur;
            inst->base_volume = vol;
            inst->bus = 0;
            inst->fade_dur = 0;
#ifdef CC_USE_AUDIO
            inst->has_al = false;
            /* If a real AL context + uploaded buffer exist, drive an AL source
               so it's actually audible. Otherwise the timer bookkeeping above
               keeps playback state correct (silent, but the game sees it play). */
            if (s->device_ok && snd->al_buffer) {
                ALuint src = 0; alGenSources(1, &src);
                if (src) {
                    alSourcei(src, AL_BUFFER, (ALint)snd->al_buffer);
                    alSourcef(src, AL_GAIN, vol * s->master_volume);
                    alSourcef(src, AL_PITCH, pitch > 0 ? pitch : 1.0f);
                    alSourcei(src, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
                    if (positional) {
                        alSource3f(src, AL_POSITION, x, y, z);
                        alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE);
                        alSourcef(src, AL_REFERENCE_DISTANCE, s->roll_ref);
                        alSourcef(src, AL_MAX_DISTANCE, s->roll_max);
                        alSourcef(src, AL_ROLLOFF_FACTOR, s->roll_factor);
                    } else {
                        alSource3f(src, AL_POSITION, 0, 0, 0);
                        alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
                    }
                    alSourcePlay(src);
                    inst->al_source = src;
                    inst->has_al = true;
                }
            }
#else
            (void)vol; (void)pitch; (void)positional; (void)x; (void)y; (void)z;
#endif
            return i;
        }
    }
    return CC_SOUND_INSTANCE_NULL;
}

CCSoundInstance cc_audio_play(CCEngine* eng, CCSoundId id, float vol, float pitch, bool loop) {
    CCAudioSystem* s = audio_sys(eng);
    if (id == 0 || id >= CC_MAX_SOUNDS || !s->sounds[id].valid) return CC_SOUND_INSTANCE_NULL;
    return start_source(s, &s->sounds[id], vol, pitch, loop, false, 0,0,0);
}

CCSoundInstance cc_audio_play_3d(CCEngine* eng, CCSoundId id, float x, float y, float z,
                                  float vol, float pitch, bool loop) {
    CCAudioSystem* s = audio_sys(eng);
    if (id == 0 || id >= CC_MAX_SOUNDS || !s->sounds[id].valid) return CC_SOUND_INSTANCE_NULL;
    return start_source(s, &s->sounds[id], vol, pitch, loop, true, x, y, z);
}

void cc_audio_stop(CCEngine* eng, CCSoundInstance inst) {
    CCAudioSystem* s = audio_sys(eng);
    if (inst == 0 || inst >= CC_MAX_INSTANCES || !s->instances[inst].active) return;
#ifdef CC_USE_AUDIO
    if (s->instances[inst].has_al) {
        alSourceStop(s->instances[inst].al_source);
        alDeleteSources(1, &s->instances[inst].al_source);
        s->instances[inst].has_al = false;
    }
#endif
    s->instances[inst].active = false;
}

void cc_audio_stop_all(CCEngine* eng) {
    CCAudioSystem* s = audio_sys(eng);
    for (uint32_t i = 1; i < CC_MAX_INSTANCES; i++)
        if (s->instances[i].active) cc_audio_stop(eng, i);
}

bool cc_audio_playing(CCEngine* eng, CCSoundInstance inst) {
    CCAudioSystem* s = audio_sys(eng);
    if (inst == 0 || inst >= CC_MAX_INSTANCES || !s->instances[inst].active) return false;
#ifdef CC_USE_AUDIO
    if (s->instances[inst].has_al) {
        ALint st; alGetSourcei(s->instances[inst].al_source, AL_SOURCE_STATE, &st);
        if (st != AL_PLAYING) { s->instances[inst].active = false; return false; }
        return true;
    }
#endif
    /* No AL source: use the timer. Looping sounds play until explicitly stopped. */
    if (s->instances[inst].paused) return true;
    if (s->instances[inst].loop) return true;
    double now = (double)cc_now_ns() * 1e-9;
    if (now >= s->instances[inst].end_time) { s->instances[inst].active = false; return false; }
    return true;
}

void cc_audio_set_master_volume(CCEngine* eng, float vol) {
    CCAudioSystem* s = audio_sys(eng);
    s->master_volume = vol < 0 ? 0 : vol;
#ifdef CC_USE_AUDIO
    if (s->device_ok) alListenerf(AL_GAIN, s->master_volume);
#endif
}

float cc_audio_master_volume(CCEngine* eng) {
    return audio_sys(eng)->master_volume;
}

void cc_audio_set_listener(CCEngine* eng, float x, float y, float z,
                            float fx, float fy, float fz) {
    CCAudioSystem* s = audio_sys(eng);
#ifdef CC_USE_AUDIO
    if (s->device_ok) {
        alListener3f(AL_POSITION, x, y, z);
        float ori[6] = { fx, fy, fz, 0, 1, 0 }; /* forward + up */
        alListenerfv(AL_ORIENTATION, ori);
    }
#else
    (void)s;(void)x;(void)y;(void)z;(void)fx;(void)fy;(void)fz;
#endif
}

void cc_sound_destroy(CCEngine* eng, CCSoundId id) {
    CCAudioSystem* s = audio_sys(eng);
    if (id == 0 || id >= CC_MAX_SOUNDS || !s->sounds[id].valid) return;
    free(s->sounds[id].pcm);
#ifdef CC_USE_AUDIO
    if (s->sounds[id].al_buffer) alDeleteBuffers(1, &s->sounds[id].al_buffer);
#endif
    memset(&s->sounds[id], 0, sizeof(CCSound));
}

/* ─── Control layer: effective gain, update, fades, pause, buses, music ── */

static float inst_effective_gain(CCAudioSystem* s, CCSoundInst* in) {
    float bus = (in->bus < CC_MAX_BUSES && s->buses[in->bus].used) ? s->buses[in->bus].volume : 1.0f;
    return in->base_volume * bus * s->master_volume;
}
#ifdef CC_USE_AUDIO
static void inst_push_gain(CCAudioSystem* s, CCSoundInst* in) {
    if (in->has_al) alSourcef(in->al_source, AL_GAIN, inst_effective_gain(s, in));
}
#else
static void inst_push_gain(CCAudioSystem* s, CCSoundInst* in){ (void)s;(void)in; }
#endif

const char* cc_audio_device_name(CCEngine* eng){
    CCAudioSystem* s = audio_sys(eng);
#ifdef CC_USE_AUDIO
    if(s->device_ok && s->device) return alcGetString(s->device, ALC_DEVICE_SPECIFIER);
    return "(no device — silent)";
#else
    (void)s; return "(no OpenAL — silent)";
#endif
}
int cc_audio_is_online(CCEngine* eng){ CCAudioSystem* s=audio_sys(eng); return s && s->device_ok; }
void cc_audio_update(CCEngine* eng, float dt) {
    CCAudioSystem* s = audio_sys(eng);
    if (dt < 0) dt = 0;
    for (uint32_t i=1;i<CC_MAX_INSTANCES;i++) {
        CCSoundInst* in=&s->instances[i];
        if (!in->active || in->paused) continue;
        if (in->fade_dur > 0.0f) {
            in->fade_t += dt;
            float k = in->fade_t / in->fade_dur; if (k>1.0f) k=1.0f;
            in->base_volume = in->fade_from + (in->fade_to - in->fade_from)*k;
            inst_push_gain(s, in);
            if (k >= 1.0f) {
                in->fade_dur = 0.0f;
                if (in->stop_on_fade_end) cc_audio_stop(eng, i);
            }
        }
    }
}

void cc_audio_fade_to(CCEngine* eng, CCSoundInstance inst, float target, float seconds) {
    CCAudioSystem* s = audio_sys(eng);
    if (inst==0 || inst>=CC_MAX_INSTANCES || !s->instances[inst].active) return;
    CCSoundInst* in=&s->instances[inst];
    if (seconds <= 0.0f) { in->base_volume = target; in->fade_dur=0; inst_push_gain(s,in); return; }
    in->fade_from = in->base_volume; in->fade_to = target;
    in->fade_t = 0.0f; in->fade_dur = seconds; in->stop_on_fade_end = false;
}
void cc_audio_fade_in(CCEngine* eng, CCSoundInstance inst, float seconds) {
    CCAudioSystem* s = audio_sys(eng);
    if (inst==0 || inst>=CC_MAX_INSTANCES || !s->instances[inst].active) return;
    s->instances[inst].base_volume = 0.0f; inst_push_gain(s,&s->instances[inst]);
    cc_audio_fade_to(eng, inst, 1.0f, seconds);
}
void cc_audio_fade_out(CCEngine* eng, CCSoundInstance inst, float seconds) {
    CCAudioSystem* s = audio_sys(eng);
    if (inst==0 || inst>=CC_MAX_INSTANCES || !s->instances[inst].active) return;
    if (seconds <= 0.0f) { cc_audio_stop(eng, inst); return; }
    cc_audio_fade_to(eng, inst, 0.0f, seconds);
    s->instances[inst].stop_on_fade_end = true;
}

void cc_audio_pause(CCEngine* eng, CCSoundInstance inst) {
    CCAudioSystem* s = audio_sys(eng);
    if (inst==0 || inst>=CC_MAX_INSTANCES || !s->instances[inst].active) return;
    CCSoundInst* in=&s->instances[inst];
    if (in->paused) return;
    in->paused = true;
    in->pause_started = (double)cc_now_ns()*1e-9;
#ifdef CC_USE_AUDIO
    if (in->has_al) alSourcePause(in->al_source);
#endif
}
void cc_audio_resume(CCEngine* eng, CCSoundInstance inst) {
    CCAudioSystem* s = audio_sys(eng);
    if (inst==0 || inst>=CC_MAX_INSTANCES || !s->instances[inst].active) return;
    CCSoundInst* in=&s->instances[inst];
    if (!in->paused) return;
    in->paused = false;
    /* shift the timer end forward by however long we were paused */
    double now=(double)cc_now_ns()*1e-9;
    in->end_time += (now - in->pause_started);
#ifdef CC_USE_AUDIO
    if (in->has_al) alSourcePlay(in->al_source);
#endif
}
void cc_audio_pause_all(CCEngine* eng) {
    for (uint32_t i=1;i<CC_MAX_INSTANCES;i++) cc_audio_pause(eng,i);
}
void cc_audio_resume_all(CCEngine* eng) {
    for (uint32_t i=1;i<CC_MAX_INSTANCES;i++) cc_audio_resume(eng,i);
}
bool cc_audio_paused(CCEngine* eng, CCSoundInstance inst) {
    CCAudioSystem* s = audio_sys(eng);
    if (inst==0 || inst>=CC_MAX_INSTANCES || !s->instances[inst].active) return false;
    return s->instances[inst].paused;
}

/* ─── Buses ──────────────────────────────────────────────────────────── */
CCAudioBus cc_audio_bus(CCEngine* eng, const char* name) {
    CCAudioSystem* s = audio_sys(eng);
    if (!name) return 0;
    for (uint32_t i=0;i<CC_MAX_BUSES;i++)
        if (s->buses[i].used && strncmp(s->buses[i].name,name,23)==0) return i;
    for (uint32_t i=0;i<CC_MAX_BUSES;i++)
        if (!s->buses[i].used) { s->buses[i].used=true; s->buses[i].volume=1.0f;
            snprintf(s->buses[i].name,24,"%s",name); return i; }
    return 0;
}
void cc_audio_set_bus_volume(CCEngine* eng, CCAudioBus bus, float vol) {
    CCAudioSystem* s = audio_sys(eng);
    if (bus>=CC_MAX_BUSES || !s->buses[bus].used) return;
    s->buses[bus].volume = vol<0?0:vol;
    /* re-push gain to all instances on this bus */
    for (uint32_t i=1;i<CC_MAX_INSTANCES;i++)
        if (s->instances[i].active && s->instances[i].bus==bus) inst_push_gain(s,&s->instances[i]);
}
float cc_audio_bus_volume(CCEngine* eng, CCAudioBus bus) {
    CCAudioSystem* s = audio_sys(eng);
    return (bus<CC_MAX_BUSES && s->buses[bus].used) ? s->buses[bus].volume : 0.0f;
}
void cc_audio_instance_set_bus(CCEngine* eng, CCSoundInstance inst, CCAudioBus bus) {
    CCAudioSystem* s = audio_sys(eng);
    if (inst==0 || inst>=CC_MAX_INSTANCES || !s->instances[inst].active) return;
    if (bus>=CC_MAX_BUSES || !s->buses[bus].used) return;
    s->instances[inst].bus = bus;
    inst_push_gain(s,&s->instances[inst]);
}

CCSoundInstance cc_audio_play_bus(CCEngine* eng, CCSoundId id, CCAudioBus bus,
                                   float volume, float pitch, bool loop) {
    CCSoundInstance inst = cc_audio_play(eng, id, volume, pitch, loop);
    if (inst) cc_audio_instance_set_bus(eng, inst, bus);
    return inst;
}

/* ─── Music (single active track, crossfaded) ────────────────────────── */
CCSoundInstance cc_audio_play_music(CCEngine* eng, CCSoundId id, float volume, float fade) {
    CCAudioSystem* s = audio_sys(eng);
    /* fade out the outgoing track */
    if (s->current_music && s->instances[s->current_music].active) {
        if (fade > 0) cc_audio_fade_out(eng, s->current_music, fade);
        else cc_audio_stop(eng, s->current_music);
    }
    CCSoundInstance m = cc_audio_play_bus(eng, id, s->music_bus, volume, 1.0f, true);
    if (m && fade > 0) cc_audio_fade_in(eng, m, fade), s->instances[m].fade_to = volume,
                       s->instances[m].fade_from = 0.0f;
    s->current_music = m;
    return m;
}
void cc_audio_stop_music(CCEngine* eng, float fade) {
    CCAudioSystem* s = audio_sys(eng);
    if (!s->current_music) return;
    if (fade > 0) cc_audio_fade_out(eng, s->current_music, fade);
    else cc_audio_stop(eng, s->current_music);
    s->current_music = 0;
}

/* ─── 3D distance model ──────────────────────────────────────────────── */
void cc_audio_set_rolloff(CCEngine* eng, float ref, float max, float factor) {
    CCAudioSystem* s = audio_sys(eng);
    s->roll_ref = ref>0?ref:1.0f;
    s->roll_max = max>0?max:100.0f;
    s->roll_factor = factor>=0?factor:1.0f;
#ifdef CC_USE_AUDIO
    if (s->device_ok) {
        alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
        for (uint32_t i=1;i<CC_MAX_INSTANCES;i++)
            if (s->instances[i].active && s->instances[i].has_al) {
                alSourcef(s->instances[i].al_source, AL_REFERENCE_DISTANCE, s->roll_ref);
                alSourcef(s->instances[i].al_source, AL_MAX_DISTANCE, s->roll_max);
                alSourcef(s->instances[i].al_source, AL_ROLLOFF_FACTOR, s->roll_factor);
            }
    }
#endif
}


const int16_t* cc_audio_debug_pcm(CCEngine* eng, CCSoundId id, uint32_t* out_frames, uint32_t* out_ch) {
    CCAudioSystem* s = audio_sys(eng);
    if (id == 0 || id >= CC_MAX_SOUNDS || !s->sounds[id].valid) { if(out_frames)*out_frames=0; return NULL; }
    if (out_frames) *out_frames = s->sounds[id].frames;
    if (out_ch) *out_ch = s->sounds[id].channels;
    return s->sounds[id].pcm;
}
