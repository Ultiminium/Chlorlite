/*
 * cc-sandbox — Chlorlite headless runner
 * Usage: cc-sandbox [--ticks N] [--screenshot N] [--fps N] [game.so]
 */
#include "cc/claudecore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

int main(int argc, char** argv) {
    uint64_t max_ticks=120; int ss_every=30; double fps=60.0;
    const char* game=NULL;
    for (int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--ticks")&&i+1<argc)   max_ticks=atoll(argv[++i]);
        else if(!strcmp(argv[i],"--screenshot")&&i+1<argc) ss_every=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--fps")&&i+1<argc) fps=atof(argv[++i]);
        else if(argv[i][0]!='-') game=argv[i];
    }
    CCEngineConfig cfg = cc_sandbox_config();
    CCEngine* eng = cc_init(&cfg);

    typedef void(*GameFn)(CCEngine*);
    typedef void(*GameTickFn)(CCEngine*,double);
    GameFn   on_init=NULL; GameTickFn on_tick=NULL; GameFn on_shutdown=NULL;
    void* dl=NULL;
    if (game) {
        dl=dlopen(game,RTLD_NOW); if(!dl){fprintf(stderr,"dlopen: %s\n",dlerror());return 1;}
        on_init=(GameFn)dlsym(dl,"cc_game_init");
        on_tick=(GameTickFn)dlsym(dl,"cc_game_tick");
        on_shutdown=(GameFn)dlsym(dl,"cc_game_shutdown");
    }

    CCScene* scene = cc_scene_create(eng, "main");
    cc_scene_set_active(eng, scene);
    if (on_init) on_init(eng);

    double dt=1.0/fps;
    for (uint64_t t=0; t<max_ticks; t++) {
        cc_frame_begin(eng);
        cc_tick(eng, dt);
        if (on_tick) on_tick(eng, dt);
        cc_frame_end(eng);
        if (ss_every>0 && t%(uint64_t)ss_every==0)
            printf("Screenshot: %s\n", cc_screenshot(eng, NULL));
    }
    CC_INFO("Done. %llu frames.", (unsigned long long)max_ticks);
    if (on_shutdown) on_shutdown(eng);
    if (dl) dlclose(dl);
    cc_scene_destroy(scene);
    cc_shutdown(eng);
    return 0;
}
