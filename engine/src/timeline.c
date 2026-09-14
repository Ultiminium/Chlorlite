/*
 * timeline.c — generic timeline mechanism (see cc/timeline.h).
 *
 * The one non-trivial part is cc_timeline_advance: moving time from t0 to t1 must
 * fire every phase/window/marker boundary in (t0, t1] IN TIME ORDER, and wrap
 * correctly when looping. We collect the boundaries crossed, sort by time, and
 * dispatch. Spans are few per timeline (a move has a handful), so this is cheap.
 */
#include "cc/timeline.h"
#include <stdlib.h>
#include <string.h>

#define MAX_SPANS   32
#define MAX_MARKERS 32

typedef struct { uint32_t id; const char* name; float start, end; bool open; } Span;
typedef struct { uint32_t id; const char* name; float t; } Marker;

struct CCTimeline {
    float    duration;
    bool     loop;
    float    t;
    bool     playing, finished;

    Span     phases[MAX_SPANS];   int nphase;
    Span     windows[MAX_SPANS];  int nwindow;
    Marker   markers[MAX_MARKERS];int nmarker;
    uint32_t cur_phase;           /* id or CC_TL_NONE */
    const char* cur_phase_name;

    CCTimelineCallback cb;
    void*    user;
};

CCTimeline* cc_timeline_create(float duration, bool loop){
    CCTimeline* tl = (CCTimeline*)calloc(1,sizeof(CCTimeline));
    if(!tl) return NULL;
    tl->duration = duration>0?duration:1.0f;
    tl->loop = loop;
    tl->cur_phase = CC_TL_NONE;
    return tl;
}
void cc_timeline_destroy(CCTimeline* tl){ free(tl); }
void cc_timeline_set_callback(CCTimeline* tl, CCTimelineCallback cb, void* u){ if(tl){tl->cb=cb;tl->user=u;} }

void cc_timeline_add_phase(CCTimeline* tl, uint32_t id, const char* name, float s, float e){
    if(!tl||tl->nphase>=MAX_SPANS) return;
    tl->phases[tl->nphase++] = (Span){id,name,s,e,false};
}
void cc_timeline_add_window(CCTimeline* tl, uint32_t id, const char* name, float s, float e){
    if(!tl||tl->nwindow>=MAX_SPANS) return;
    tl->windows[tl->nwindow++] = (Span){id,name,s,e,false};
}
void cc_timeline_add_marker(CCTimeline* tl, uint32_t id, const char* name, float t){
    if(!tl||tl->nmarker>=MAX_MARKERS) return;
    tl->markers[tl->nmarker++] = (Marker){id,name,t};
}

void cc_timeline_play(CCTimeline* tl){ if(!tl)return; tl->t=0; tl->playing=true; tl->finished=false;
    for(int i=0;i<tl->nwindow;i++) tl->windows[i].open=false;
    tl->cur_phase=CC_TL_NONE; tl->cur_phase_name=NULL; }
void cc_timeline_stop(CCTimeline* tl){ if(tl) tl->playing=false; }
void cc_timeline_reset(CCTimeline* tl){ if(!tl)return; tl->t=0; tl->playing=false; tl->finished=false;
    for(int i=0;i<tl->nwindow;i++) tl->windows[i].open=false; tl->cur_phase=CC_TL_NONE; tl->cur_phase_name=NULL; }
void cc_timeline_seek(CCTimeline* tl, float t){ if(tl){ tl->t = t<0?0:(t>tl->duration?tl->duration:t); } }

/* ── boundary collection for a monotonic sweep t0 -> t1 (no wrap) ──────────
   Emits: window opens/closes, markers, and phase changes. We dispatch phase
   changes by sampling the phase at t1 vs the current phase (phases tile, so the
   "current phase" is whichever span contains the time). */
static void fire(CCTimeline* tl, CCTimelineEventType type, uint32_t id, const char* name, float t){
    if(tl->cb){ CCTimelineEvent ev={type,name,id,t,tl->user}; tl->cb(&ev,tl->user); }
}

/* dispatch all window open/close + marker boundaries in (t0, t1], in time order */
typedef struct { float t; int kind; int idx; } Bnd;  /* kind: 0=win-open 1=win-close 2=marker */
static int bnd_cmp(const void* a, const void* b){
    float d = ((const Bnd*)a)->t - ((const Bnd*)b)->t;
    return d<0?-1:(d>0?1:0);
}
static void sweep(CCTimeline* tl, float t0, float t1){
    Bnd list[MAX_SPANS*2+MAX_MARKERS]; int n=0;
    for(int i=0;i<tl->nwindow;i++){
        float s=tl->windows[i].start, e=tl->windows[i].end;
        if(s>t0 && s<=t1) list[n++]=(Bnd){s,0,i};
        if(e>t0 && e<=t1) list[n++]=(Bnd){e,1,i};
    }
    for(int i=0;i<tl->nmarker;i++){
        float mt=tl->markers[i].t;
        if(mt>t0 && mt<=t1) list[n++]=(Bnd){mt,2,i};
    }
    qsort(list,n,sizeof(Bnd),bnd_cmp);
    for(int i=0;i<n;i++){
        if(list[i].kind==0){ Span* w=&tl->windows[list[i].idx]; w->open=true;
            fire(tl,CC_TL_WINDOW_OPEN,w->id,w->name,list[i].t); }
        else if(list[i].kind==1){ Span* w=&tl->windows[list[i].idx]; w->open=false;
            fire(tl,CC_TL_WINDOW_CLOSE,w->id,w->name,list[i].t); }
        else { Marker* m=&tl->markers[list[i].idx];
            fire(tl,CC_TL_MARKER,m->id,m->name,list[i].t); }
    }
    /* phase change: find phase containing t1 */
    uint32_t np=CC_TL_NONE; const char* nn=NULL;
    for(int i=0;i<tl->nphase;i++){
        if(t1>=tl->phases[i].start && t1<tl->phases[i].end){ np=tl->phases[i].id; nn=tl->phases[i].name; break; }
    }
    if(np!=tl->cur_phase){
        if(tl->cur_phase!=CC_TL_NONE) fire(tl,CC_TL_PHASE_EXIT,tl->cur_phase,tl->cur_phase_name,t1);
        tl->cur_phase=np; tl->cur_phase_name=nn;
        if(np!=CC_TL_NONE) fire(tl,CC_TL_PHASE_ENTER,np,nn,t1);
    }
}

bool cc_timeline_advance(CCTimeline* tl, float dt){
    if(!tl || !tl->playing || tl->finished) return tl && !tl->finished;
    float t0 = tl->t;
    float t1 = t0 + dt;
    if(t1 < tl->duration){
        sweep(tl,t0,t1);
        tl->t=t1;
        return true;
    }
    /* crossed the end */
    sweep(tl,t0,tl->duration);
    if(tl->loop){
        float over = t1 - tl->duration;
        /* close any windows still open at end, reset phase, then wrap */
        for(int i=0;i<tl->nwindow;i++) if(tl->windows[i].open){ tl->windows[i].open=false;
            fire(tl,CC_TL_WINDOW_CLOSE,tl->windows[i].id,tl->windows[i].name,tl->duration); }
        if(tl->cur_phase!=CC_TL_NONE){ fire(tl,CC_TL_PHASE_EXIT,tl->cur_phase,tl->cur_phase_name,tl->duration);
            tl->cur_phase=CC_TL_NONE; tl->cur_phase_name=NULL; }
        fire(tl,CC_TL_FINISHED,0,NULL,tl->duration);
        while(over>=tl->duration) over-=tl->duration;   /* handle absurd dt */
        tl->t=0;
        sweep(tl,0.0f-1e-6f,over);   /* fire boundaries in the wrapped remainder */
        tl->t=over;
        return true;
    } else {
        for(int i=0;i<tl->nwindow;i++) if(tl->windows[i].open){ tl->windows[i].open=false;
            fire(tl,CC_TL_WINDOW_CLOSE,tl->windows[i].id,tl->windows[i].name,tl->duration); }
        if(tl->cur_phase!=CC_TL_NONE){ fire(tl,CC_TL_PHASE_EXIT,tl->cur_phase,tl->cur_phase_name,tl->duration);
            tl->cur_phase=CC_TL_NONE; tl->cur_phase_name=NULL; }
        tl->t=tl->duration; tl->playing=false; tl->finished=true;
        fire(tl,CC_TL_FINISHED,0,NULL,tl->duration);
        return false;
    }
}

float       cc_timeline_time(const CCTimeline* tl){ return tl?tl->t:0; }
bool        cc_timeline_playing(const CCTimeline* tl){ return tl&&tl->playing; }
bool        cc_timeline_finished(const CCTimeline* tl){ return tl&&tl->finished; }
uint32_t    cc_timeline_current_phase(const CCTimeline* tl){ return tl?tl->cur_phase:CC_TL_NONE; }
const char* cc_timeline_current_phase_name(const CCTimeline* tl){ return tl?tl->cur_phase_name:NULL; }
bool        cc_timeline_window_open(const CCTimeline* tl, uint32_t id){
    if(!tl) return false;
    for(int i=0;i<tl->nwindow;i++) if(tl->windows[i].id==id) return tl->windows[i].open;
    return false;
}
bool        cc_timeline_window_open_name(const CCTimeline* tl, const char* name){
    if(!tl||!name) return false;
    for(int i=0;i<tl->nwindow;i++) if(tl->windows[i].name && strcmp(tl->windows[i].name,name)==0) return tl->windows[i].open;
    return false;
}
