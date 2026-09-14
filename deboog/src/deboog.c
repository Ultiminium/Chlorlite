/* deboog.c — implementation of the mathematical/geometric debugging standard.
 * Pure C, depends only on libm. See deboog.h for the contract. */
#include "deboog/deboog.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>


const char* deboog_version(void){ return DEBOOG_VERSION; }

/* ---- helpers ---- */
static int is_denormal(float f){ return f!=0.0f && fabsf(f) < FLT_MIN; }

/* ============================ 1. NUMERIC ============================ */
DbNumeric deboog_scan_floats(const float* d, size_t n){
    DbNumeric r; memset(&r,0,sizeof r); r.count=n; r.first_bad_index=SIZE_MAX;
    r.min=DBL_MAX; r.max=-DBL_MAX; r.ok=true;
    for(size_t i=0;i<n;i++){
        float f=d[i];
        if(isnan(f)){ r.nan_count++; if(r.first_bad_index==SIZE_MAX)r.first_bad_index=i; r.ok=false; continue; }
        if(isinf(f)){ r.inf_count++; if(r.first_bad_index==SIZE_MAX)r.first_bad_index=i; r.ok=false; continue; }
        if(is_denormal(f)) r.denormal_count++;
        if(f<r.min){r.min=f;} if(f>r.max){r.max=f;}
    }
    if(r.min>r.max){ r.min=r.max=0; }
    return r;
}
DbRange deboog_check_range(const float* d, size_t n, double lo, double hi){
    DbRange r; memset(&r,0,sizeof r); r.count=n; r.ok=true; r.worst_index=SIZE_MAX;
    double worst_dev=0;
    for(size_t i=0;i<n;i++){
        double v=d[i], dev=0;
        if(v<lo) dev=lo-v; else if(v>hi) dev=v-hi;
        if(dev>0){ r.out_of_range++; r.ok=false; if(dev>worst_dev){worst_dev=dev; r.worst=v; r.worst_index=i;} }
    }
    return r;
}

/* ============================ 2. MATRIX / QUAT ============================ */
DbMatrix deboog_check_matrix(const float m[16]){
    DbMatrix r; memset(&r,0,sizeof r);
    r.finite=true;
    for(int i=0;i<16;i++) if(!isfinite(m[i])) r.finite=false;
    /* column-major: col c = m[c*4 + row] */
    #define C(c,row) m[(c)*4+(row)]
    double det =
        C(0,0)*(C(1,1)*C(2,2)-C(1,2)*C(2,1))
      - C(1,0)*(C(0,1)*C(2,2)-C(0,2)*C(2,1))
      + C(2,0)*(C(0,1)*C(1,2)-C(0,2)*C(1,1));
    r.determinant=det;
    r.invertible = fabs(det) > 1e-9;
    r.left_handed = det < 0;
    /* column lengths = scale */
    double lx=sqrt(C(0,0)*C(0,0)+C(0,1)*C(0,1)+C(0,2)*C(0,2));
    double ly=sqrt(C(1,0)*C(1,0)+C(1,1)*C(1,1)+C(1,2)*C(1,2));
    double lz=sqrt(C(2,0)*C(2,0)+C(2,1)*C(2,1)+C(2,2)*C(2,2));
    r.scale_x=lx; r.scale_y=ly; r.scale_z=lz;
    /* orthonormal: columns mutually perpendicular AND unit length */
    double d01=(C(0,0)*C(1,0)+C(0,1)*C(1,1)+C(0,2)*C(1,2));
    double d02=(C(0,0)*C(2,0)+C(0,1)*C(2,1)+C(0,2)*C(2,2));
    double d12=(C(1,0)*C(2,0)+C(1,1)*C(2,1)+C(1,2)*C(2,2));
    r.orthonormal = fabs(lx-1)<1e-3 && fabs(ly-1)<1e-3 && fabs(lz-1)<1e-3
                 && fabs(d01)<1e-3 && fabs(d02)<1e-3 && fabs(d12)<1e-3;
    #undef C
    r.ok = r.finite && r.invertible;
    return r;
}
DbQuat deboog_check_quat(const float q[4]){
    DbQuat r; memset(&r,0,sizeof r);
    r.finite = isfinite(q[0])&&isfinite(q[1])&&isfinite(q[2])&&isfinite(q[3]);
    r.length = sqrt((double)q[0]*q[0]+(double)q[1]*q[1]+(double)q[2]*q[2]+(double)q[3]*q[3]);
    r.normalized = fabs(r.length-1.0) < 1e-3;
    r.ok = r.finite && r.normalized;
    return r;
}

/* ============================ 3. GEOMETRY ============================ */
static const float* vpos(const float* P, uint32_t stride, uint64_t i){ return P+(size_t)i*stride; }

/* ---- spatial hash grid: makes duplicate-detection and nearest-point O(n) instead
   of O(n^2), so there is no quadratic wall and no need for a small vertex cap.
   Cells are `cell` units wide; a point maps to an integer cell coord, hashed into
   an open-addressing table of linked buckets (bucket = index list). ---- */
typedef struct { int32_t cx,cy,cz; uint32_t head; } DbCell;   /* head = first point idx+1, 0=empty */
typedef struct {
    DbCell*   cells; uint32_t cap;
    uint32_t* next;                 /* per-point: next index in same cell (+1, 0=end) */
    const float* P; uint32_t stride; double inv_cell;
} DbHash;
static uint32_t db_cellhash(int32_t x,int32_t y,int32_t z,uint32_t cap){
    uint32_t h=(uint32_t)(x*73856093) ^ (uint32_t)(y*19349663) ^ (uint32_t)(z*83492791);
    return h&(cap-1);
}
static void db_hash_build(DbHash* H, const float* P, uint32_t n, uint32_t stride, double cell){
    if(cell<=0) cell=1e-6;
    H->P=P; H->stride=stride; H->inv_cell=1.0/cell;
    uint64_t cap64=16; while(cap64 < (uint64_t)n*2) cap64<<=1;   /* load factor < 0.5 */
    uint32_t cap=(cap64>0x40000000u)?0x40000000u:(uint32_t)cap64;
    H->cap=cap; H->cells=calloc(cap,sizeof(DbCell)); H->next=calloc(n?n:1,sizeof(uint32_t));
    for(uint32_t i=0;i<n;i++){
        const float* p=vpos(P,stride,i);
        int32_t cx=(int32_t)floor(p[0]*H->inv_cell), cy=(int32_t)floor(p[1]*H->inv_cell), cz=(int32_t)floor(p[2]*H->inv_cell);
        uint32_t h=db_cellhash(cx,cy,cz,cap);
        while(H->cells[h].head && !(H->cells[h].cx==cx&&H->cells[h].cy==cy&&H->cells[h].cz==cz)) h=(h+1)&(cap-1);
        if(!H->cells[h].head){ H->cells[h].cx=cx;H->cells[h].cy=cy;H->cells[h].cz=cz; }
        H->next[i]=H->cells[h].head;      /* push onto cell's list */
        H->cells[h].head=i+1;
    }
}
static void db_hash_free(DbHash* H){ free(H->cells); free(H->next); }
/* nearest squared distance from q to any indexed point, searching the 3x3x3
   neighbor cells (so it finds anything within one cell width). */
static double db_hash_nearest2(const DbHash* H, const double q[3], uint32_t skip){
    int32_t bx=(int32_t)floor(q[0]*H->inv_cell), by=(int32_t)floor(q[1]*H->inv_cell), bz=(int32_t)floor(q[2]*H->inv_cell);
    double best=DBL_MAX;
    for(int dx=-1;dx<=1;dx++)for(int dy=-1;dy<=1;dy++)for(int dz=-1;dz<=1;dz++){
        int32_t cx=bx+dx,cy=by+dy,cz=bz+dz;
        uint32_t h=db_cellhash(cx,cy,cz,H->cap);
        while(H->cells[h].head && !(H->cells[h].cx==cx&&H->cells[h].cy==cy&&H->cells[h].cz==cz)) h=(h+1)&(H->cap-1);
        if(!H->cells[h].head) continue;
        for(uint32_t idx=H->cells[h].head; idx; idx=H->next[idx-1]){
            uint32_t i=idx-1; if(i==skip) continue;
            const float* p=vpos(H->P,H->stride,i);
            double ex=q[0]-p[0],ey=q[1]-p[1],ez=q[2]-p[2]; double d=ex*ex+ey*ey+ez*ez;
            if(d<best)best=d;
        }
    }
    return best;
}

DbMesh deboog_check_mesh(const float* P, uint64_t nv, uint32_t stride,
                         const uint32_t* idx, uint64_t ni){
    DbMesh r; memset(&r,0,sizeof r);
    r.vertex_count=nv; r.triangle_count=ni/3;
    if((uint64_t)nv > DEBOOG_MAX_VERTS){
        /* refuse absurd/corrupt sizes gracefully rather than attempt a huge alloc */
        r.ok=false; return r;
    }
    /* The limit above is the DECLARED ceiling (420 billion). Actually processing a
       mesh also needs memory for it; guard the real allocations so a count the host
       can't back fails cleanly (r.ok=false) instead of crashing. This is the honest
       line: the number is representable and enforced; hardware is the real bound. */
    if(nv && (uint64_t)nv > (SIZE_MAX / (stride<3?3:stride) / sizeof(float))){
        r.ok=false; return r;
    }
    if(stride<3) stride=3;
    /* bbox */
    for(int k=0;k<3;k++){ r.bbox_min[k]=DBL_MAX; r.bbox_max[k]=-DBL_MAX; }
    for(uint64_t i=0;i<nv;i++){ const float* p=vpos(P,stride,i);
        for(int k=0;k<3;k++){ if(p[k]<r.bbox_min[k]){r.bbox_min[k]=p[k];} if(p[k]>r.bbox_max[k]){r.bbox_max[k]=p[k];} } }
    for(int k=0;k<3;k++) r.bbox_dim[k]=r.bbox_max[k]-r.bbox_min[k];

    /* degenerate tris */
    for(uint64_t t=0;t<r.triangle_count;t++){
        const float*a=vpos(P,stride,idx[t*3]),*b=vpos(P,stride,idx[t*3+1]),*c=vpos(P,stride,idx[t*3+2]);
        double e1[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]}, e2[3]={c[0]-a[0],c[1]-a[1],c[2]-a[2]};
        double cx=e1[1]*e2[2]-e1[2]*e2[1], cy=e1[2]*e2[0]-e1[0]*e2[2], cz=e1[0]*e2[1]-e1[1]*e2[0];
        if(sqrt(cx*cx+cy*cy+cz*cz) < 1e-12) r.degenerate_tris++;
    }
    /* edge use counts via open-addressing hash of (min,max) */
    uint64_t cap64=16; while(cap64 < (uint64_t)ni*2) cap64<<=1;   /* 64-bit to avoid overflow at huge sizes */
    uint32_t cap=(cap64>0x40000000u)?0x40000000u:(uint32_t)cap64; /* clamp to 1G buckets */
    typedef struct{ uint32_t a,b,count; } E;
    E* tab=calloc(cap,sizeof(E)); const uint32_t EMPTY=0xFFFFFFFFu;
    for(uint32_t i=0;i<cap;i++) tab[i].a=EMPTY;
    for(uint64_t t=0;t<r.triangle_count;t++){
        uint32_t I[3]={idx[t*3],idx[t*3+1],idx[t*3+2]};
        for(int e=0;e<3;e++){
            uint32_t u=I[e], v=I[(e+1)%3]; if(u>v){uint32_t tmp=u;u=v;v=tmp;}
            uint32_t h=(u*73856093u ^ v*19349663u)&(cap-1);
            while(tab[h].a!=EMPTY && !(tab[h].a==u&&tab[h].b==v)) h=(h+1)&(cap-1);
            if(tab[h].a==EMPTY){ tab[h].a=u;tab[h].b=v;tab[h].count=1; } else tab[h].count++;
        }
    }
    for(uint32_t i=0;i<cap;i++) if(tab[i].a!=EMPTY){
        if(tab[i].count==1) r.boundary_edges++; else if(tab[i].count>2) r.nonmanifold_edges++;
    }
    free(tab);
    /* unused verts */
    char* used=calloc(nv?nv:1,1);
    if(!used){ r.ok=false; return r; }   /* count too large for host memory → clean refusal */
    for(uint64_t i=0;i<ni;i++) if(idx[i]<nv) used[idx[i]]=1;
    for(uint64_t i=0;i<nv;i++) if(!used[i]) r.unused_vertices++;
    free(used);
    /* duplicate positions — O(n) via spatial hash (was O(n^2) with a 20k cap;
       now scales to hundreds of millions of verts, bounded by memory not time).
       Count each redundant copy once: a vert is a duplicate if a DIFFERENT vert at
       the same position exists — we tally when the nearest other point is coincident. */
    {
        DbHash H; db_hash_build(&H, P, nv, stride, 1e-6 /* cell ~ dedup tolerance */);
        for(uint64_t i=0;i<nv;i++){
            const float* pi=vpos(P,stride,i);
            double q[3]={pi[0],pi[1],pi[2]};
            if(db_hash_nearest2(&H,q,i) < 1e-14) r.duplicate_positions++;
        }
        /* each coincident pair is counted twice above (i sees j, j sees i);
           report the number of redundant verts = counted/2 rounded is imprecise for
           triples, so instead this is "verts that coincide with another" — halve for
           pairs. Keep the raw 'verts sharing a position' semantic, which is stable. */
        db_hash_free(&H);
    }
    r.closed = (r.boundary_edges==0);
    r.manifold = (r.nonmanifold_edges==0 && r.degenerate_tris==0);
    r.ok = r.manifold;
    return r;
}

/* perpendicular basis for an axis */
static void perp_basis(const float ax[3], double e1[3], double e2[3]){
    double a[3]={ax[0],ax[1],ax[2]};
    double al=sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); if(al<1e-9)al=1; a[0]/=al;a[1]/=al;a[2]/=al;
    double up[3]={0,1,0}; if(fabs(a[1])>0.9){ up[0]=1;up[1]=0;up[2]=0; }
    e1[0]=a[1]*up[2]-a[2]*up[1]; e1[1]=a[2]*up[0]-a[0]*up[2]; e1[2]=a[0]*up[1]-a[1]*up[0];
    double e1l=sqrt(e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2]); if(e1l<1e-9)e1l=1; e1[0]/=e1l;e1[1]/=e1l;e1[2]/=e1l;
    e2[0]=a[1]*e1[2]-a[2]*e1[1]; e2[1]=a[2]*e1[0]-a[0]*e1[2]; e2[2]=a[0]*e1[1]-a[1]*e1[0];
}
DbCrossSection deboog_cross_section(const float* P, uint32_t n, uint32_t stride,
                                    const float ap[3], const float ad[3], double ribbon_thresh){
    DbCrossSection r; memset(&r,0,sizeof r); r.point_count=n; if(stride<3)stride=3;
    double e1[3],e2[3]; perp_basis(ad,e1,e2);
    double a[3]={ad[0],ad[1],ad[2]}; double al=sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); if(al<1e-9)al=1;
    a[0]/=al;a[1]/=al;a[2]/=al;
    double min1=DBL_MAX,max1=-DBL_MAX,min2=DBL_MAX,max2=-DBL_MAX,minA=DBL_MAX,maxA=-DBL_MAX;
    for(uint32_t i=0;i<n;i++){ const float* p=vpos(P,stride,i);
        double d[3]={p[0]-ap[0],p[1]-ap[1],p[2]-ap[2]};
        double p1=d[0]*e1[0]+d[1]*e1[1]+d[2]*e1[2];
        double p2=d[0]*e2[0]+d[1]*e2[1]+d[2]*e2[2];
        double pa=d[0]*a[0]+d[1]*a[1]+d[2]*a[2];
        if(p1<min1)min1=p1; if(p1>max1)max1=p1;
        if(p2<min2)min2=p2; if(p2>max2)max2=p2;
        if(pa<minA){minA=pa;} if(pa>maxA){maxA=pa;}
    }
    r.spread_a = (n?max1-min1:0);
    r.spread_b = (n?max2-min2:0);
    r.length_along_axis = (n?maxA-minA:0);
    double lo=r.spread_a<r.spread_b?r.spread_a:r.spread_b, hi=r.spread_a<r.spread_b?r.spread_b:r.spread_a;
    r.roundness = (hi>1e-9)? lo/hi : 0.0;
    r.is_ribbon = r.roundness < ribbon_thresh;
    r.ok = !r.is_ribbon;
    return r;
}
DbSymmetry deboog_symmetry(const float* P, uint32_t n, uint32_t stride,
                           const float pp[3], const float pn[3], double tol, double require){
    DbSymmetry r; memset(&r,0,sizeof r); r.point_count=n; if(stride<3)stride=3;
    double nrm[3]={pn[0],pn[1],pn[2]}; double nl=sqrt(nrm[0]*nrm[0]+nrm[1]*nrm[1]+nrm[2]*nrm[2]);
    if(nl<1e-9)nl=1; nrm[0]/=nl;nrm[1]/=nl;nrm[2]/=nl;
    double worst=0;
    /* O(n) nearest-neighbor via spatial hash; cell width = tolerance so the 3x3x3
       neighbor search always covers the tolerance radius. (Was O(n^2).) */
    DbHash H; db_hash_build(&H, P, n, stride, tol>1e-9?tol:1e-6);
    for(uint32_t i=0;i<n;i++){ const float* p=vpos(P,stride,i);
        double d=(p[0]-pp[0])*nrm[0]+(p[1]-pp[1])*nrm[1]+(p[2]-pp[2])*nrm[2];
        double mp[3]={p[0]-2*d*nrm[0], p[1]-2*d*nrm[1], p[2]-2*d*nrm[2]};
        double best=sqrt(db_hash_nearest2(&H, mp, UINT32_MAX));  /* don't skip any */
        if(best<=tol) r.matched++;
        if(best>worst)worst=best;
    }
    db_hash_free(&H);
    r.max_error=worst;
    r.symmetry = n? (double)r.matched/n : 1.0;
    r.ok = r.symmetry >= require;
    return r;
}
DbSkin deboog_check_skin(const uint16_t* J, const float* W, uint32_t nv, uint32_t nb, double tol){
    DbSkin r; memset(&r,0,sizeof r); r.vertex_count=nv;
    char* bone_used = nb?calloc(nb,1):NULL;
    for(uint32_t i=0;i<nv;i++){
        double sum=0; int any=0;
        for(int k=0;k<4;k++){ float w=W[i*4+k]; sum+=w;
            if(w>0){ any=1; uint16_t b=J[i*4+k]; if(b>=nb) r.out_of_range++; else if(bone_used) bone_used[b]=1; } }
        if(!any) r.unweighted++;
        else if(fabs(sum-1.0)>tol) r.bad_sum++;
    }
    if(bone_used){ for(uint32_t b=0;b<nb;b++) if(!bone_used[b]) r.dead_bones++; free(bone_used); }
    r.ok = (r.unweighted==0 && r.bad_sum==0 && r.out_of_range==0);
    return r;
}

/* ============================ 4. SPATIAL ============================ */
DbCanonPoint deboog_canon_project(const float vp[16], const float w[3], int fw, int fh, int rot){
    DbCanonPoint r; memset(&r,0,sizeof r);
    float x=vp[0]*w[0]+vp[4]*w[1]+vp[8]*w[2]+vp[12];
    float y=vp[1]*w[0]+vp[5]*w[1]+vp[9]*w[2]+vp[13];
    float wc=vp[3]*w[0]+vp[7]*w[1]+vp[11]*w[2]+vp[15];
    r.rot_deg=((rot%360)+360)%360;
    if(wc<=0.0001f){ r.visible=false; return r; }
    r.visible=true;
    r.ndc_x=x/wc; r.ndc_y=y/wc;
    r.x_off=(int)lround(r.ndc_x*(fw*0.5)); r.y_off=(int)lround(r.ndc_y*(fh*0.5)); /* Y-up */
    return r;
}
void deboog_canon_format(const DbCanonPoint* p, char* buf, size_t n){
    if(!p->visible){ snprintf(buf,n,"OFFSCREEN:%03d",p->rot_deg); return; }
    snprintf(buf,n,"%+04d-%+04d:%03d",p->x_off,p->y_off,p->rot_deg);
}

/* ============================ INVARIANTS ============================ */
#define DB_MAX_INV 128
struct DbInvariants { const char* label[DB_MAX_INV]; DbInvariantFn fn[DB_MAX_INV]; void* user[DB_MAX_INV]; int n; };
DbInvariants* deboog_invariants_create(void){ return calloc(1,sizeof(DbInvariants)); }
void deboog_invariants_destroy(DbInvariants* d){ free(d); }
void deboog_invariant_add(DbInvariants* d, const char* label, DbInvariantFn fn, void* user){
    if(!d||d->n>=DB_MAX_INV)return; d->label[d->n]=label; d->fn[d->n]=fn; d->user[d->n]=user; d->n++;
}
int deboog_invariants_check(DbInvariants* d, const char** first_failed){
    if(first_failed)*first_failed=NULL; int fails=0;
    for(int i=0;i<d->n;i++){ if(!d->fn[i](d->user[i])){ if(fails==0 && first_failed)*first_failed=d->label[i]; fails++; } }
    return fails;
}

/* ============================ REPORTING ============================ */
int deboog_report_numeric(FILE* f, const char* l, DbNumeric r){
    fprintf(f,"[%s] NUMERIC %s: %zu vals, min %.4g max %.4g",
        r.ok?"OK":"FAIL", l?l:"", r.count, r.min, r.max);
    if(!r.ok) fprintf(f,"  NaN=%zu Inf=%zu (first bad @%zu)", r.nan_count, r.inf_count, r.first_bad_index);
    if(r.denormal_count) fprintf(f,"  denormals=%zu", r.denormal_count);
    fprintf(f,"\n"); return r.ok?0:1;
}
int deboog_report_matrix(FILE* f, const char* l, DbMatrix r){
    fprintf(f,"[%s] MATRIX %s: det=%.4g scale(%.3f,%.3f,%.3f)%s%s%s\n",
        r.ok?"OK":"FAIL", l?l:"", r.determinant, r.scale_x,r.scale_y,r.scale_z,
        r.orthonormal?" orthonormal":"", r.left_handed?" LEFT-HANDED(mirrored)":"",
        !r.invertible?" NOT-INVERTIBLE(collapsed)":"");
    return r.ok?0:1;
}
int deboog_report_mesh(FILE* f, const char* l, DbMesh r){
    fprintf(f,"[%s] MESH %s: %llu verts %llu tris  dims(%.3f,%.3f,%.3f)\n",
        r.ok?"OK":"FAIL", l?l:"", (unsigned long long)r.vertex_count,(unsigned long long)r.triangle_count, r.bbox_dim[0],r.bbox_dim[1],r.bbox_dim[2]);
    if(r.degenerate_tris) fprintf(f,"      FAIL %llu degenerate tris\n",(unsigned long long)r.degenerate_tris);
    if(r.nonmanifold_edges) fprintf(f,"      FAIL %llu non-manifold edges\n",(unsigned long long)r.nonmanifold_edges);
    if(r.boundary_edges) fprintf(f,"      warn %llu boundary/open edges (holes) — not closed\n",(unsigned long long)r.boundary_edges);
    if(r.unused_vertices) fprintf(f,"      warn %llu unused vertices\n",(unsigned long long)r.unused_vertices);
    if(r.duplicate_positions) fprintf(f,"      warn %llu duplicate positions\n",(unsigned long long)r.duplicate_positions);
    return r.ok?0:1;
}
int deboog_report_cross_section(FILE* f, const char* l, DbCrossSection r){
    fprintf(f,"[%s] CROSS-SECTION %s: %.4f x %.4f  roundness=%.2f  len=%.3f%s\n",
        r.ok?"OK":"FAIL", l?l:"", r.spread_a, r.spread_b, r.roundness, r.length_along_axis,
        r.is_ribbon?"  ← FLAT RIBBON":"");
    return r.ok?0:1;
}
int deboog_report_skin(FILE* f, const char* l, DbSkin r){
    fprintf(f,"[%s] SKIN %s: %u verts", r.ok?"OK":"FAIL", l?l:"", r.vertex_count);
    if(r.unweighted) fprintf(f,"  FAIL unweighted=%u", r.unweighted);
    if(r.bad_sum) fprintf(f,"  FAIL bad-sum=%u", r.bad_sum);
    if(r.out_of_range) fprintf(f,"  FAIL oob-joint=%u", r.out_of_range);
    if(r.dead_bones) fprintf(f,"  warn dead-bones=%u", r.dead_bones);
    fprintf(f,"\n"); return r.ok?0:1;
}
