/* Audio test — audio is invisible, so this VISUALIZES the real audio system:
 * (1) plots the actual generated PCM of several synth sounds as 3D bar
 * waveforms (sampling cc_audio_debug_pcm), and (2) shows live mixer state —
 * bus volumes + a music crossfade in progress — as colored level bars. Proves
 * synthesis produces real signals and the control layer (buses/fades/music)
 * runs. Rendered headless. */
#include "cc/claudecore.h"
#include "cc/audio.h"
#include <math.h>
#include <stdio.h>

extern const int16_t* cc_audio_debug_pcm(CCEngine*,CCSoundId,uint32_t*,uint32_t*);

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/audio_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh bar = cc_mesh_cube(eng,1.0f);

    CCMaterialDesc wd[4]={
        {.base_color={0.9f,0.4f,0.3f,1},.roughness=0.5f,.emissive={0.15f,0.03f,0.02f}},
        {.base_color={0.4f,0.7f,0.95f,1},.roughness=0.5f,.emissive={0.02f,0.08f,0.15f}},
        {.base_color={0.5f,0.9f,0.5f,1},.roughness=0.5f,.emissive={0.03f,0.15f,0.03f}},
        {.base_color={0.9f,0.8f,0.4f,1},.roughness=0.5f,.emissive={0.15f,0.12f,0.02f}},
    };
    CCMaterial wm[4]; for(int i=0;i<4;i++) wm[i]=cc_material_create(eng,&wd[i]);
    CCMaterialDesc gd={.base_color={0.5f,0.52f,0.55f,1},.roughness=0.95f};
    CCMaterial mg=cc_material_create(eng,&gd);

    cc_light_set_ambient(eng,0.2f,0.21f,0.24f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.9f,-0.35f},
                 .color={1,0.96f,0.9f},.intensity=2.6f,.cast_shadows=true};
    cc_light_add(eng,&sun);
    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.0f; fx.bloom_intensity=0.12f;
    fx.vignette=true; fx.vignette_strength=0.3f;
    cc_postfx_set(eng,&fx);

    /* generate four sounds */
    CCSoundId snd[4];
    snd[0]=cc_sound_beep(eng,440.0f,1.0f);
    { CCProceduralToneDesc d={0}; d.waveform=CC_WAVE_SQUARE; d.frequency=220; d.duration=1.0f;
      d.amplitude=0.8f; d.sustain=1.0f; snd[1]=cc_sound_procedural_tone(eng,&d); }
    snd[2]=cc_sound_laser(eng,1400.0f,200.0f,0.6f);
    snd[3]=cc_sound_explosion(eng);

    /* --- exercise the control layer so mixer bars show real state --- */
    CCAudioBus sfx=cc_audio_bus(eng,"sfx");
    CCAudioBus ui =cc_audio_bus(eng,"ui");
    cc_audio_set_bus_volume(eng,sfx,0.7f);
    cc_audio_set_bus_volume(eng,ui,0.4f);
    CCSoundInstance mus=cc_audio_play_music(eng,snd[0],1.0f,0.5f);
    cc_audio_play_music(eng,snd[1],1.0f,0.5f);   /* crossfade */
    for(int i=0;i<15;i++) cc_audio_update(eng,1.0f/60.0f); /* advance the crossfade partway */
    (void)mus;

    float bus_levels[4]={
        cc_audio_master_volume(eng),
        cc_audio_bus_volume(eng, cc_audio_bus(eng,"music")),
        cc_audio_bus_volume(eng,sfx),
        cc_audio_bus_volume(eng,ui),
    };

    /* --- render: 4 waveform strips + a mixer bar group --- */
    const int NBARS=48;
    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,9,15},.target={0,0,-1},.up={0,1,0},
                          .fov_deg=50,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,-0.05f,-2},.rot={0,0,0,1},.scale={30,0.1f,16}};
        cc_draw_mesh(eng,bar,mg,&gxf);

        /* waveform strips, one per sound, stacked in Z */
        for(int si=0; si<4; si++){
            uint32_t n=0,ch=0; const int16_t* p=cc_audio_debug_pcm(eng,snd[si],&n,&ch);
            if(!p||!n) continue;
            float z = -6.0f + si*2.2f;
            for(int b=0;b<NBARS;b++){
                /* peak amplitude in this window → bar height */
                uint32_t a0=(uint32_t)((uint64_t)n*b/NBARS), a1=(uint32_t)((uint64_t)n*(b+1)/NBARS);
                int16_t pk=0; for(uint32_t i=a0*ch;i<a1*ch;i++){int16_t v=p[i]<0?-p[i]:p[i]; if(v>pk)pk=v;}
                float h = (pk/32767.0f)*3.0f + 0.02f;
                float x = -11.5f + b*(23.0f/NBARS);
                CCTransform3D t={.pos={x,h*0.5f,z},.rot={0,0,0,1},.scale={0.36f,h,0.8f}};
                cc_draw_mesh(eng,bar,wm[si],&t);
            }
        }

        /* mixer bars (master/music/sfx/ui) on the right, tall & emissive */
        for(int m=0;m<4;m++){
            float h=bus_levels[m]*4.5f+0.05f;
            CCTransform3D t={.pos={7.0f+m*1.15f, h*0.5f, 2.5f},.rot={0,0,0,1},.scale={0.85f,h,0.85f}};
            cc_draw_mesh(eng,bar,wm[m],&t);
        }
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s  buses[master,music,sfx,ui]=%.2f,%.2f,%.2f,%.2f\n",
        s?s:"(null)", bus_levels[0],bus_levels[1],bus_levels[2],bus_levels[3]);
    cc_shutdown(eng); return 0;
}
