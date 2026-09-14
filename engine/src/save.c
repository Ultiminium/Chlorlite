#include "cc/save.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum { SV_INT, SV_FLOAT, SV_BOOL, SV_STR, SV_VEC3 } SaveType;

typedef struct {
    char     key[128];
    SaveType type;
    int64_t  i;
    float    f;
    int      b;
    float    v3[3];
    char*    str;   /* heap, for SV_STR */
} SaveEntry;

struct CCSaveState {
    SaveEntry* items;
    uint32_t   count, cap;
};

CCSaveState* cc_save_new(void){
    CCSaveState* s=(CCSaveState*)calloc(1,sizeof(CCSaveState));
    return s;
}
void cc_save_free(CCSaveState* s){
    if(!s) return;
    for(uint32_t i=0;i<s->count;i++) if(s->items[i].type==SV_STR) free(s->items[i].str);
    free(s->items); free(s);
}

/* find entry index by key, or -1 */
static int save_find(const CCSaveState* s, const char* key){
    if(!s||!key) return -1;
    for(uint32_t i=0;i<s->count;i++) if(!strcmp(s->items[i].key,key)) return (int)i;
    return -1;
}
/* get-or-create an entry for a key (clears any prior string alloc) */
static SaveEntry* save_slot(CCSaveState* s, const char* key){
    int idx=save_find(s,key);
    if(idx>=0){
        SaveEntry* e=&s->items[idx];
        if(e->type==SV_STR){ free(e->str); e->str=NULL; }
        return e;
    }
    if(s->count>=s->cap){ s->cap=s->cap?s->cap*2:16; s->items=realloc(s->items,s->cap*sizeof(SaveEntry)); }
    SaveEntry* e=&s->items[s->count++];
    memset(e,0,sizeof(*e));
    snprintf(e->key,sizeof(e->key),"%s",key);
    return e;
}

void cc_save_set_int(CCSaveState* s,const char* k,int64_t v){ SaveEntry* e=save_slot(s,k); e->type=SV_INT; e->i=v; }
void cc_save_set_float(CCSaveState* s,const char* k,float v){ SaveEntry* e=save_slot(s,k); e->type=SV_FLOAT; e->f=v; }
void cc_save_set_bool(CCSaveState* s,const char* k,bool v){ SaveEntry* e=save_slot(s,k); e->type=SV_BOOL; e->b=v?1:0; }
void cc_save_set_str(CCSaveState* s,const char* k,const char* v){
    SaveEntry* e=save_slot(s,k); e->type=SV_STR;
    size_t n=v?strlen(v):0; e->str=(char*)malloc(n+1); if(v)memcpy(e->str,v,n); e->str[n]=0;
}
void cc_save_set_vec3(CCSaveState* s,const char* k,float x,float y,float z){
    SaveEntry* e=save_slot(s,k); e->type=SV_VEC3; e->v3[0]=x;e->v3[1]=y;e->v3[2]=z;
}

int64_t cc_save_get_int(const CCSaveState* s,const char* k,int64_t def){
    int i=save_find(s,k); if(i<0) return def; const SaveEntry* e=&s->items[i];
    if(e->type==SV_INT) return e->i;
    if(e->type==SV_FLOAT) return (int64_t)e->f;
    if(e->type==SV_BOOL) return e->b;
    return def;
}
float cc_save_get_float(const CCSaveState* s,const char* k,float def){
    int i=save_find(s,k); if(i<0) return def; const SaveEntry* e=&s->items[i];
    if(e->type==SV_FLOAT) return e->f;
    if(e->type==SV_INT) return (float)e->i;
    return def;
}
bool cc_save_get_bool(const CCSaveState* s,const char* k,bool def){
    int i=save_find(s,k); if(i<0) return def; const SaveEntry* e=&s->items[i];
    if(e->type==SV_BOOL) return e->b!=0;
    if(e->type==SV_INT) return e->i!=0;
    return def;
}
const char* cc_save_get_str(const CCSaveState* s,const char* k,const char* def){
    int i=save_find(s,k); if(i<0) return def; const SaveEntry* e=&s->items[i];
    return e->type==SV_STR ? e->str : def;
}
void cc_save_get_vec3(const CCSaveState* s,const char* k,float* x,float* y,float* z){
    int i=save_find(s,k);
    if(i<0 || s->items[i].type!=SV_VEC3){ if(x)*x=0;if(y)*y=0;if(z)*z=0; return; }
    const SaveEntry* e=&s->items[i];
    if(x)*x=e->v3[0]; if(y)*y=e->v3[1]; if(z)*z=e->v3[2];
}

bool cc_save_has(const CCSaveState* s,const char* k){ return save_find(s,k)>=0; }
void cc_save_remove(CCSaveState* s,const char* k){
    int i=save_find(s,k); if(i<0) return;
    if(s->items[i].type==SV_STR) free(s->items[i].str);
    s->items[i]=s->items[--s->count];   /* swap-remove */
}
uint32_t cc_save_count(const CCSaveState* s){ return s?s->count:0; }
const char* cc_save_key_at(const CCSaveState* s,uint32_t index){
    return (s && index<s->count) ? s->items[index].key : NULL;
}

/* ─── text serialization (.ccsave v1) ────────────────────────────────────
 * Format (one entry per line): '<type> <key> <value...>'  with '#' comments.
 *   i <key> <int>
 *   f <key> <float>
 *   b <key> <0|1>
 *   v <key> <x> <y> <z>
 *   s <key> <string-to-eol>        (strings run to end of line; no spaces in key)
 */
bool cc_save_write(const CCSaveState* s, const char* path){
    if(!s) return false;
    FILE* f=fopen(path,"w");
    if(!f){ fprintf(stderr,"cc_save_write: cannot open %s\n",path); return false; }
    fprintf(f,"ccsave 1\n");
    for(uint32_t i=0;i<s->count;i++){
        const SaveEntry* e=&s->items[i];
        switch(e->type){
            case SV_INT:   fprintf(f,"i %s %lld\n", e->key,(long long)e->i); break;
            case SV_FLOAT: fprintf(f,"f %s %.7g\n", e->key,e->f); break;
            case SV_BOOL:  fprintf(f,"b %s %d\n",  e->key,e->b?1:0); break;
            case SV_VEC3:  fprintf(f,"v %s %.7g %.7g %.7g\n", e->key,e->v3[0],e->v3[1],e->v3[2]); break;
            case SV_STR:   fprintf(f,"s %s %s\n",  e->key,e->str?e->str:""); break;
        }
    }
    fclose(f);
    return true;
}

static void save_strip(char* s){
    /* strip trailing newline/cr only (keep inner spaces for string values) */
    size_t n=strlen(s);
    while(n && (s[n-1]=='\n'||s[n-1]=='\r')) s[--n]=0;
}

CCSaveState* cc_save_read(const char* path){
    FILE* f=fopen(path,"rb");
    if(!f){ fprintf(stderr,"cc_save_read: cannot open %s\n",path); return NULL; }
    char line[1024];
    /* header */
    int ok=0;
    while(fgets(line,sizeof(line),f)){
        save_strip(line);
        if(line[0]=='#'||line[0]==0) continue;
        int ver=0; if(sscanf(line,"ccsave %d",&ver)==1 && ver==1){ ok=1; break; }
        fprintf(stderr,"cc_save_read: bad header (expected 'ccsave 1')\n"); fclose(f); return NULL;
    }
    if(!ok){ fclose(f); return NULL; }

    CCSaveState* s=cc_save_new();
    while(fgets(line,sizeof(line),f)){
        save_strip(line);
        if(line[0]=='#'||line[0]==0) continue;
        char type=line[0];
        char key[128]={0};
        /* parse "<type> <key> <rest...>" */
        const char* p=line+1; while(*p==' ')p++;
        int ki=0; while(*p && *p!=' ' && ki<127){ key[ki++]=*p++; } key[ki]=0;
        while(*p==' ')p++;   /* p now at value */
        switch(type){
            case 'i': cc_save_set_int(s,key,(int64_t)strtoll(p,NULL,10)); break;
            case 'f': cc_save_set_float(s,key,(float)atof(p)); break;
            case 'b': cc_save_set_bool(s,key,atoi(p)!=0); break;
            case 'v': { float x=0,y=0,z=0; sscanf(p,"%f %f %f",&x,&y,&z); cc_save_set_vec3(s,key,x,y,z); break; }
            case 's': cc_save_set_str(s,key,p); break;
            default: break;   /* unknown line ignored */
        }
    }
    fclose(f);
    return s;
}
