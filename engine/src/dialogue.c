#include "cc/dialogue.h"
#include "cc/event.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_CHOICES 8

typedef struct {
    char text[192];
    char target[64];
    char gate[64];      /* required flag, or "" */
} DlgChoice;

typedef struct {
    char      id[64];
    char      speaker[64];
    char      text[512];
    char      action[128];    /* fires on entry, or "" */
    char      goto_id[64];    /* auto-advance target, or "" */
    DlgChoice choices[MAX_CHOICES];
    uint32_t  choice_count;
    int       is_end;
} DlgNode;

struct CCDialogue {
    DlgNode* nodes;
    uint32_t count, cap;
};

struct CCDialogueRunner {
    CCDialogue*     d;
    int             cur;        /* index into nodes, -1 = finished */
    CCDialogueHooks hooks;
    /* cached visible choices for the current node */
    uint32_t vis[MAX_CHOICES];
    uint32_t vis_count;
    /* optional event bus (NULL = off) + caller-chosen conversation id (sender) */
    CCEventBus* bus;
    uint64_t    convo_id;
    int         started_emitted; /* so STARTED fires once */
};

/* ─── parsing ─── */
static void dlg_strip(char* s){
    for(char* p=s;*p;p++){ if(*p=='#'){ *p=0; break; } }
    size_t n=strlen(s);
    while(n && (s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' '||s[n-1]=='\t')) s[--n]=0;
}
static char* dlg_skipws(char* s){ while(*s==' '||*s=='\t') s++; return s; }

static DlgNode* dlg_add_node(CCDialogue* d, const char* id){
    if(d->count>=d->cap){ d->cap=d->cap?d->cap*2:16; d->nodes=realloc(d->nodes,d->cap*sizeof(DlgNode)); }
    DlgNode* n=&d->nodes[d->count++];
    memset(n,0,sizeof(*n));
    snprintf(n->id,sizeof(n->id),"%s",id);
    return n;
}
static int dlg_find(const CCDialogue* d, const char* id){
    for(uint32_t i=0;i<d->count;i++) if(!strcmp(d->nodes[i].id,id)) return (int)i;
    return -1;
}

CCDialogue* cc_dialogue_parse(const char* text){
    if(!text) return NULL;
    CCDialogue* d=(CCDialogue*)calloc(1,sizeof(CCDialogue));
    DlgNode* cur=NULL;
    /* work on a mutable copy, split into lines */
    char* buf=strdup(text);
    char* save=NULL;
    int header_ok=0;
    for(char* line=strtok_r(buf,"\n",&save); line; line=strtok_r(NULL,"\n",&save)){
        char tmp[1024]; snprintf(tmp,sizeof(tmp),"%s",line);
        dlg_strip(tmp);
        char* p=dlg_skipws(tmp);
        if(!*p) continue;
        if(!header_ok){
            int ver=0;
            if(sscanf(p,"ccdlg %d",&ver)==1 && ver==1){ header_ok=1; continue; }
            /* tolerate missing header: treat as v1 */
            header_ok=1;
        }
        char kw[32]={0}; sscanf(p,"%31s",kw);
        if(!strcmp(kw,"node")){
            char id[64]={0}; sscanf(dlg_skipws(p+4),"%63s",id);
            cur=dlg_add_node(d,id);
        } else if(!cur){
            continue;   /* content before first node: ignore */
        } else if(!strcmp(kw,"speaker")){
            snprintf(cur->speaker,sizeof(cur->speaker),"%s",dlg_skipws(p+7));
        } else if(!strcmp(kw,"text")){
            snprintf(cur->text,sizeof(cur->text),"%s",dlg_skipws(p+4));
        } else if(!strcmp(kw,"action")){
            snprintf(cur->action,sizeof(cur->action),"%s",dlg_skipws(p+6));
        } else if(!strcmp(kw,"goto")){
            snprintf(cur->goto_id,sizeof(cur->goto_id),"%s",dlg_skipws(p+4));
        } else if(!strcmp(kw,"end")){
            cur->is_end=1;
        } else if(!strcmp(kw,"choice")){
            if(cur->choice_count<MAX_CHOICES){
                DlgChoice* c=&cur->choices[cur->choice_count];
                memset(c,0,sizeof(*c));
                char* rest=dlg_skipws(p+6);
                /* optional [gate] prefix */
                if(*rest=='['){
                    char* close=strchr(rest,']');
                    if(close){
                        size_t gl=(size_t)(close-rest-1);
                        if(gl>63)gl=63; memcpy(c->gate,rest+1,gl); c->gate[gl]=0;
                        rest=dlg_skipws(close+1);
                    }
                }
                /* split on "->" */
                char* arrow=strstr(rest,"->");
                if(arrow){
                    *arrow=0;
                    char* ttext=rest; char* ttarget=dlg_skipws(arrow+2);
                    /* trim trailing space on choice text */
                    size_t tl=strlen(ttext); while(tl && (ttext[tl-1]==' '||ttext[tl-1]=='\t')) ttext[--tl]=0;
                    snprintf(c->text,sizeof(c->text),"%s",ttext);
                    snprintf(c->target,sizeof(c->target),"%s",ttarget);
                    cur->choice_count++;
                }
            }
        }
        /* unknown keywords ignored */
    }
    free(buf);
    if(d->count==0){ free(d); return NULL; }
    return d;
}

CCDialogue* cc_dialogue_load(const char* path){
    FILE* f=fopen(path,"rb");
    if(!f){ fprintf(stderr,"cc_dialogue_load: cannot open %s\n",path); return NULL; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    if(sz<=0){ fclose(f); return NULL; }
    char* buf=(char*)malloc((size_t)sz+1);
    size_t got=fread(buf,1,(size_t)sz,f); buf[got]=0; fclose(f);
    CCDialogue* d=cc_dialogue_parse(buf);
    free(buf);
    return d;
}

void cc_dialogue_free(CCDialogue* d){ if(d){ free(d->nodes); free(d); } }
uint32_t cc_dialogue_node_count(const CCDialogue* d){ return d? d->count : 0; }

/* ─── runtime ─── */
static bool gate_passes(CCDialogueRunner* r, const char* gate){
    if(!gate||!gate[0]) return true;
    if(r->hooks.flag_query) return r->hooks.flag_query(gate, r->hooks.userdata);
    return true;   /* no hook: gates pass */
}
/* recompute which choices are visible + fire the node's action on entry */
static void enter_node(CCDialogueRunner* r){
    r->vis_count=0;
    if(r->cur<0) return;
    DlgNode* n=&r->d->nodes[r->cur];
    if(n->action[0] && r->hooks.action_apply) r->hooks.action_apply(n->action, r->hooks.userdata);
    for(uint32_t i=0;i<n->choice_count;i++)
        if(gate_passes(r,n->choices[i].gate)) r->vis[r->vis_count++]=i;
}

CCDialogueRunner* cc_dialogue_start(CCDialogue* d, const char* start_node){
    if(!d) return NULL;
    CCDialogueRunner* r=(CCDialogueRunner*)calloc(1,sizeof(CCDialogueRunner));
    r->d=d;
    int idx = start_node ? dlg_find(d,start_node) : 0;
    if(idx<0) idx=0;
    r->cur=idx;
    /* hooks default to none; enter_node called after set_hooks or immediately */
    enter_node(r);
    return r;
}
void cc_dialogue_set_event_bus(CCDialogueRunner* r, CCEventBus* bus, uint64_t convo_id){
    if(!r) return;
    r->bus=bus; r->convo_id=convo_id;
    /* Emit the deferred STARTED + initial NODE now that a bus exists. Fire once. */
    if(bus && !r->started_emitted){
        r->started_emitted=1;
        cc_event_emit_i(bus, CC_EVT_DIALOGUE_STARTED, convo_id, 0);
        if(r->cur>=0) cc_event_emit_i(bus, CC_EVT_DIALOGUE_NODE, convo_id, r->cur);
    }
}
void cc_dialogue_set_hooks(CCDialogueRunner* r, const CCDialogueHooks* h){
    if(!r) return;
    if(h) r->hooks=*h; else memset(&r->hooks,0,sizeof(r->hooks));
    enter_node(r);   /* re-evaluate gating/actions now that hooks exist */
}
void cc_dialogue_free_runner(CCDialogueRunner* r){ free(r); }

const char* cc_dialogue_speaker(const CCDialogueRunner* r){
    return (r && r->cur>=0) ? r->d->nodes[r->cur].speaker : "";
}
const char* cc_dialogue_text(const CCDialogueRunner* r){
    return (r && r->cur>=0) ? r->d->nodes[r->cur].text : "";
}
bool cc_dialogue_finished(const CCDialogueRunner* r){ return !r || r->cur<0; }

uint32_t cc_dialogue_choice_count(const CCDialogueRunner* r){ return r? r->vis_count : 0; }
const char* cc_dialogue_choice_text(const CCDialogueRunner* r, uint32_t i){
    if(!r || i>=r->vis_count) return "";
    return r->d->nodes[r->cur].choices[ r->vis[i] ].text;
}

static void go_to(CCDialogueRunner* r, const char* target){
    int idx = target && target[0] ? dlg_find(r->d,target) : -1;
    r->cur = idx;
    enter_node(r);
    if(r->bus){
        if(r->cur>=0) cc_event_emit_i(r->bus, CC_EVT_DIALOGUE_NODE, r->convo_id, r->cur);
        else          cc_event_emit_i(r->bus, CC_EVT_DIALOGUE_ENDED, r->convo_id, 0);
    }
}

bool cc_dialogue_advance(CCDialogueRunner* r){
    if(!r || r->cur<0) return false;
    DlgNode* n=&r->d->nodes[r->cur];
    if(r->vis_count>0) return true;      /* has choices: caller must choose */
    if(n->is_end || !n->goto_id[0]){
        r->cur=-1;
        if(r->bus) cc_event_emit_i(r->bus, CC_EVT_DIALOGUE_ENDED, r->convo_id, 0);
        return false;
    }
    go_to(r, n->goto_id);
    return r->cur>=0;
}
bool cc_dialogue_choose(CCDialogueRunner* r, uint32_t i){
    if(!r || r->cur<0 || i>=r->vis_count) return false;
    DlgNode* n=&r->d->nodes[r->cur];
    const char* target=n->choices[ r->vis[i] ].target;
    go_to(r, target);
    return true;
}
