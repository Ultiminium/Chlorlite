#include "cc/ccscene.h"
#include "cc/ccmodel.h"
#include "cc/claudecore.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

/* ---- text helpers ---- */
static void cs_strip(char* s){
    for(char* p=s;*p;p++){ if(*p=='#'){ *p=0; break; } }
    size_t n=strlen(s);
    while(n && (s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' '||s[n-1]=='\t')) s[--n]=0;
}
static char* cs_skipws(char* s){ while(*s==' '||*s=='\t') s++; return s; }
static void cs_dirof(const char* path, char* out, size_t n){
    const char* slash=strrchr(path,'/');
    if(!slash){ out[0]=0; return; }
    size_t len=(size_t)(slash-path)+1; if(len>=n) len=n-1;
    memcpy(out,path,len); out[len]=0;
}
static void cs_quat_y(float deg, float* q){
    float r=deg*3.14159265f/180.0f, h=r*0.5f;
    q[0]=0; q[1]=sinf(h); q[2]=0; q[3]=cosf(h);
}
/* recover a Y-rotation in degrees from a (mostly-Y) quaternion */
static float cs_yaw_deg(const float* q){
    /* yaw from quaternion (assumes rotation about Y): 2*atan2(qy,qw) */
    float deg = 2.0f*atan2f(q[1],q[3]) * 180.0f/3.14159265f;
    if(deg<=-180.0f) deg+=360.0f; if(deg>180.0f) deg-=360.0f;
    return deg;
}

/* ---- per-scene model cache (path -> mesh + materials), stored on the heap and
   referenced by the scene's meshes[]/materials[] ownership arrays ---- */
typedef struct {
    char       path[256];
    CCMesh     mesh;
    CCMaterial mats[16];
    uint32_t   mat_count;
} CachedModel;

typedef struct {
    CachedModel* items; uint32_t count, cap;
} ModelCache;

/* find-or-load a model into the cache; returns index or -1 on failure */
static int cache_get(CCEngine* eng, ModelCache* mc, const char* base_dir, const char* rel){
    char full[512];
    if(rel[0]=='/'||!base_dir||!base_dir[0]) snprintf(full,sizeof(full),"%s",rel);
    else {
        size_t bl=strlen(base_dir);
        if(base_dir[bl-1]=='/') snprintf(full,sizeof(full),"%s%s",base_dir,rel);
        else                    snprintf(full,sizeof(full),"%s/%s",base_dir,rel);
    }
    for(uint32_t i=0;i<mc->count;i++) if(!strcmp(mc->items[i].path,rel)) return (int)i;
    CCModel* mdl=ccm_load_text(full);
    if(!mdl){ fprintf(stderr,"cc_scene: model load failed: %s\n",full); return -1; }
    if(mc->count>=mc->cap){ mc->cap=mc->cap?mc->cap*2:8; mc->items=realloc(mc->items,mc->cap*sizeof(CachedModel)); }
    CachedModel* cm=&mc->items[mc->count];
    snprintf(cm->path,sizeof(cm->path),"%s",rel);   /* store the RELATIVE path for round-trip */
    cm->mesh=cc_mesh_from_model(eng,mdl);
    cm->mat_count=cc_materials_from_model(eng,mdl,cm->mats,16,base_dir);
    ccm_model_free(mdl);
    return (int)mc->count++;
}

/* push an instance onto a scene (grows the array) */
static uint32_t scene_push(CCSceneAsset* sc, CCMesh mesh, CCMaterial mat, const CCTransform3D* xf,
                           const char* model_path, uint32_t slot){
    if(sc->instance_count>=sc->instance_cap){
        sc->instance_cap = sc->instance_cap? sc->instance_cap*2 : 16;
        sc->instances = realloc(sc->instances, sc->instance_cap*sizeof(CCSceneAssetInstance));
    }
    CCSceneAssetInstance* in=&sc->instances[sc->instance_count];
    in->mesh=mesh; in->material=mat; in->xform=*xf;
    snprintf(in->model_path,sizeof(in->model_path),"%s",model_path?model_path:"");
    in->material_slot=slot;
    return sc->instance_count++;
}

/* copy a cache's unique resources into the scene's ownership arrays */
static void scene_take_cache(CCSceneAsset* sc, ModelCache* mc){
    sc->meshes=malloc((mc->count?mc->count:1)*sizeof(CCMesh));
    sc->mesh_count=mc->count;
    uint32_t mtotal=0; for(uint32_t i=0;i<mc->count;i++) mtotal+=mc->items[i].mat_count;
    sc->materials=malloc((mtotal?mtotal:1)*sizeof(CCMaterial));
    uint32_t mi=0;
    for(uint32_t i=0;i<mc->count;i++){
        sc->meshes[i]=mc->items[i].mesh;
        for(uint32_t j=0;j<mc->items[i].mat_count;j++) sc->materials[mi++]=mc->items[i].mats[j];
    }
    sc->material_count=mi;
    free(mc->items);
}

/* ══════════════════════════════════════════════════════════════════════ */

CCSceneAsset* cc_sceneasset_new(const char* name, const char* base_dir){
    CCSceneAsset* sc=calloc(1,sizeof(CCSceneAsset));
    snprintf(sc->name,sizeof(sc->name),"%s",name&&name[0]?name:"scene");
    snprintf(sc->base_dir,sizeof(sc->base_dir),"%s",base_dir?base_dir:"");
    return sc;
}

/* Builder cache: unique models (mesh+materials) loaded by cc_sceneasset_add. Stored
   on the scene via _build_cache and released in cc_sceneasset_free. */
typedef struct { char path[256]; CCMesh mesh; CCMaterial mats[16]; uint32_t mat_count; } BuildEntry;
typedef struct { BuildEntry* e; uint32_t n, cap; } BuildCache;

uint32_t cc_sceneasset_add(CCEngine* eng, CCSceneAsset* sc, const char* model_path,
                      float px,float py,float pz, float sxs,float sys,float szs,
                      float rot_y_deg, uint32_t slot){
    if(!eng||!sc||!model_path) return 0xFFFFFFFFu;
    if(!sc->_build_cache) sc->_build_cache=calloc(1,sizeof(BuildCache));
    BuildCache* bc=(BuildCache*)sc->_build_cache;
    int found=-1;
    for(uint32_t i=0;i<bc->n;i++) if(!strcmp(bc->e[i].path,model_path)){ found=(int)i; break; }
    if(found<0){
        char full[512];
        if(model_path[0]=='/'||!sc->base_dir[0]) snprintf(full,sizeof(full),"%s",model_path);
        else { size_t bl=strlen(sc->base_dir);
            if(sc->base_dir[bl-1]=='/') snprintf(full,sizeof(full),"%s%s",sc->base_dir,model_path);
            else snprintf(full,sizeof(full),"%s/%s",sc->base_dir,model_path); }
        CCModel* mdl=ccm_load_text(full);
        if(!mdl){ fprintf(stderr,"cc_sceneasset_add: load failed %s\n",full); return 0xFFFFFFFFu; }
        if(bc->n>=bc->cap){ bc->cap=bc->cap?bc->cap*2:8; bc->e=realloc(bc->e,bc->cap*sizeof(BuildEntry)); }
        BuildEntry* be=&bc->e[bc->n];
        snprintf(be->path,sizeof(be->path),"%s",model_path);
        be->mesh=cc_mesh_from_model(eng,mdl);
        be->mat_count=cc_materials_from_model(eng,mdl,be->mats,16,sc->base_dir);
        ccm_model_free(mdl);
        found=(int)bc->n++;
        /* mirror unique resources into ownership arrays so cc_sceneasset_free can
           release them and cc_sceneasset_draw/save see a consistent view */
        sc->meshes=realloc(sc->meshes,(found+1)*sizeof(CCMesh));
        sc->meshes[found]=be->mesh; sc->mesh_count=found+1;
        for(uint32_t j=0;j<be->mat_count;j++){
            sc->materials=realloc(sc->materials,(sc->material_count+1)*sizeof(CCMaterial));
            sc->materials[sc->material_count++]=be->mats[j];
        }
    }
    BuildEntry* be=&bc->e[found];
    uint32_t sl=slot<be->mat_count?slot:0;
    CCTransform3D xf; xf.pos[0]=px;xf.pos[1]=py;xf.pos[2]=pz;
    xf.scale[0]=sxs;xf.scale[1]=sys;xf.scale[2]=szs; cs_quat_y(rot_y_deg,xf.rot);
    return scene_push(sc, be->mesh, be->mat_count?be->mats[sl]:0, &xf, model_path, sl);
}

CCSceneAsset* cc_sceneasset_load(CCEngine* eng, const char* path){
    FILE* f=fopen(path,"rb");
    if(!f){ fprintf(stderr,"cc_sceneasset_load: cannot open %s\n",path); return NULL; }
    char basedir[256]; cs_dirof(path,basedir,sizeof(basedir));

    char line[1024]; int ok=0;
    while(fgets(line,sizeof(line),f)){
        cs_strip(line); char* p=cs_skipws(line); if(!*p) continue;
        int ver=0; if(sscanf(p,"cclist %d",&ver)==1 && ver==1){ ok=1; break; }
        fprintf(stderr,"cc_sceneasset_load: bad header (expected 'cclist 1')\n"); fclose(f); return NULL;
    }
    if(!ok){ fclose(f); return NULL; }

    CCSceneAsset* sc=cc_sceneasset_new("scene",basedir);
    ModelCache mc={0};
    int cur=-1; char cur_rel[256]={0};
    int have_pending=0; CCTransform3D pend; uint32_t pend_slot=0;
    #define RESET_PEND() do{ pend.pos[0]=pend.pos[1]=pend.pos[2]=0; \
        pend.rot[0]=pend.rot[1]=pend.rot[2]=0; pend.rot[3]=1; \
        pend.scale[0]=pend.scale[1]=pend.scale[2]=1; pend_slot=0; }while(0)
    RESET_PEND();
    #define FLUSH_PEND() do{ if(have_pending && cur>=0){ \
        CachedModel* cm=&mc.items[cur]; uint32_t sl=pend_slot<cm->mat_count?pend_slot:0; \
        scene_push(sc, cm->mesh, cm->mat_count?cm->mats[sl]:0, &pend, cur_rel, sl); \
        have_pending=0; RESET_PEND(); } }while(0)

    while(fgets(line,sizeof(line),f)){
        cs_strip(line); char* p=cs_skipws(line); if(!*p) continue;
        char kw[64]={0}; sscanf(p,"%63s",kw);
        if(!strcmp(kw,"name")){ char* rest=cs_skipws(p+4); snprintf(sc->name,sizeof(sc->name),"%s",rest); }
        else if(!strcmp(kw,"model")){
            FLUSH_PEND();
            char rel[256]={0}; sscanf(cs_skipws(p+5),"%255s",rel);
            cur=cache_get(eng,&mc,basedir,rel);
            if(cur>=0){ snprintf(cur_rel,sizeof(cur_rel),"%s",rel); have_pending=1; RESET_PEND(); }
            else have_pending=0;
        }
        else if(!strcmp(kw,"at")){ sscanf(p,"at %f %f %f",&pend.pos[0],&pend.pos[1],&pend.pos[2]); have_pending=1; }
        else if(!strcmp(kw,"scale")){ sscanf(p,"scale %f %f %f",&pend.scale[0],&pend.scale[1],&pend.scale[2]); have_pending=1; }
        else if(!strcmp(kw,"rot_y")){ float d=0; sscanf(p,"rot_y %f",&d); cs_quat_y(d,pend.rot); have_pending=1; }
        else if(!strcmp(kw,"slot")){ sscanf(p,"slot %u",&pend_slot); have_pending=1; }
        else if(!strcmp(kw,"instance")){
            FLUSH_PEND();
            float x=0,y=0,z=0,sx=1,sy=1,sz=1,ry=0;
            sscanf(p,"instance %f %f %f %f %f %f %f",&x,&y,&z,&sx,&sy,&sz,&ry);
            pend.pos[0]=x;pend.pos[1]=y;pend.pos[2]=z; pend.scale[0]=sx;pend.scale[1]=sy;pend.scale[2]=sz;
            cs_quat_y(ry,pend.rot); have_pending=1; FLUSH_PEND();
        }
    }
    FLUSH_PEND();
    fclose(f);
    scene_take_cache(sc,&mc);
    return sc;
}

bool cc_sceneasset_save(const CCSceneAsset* sc, const char* path){
    if(!sc) return false;
    FILE* f=fopen(path,"w");
    if(!f){ fprintf(stderr,"cc_sceneasset_save: cannot open %s\n",path); return false; }
    fprintf(f,"cclist 1\n");
    fprintf(f,"name %s\n", sc->name[0]?sc->name:"scene");
    /* group instances by model path: emit one 'model' block per unique path,
       first instance as the model line + at/scale/rot_y/slot, rest as 'instance'. */
    char seen[512][256]; uint32_t seen_n=0;
    for(uint32_t i=0;i<sc->instance_count;i++){
        const char* mp=sc->instances[i].model_path;
        if(!mp[0]) continue;
        int already=0; for(uint32_t s=0;s<seen_n;s++) if(!strcmp(seen[s],mp)){ already=1; break; }
        if(already) continue;
        if(seen_n<512){ snprintf(seen[seen_n],256,"%s",mp); seen_n++; }
        int first=1;
        for(uint32_t j=i;j<sc->instance_count;j++){
            const CCSceneAssetInstance* in=&sc->instances[j];
            if(strcmp(in->model_path,mp)!=0) continue;
            float ry=cs_yaw_deg(in->xform.rot);
            if(first){
                fprintf(f,"model %s\n",mp);
                fprintf(f,"  at %.6g %.6g %.6g\n", in->xform.pos[0],in->xform.pos[1],in->xform.pos[2]);
                fprintf(f,"  scale %.6g %.6g %.6g\n", in->xform.scale[0],in->xform.scale[1],in->xform.scale[2]);
                if(fabsf(ry)>1e-3f) fprintf(f,"  rot_y %.5g\n", ry);
                if(in->material_slot) fprintf(f,"  slot %u\n", in->material_slot);
                first=0;
            } else {
                fprintf(f,"instance %.6g %.6g %.6g  %.6g %.6g %.6g  %.5g\n",
                    in->xform.pos[0],in->xform.pos[1],in->xform.pos[2],
                    in->xform.scale[0],in->xform.scale[1],in->xform.scale[2], ry);
            }
        }
    }
    fclose(f);
    return true;
}

void cc_sceneasset_draw(CCEngine* eng, const CCSceneAsset* sc){
    if(!eng||!sc) return;
    for(uint32_t i=0;i<sc->instance_count;i++)
        cc_draw_mesh(eng, sc->instances[i].mesh, sc->instances[i].material, &sc->instances[i].xform);
}

void cc_sceneasset_free(CCEngine* eng, CCSceneAsset* sc, bool destroy_gpu){
    if(!sc) return;
    if(sc->_build_cache){ BuildCache* bc=(BuildCache*)sc->_build_cache; free(bc->e); free(bc); sc->_build_cache=NULL; }
    if(destroy_gpu && eng){
        for(uint32_t i=0;i<sc->mesh_count;i++)    cc_mesh_destroy(eng,sc->meshes[i]);
        for(uint32_t i=0;i<sc->material_count;i++) cc_material_destroy(eng,sc->materials[i]);
    }
    free(sc->instances); free(sc->meshes); free(sc->materials);
    free(sc);
}
