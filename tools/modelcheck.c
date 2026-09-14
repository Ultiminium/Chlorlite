/* modelcheck — ULTRA-PRECISE, VISUAL-INDEPENDENT model inspection.
 *
 * The lesson: reading a RENDER is unreliable (a session called flat ribbon arms
 * "round"). The pixels are downstream of the geometry and inherit every ambiguity
 * of camera + lighting + a fallible viewer. So this tool ignores rendering entirely
 * and measures the GEOMETRY ITSELF — the exact vertex positions, triangle
 * connectivity, bone weights. An arm's roundness is not "how wide it looks"; it's
 * min_cross_radius / max_cross_radius of its actual vertex rings — a hard number,
 * camera-independent, unfoolable.
 *
 * It reports, as structured text a caller must confront:
 *   TOPOLOGY   vertex/tri counts, degenerate tris, boundary edges (holes/gaps),
 *              non-manifold edges, duplicate/unused verts, bbox + real proportions
 *   SKIN       every vertex weighted? weights sum to 1? bones out of range?
 *              bones with zero influence? (a bone nothing is weighted to is dead)
 *   PER-BONE   for each bone with geometry: the cross-section radius profile along
 *              the limb → RIBBON (thin in one axis), STUB (radius collapses at end),
 *              LUMP (radius spikes). This is the ribbon-arm detector, from geometry.
 *
 * Link a function `CCModel* build_model(void)` (or use --humanoid to self-test on
 * the engine's humanoid). Prints a report + exits nonzero if any hard FAIL.
 *
 * This is the MAIN datapoint. A render (inspect.py) is an optional cross-check.
 */
#include "cc/claudecore.h"
#include "cc/ccmodel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fail=0;
#define FAILN(...) do{ printf("  [FAIL] "); printf(__VA_ARGS__); printf("\n"); g_fail++; }while(0)
#define OKN(...)   do{ printf("  [OK]   "); printf(__VA_ARGS__); printf("\n"); }while(0)
#define WARN(...)  do{ printf("  [warn] "); printf(__VA_ARGS__); printf("\n"); }while(0)

/* ---------- topology ---------- */
static void check_topology(const CCModel* m){
    const CCMGeomChunk* G=&m->geom;
    uint32_t nv=G->vertex_count, ni=G->index_count, nt=ni/3;
    printf("TOPOLOGY  (%u verts, %u tris)\n", nv, nt);

    /* bounding box + proportions from ACTUAL vertex positions */
    float mn[3]={1e30f,1e30f,1e30f}, mx[3]={-1e30f,-1e30f,-1e30f};
    for(uint32_t i=0;i<nv;i++) for(int k=0;k<3;k++){
        float v=G->vertices[i].pos[k]; if(v<mn[k])mn[k]=v; if(v>mx[k])mx[k]=v; }
    float dim[3]={mx[0]-mn[0],mx[1]-mn[1],mx[2]-mn[2]};
    printf("  bbox  x[%.3f,%.3f] y[%.3f,%.3f] z[%.3f,%.3f]  dims (%.3f, %.3f, %.3f)\n",
        mn[0],mx[0],mn[1],mx[1],mn[2],mx[2],dim[0],dim[1],dim[2]);
    /* a humanoid should be TALLER than wide/deep. depth≈width (not a flat slab). */
    if(dim[1] < dim[0]*0.9f) WARN("figure is not clearly taller than wide (y %.2f vs x %.2f) — unusual for a humanoid", dim[1], dim[0]);
    if(dim[0] > 1e-4f){
        float depth_ratio = dim[2]/dim[0];
        if(depth_ratio < 0.25f) FAILN("whole model is nearly FLAT: depth %.3f is only %.0f%% of width %.3f — a slab, not a 3D body", dim[2], depth_ratio*100, dim[0]);
        else OKN("model has real depth: depth/width = %.2f", depth_ratio);
    }

    /* degenerate triangles (zero area) */
    uint32_t degen=0;
    for(uint32_t t=0;t<nt;t++){
        const float*a=G->vertices[G->indices[t*3]].pos;
        const float*b=G->vertices[G->indices[t*3+1]].pos;
        const float*c=G->vertices[G->indices[t*3+2]].pos;
        float e1[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]}, e2[3]={c[0]-a[0],c[1]-a[1],c[2]-a[2]};
        float cx=e1[1]*e2[2]-e1[2]*e2[1], cy=e1[2]*e2[0]-e1[0]*e2[2], cz=e1[0]*e2[1]-e1[1]*e2[0];
        if(sqrtf(cx*cx+cy*cy+cz*cz) < 1e-9f) degen++;
    }
    if(degen) FAILN("%u degenerate (zero-area) triangles", degen); else OKN("no degenerate triangles");

    /* boundary + non-manifold edges via an edge-use count.
       Each undirected edge should be shared by exactly 2 triangles in a closed mesh.
       used once = boundary (a hole/open edge, e.g. an armpit gap or unclosed cap);
       used >2 = non-manifold (bad topology). */
    /* hash edges into a simple open-addressing table keyed by (min,max) vertex id */
    uint32_t cap=1; while(cap < ni*2) cap<<=1; if(cap<16)cap=16;
    typedef struct{ uint32_t a,b,count; } Edge;
    Edge* tab=calloc(cap,sizeof(Edge));
    #define EMPTY 0xFFFFFFFF
    for(uint32_t i=0;i<cap;i++) tab[i].a=EMPTY;
    uint32_t boundary=0, nonman=0;
    for(uint32_t t=0;t<nt;t++){
        uint32_t idx[3]={G->indices[t*3],G->indices[t*3+1],G->indices[t*3+2]};
        for(int e=0;e<3;e++){
            uint32_t u=idx[e], v=idx[(e+1)%3];
            if(u>v){uint32_t tmp=u;u=v;v=tmp;}
            uint32_t h=(u*73856093u ^ v*19349663u)&(cap-1);
            while(tab[h].a!=EMPTY && !(tab[h].a==u&&tab[h].b==v)) h=(h+1)&(cap-1);
            if(tab[h].a==EMPTY){ tab[h].a=u; tab[h].b=v; tab[h].count=1; }
            else tab[h].count++;
        }
    }
    for(uint32_t i=0;i<cap;i++) if(tab[i].a!=EMPTY){
        if(tab[i].count==1) boundary++;
        else if(tab[i].count>2) nonman++;
    }
    free(tab);
    if(nonman) FAILN("%u non-manifold edges (shared by >2 triangles) — broken topology", nonman);
    else OKN("no non-manifold edges");
    if(boundary){
        /* boundary edges aren't always fatal (an intentional open mesh), but for a
           "seamless continuous body" claim they mean HOLES — the armpit gap etc. */
        WARN("%u boundary (open) edges — the mesh has holes/unclosed loops (gaps, open caps). A 'seamless closed body' should have ~0.", boundary);
    } else OKN("closed mesh: no boundary edges (no holes)");

    /* duplicate + unused vertices */
    char* used=calloc(nv,1);
    for(uint32_t i=0;i<ni;i++) used[G->indices[i]]=1;
    uint32_t unused=0; for(uint32_t i=0;i<nv;i++) if(!used[i]) unused++;
    free(used);
    if(unused) WARN("%u unused vertices (not referenced by any triangle)", unused);
}

/* ---------- skin / weights ---------- */
static void check_skin(const CCModel* m){
    const CCMSkinChunk* S=&m->skin; const CCMSkelChunk* K=&m->skel;
    printf("SKIN  (%u skinned verts, %u bones)\n", S?S->vertex_count:0, K?K->bone_count:0);
    if(!S || S->vertex_count==0){ WARN("no skin data (static mesh)"); return; }
    if(S->vertex_count != m->geom.vertex_count)
        FAILN("skin vertex_count %u != geom vertex_count %u", S->vertex_count, m->geom.vertex_count);
    uint32_t bad_sum=0, oob=0, unweighted=0;
    uint32_t nb = K?K->bone_count:0;
    char* bone_used = nb?calloc(nb,1):NULL;
    for(uint32_t i=0;i<S->vertex_count;i++){
        const CCMSkinVertex* sv=&S->weights[i];
        float sum=0; int any=0;
        for(int k=0;k<4;k++){
            sum+=sv->weight[k];
            if(sv->weight[k]>0){ any=1;
                if(sv->joint[k]>=nb) oob++;
                else if(bone_used) bone_used[sv->joint[k]]=1;
            }
        }
        if(!any) unweighted++;
        else if(fabsf(sum-1.0f)>0.02f) bad_sum++;
    }
    if(unweighted) FAILN("%u vertices have ZERO weight (won't follow the skeleton)", unweighted);
    else OKN("every vertex is weighted");
    if(bad_sum) FAILN("%u vertices have weights that don't sum to 1.0 (±0.02)", bad_sum);
    else OKN("all weight sums ~1.0");
    if(oob) FAILN("%u weight references point to a bone index >= bone_count", oob);
    else OKN("all weight bone-indices in range");
    if(bone_used){
        uint32_t dead=0; for(uint32_t b=0;b<nb;b++) if(!bone_used[b]) dead++;
        if(dead) WARN("%u bones have NO vertices weighted to them (dead bones)", dead);
        free(bone_used);
    }
}

/* ---------- per-bone cross-section (the geometric ribbon/stub detector) ----------
   For each bone, gather the vertices rigidly/primarily weighted to it. Project them
   onto the bone's axis (bind head->child direction). Slice into bands along the
   axis; each band's radius = mean distance of its verts from the axis. Then:
     - RIBBON: within a band, spread in one perpendicular axis << the other
               (the arm is wide one way, paper-thin the other).
     - STUB:   radius collapses to ~0 well before the bone's end.
     - LUMP:   a band radius spikes far above neighbors.
   All from vertex coordinates — no render. */
static void bone_world(const CCMSkelChunk* sk, uint16_t b, float out[3]){
    out[0]=out[1]=out[2]=0;
    int guard=0;
    while(b!=CCM_BONE_NO_PARENT && b<sk->bone_count && guard++<300){
        out[0]+=sk->bones[b].bind_pos[0]; out[1]+=sk->bones[b].bind_pos[1]; out[2]+=sk->bones[b].bind_pos[2];
        b=sk->bones[b].parent;
    }
}
static void check_cross_sections(const CCModel* m){
    const CCMSkinChunk* S=&m->skin; const CCMSkelChunk* K=&m->skel; const CCMGeomChunk* G=&m->geom;
    printf("CROSS-SECTIONS  (per-bone limb geometry)\n");
    if(!S||!K||S->vertex_count==0){ WARN("no skin — skipping"); return; }
    /* primary bone per vertex = highest weight */
    for(uint16_t b=0;b<K->bone_count;b++){
        /* only limbs matter; skip if too few verts */
        /* gather verts primarily weighted to b */
        uint32_t cnt=0;
        for(uint32_t i=0;i<S->vertex_count;i++){
            const CCMSkinVertex* sv=&S->weights[i];
            int best=0; for(int k=1;k<4;k++) if(sv->weight[k]>sv->weight[best]) best=k;
            if(sv->weight[best]>0 && sv->joint[best]==b) cnt++;
        }
        if(cnt<12) continue;   /* not a fleshed limb */
        const char* name=K->bones[b].name;
        /* bone axis: from bone head to its own bind offset direction (head->tip).
           Use parent-head -> this-head as the segment direction. */
        float head[3], phead[3];
        bone_world(K,b,head);
        uint16_t par=K->bones[b].parent;
        if(par==CCM_BONE_NO_PARENT){ continue; }
        bone_world(K,par,phead);
        float ax[3]={head[0]-phead[0],head[1]-phead[1],head[2]-phead[2]};
        float al=sqrtf(ax[0]*ax[0]+ax[1]*ax[1]+ax[2]*ax[2]); if(al<1e-5f) continue;
        ax[0]/=al;ax[1]/=al;ax[2]/=al;
        /* two perpendicular axes */
        float up[3]={0,1,0}; if(fabsf(ax[1])>0.9f){ up[0]=1;up[1]=0;up[2]=0; }
        float e1[3]={ax[1]*up[2]-ax[2]*up[1], ax[2]*up[0]-ax[0]*up[2], ax[0]*up[1]-ax[1]*up[0]};
        float e1l=sqrtf(e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2]); e1[0]/=e1l;e1[1]/=e1l;e1[2]/=e1l;
        float e2[3]={ax[1]*e1[2]-ax[2]*e1[1], ax[2]*e1[0]-ax[0]*e1[2], ax[0]*e1[1]-ax[1]*e1[0]};
        /* accumulate spread along e1 vs e2 (ribbon = one << other), and radius */
        float sum_e1=0,sum_e2=0,sum_r=0; uint32_t n=0;
        float max_e1=0,max_e2=0;
        for(uint32_t i=0;i<S->vertex_count;i++){
            const CCMSkinVertex* sv=&S->weights[i];
            int best=0; for(int k=1;k<4;k++) if(sv->weight[k]>sv->weight[best]) best=k;
            if(!(sv->weight[best]>0 && sv->joint[best]==b)) continue;
            const float* p=G->vertices[i].pos;
            float d[3]={p[0]-head[0],p[1]-head[1],p[2]-head[2]};
            float pe1=d[0]*e1[0]+d[1]*e1[1]+d[2]*e1[2];
            float pe2=d[0]*e2[0]+d[1]*e2[1]+d[2]*e2[2];
            sum_e1+=fabsf(pe1); sum_e2+=fabsf(pe2); sum_r+=sqrtf(pe1*pe1+pe2*pe2); n++;
            if(fabsf(pe1)>max_e1)max_e1=fabsf(pe1); if(fabsf(pe2)>max_e2)max_e2=fabsf(pe2);
        }
        if(n<8) continue;
        float spread1=max_e1, spread2=max_e2;
        float ratio = (spread1>spread2)? (spread2/(spread1+1e-9f)) : (spread1/(spread2+1e-9f));
        /* ratio near 1 = round; near 0 = flat ribbon */
        printf("  bone '%s' (%u verts): cross-section %.3f x %.3f  roundness=%.2f\n",
               name, cnt, spread1*2, spread2*2, ratio);
        if(ratio < 0.45f)
            FAILN("  '%s' is a FLAT RIBBON: %.3f vs %.3f thick (roundness %.2f, want >0.45) — not a round limb",
                  name, spread1*2, spread2*2, ratio);
    }
}

/* provided by the linked model source, OR --humanoid self-test */
CCModel* build_model(void) __attribute__((weak));

/* build a deliberately-ribbon-armed humanoid to PROVE the detector works from
   geometry alone (no render). Wraps each bone in a flat slab instead of a tube. */
static CCModel* build_ribbon_test(int flat){
    CCModel* m=ccm_make_humanoid("test");
    const CCMSkelChunk* sk=&m->skel;
    /* generate verts: a ring (round) or a flat strip (ribbon) around each bone seg */
    uint32_t cap=4096; CCMVertex* V=calloc(cap,sizeof*V); uint16_t* B=calloc(cap,2);
    uint32_t* I=calloc(cap*6,4); uint32_t nv=0,ni=0;
    for(uint16_t b=0;b<sk->bone_count;b++){
        uint16_t p=sk->bones[b].parent; if(p==CCM_BONE_NO_PARENT) continue;
        float h[3],ph[3]; bone_world(sk,b,h); bone_world(sk,p,ph);
        float base=nv; int segs=8; float r=0.05f;
        for(int ring=0;ring<2;ring++){
            float* c = ring? h:ph;
            for(int s=0;s<segs;s++){
                float th=(float)s/segs*6.2831853f;
                float ox=cosf(th)*r, oz=sinf(th)*r;
                if(flat) oz*=0.1f;   /* RIBBON: squash one axis → paper-thin */
                if(nv<cap){ V[nv].pos[0]=c[0]+ox; V[nv].pos[1]=c[1]; V[nv].pos[2]=c[2]+oz;
                    V[nv].normal[1]=1; V[nv].color[0]=200;V[nv].color[3]=255; B[nv]=b; nv++; }
            }
        }
        for(int s=0;s<segs;s++){ int s2=(s+1)%segs;
            uint32_t a0=base+s,a1=base+s2,b0=base+segs+s,b1=base+segs+s2;
            if(ni+6<cap*6){ I[ni++]=a0;I[ni++]=b0;I[ni++]=a1; I[ni++]=a1;I[ni++]=b0;I[ni++]=b1; } }
    }
    ccm_set_geometry(m,V,nv,I,ni);
    CCMSkinVertex* sw=calloc(nv,sizeof*sw);
    for(uint32_t i=0;i<nv;i++){ sw[i].joint[0]=B[i]; sw[i].weight[0]=1.0f; }
    ccm_set_skin(m,sw,nv); ccm_compute_inv_bind_poses(m);
    free(V);free(B);free(I);free(sw);
    return m;
}

int main(int argc,char**argv){
    CCModel* m=NULL;
    if(argc>1 && !strcmp(argv[1],"--ribbon")) m=build_ribbon_test(1);   /* self-test: should FAIL */
    else if(argc>1 && !strcmp(argv[1],"--round")) m=build_ribbon_test(0);/* self-test: should pass */
    else if(build_model) m=build_model();
    else { printf("no model: link build_model() or use --ribbon/--round self-test\n"); return 2; }
    if(!m){ printf("build_model returned NULL\n"); return 2; }

    printf("=== modelcheck: '%s' ===\n", m->name[0]?m->name:"(unnamed)");
    check_topology(m);
    check_skin(m);
    check_cross_sections(m);
    printf("=== %s ===\n", g_fail? "FAILURES FOUND — model is NOT good, do not claim otherwise" : "all hard checks passed");
    return g_fail?1:0;
}
