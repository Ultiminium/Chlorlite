/* deboog_chlorlite.c — the ADAPTER: bridges Chlorlite's CCModel into Deboog's
 * primitive API. This is the pattern every engine uses — Deboog knows nothing about
 * CCModel; this file unpacks CCModel into flat arrays and calls Deboog. ~a screenful.
 *
 * Build it into a Chlorlite tool alongside libdeboog.a. It does NOT make Deboog
 * depend on the engine — the dependency points one way (this adapter uses both). */
#include "cc/ccmodel.h"
#include "deboog/deboog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Run the full Deboog battery on a CCModel. Returns total FAIL count. */
int deboog_check_ccmodel(const CCModel* m, FILE* out){
    if(!out) out=stdout;
    int fails=0;
    const CCMGeomChunk* G=&m->geom;

    /* --- unpack vertex positions into a flat float[3] array (stride 3) --- */
    uint32_t nv=G->vertex_count;
    float* pos=malloc((size_t)nv*3*sizeof(float));
    for(uint32_t i=0;i<nv;i++){ pos[i*3]=G->vertices[i].pos[0]; pos[i*3+1]=G->vertices[i].pos[1]; pos[i*3+2]=G->vertices[i].pos[2]; }

    /* 1. numeric: no NaN/Inf in any vertex position */
    fails += deboog_report_numeric(out, "positions", deboog_scan_floats(pos, (size_t)nv*3));

    /* 2. mesh topology */
    DbMesh mesh = deboog_check_mesh(pos, nv, 3, G->indices, G->index_count);
    fails += deboog_report_mesh(out, m->name, mesh);

    /* 3. skin weights (unpack CCMSkinVertex → flat joint/weight arrays) */
    const CCMSkinChunk* S=&m->skin;
    if(S && S->vertex_count){
        uint16_t* J=malloc((size_t)S->vertex_count*4*sizeof(uint16_t));
        float*    W=malloc((size_t)S->vertex_count*4*sizeof(float));
        for(uint32_t i=0;i<S->vertex_count;i++) for(int k=0;k<4;k++){
            J[i*4+k]=S->weights[i].joint[k]; W[i*4+k]=S->weights[i].weight[k]; }
        DbSkin sk=deboog_check_skin(J,W,S->vertex_count, m->skel.bone_count, 0.02);
        fails += deboog_report_skin(out, "skin", sk);
        free(J); free(W);
    }

    /* 4. per-bone roundness (ribbon detector). For each bone with enough verts
          primarily weighted to it, gather those verts and measure cross-section
          along the bone's bind axis (parent-head → head). */
    if(S && S->vertex_count && m->skel.bone_count){
        const CCMSkelChunk* K=&m->skel;
        float* limb=malloc((size_t)nv*3*sizeof(float));
        for(uint16_t b=0;b<K->bone_count;b++){
            uint16_t par=K->bones[b].parent; if(par==CCM_BONE_NO_PARENT) continue;
            /* bind-world head of b and parent = sum of bind_pos up the chain */
            float head[3]={0,0,0}, ph[3]={0,0,0}; uint16_t t;
            for(t=b; t!=CCM_BONE_NO_PARENT && t<K->bone_count; t=K->bones[t].parent){ head[0]+=K->bones[t].bind_pos[0];head[1]+=K->bones[t].bind_pos[1];head[2]+=K->bones[t].bind_pos[2]; }
            for(t=par; t!=CCM_BONE_NO_PARENT && t<K->bone_count; t=K->bones[t].parent){ ph[0]+=K->bones[t].bind_pos[0];ph[1]+=K->bones[t].bind_pos[1];ph[2]+=K->bones[t].bind_pos[2]; }
            float axis[3]={head[0]-ph[0],head[1]-ph[1],head[2]-ph[2]};
            if(sqrtf(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2])<1e-5f) continue;
            uint32_t cnt=0;
            for(uint32_t i=0;i<S->vertex_count;i++){
                int best=0; for(int k=1;k<4;k++) if(S->weights[i].weight[k]>S->weights[i].weight[best]) best=k;
                if(S->weights[i].weight[best]>0 && S->weights[i].joint[best]==b){
                    limb[cnt*3]=pos[i*3]; limb[cnt*3+1]=pos[i*3+1]; limb[cnt*3+2]=pos[i*3+2]; cnt++; }
            }
            if(cnt<12) continue;
            DbCrossSection cs=deboog_cross_section(limb,cnt,3,head,axis,0.45);
            if(cs.is_ribbon){ char lbl[80]; snprintf(lbl,sizeof lbl,"bone '%s'",K->bones[b].name);
                fails += deboog_report_cross_section(out, lbl, cs); }
        }
        free(limb);
    }

    free(pos);
    fprintf(out, "=== deboog: %s → %s ===\n", m->name[0]?m->name:"(model)",
            fails? "FAILURES FOUND" : "all checks passed");
    return fails;
}
