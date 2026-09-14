#include "cc/inputrec.h"
#include "cc/input.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    double  t;       /* seconds since recording start */
    QEvent  ev;
} RecEntry;

struct CCInputRec {
    RecEntry* items;
    uint32_t  count, cap;
    /* recording */
    int       recording;
    double    rec_start;      /* first-capture time offset */
    int       rec_started;
    /* playback */
    int       playing;
    uint32_t  play_cursor;    /* next event index to inject */
};

CCInputRec* cc_inputrec_create(void){ return (CCInputRec*)calloc(1,sizeof(CCInputRec)); }
void cc_inputrec_destroy(CCInputRec* r){ if(r){ free(r->items); free(r); } }
void cc_inputrec_clear(CCInputRec* r){
    if(!r) return; r->count=0; r->play_cursor=0; r->rec_started=0; r->recording=0; r->playing=0;
}

static void rec_push(CCInputRec* r, double t, const QEvent* ev){
    if(r->count>=r->cap){ r->cap=r->cap?r->cap*2:64; r->items=realloc(r->items,r->cap*sizeof(RecEntry)); }
    r->items[r->count].t=t; r->items[r->count].ev=*ev; r->count++;
}

/* ─── recording ─── */
void cc_inputrec_start_recording(CCInputRec* r){
    if(!r) return; r->count=0; r->recording=1; r->rec_started=0; r->playing=0;
}
void cc_inputrec_capture(CCInputRec* r, double t, const QEvent* events, uint32_t n){
    if(!r||!r->recording||!events) return;
    if(!r->rec_started){ r->rec_start=t; r->rec_started=1; }
    double rel=t - r->rec_start;
    for(uint32_t i=0;i<n;i++) rec_push(r, rel, &events[i]);
}
uint32_t cc_inputrec_event_count(const CCInputRec* r){ return r? r->count : 0; }
double cc_inputrec_duration(const CCInputRec* r){
    return (r && r->count) ? r->items[r->count-1].t : 0.0;
}

/* ─── playback ─── */
void cc_inputrec_start_playback(CCInputRec* r){
    if(!r) return; r->playing=1; r->recording=0; r->play_cursor=0;
}
void cc_inputrec_play(CCInputRec* r, CCEngine* eng, double t){
    if(!r||!eng||!r->playing) return;
    while(r->play_cursor < r->count && r->items[r->play_cursor].t <= t){
        cc_input_inject(eng, &r->items[r->play_cursor].ev);
        r->play_cursor++;
    }
    if(r->play_cursor>=r->count) r->playing=0;
}
bool cc_inputrec_finished(const CCInputRec* r){
    return !r || r->play_cursor>=r->count;
}

/* ─── file I/O ─── */
#define CCREC_MAGIC 0x43435231u  /* "CCR1" */
bool cc_inputrec_save(const CCInputRec* r, const char* path){
    if(!r) return false;
    FILE* f=fopen(path,"wb");
    if(!f){ fprintf(stderr,"cc_inputrec_save: cannot open %s\n",path); return false; }
    uint32_t magic=CCREC_MAGIC, count=r->count, esz=(uint32_t)sizeof(RecEntry);
    fwrite(&magic,4,1,f); fwrite(&count,4,1,f); fwrite(&esz,4,1,f);
    if(count) fwrite(r->items, sizeof(RecEntry), count, f);
    fclose(f);
    return true;
}
CCInputRec* cc_inputrec_load(const char* path){
    FILE* f=fopen(path,"rb");
    if(!f){ fprintf(stderr,"cc_inputrec_load: cannot open %s\n",path); return NULL; }
    uint32_t magic=0,count=0,esz=0;
    if(fread(&magic,4,1,f)!=1 || fread(&count,4,1,f)!=1 || fread(&esz,4,1,f)!=1){ fclose(f); return NULL; }
    if(magic!=CCREC_MAGIC || esz!=(uint32_t)sizeof(RecEntry)){
        fprintf(stderr,"cc_inputrec_load: bad/incompatible .ccrec\n"); fclose(f); return NULL;
    }
    CCInputRec* r=cc_inputrec_create();
    if(count){
        r->items=(RecEntry*)malloc(count*sizeof(RecEntry));
        r->cap=count;
        if(fread(r->items,sizeof(RecEntry),count,f)!=count){ cc_inputrec_destroy(r); fclose(f); return NULL; }
        r->count=count;
    }
    fclose(f);
    return r;
}
