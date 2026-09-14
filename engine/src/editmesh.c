/*
 * editmesh.c — Chlorlite editable half-edge mesh.
 *
 * Data model (indices into growable pools; CC_EM_INVALID = none):
 *   Vertex { position; halfedge (one outgoing) }
 *   HalfEdge { origin vertex; face; next (CCW around face); twin; edge }
 *   Edge   { one of its two half-edges }
 *   Face   { one boundary half-edge }
 *
 * Faces may be n-gons (kept as half-edge rings); triangulation happens only at
 * bake time. Edits maintain twin/next/origin consistency so subdivision and
 * extrusion stay watertight. See cc/editmesh.h for the contract + rationale.
 */
#include "cc/editmesh.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ─── growable pool ──────────────────────────────────────────────────── */
typedef struct { void* data; uint32_t count, cap, stride; } Pool;
static void  pool_init(Pool* p, uint32_t stride){ p->data=NULL;p->count=0;p->cap=0;p->stride=stride; }
static void  pool_free(Pool* p){ free(p->data); p->data=NULL; p->count=p->cap=0; }
static uint32_t pool_add(Pool* p){
    if (p->count>=p->cap){ p->cap=p->cap?p->cap*2:16; p->data=realloc(p->data,(size_t)p->cap*p->stride); }
    uint32_t i=p->count++; memset((char*)p->data+(size_t)i*p->stride,0,p->stride); return i;
}
#define POOL_AT(p,T,i) ((T*)((char*)(p).data + (size_t)(i)*(p).stride))

typedef struct { CCVec3 position; uint32_t he; bool alive; } EMVert;
typedef struct { uint32_t origin, face, next, twin, edge; bool alive; } EMHalf;
typedef struct { uint32_t he; bool alive; } EMEdge;
typedef struct { uint32_t he; bool alive; } EMFace;

struct CCEditMesh {
    Pool V, H, E, F;         /* EMVert, EMHalf, EMEdge, EMFace */
    float snap;              /* 0 = off */
};

/* ─── lifecycle ──────────────────────────────────────────────────────── */
CCEditMesh* cc_editmesh_new(void){
    CCEditMesh* m=calloc(1,sizeof(CCEditMesh));
    pool_init(&m->V,sizeof(EMVert)); pool_init(&m->H,sizeof(EMHalf));
    pool_init(&m->E,sizeof(EMEdge)); pool_init(&m->F,sizeof(EMFace));
    m->snap=0.0f;
    return m;
}
void cc_editmesh_free(CCEditMesh* m){ if(!m)return; pool_free(&m->V);pool_free(&m->H);pool_free(&m->E);pool_free(&m->F); free(m); }

/* ─── snap ───────────────────────────────────────────────────────────── */
void  cc_editmesh_set_snap(CCEditMesh* m, float s){ if(m) m->snap=s>0?s:0; }
float cc_editmesh_snap(const CCEditMesh* m){ return m?m->snap:0; }
CCVec3 cc_editmesh_apply_snap(const CCEditMesh* m, CCVec3 p){
    if(!m||m->snap<=0) return p; float s=m->snap;
    return (CCVec3){ roundf(p.x/s)*s, roundf(p.y/s)*s, roundf(p.z/s)*s };
}

/* ─── low-level builders ─────────────────────────────────────────────── */
uint32_t cc_editmesh_add_vertex(CCEditMesh* m, CCVec3 p){
    p=cc_editmesh_apply_snap(m,p);
    uint32_t i=pool_add(&m->V); EMVert* v=POOL_AT(m->V,EMVert,i);
    v->position=p; v->he=CC_EM_INVALID; v->alive=true; return i;
}

/* find the half-edge from va→vb if one exists (search all alive half-edges) */
static uint32_t he_from_to(CCEditMesh* m, uint32_t va, uint32_t vb){
    for(uint32_t h=0; h<m->H.count; h++){
        EMHalf* he=POOL_AT(m->H,EMHalf,h);
        if(!he->alive) continue;
        if(he->origin==va){
            EMHalf* nx=POOL_AT(m->H,EMHalf,he->next);
            if(nx->origin==vb) return h;
        }
    }
    return CC_EM_INVALID;
}

uint32_t cc_editmesh_add_face(CCEditMesh* m, const uint32_t* vids, uint32_t n){
    if(n<3) return CC_EM_INVALID;
    uint32_t f=pool_add(&m->F); POOL_AT(m->F,EMFace,f)->alive=true;
    uint32_t* hes=malloc(n*sizeof(uint32_t));
    for(uint32_t i=0;i<n;i++) hes[i]=pool_add(&m->H);
    for(uint32_t i=0;i<n;i++){
        EMHalf* he=POOL_AT(m->H,EMHalf,hes[i]);
        he->origin=vids[i]; he->face=f; he->next=hes[(i+1)%n];
        he->twin=CC_EM_INVALID; he->edge=CC_EM_INVALID; he->alive=true;
        EMVert* v=POOL_AT(m->V,EMVert,vids[i]); if(v->he==CC_EM_INVALID) v->he=hes[i];
    }
    POOL_AT(m->F,EMFace,f)->he=hes[0];
    /* twin up + create edges */
    for(uint32_t i=0;i<n;i++){
        EMHalf* he=POOL_AT(m->H,EMHalf,hes[i]);
        if(he->edge!=CC_EM_INVALID) continue;
        uint32_t a=vids[i], b=vids[(i+1)%n];
        uint32_t opp=he_from_to(m,b,a);
        if(opp!=CC_EM_INVALID && opp!=hes[i]){
            EMHalf* ot=POOL_AT(m->H,EMHalf,opp);
            he->twin=opp; ot->twin=hes[i];
            if(ot->edge!=CC_EM_INVALID){ he->edge=ot->edge; }
            else { uint32_t e=pool_add(&m->E); POOL_AT(m->E,EMEdge,e)->he=hes[i];
                   POOL_AT(m->E,EMEdge,e)->alive=true; he->edge=e; ot->edge=e; }
        } else {
            uint32_t e=pool_add(&m->E); POOL_AT(m->E,EMEdge,e)->he=hes[i];
            POOL_AT(m->E,EMEdge,e)->alive=true; he->edge=e;
        }
    }
    free(hes);
    return f;
}

/* ─── starter shapes ─────────────────────────────────────────────────── */
CCEditMesh* cc_editmesh_cube(float s){
    CCEditMesh* m=cc_editmesh_new(); float h=s*0.5f;
    uint32_t v[8];
    CCVec3 P[8]={{-h,-h,-h},{h,-h,-h},{h,-h,h},{-h,-h,h},
                 {-h, h,-h},{h, h,-h},{h, h,h},{-h, h,h}};
    for(int i=0;i<8;i++) v[i]=cc_editmesh_add_vertex(m,P[i]);
    /* 6 quad faces, CCW outward */
    uint32_t faces[6][4]={
        {0,1,2,3}, /* bottom (-y) note: wound so normal points down */
        {4,7,6,5}, /* top (+y) */
        {0,4,5,1}, /* -z */
        {2,6,7,3}, /* +z */
        {0,3,7,4}, /* -x */
        {1,5,6,2}  /* +x */
    };
    for(int f=0;f<6;f++) cc_editmesh_add_face(m,faces[f],4);
    return m;
}
CCEditMesh* cc_editmesh_plane(float s, uint32_t sub){
    CCEditMesh* m=cc_editmesh_new(); if(sub<1)sub=1;
    uint32_t cols=sub+1; float h=s*0.5f;
    uint32_t* grid=malloc(cols*cols*sizeof(uint32_t));
    for(uint32_t z=0;z<cols;z++)for(uint32_t x=0;x<cols;x++){
        float fx=-h + s*(float)x/sub, fz=-h + s*(float)z/sub;
        grid[z*cols+x]=cc_editmesh_add_vertex(m,(CCVec3){fx,0,fz});
    }
    for(uint32_t z=0;z<sub;z++)for(uint32_t x=0;x<sub;x++){
        uint32_t q[4]={grid[z*cols+x],grid[z*cols+x+1],grid[(z+1)*cols+x+1],grid[(z+1)*cols+x]};
        cc_editmesh_add_face(m,q,4);
    }
    free(grid); return m;
}
CCEditMesh* cc_editmesh_from_mesh_data(const CCVertex* verts, uint32_t nv,
                                       const uint32_t* idx, uint32_t ni){
    CCEditMesh* m=cc_editmesh_new();
    for(uint32_t i=0;i<nv;i++) cc_editmesh_add_vertex(m,(CCVec3){verts[i].pos[0],verts[i].pos[1],verts[i].pos[2]});
    for(uint32_t i=0;i+2<ni;i+=3){ uint32_t t[3]={idx[i],idx[i+1],idx[i+2]}; cc_editmesh_add_face(m,t,3); }
    return m;
}

/* ─── queries ────────────────────────────────────────────────────────── */
uint32_t cc_editmesh_vertex_count(const CCEditMesh* m){ uint32_t n=0; for(uint32_t i=0;i<m->V.count;i++) if(POOL_AT(m->V,EMVert,i)->alive)n++; return n; }
uint32_t cc_editmesh_face_count(const CCEditMesh* m){ uint32_t n=0; for(uint32_t i=0;i<m->F.count;i++) if(POOL_AT(m->F,EMFace,i)->alive)n++; return n; }
uint32_t cc_editmesh_edge_count(const CCEditMesh* m){ uint32_t n=0; for(uint32_t i=0;i<m->E.count;i++) if(POOL_AT(m->E,EMEdge,i)->alive)n++; return n; }
bool cc_editmesh_vertex_valid(const CCEditMesh* m, uint32_t v){ return v<m->V.count && POOL_AT(m->V,EMVert,v)->alive; }
CCVec3 cc_editmesh_vertex_position(const CCEditMesh* m, uint32_t v){ return cc_editmesh_vertex_valid(m,v)?POOL_AT(m->V,EMVert,v)->position:(CCVec3){0,0,0}; }

uint32_t cc_editmesh_find_edge(const CCEditMesh* m, uint32_t va, uint32_t vb){
    for(uint32_t h=0;h<m->H.count;h++){ EMHalf* he=POOL_AT(m->H,EMHalf,h); if(!he->alive)continue;
        EMHalf* nx=POOL_AT(m->H,EMHalf,he->next);
        if((he->origin==va&&nx->origin==vb)||(he->origin==vb&&nx->origin==va)) return he->edge;
    }
    return CC_EM_INVALID;
}
uint32_t cc_editmesh_face_vertices(const CCEditMesh* m, uint32_t f, uint32_t* out, uint32_t max){
    if(f>=m->F.count||!POOL_AT(m->F,EMFace,f)->alive) return 0;
    uint32_t start=POOL_AT(m->F,EMFace,f)->he, h=start, n=0; int guard=0;
    do{ EMHalf* he=POOL_AT(m->H,EMHalf,h); if(n<max)out[n]=he->origin; n++; h=he->next; }
    while(h!=start && ++guard<10000);
    return n;
}
uint32_t cc_editmesh_pick_vertices(const CCEditMesh* m, CCVec3 c, float r, uint32_t* out, uint32_t max){
    float r2=r*r; uint32_t n=0;
    for(uint32_t i=0;i<m->V.count;i++){ EMVert* v=POOL_AT(m->V,EMVert,i); if(!v->alive)continue;
        float dx=v->position.x-c.x,dy=v->position.y-c.y,dz=v->position.z-c.z;
        if(dx*dx+dy*dy+dz*dz<=r2){ if(n<max)out[n]=i; n++; } }
    return n;
}

/* ─── move ───────────────────────────────────────────────────────────── */
void cc_editmesh_move_vertex(CCEditMesh* m, uint32_t v, CCVec3 to){
    if(!cc_editmesh_vertex_valid(m,v))return; POOL_AT(m->V,EMVert,v)->position=cc_editmesh_apply_snap(m,to);
}
void cc_editmesh_translate_vertex(CCEditMesh* m, uint32_t v, CCVec3 by){
    if(!cc_editmesh_vertex_valid(m,v))return; EMVert* vv=POOL_AT(m->V,EMVert,v);
    cc_editmesh_move_vertex(m,v,(CCVec3){vv->position.x+by.x,vv->position.y+by.y,vv->position.z+by.z});
}

/* face centroid + normal (Newell) */
static CCVec3 face_centroid(CCEditMesh* m, uint32_t f){
    uint32_t start=POOL_AT(m->F,EMFace,f)->he,h=start; CCVec3 c={0,0,0}; int n=0,guard=0;
    do{ EMVert* v=POOL_AT(m->V,EMVert,POOL_AT(m->H,EMHalf,h)->origin);
        c.x+=v->position.x;c.y+=v->position.y;c.z+=v->position.z;n++;
        h=POOL_AT(m->H,EMHalf,h)->next; }while(h!=start&&++guard<10000);
    if(n){c.x/=n;c.y/=n;c.z/=n;} return c;
}
static CCVec3 face_normal(CCEditMesh* m, uint32_t f){
    uint32_t start=POOL_AT(m->F,EMFace,f)->he,h=start; CCVec3 nrm={0,0,0}; int guard=0;
    do{ EMVert* a=POOL_AT(m->V,EMVert,POOL_AT(m->H,EMHalf,h)->origin);
        EMVert* b=POOL_AT(m->V,EMVert,POOL_AT(m->H,EMHalf,POOL_AT(m->H,EMHalf,h)->next)->origin);
        nrm.x+=(a->position.y-b->position.y)*(a->position.z+b->position.z);
        nrm.y+=(a->position.z-b->position.z)*(a->position.x+b->position.x);
        nrm.z+=(a->position.x-b->position.x)*(a->position.y+b->position.y);
        h=POOL_AT(m->H,EMHalf,h)->next; }while(h!=start&&++guard<10000);
    float l=sqrtf(nrm.x*nrm.x+nrm.y*nrm.y+nrm.z*nrm.z); if(l>1e-9f){nrm.x/=l;nrm.y/=l;nrm.z/=l;} return nrm;
}

/* ─── the core: add points anywhere ──────────────────────────────────── */
/* Rebuild a face's vertex ring into a fresh face (drops old half-edges' aliveness
 * for that face). Simpler + robust: we collect all faces as vertex-id rings,
 * mutate the ring lists, then rebuild all topology. Used by split/poke. */
typedef struct { uint32_t* ids; uint32_t n; } Ring;
static void collect_rings(CCEditMesh* m, Ring** out_rings, uint32_t* out_count){
    uint32_t fc=0; for(uint32_t i=0;i<m->F.count;i++) if(POOL_AT(m->F,EMFace,i)->alive)fc++;
    Ring* rings=calloc(fc,sizeof(Ring)); uint32_t ri=0;
    for(uint32_t f=0;f<m->F.count;f++){ if(!POOL_AT(m->F,EMFace,f)->alive)continue;
        uint32_t tmp[256]; uint32_t n=cc_editmesh_face_vertices(m,f,tmp,256);
        rings[ri].ids=malloc(n*sizeof(uint32_t)); memcpy(rings[ri].ids,tmp,n*sizeof(uint32_t)); rings[ri].n=n; ri++; }
    *out_rings=rings; *out_count=fc;
}
static void rebuild_from_rings(CCEditMesh* m, Ring* rings, uint32_t rc){
    /* wipe topology (keep vertices), then re-add faces */
    pool_free(&m->H); pool_free(&m->E); pool_free(&m->F);
    pool_init(&m->H,sizeof(EMHalf)); pool_init(&m->E,sizeof(EMEdge)); pool_init(&m->F,sizeof(EMFace));
    for(uint32_t i=0;i<m->V.count;i++) if(POOL_AT(m->V,EMVert,i)->alive) POOL_AT(m->V,EMVert,i)->he=CC_EM_INVALID;
    for(uint32_t r=0;r<rc;r++){ if(rings[r].n>=3) cc_editmesh_add_face(m,rings[r].ids,rings[r].n); free(rings[r].ids); }
    free(rings);
}

uint32_t cc_editmesh_split_edge(CCEditMesh* m, uint32_t e, float t){
    if(e>=m->E.count||!POOL_AT(m->E,EMEdge,e)->alive) return CC_EM_INVALID;
    /* endpoints of the edge */
    uint32_t he=POOL_AT(m->E,EMEdge,e)->he; EMHalf* H0=POOL_AT(m->H,EMHalf,he);
    uint32_t va=H0->origin, vb=POOL_AT(m->H,EMHalf,H0->next)->origin;
    CCVec3 pa=POOL_AT(m->V,EMVert,va)->position, pb=POOL_AT(m->V,EMVert,vb)->position;
    if(t<0)t=0; if(t>1)t=1;
    CCVec3 mid={pa.x+(pb.x-pa.x)*t, pa.y+(pb.y-pa.y)*t, pa.z+(pb.z-pa.z)*t};
    uint32_t nv=cc_editmesh_add_vertex(m,mid);
    /* collect rings, insert nv between va and vb wherever that edge appears */
    Ring* rings; uint32_t rc; collect_rings(m,&rings,&rc);
    for(uint32_t r=0;r<rc;r++){ Ring* R=&rings[r];
        for(uint32_t i=0;i<R->n;i++){ uint32_t a=R->ids[i], b=R->ids[(i+1)%R->n];
            if((a==va&&b==vb)||(a==vb&&b==va)){
                uint32_t* ni=malloc((R->n+1)*sizeof(uint32_t)); uint32_t k=0;
                for(uint32_t j=0;j<R->n;j++){ ni[k++]=R->ids[j]; if(j==i)ni[k++]=nv; }
                free(R->ids); R->ids=ni; R->n++; break;
            } } }
    rebuild_from_rings(m,rings,rc);
    return nv;
}
uint32_t cc_editmesh_poke_face(CCEditMesh* m, uint32_t f){
    if(f>=m->F.count||!POOL_AT(m->F,EMFace,f)->alive) return CC_EM_INVALID;
    CCVec3 c=face_centroid(m,f);
    uint32_t tmp[256]; uint32_t n=cc_editmesh_face_vertices(m,f,tmp,256);
    uint32_t center=cc_editmesh_add_vertex(m,c);
    /* rebuild: replace face f with a triangle fan to the center */
    Ring* rings; uint32_t rc; collect_rings(m,&rings,&rc);
    /* find the ring equal to face f's ring (match by set+order start) */
    rebuild_from_rings(m,rings,rc); /* rings already reflect current faces incl. f */
    /* now delete f's triangle-equivalent by adding fan faces and removing original:
       simplest correct approach — rebuild once more with f replaced */
    Ring* r2; uint32_t rc2; collect_rings(m,&r2,&rc2);
    /* find f again by matching vertex set */
    /* (after rebuild ids of faces changed; match the ring whose vertex set == tmp[]) */
    int target=-1;
    for(uint32_t r=0;r<rc2;r++){ if(r2[r].n!=n)continue; int match=1;
        for(uint32_t i=0;i<n&&match;i++){ int found=0; for(uint32_t j=0;j<n;j++) if(r2[r].ids[j]==tmp[i]){found=1;break;} if(!found)match=0; }
        if(match){target=(int)r;break;} }
    /* build new ring list: all rings except target, plus n fan triangles */
    uint32_t newc=rc2-(target>=0?1:0)+n;
    Ring* nr=calloc(newc,sizeof(Ring)); uint32_t w=0;
    for(uint32_t r=0;r<rc2;r++){ if((int)r==target){ continue; } nr[w].ids=r2[r].ids; nr[w].n=r2[r].n; w++; }
    if(target>=0){ uint32_t* ring=r2[target].ids;
        for(uint32_t i=0;i<n;i++){ uint32_t a=ring[i], b=ring[(i+1)%n];
            nr[w].ids=malloc(3*sizeof(uint32_t)); nr[w].ids[0]=a; nr[w].ids[1]=b; nr[w].ids[2]=center; nr[w].n=3; w++; }
        free(r2[target].ids); }
    free(r2);
    rebuild_from_rings(m,nr,w);
    return center;
}
uint32_t cc_editmesh_connect_verts(CCEditMesh* m, uint32_t va, uint32_t vb){
    /* find a face containing both, split its ring in two */
    for(uint32_t f=0;f<m->F.count;f++){ if(!POOL_AT(m->F,EMFace,f)->alive)continue;
        uint32_t tmp[256]; uint32_t n=cc_editmesh_face_vertices(m,f,tmp,256);
        int ia=-1,ib=-1; for(uint32_t i=0;i<n;i++){ if(tmp[i]==va)ia=(int)i; if(tmp[i]==vb)ib=(int)i; }
        if(ia<0||ib<0||abs(ia-ib)<2) continue; /* must be non-adjacent, same face */
        Ring* rings; uint32_t rc; collect_rings(m,&rings,&rc);
        /* find matching ring, split */
        for(uint32_t r=0;r<rc;r++){ if(rings[r].n!=n)continue; int m0=1;
            for(uint32_t i=0;i<n;i++) if(rings[r].ids[i]!=tmp[i]){m0=0;break;}
            if(!m0)continue;
            /* ring A: ia..ib ; ring B: ib..ia */
            uint32_t na=0,nb=0; uint32_t *A=malloc(n*sizeof(uint32_t)),*B=malloc(n*sizeof(uint32_t));
            for(int i=ia;;i=(i+1)%n){ A[na++]=tmp[i]; if(i==ib)break; }
            for(int i=ib;;i=(i+1)%n){ B[nb++]=tmp[i]; if(i==ia)break; }
            /* replace ring r with A, append B */
            Ring* nr=calloc(rc+1,sizeof(Ring)); uint32_t w=0;
            for(uint32_t rr=0;rr<rc;rr++){ if(rr==r){ nr[w].ids=A;nr[w].n=na;w++; } else { nr[w]=rings[rr]; w++; } }
            nr[w].ids=B; nr[w].n=nb; w++;
            free(rings[r].ids); free(rings);
            rebuild_from_rings(m,nr,w);
            return cc_editmesh_find_edge(m,va,vb);
        }
        for(uint32_t r=0;r<rc;r++) free(rings[r].ids); free(rings);
    }
    return CC_EM_INVALID;
}

/* ─── extrude ────────────────────────────────────────────────────────── */
uint32_t cc_editmesh_extrude_face(CCEditMesh* m, uint32_t f, float dist){
    if(f>=m->F.count||!POOL_AT(m->F,EMFace,f)->alive) return CC_EM_INVALID;
    uint32_t ring[256]; uint32_t n=cc_editmesh_face_vertices(m,f,ring,256);
    CCVec3 nrm=face_normal(m,f);
    /* duplicate ring vertices pushed along normal */
    uint32_t* top=malloc(n*sizeof(uint32_t));
    for(uint32_t i=0;i<n;i++){ CCVec3 p=POOL_AT(m->V,EMVert,ring[i])->position;
        top[i]=cc_editmesh_add_vertex(m,(CCVec3){p.x+nrm.x*dist,p.y+nrm.y*dist,p.z+nrm.z*dist}); }
    Ring* rings; uint32_t rc; collect_rings(m,&rings,&rc);
    /* find + remove original face ring, add top ring + side quads */
    int target=-1;
    for(uint32_t r=0;r<rc;r++){ if(rings[r].n!=n)continue; int mt=1;
        for(uint32_t i=0;i<n;i++) if(rings[r].ids[i]!=ring[i]){mt=0;break;} if(mt){target=(int)r;break;} }
    uint32_t newc=rc + n + (target>=0?0:1); /* replace target with top, +n sides */
    Ring* nr=calloc(newc,sizeof(Ring)); uint32_t w=0;
    for(uint32_t r=0;r<rc;r++){ if((int)r==target){ continue; } nr[w]=rings[r]; w++; }
    /* top face (same winding) */
    uint32_t* tr=malloc(n*sizeof(uint32_t)); memcpy(tr,top,n*sizeof(uint32_t));
    nr[w].ids=tr; nr[w].n=n; uint32_t topface_ringidx=w; w++;
    /* side quads: (ring[i], ring[i+1], top[i+1], top[i]) */
    for(uint32_t i=0;i<n;i++){ uint32_t j=(i+1)%n;
        uint32_t* q=malloc(4*sizeof(uint32_t)); q[0]=ring[i];q[1]=ring[j];q[2]=top[j];q[3]=top[i];
        nr[w].ids=q; nr[w].n=4; w++; }
    if(target>=0) free(rings[target].ids);
    free(rings); free(top);
    /* snapshot the top ring's vertex ids on the stack — `tr` is owned by `nr`
       and will be freed inside rebuild_from_rings. */
    uint32_t topsnap[256]; for(uint32_t i=0;i<n&&i<256;i++) topsnap[i]=tr[i];
    rebuild_from_rings(m,nr,w);
    (void)topface_ringidx;
    /* return the (new) top face id: the face whose vertex set == topsnap */
    for(uint32_t ff=0;ff<m->F.count;ff++){ if(!POOL_AT(m->F,EMFace,ff)->alive)continue;
        uint32_t t2[256]; uint32_t nn=cc_editmesh_face_vertices(m,ff,t2,256); if(nn!=n)continue;
        int mt=1; for(uint32_t i=0;i<n;i++){int fnd=0;for(uint32_t j=0;j<n;j++)if(t2[j]==topsnap[i]){fnd=1;break;}if(!fnd){mt=0;break;}}
        if(mt) return ff; }
    return CC_EM_INVALID;
}

/* Vertex bevel (chamfer): truncate vertex v. Each face using v gets its v-corner
 * replaced by two new vertices inset by `amount` along the two incident edges;
 * the ring of new vertices is capped with a new face, cutting the corner off. */
void cc_editmesh_bevel_vertex(CCEditMesh* m, uint32_t v, float amount){
    if(!cc_editmesh_vertex_valid(m,v)||amount<=0) return;
    Ring* rings; uint32_t rc; collect_rings(m,&rings,&rc);
    CCVec3 P=POOL_AT(m->V,EMVert,v)->position;
    /* new-vertex cache keyed by neighbour id: the inset point along edge (v→nb)
       is shared by the two faces meeting at that edge, so dedup by neighbour. */
    uint32_t* nbId=NULL; uint32_t* nbVert=NULL; uint32_t nnb=0,ncap=0;
    #define GET_INSET(NB, OUT) do{ uint32_t _f=UINT32_MAX; \
        for(uint32_t _k=0;_k<nnb;_k++) if(nbId[_k]==(NB)){_f=nbVert[_k];break;} \
        if(_f==UINT32_MAX){ CCVec3 pn=POOL_AT(m->V,EMVert,(NB))->position; \
            CCVec3 dir={pn.x-P.x,pn.y-P.y,pn.z-P.z}; float l=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z); \
            if(l>1e-6f){dir.x/=l;dir.y/=l;dir.z/=l;} float t=amount; if(t>l*0.5f)t=l*0.5f; \
            CCVec3 ip={P.x+dir.x*t,P.y+dir.y*t,P.z+dir.z*t}; _f=cc_editmesh_add_vertex(m,ip); \
            if(nnb>=ncap){ncap=ncap?ncap*2:8;nbId=realloc(nbId,ncap*4);nbVert=realloc(nbVert,ncap*4);} \
            nbId[nnb]=(NB); nbVert[nnb]=_f; nnb++; } (OUT)=_f; }while(0)

    /* rewrite each ring that uses v: replace v by [insetPrev, insetNext] */
    Ring* nr=calloc(rc+1,sizeof(Ring)); uint32_t w=0;
    uint32_t* capRing=malloc((rc*2+4)*sizeof(uint32_t)); uint32_t capN=0; /* collected cap verts */
    for(uint32_t r=0;r<rc;r++){ Ring* R=&rings[r]; int at=-1;
        for(uint32_t i=0;i<R->n;i++) if(R->ids[i]==v){at=(int)i;break;}
        if(at<0){ nr[w].ids=R->ids; nr[w].n=R->n; w++; continue; }
        uint32_t prev=R->ids[(at+R->n-1)%R->n], next=R->ids[(at+1)%R->n];
        uint32_t iprev,inext; GET_INSET(prev,iprev); GET_INSET(next,inext);
        uint32_t* ring=malloc((R->n+1)*sizeof(uint32_t)); uint32_t k=0;
        for(uint32_t i=0;i<R->n;i++){ if((int)i==at){ ring[k++]=iprev; ring[k++]=inext; } else ring[k++]=R->ids[i]; }
        nr[w].ids=ring; nr[w].n=k; w++;
        free(R->ids);
        capRing[capN++]=inext; /* collect for cap (order fixed up below) */
    }
    /* cap face: the unique inset vertices, ordered around the vertex.
       Order them by angle in the plane whose normal is the average direction. */
    if(nnb>=3){
        /* centroid of inset verts */
        CCVec3 c={0,0,0}; for(uint32_t k=0;k<nnb;k++){CCVec3 p=POOL_AT(m->V,EMVert,nbVert[k])->position;c.x+=p.x;c.y+=p.y;c.z+=p.z;}
        c.x/=nnb;c.y/=nnb;c.z/=nnb;
        /* normal = from vertex P toward centroid */
        CCVec3 nrm={c.x-P.x,c.y-P.y,c.z-P.z}; float nl=sqrtf(nrm.x*nrm.x+nrm.y*nrm.y+nrm.z*nrm.z); if(nl>1e-6f){nrm.x/=nl;nrm.y/=nl;nrm.z/=nl;}
        /* build tangent basis */
        CCVec3 up=(fabsf(nrm.y)<0.9f)?(CCVec3){0,1,0}:(CCVec3){1,0,0};
        CCVec3 tx={up.y*nrm.z-up.z*nrm.y,up.z*nrm.x-up.x*nrm.z,up.x*nrm.y-up.y*nrm.x};
        float tl=sqrtf(tx.x*tx.x+tx.y*tx.y+tx.z*tx.z); if(tl>1e-6f){tx.x/=tl;tx.y/=tl;tx.z/=tl;}
        CCVec3 ty={nrm.y*tx.z-nrm.z*tx.y,nrm.z*tx.x-nrm.x*tx.z,nrm.x*tx.y-nrm.y*tx.x};
        /* sort insets by angle */
        typedef struct{uint32_t id;float ang;} AV; AV* av=malloc(nnb*sizeof(AV));
        for(uint32_t k=0;k<nnb;k++){ CCVec3 p=POOL_AT(m->V,EMVert,nbVert[k])->position; CCVec3 d={p.x-c.x,p.y-c.y,p.z-c.z};
            av[k].id=nbVert[k]; av[k].ang=atan2f(d.x*ty.x+d.y*ty.y+d.z*ty.z, d.x*tx.x+d.y*tx.y+d.z*tx.z); }
        for(uint32_t i=0;i<nnb;i++)for(uint32_t j=i+1;j<nnb;j++) if(av[j].ang<av[i].ang){AV t=av[i];av[i]=av[j];av[j]=t;}
        uint32_t* cap=malloc(nnb*sizeof(uint32_t)); for(uint32_t k=0;k<nnb;k++) cap[k]=av[k].id;
        nr[w].ids=cap; nr[w].n=nnb; w++;
        free(av);
    }
    free(capRing); free(nbId); free(nbVert); free(rings);
    POOL_AT(m->V,EMVert,v)->alive=false;   /* original corner removed */
    rebuild_from_rings(m,nr,w);
    #undef GET_INSET
}

/* True Catmull-Clark subdivision. Works on the polygon-ring representation:
 *   face point F  = centroid of the face
 *   edge point E  = average of the edge's 2 endpoints and the 2 adjacent F's
 *                   (boundary edge: midpoint of endpoints)
 *   moved vertex P'= (F_avg + 2*R_avg + (n-3)*P)/n  (Catmull-Clark vertex rule)
 *                   where F_avg = avg of adjacent face points,
 *                         R_avg = avg of adjacent edge midpoints, n = valence
 * then each face of k sides becomes k quads: P' - E(prev) - F - E(next).
 */
typedef struct { uint32_t a,b; uint32_t ep; CCVec3 mid; int face_count; CCVec3 fsum; } CCEdgeRec;

void cc_editmesh_subdivide(CCEditMesh* m, uint32_t iters){
    for(uint32_t it=0; it<iters; it++){
        Ring* rings; uint32_t rc; collect_rings(m,&rings,&rc);
        if(rc==0){ free(rings); return; }

        uint32_t origVcount = m->V.count;   /* original vertex slots */

        /* 1) face points */
        uint32_t* facePt = malloc(rc*sizeof(uint32_t));
        CCVec3*   faceC  = malloc(rc*sizeof(CCVec3));
        for(uint32_t r=0;r<rc;r++){
            CCVec3 c={0,0,0}; for(uint32_t i=0;i<rings[r].n;i++){ CCVec3 p=POOL_AT(m->V,EMVert,rings[r].ids[i])->position; c.x+=p.x;c.y+=p.y;c.z+=p.z; }
            float inv=1.0f/rings[r].n; c.x*=inv;c.y*=inv;c.z*=inv; faceC[r]=c;
            facePt[r]=cc_editmesh_add_vertex(m,c);
        }

        /* 2) edge records (dedup by unordered endpoint pair) */
        CCEdgeRec* edges=NULL; uint32_t ne=0, ecap=0;
        /* per-original-vertex accumulation for the vertex rule */
        CCVec3* Favg=calloc(origVcount,sizeof(CCVec3)); uint32_t* Fn=calloc(origVcount,4);
        CCVec3* Ravg=calloc(origVcount,sizeof(CCVec3)); uint32_t* Rn=calloc(origVcount,4);

        /* first pass: build/accumulate edges + face-point sums */
        for(uint32_t r=0;r<rc;r++){ Ring* R=&rings[r];
            for(uint32_t i=0;i<R->n;i++){ uint32_t a=R->ids[i], b=R->ids[(i+1)%R->n];
                uint32_t lo=a<b?a:b, hi=a<b?b:a;
                /* find existing */
                uint32_t idx=UINT32_MAX; for(uint32_t k=0;k<ne;k++) if(edges[k].a==lo&&edges[k].b==hi){idx=k;break;}
                if(idx==UINT32_MAX){ if(ne>=ecap){ecap=ecap?ecap*2:64; edges=realloc(edges,ecap*sizeof(CCEdgeRec));}
                    CCVec3 pa=POOL_AT(m->V,EMVert,lo)->position, pb=POOL_AT(m->V,EMVert,hi)->position;
                    edges[ne]=(CCEdgeRec){lo,hi,CC_EM_INVALID,{(pa.x+pb.x)*.5f,(pa.y+pb.y)*.5f,(pa.z+pb.z)*.5f},0,{0,0,0}};
                    idx=ne++; }
                edges[idx].face_count++; edges[idx].fsum.x+=faceC[r].x; edges[idx].fsum.y+=faceC[r].y; edges[idx].fsum.z+=faceC[r].z;
                /* accumulate face point onto both endpoints (for F_avg) */
                Favg[a].x+=faceC[r].x;Favg[a].y+=faceC[r].y;Favg[a].z+=faceC[r].z;Fn[a]++;
            }
        }
        /* edge points + accumulate edge midpoints onto endpoints (for R_avg) */
        for(uint32_t k=0;k<ne;k++){ CCEdgeRec* E=&edges[k];
            CCVec3 pa=POOL_AT(m->V,EMVert,E->a)->position, pb=POOL_AT(m->V,EMVert,E->b)->position;
            CCVec3 ep;
            if(E->face_count>=2){ /* interior: (a+b+f1+f2)/4 */
                ep.x=(pa.x+pb.x+E->fsum.x)/4.0f; ep.y=(pa.y+pb.y+E->fsum.y)/4.0f; ep.z=(pa.z+pb.z+E->fsum.z)/4.0f;
            } else { ep=E->mid; } /* boundary edge → midpoint */
            E->ep=cc_editmesh_add_vertex(m,ep);
            /* R_avg uses edge MIDPOINTS (not edge points) per Catmull-Clark */
            Ravg[E->a].x+=E->mid.x;Ravg[E->a].y+=E->mid.y;Ravg[E->a].z+=E->mid.z;Rn[E->a]++;
            Ravg[E->b].x+=E->mid.x;Ravg[E->b].y+=E->mid.y;Ravg[E->b].z+=E->mid.z;Rn[E->b]++;
        }

        /* 3) move original vertices (interior rule; boundary vertices left ~put) */
        for(uint32_t v=0; v<origVcount; v++){
            if(!POOL_AT(m->V,EMVert,v)->alive) continue;
            uint32_t n=Fn[v]; if(n<3) continue; /* skip boundary/degenerate */
            CCVec3 P=POOL_AT(m->V,EMVert,v)->position;
            CCVec3 F={Favg[v].x/n,Favg[v].y/n,Favg[v].z/n};
            CCVec3 Rm={Ravg[v].x/Rn[v],Ravg[v].y/Rn[v],Ravg[v].z/Rn[v]};
            CCVec3 np={ (F.x+2*Rm.x+(n-3)*P.x)/n, (F.y+2*Rm.y+(n-3)*P.y)/n, (F.z+2*Rm.z+(n-3)*P.z)/n };
            POOL_AT(m->V,EMVert,v)->position=np;
        }

        /* 4) build new quad rings: for each face, for each corner i:
              [ P'(i), E(i,i+1), F, E(i-1,i) ] */
        uint32_t newRingCount=0; for(uint32_t r=0;r<rc;r++) newRingCount+=rings[r].n;
        Ring* nr=calloc(newRingCount,sizeof(Ring)); uint32_t w=0;
        for(uint32_t r=0;r<rc;r++){ Ring* R=&rings[r]; uint32_t k=R->n;
            for(uint32_t i=0;i<k;i++){
                uint32_t vP=R->ids[i];
                uint32_t vNext=R->ids[(i+1)%k], vPrev=R->ids[(i+k-1)%k];
                /* edge point for (i,i+1) */
                uint32_t e_next=UINT32_MAX,e_prev=UINT32_MAX;
                { uint32_t lo=vP<vNext?vP:vNext, hi=vP<vNext?vNext:vP;
                  for(uint32_t x=0;x<ne;x++) if(edges[x].a==lo&&edges[x].b==hi){e_next=edges[x].ep;break;} }
                { uint32_t lo=vP<vPrev?vP:vPrev, hi=vP<vPrev?vPrev:vP;
                  for(uint32_t x=0;x<ne;x++) if(edges[x].a==lo&&edges[x].b==hi){e_prev=edges[x].ep;break;} }
                uint32_t* q=malloc(4*sizeof(uint32_t));
                q[0]=vP; q[1]=e_next; q[2]=facePt[r]; q[3]=e_prev;
                nr[w].ids=q; nr[w].n=4; w++;
            }
        }
        for(uint32_t r=0;r<rc;r++) free(rings[r].ids); free(rings);
        free(facePt); free(faceC); free(edges);
        free(Favg);free(Fn);free(Ravg);free(Rn);
        rebuild_from_rings(m,nr,w);
    }
}

/* ─── deletion ───────────────────────────────────────────────────────── */
void cc_editmesh_delete_face(CCEditMesh* m, uint32_t f){
    if(f>=m->F.count||!POOL_AT(m->F,EMFace,f)->alive)return;
    Ring* rings; uint32_t rc; uint32_t tmp[256]; uint32_t n=cc_editmesh_face_vertices(m,f,tmp,256);
    collect_rings(m,&rings,&rc);
    Ring* nr=calloc(rc,sizeof(Ring)); uint32_t w=0;
    for(uint32_t r=0;r<rc;r++){ int same=(rings[r].n==n); if(same)for(uint32_t i=0;i<n;i++)if(rings[r].ids[i]!=tmp[i]){same=0;break;}
        if(same){ free(rings[r].ids); continue; } nr[w++]=rings[r]; }
    free(rings); rebuild_from_rings(m,nr,w);
}
void cc_editmesh_delete_vertex(CCEditMesh* m, uint32_t v){
    if(!cc_editmesh_vertex_valid(m,v))return;
    Ring* rings; uint32_t rc; collect_rings(m,&rings,&rc);
    Ring* nr=calloc(rc,sizeof(Ring)); uint32_t w=0;
    for(uint32_t r=0;r<rc;r++){ int uses=0; for(uint32_t i=0;i<rings[r].n;i++) if(rings[r].ids[i]==v){uses=1;break;}
        if(uses){ free(rings[r].ids); } else nr[w++]=rings[r]; }
    free(rings); rebuild_from_rings(m,nr,w);
    POOL_AT(m->V,EMVert,v)->alive=false;
}

/* ─── validate + dump ────────────────────────────────────────────────── */
bool cc_editmesh_validate(const CCEditMesh* m, char* err, uint32_t elen){
    for(uint32_t h=0;h<m->H.count;h++){ EMHalf* he=POOL_AT(m->H,EMHalf,h); if(!he->alive)continue;
        if(he->next>=m->H.count){ if(err)snprintf(err,elen,"he %u bad next",h); return false; }
        if(he->twin!=CC_EM_INVALID){ EMHalf* tw=POOL_AT(m->H,EMHalf,he->twin);
            if(tw->twin!=h){ if(err)snprintf(err,elen,"he %u twin not mutual",h); return false; } }
        if(!POOL_AT(m->V,EMVert,he->origin)->alive){ if(err)snprintf(err,elen,"he %u dead origin",h); return false; }
    }
    return true;
}
void cc_editmesh_dump(const CCEditMesh* m, uint32_t max){
    printf("CCEditMesh: %u verts, %u edges, %u faces (snap=%.3g)\n",
        cc_editmesh_vertex_count(m),cc_editmesh_edge_count(m),cc_editmesh_face_count(m),m->snap);
    uint32_t shown=0;
    for(uint32_t i=0;i<m->V.count&&shown<max;i++){ EMVert* v=POOL_AT(m->V,EMVert,i); if(!v->alive)continue;
        printf("  v%u (%.2f, %.2f, %.2f)\n",i,v->position.x,v->position.y,v->position.z); shown++; }
    shown=0;
    for(uint32_t f=0;f<m->F.count&&shown<max;f++){ if(!POOL_AT(m->F,EMFace,f)->alive)continue;
        uint32_t r[64]; uint32_t n=cc_editmesh_face_vertices(m,f,r,64);
        printf("  f%u [",f); for(uint32_t i=0;i<n;i++)printf("%u%s",r[i],i+1<n?" ":""); printf("]\n"); shown++; }
}

/* ─── bake ───────────────────────────────────────────────────────────── */
uint32_t cc_editmesh_bake_cpu(CCEditMesh* m, bool smooth,
                              CCVertex** out_v, uint32_t* out_nv,
                              uint32_t** out_i, uint32_t* out_ni){
    /* map alive vertex ids → compact index */
    uint32_t* remap=malloc(m->V.count*sizeof(uint32_t)); uint32_t nv=0;
    for(uint32_t i=0;i<m->V.count;i++){ if(POOL_AT(m->V,EMVert,i)->alive) remap[i]=nv++; else remap[i]=CC_EM_INVALID; }
    CCVertex* verts=calloc(nv?nv:1,sizeof(CCVertex));
    for(uint32_t i=0;i<m->V.count;i++){ if(!POOL_AT(m->V,EMVert,i)->alive)continue; CCVec3 p=POOL_AT(m->V,EMVert,i)->position;
        CCVertex* d=&verts[remap[i]]; d->pos[0]=p.x;d->pos[1]=p.y;d->pos[2]=p.z;
        d->normal[1]=1; d->tangent[0]=1; d->tangent[3]=1; d->color[0]=d->color[1]=d->color[2]=d->color[3]=255; }
    /* triangulate faces (fan) */
    uint32_t icap=256,ni=0; uint32_t* idx=malloc(icap*sizeof(uint32_t));
    for(uint32_t f=0;f<m->F.count;f++){ if(!POOL_AT(m->F,EMFace,f)->alive)continue;
        uint32_t r[256]; uint32_t n=cc_editmesh_face_vertices(m,f,r,256);
        for(uint32_t k=1;k+1<n;k++){ if(ni+3>icap){icap*=2;idx=realloc(idx,icap*sizeof(uint32_t));}
            idx[ni++]=remap[r[0]]; idx[ni++]=remap[r[k]]; idx[ni++]=remap[r[k+1]]; } }
    cc_geometry_recompute_normals(verts,nv,idx,ni,smooth);
    cc_geometry_recompute_tangents(verts,nv,idx,ni);
    free(remap);
    *out_v=verts; *out_nv=nv; *out_i=idx; *out_ni=ni;
    return ni/3;
}
CCMesh cc_editmesh_bake(CCEditMesh* m, CCEngine* eng, bool smooth){
    CCVertex* v; uint32_t nv; uint32_t* i; uint32_t ni;
    cc_editmesh_bake_cpu(m,smooth,&v,&nv,&i,&ni);
    CCMesh mesh=cc_mesh_create(eng,v,nv,i,ni,CC_MESH_STATIC);
    free(v); free(i);
    return mesh;
}

/* copy */
CCEditMesh* cc_editmesh_copy(const CCEditMesh* src){
    if(!src)return NULL; CCEditMesh* m=cc_editmesh_new(); m->snap=src->snap;
    for(uint32_t i=0;i<src->V.count;i++){ EMVert* v=POOL_AT(src->V,EMVert,i);
        if(v->alive) cc_editmesh_add_vertex(m,v->position); else { uint32_t x=pool_add(&m->V); POOL_AT(m->V,EMVert,x)->alive=false; } }
    Ring* rings; uint32_t rc; collect_rings((CCEditMesh*)src,&rings,&rc);
    for(uint32_t r=0;r<rc;r++){ cc_editmesh_add_face(m,rings[r].ids,rings[r].n); free(rings[r].ids);} free(rings);
    return m;
}
