#include "cc/ccmodel.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

/* ── zstd for compression ─────────────────────────────────────────────── */
#ifdef CC_HAS_ZSTD
#  include <zstd.h>
#else
/* Fallback: no compression, store raw */
static size_t ZSTD_compress(void* dst, size_t dstCap, const void* src, size_t srcSize, int lvl) {
    (void)lvl;
    if (dstCap < srcSize) return 0;
    memcpy(dst, src, srcSize);
    return srcSize;
}
static size_t ZSTD_decompress(void* dst, size_t dstCap, const void* src, size_t srcSize) {
    (void)srcSize;
    memcpy(dst, src, dstCap);
    return dstCap;
}
static int ZSTD_isError(size_t r) { (void)r; return 0; }
/* Upper bound on the compressed size for a given input. The real zstd returns a
   worst-case bound; the raw-store fallback never grows the data, so the input size
   itself is a safe bound. Without this shim the Windows/no-zstd build fails to link
   (every other ZSTD_* here has a fallback, but this one was missing — it was the
   sole blocker for cross-compiling any game to Windows). */
static size_t ZSTD_compressBound(size_t srcSize) { return srcSize; }
#endif

/* ── CRC32 ────────────────────────────────────────────────────────────── */
static uint32_t crc32_table[256];
static bool     crc32_ready = false;
static void crc32_init(void) {
    if (crc32_ready) return;
    for (uint32_t i=0;i<256;i++) {
        uint32_t c=i;
        for (int j=0;j<8;j++) c=(c&1)?(0xEDB88320^(c>>1)):(c>>1);
        crc32_table[i]=c;
    }
    crc32_ready=true;
}
static uint32_t crc32(const void* data, size_t len) {
    crc32_init();
    uint32_t c=0xFFFFFFFF;
    const uint8_t* p=data;
    for (size_t i=0;i<len;i++) c=crc32_table[(c^p[i])&0xFF]^(c>>8);
    return c^0xFFFFFFFF;
}

/* ══════════════════════════════════════════════════════════════════════
   MODEL LIFECYCLE
   ══════════════════════════════════════════════════════════════════════ */

CCModel* ccm_model_new(const char* name) {
    CCModel* m = calloc(1, sizeof(CCModel));
    strncpy(m->name, name ? name : "model", sizeof(m->name)-1);
    return m;
}

void ccm_model_free(CCModel* m) {
    if (!m) return;
    free(m->geom.vertices); free(m->geom.indices);
    free(m->geom.submesh_start); free(m->geom.submesh_count_arr);
    free(m->geom.submesh_material);
    free(m->skel.bones);
    free(m->skin.weights);
    for (uint32_t i=0;i<m->bshp.shape_count;i++) {
        free(m->bshp.shapes[i].deltas);
    }
    free(m->bshp.shapes);
    free(m->matl.slots);
    for (uint32_t i=0;i<m->anim_count;i++) {
        for (uint32_t t=0;t<m->anims[i].track_count;t++)
            free(m->anims[i].tracks[t].keys);
        free(m->anims[i].tracks);
    }
    free(m->anims);
    free(m);
}

/* ══════════════════════════════════════════════════════════════════════
   GEOMETRY
   ══════════════════════════════════════════════════════════════════════ */

void ccm_set_geometry(CCModel* m, const CCMVertex* verts, uint32_t nv,
                       const uint32_t* idx, uint32_t ni) {
    free(m->geom.vertices); free(m->geom.indices);
    m->geom.vertex_count = nv;
    m->geom.index_count  = ni;
    m->geom.vertices = malloc(nv * sizeof(CCMVertex));
    m->geom.indices  = malloc(ni * sizeof(uint32_t));
    memcpy(m->geom.vertices, verts, nv * sizeof(CCMVertex));
    memcpy(m->geom.indices,  idx,   ni * sizeof(uint32_t));
}

void ccm_add_submesh(CCModel* m, uint32_t start, uint32_t count, uint32_t mat) {
    uint32_t n = m->geom.submesh_count;
    m->geom.submesh_count++;
    m->geom.submesh_start    = realloc(m->geom.submesh_start,    m->geom.submesh_count*4);
    m->geom.submesh_count_arr= realloc(m->geom.submesh_count_arr,m->geom.submesh_count*4);
    m->geom.submesh_material = realloc(m->geom.submesh_material, m->geom.submesh_count*4);
    m->geom.submesh_start[n]    = start;
    m->geom.submesh_count_arr[n]= count;
    m->geom.submesh_material[n] = mat;
}

/* ── Normal computation ───────────────────────────────────────────────── */
void ccm_compute_normals(CCModel* m) {
    uint32_t nv=m->geom.vertex_count, ni=m->geom.index_count;
    CCMVertex* v=m->geom.vertices; uint32_t* idx=m->geom.indices;
    float* acc=calloc(nv*3, sizeof(float));
    for (uint32_t i=0;i<ni;i+=3) {
        uint32_t ia=idx[i],ib=idx[i+1],ic=idx[i+2];
        float ax=v[ib].pos[0]-v[ia].pos[0], ay=v[ib].pos[1]-v[ia].pos[1], az=v[ib].pos[2]-v[ia].pos[2];
        float bx=v[ic].pos[0]-v[ia].pos[0], by=v[ic].pos[1]-v[ia].pos[1], bz=v[ic].pos[2]-v[ia].pos[2];
        float nx=ay*bz-az*by, ny=az*bx-ax*bz, nz=ax*by-ay*bx;
        for (int j=0;j<3;j++) {
            uint32_t k=idx[i+j];
            acc[k*3]+=nx; acc[k*3+1]+=ny; acc[k*3+2]+=nz;
        }
    }
    for (uint32_t i=0;i<nv;i++) {
        float l=sqrtf(acc[i*3]*acc[i*3]+acc[i*3+1]*acc[i*3+1]+acc[i*3+2]*acc[i*3+2]);
        if (l>1e-6f) { l=1/l; v[i].normal[0]=acc[i*3]*l; v[i].normal[1]=acc[i*3+1]*l; v[i].normal[2]=acc[i*3+2]*l; }
    }
    free(acc);
}

/* ── Tangent computation (Mikktspace-like) ───────────────────────────── */
void ccm_compute_tangents(CCModel* m) {
    uint32_t nv=m->geom.vertex_count, ni=m->geom.index_count;
    CCMVertex* v=m->geom.vertices; uint32_t* idx=m->geom.indices;
    float* tan1=calloc(nv*3,4); float* tan2=calloc(nv*3,4);
    for (uint32_t i=0;i<ni;i+=3) {
        uint32_t i0=idx[i],i1=idx[i+1],i2=idx[i+2];
        float dx1=v[i1].pos[0]-v[i0].pos[0],dy1=v[i1].pos[1]-v[i0].pos[1],dz1=v[i1].pos[2]-v[i0].pos[2];
        float dx2=v[i2].pos[0]-v[i0].pos[0],dy2=v[i2].pos[1]-v[i0].pos[1],dz2=v[i2].pos[2]-v[i0].pos[2];
        float du1=v[i1].uv[0]-v[i0].uv[0],dv1=v[i1].uv[1]-v[i0].uv[1];
        float du2=v[i2].uv[0]-v[i0].uv[0],dv2=v[i2].uv[1]-v[i0].uv[1];
        float r=1.0f/(du1*dv2-du2*dv1+1e-8f);
        float sx=(dv2*dx1-dv1*dx2)*r, sy=(dv2*dy1-dv1*dy2)*r, sz=(dv2*dz1-dv1*dz2)*r;
        float tx=(du1*dx2-du2*dx1)*r, ty=(du1*dy2-du2*dy1)*r, tz=(du1*dz2-du2*dz1)*r;
        for (int j=0;j<3;j++){uint32_t k=idx[i+j];tan1[k*3]+=sx;tan1[k*3+1]+=sy;tan1[k*3+2]+=sz;tan2[k*3]+=tx;tan2[k*3+1]+=ty;tan2[k*3+2]+=tz;}
    }
    for (uint32_t i=0;i<nv;i++) {
        float nx=v[i].normal[0],ny=v[i].normal[1],nz=v[i].normal[2];
        float tx=tan1[i*3],ty=tan1[i*3+1],tz=tan1[i*3+2];
        float dot=nx*tx+ny*ty+nz*tz;
        float ox=tx-nx*dot,oy=ty-ny*dot,oz=tz-nz*dot;
        float l=sqrtf(ox*ox+oy*oy+oz*oz); if(l>1e-6f){l=1/l;}
        v[i].tangent[0]=ox*l; v[i].tangent[1]=oy*l; v[i].tangent[2]=oz*l;
        float cx=ny*tz-nz*ty, cy=nz*tx-nx*tz, cz=nx*ty-ny*tx;
        v[i].tangent[3]=(cx*tan2[i*3]+cy*tan2[i*3+1]+cz*tan2[i*3+2])<0?-1.0f:1.0f;
    }
    free(tan1); free(tan2);
}

void ccm_weld_vertices(CCModel* m, float threshold) {
    /* Simple O(n²) weld for small meshes */
    uint32_t nv=m->geom.vertex_count;
    uint32_t* remap=malloc(nv*4);
    uint32_t new_count=0;
    CCMVertex* new_verts=malloc(nv*sizeof(CCMVertex));
    float t2=threshold*threshold;
    for (uint32_t i=0;i<nv;i++) {
        remap[i]=new_count;
        bool found=false;
        for (uint32_t j=0;j<new_count;j++) {
            float dx=m->geom.vertices[i].pos[0]-new_verts[j].pos[0];
            float dy=m->geom.vertices[i].pos[1]-new_verts[j].pos[1];
            float dz=m->geom.vertices[i].pos[2]-new_verts[j].pos[2];
            if (dx*dx+dy*dy+dz*dz<t2){remap[i]=j;found=true;break;}
        }
        if (!found) new_verts[new_count++]=m->geom.vertices[i];
    }
    for (uint32_t i=0;i<m->geom.index_count;i++) m->geom.indices[i]=remap[m->geom.indices[i]];
    free(m->geom.vertices); m->geom.vertices=new_verts; m->geom.vertex_count=new_count;
    free(remap);
}

void ccm_center_pivot(CCModel* m) {
    uint32_t nv=m->geom.vertex_count; if(!nv)return;
    float cx=0,cy=0,cz=0;
    for (uint32_t i=0;i<nv;i++){cx+=m->geom.vertices[i].pos[0];cy+=m->geom.vertices[i].pos[1];cz+=m->geom.vertices[i].pos[2];}
    cx/=nv;cy/=nv;cz/=nv;
    for (uint32_t i=0;i<nv;i++){m->geom.vertices[i].pos[0]-=cx;m->geom.vertices[i].pos[1]-=cy;m->geom.vertices[i].pos[2]-=cz;}
}

void ccm_flip_normals(CCModel* m) {
    for (uint32_t i=0;i<m->geom.vertex_count;i++){m->geom.vertices[i].normal[0]*=-1;m->geom.vertices[i].normal[1]*=-1;m->geom.vertices[i].normal[2]*=-1;}
    /* Also reverse winding */
    for (uint32_t i=0;i<m->geom.index_count;i+=3){uint32_t t=m->geom.indices[i+1];m->geom.indices[i+1]=m->geom.indices[i+2];m->geom.indices[i+2]=t;}
}

void ccm_scale_geometry(CCModel* m, float sx, float sy, float sz) {
    for (uint32_t i=0;i<m->geom.vertex_count;i++){m->geom.vertices[i].pos[0]*=sx;m->geom.vertices[i].pos[1]*=sy;m->geom.vertices[i].pos[2]*=sz;}
}
void ccm_translate_geometry(CCModel* m, float tx, float ty, float tz) {
    for (uint32_t i=0;i<m->geom.vertex_count;i++){m->geom.vertices[i].pos[0]+=tx;m->geom.vertices[i].pos[1]+=ty;m->geom.vertices[i].pos[2]+=tz;}
}

/* ── Subdivision (Loop-ish) ──────────────────────────────────────────── */
void ccm_subdivide(CCModel* m, uint32_t iterations) {
    for (uint32_t iter=0;iter<iterations;iter++) {
        uint32_t old_nv=m->geom.vertex_count, old_ni=m->geom.index_count;
        uint32_t new_ni=old_ni*4, new_nv=old_nv+old_ni; /* one edge midpoint per edge, rough */
        CCMVertex* new_v=malloc((old_nv+old_ni)*sizeof(CCMVertex));
        uint32_t* new_i=malloc(new_ni*sizeof(uint32_t));
        memcpy(new_v, m->geom.vertices, old_nv*sizeof(CCMVertex));
        uint32_t ep=old_nv;
        for (uint32_t i=0;i<old_ni;i+=3) {
            uint32_t a=m->geom.indices[i],b=m->geom.indices[i+1],c=m->geom.indices[i+2];
            /* Create midpoint vertices */
            CCMVertex* va=&m->geom.vertices[a], *vb=&m->geom.vertices[b], *vc=&m->geom.vertices[c];
            CCMVertex mab,mbc,mca;
            for (int k=0;k<3;k++){mab.pos[k]=(va->pos[k]+vb->pos[k])*0.5f;mbc.pos[k]=(vb->pos[k]+vc->pos[k])*0.5f;mca.pos[k]=(vc->pos[k]+va->pos[k])*0.5f;}
            for (int k=0;k<2;k++){mab.uv[k]=(va->uv[k]+vb->uv[k])*0.5f;mbc.uv[k]=(vb->uv[k]+vc->uv[k])*0.5f;mca.uv[k]=(vc->uv[k]+va->uv[k])*0.5f;}
            new_v[ep]=mab; new_v[ep+1]=mbc; new_v[ep+2]=mca;
            /* 4 triangles */
            uint32_t mi=i*4;
            new_i[mi+0]=a;    new_i[mi+1]=ep;   new_i[mi+2]=ep+2;
            new_i[mi+3]=ep;   new_i[mi+4]=b;    new_i[mi+5]=ep+1;
            new_i[mi+6]=ep+2; new_i[mi+7]=ep+1; new_i[mi+8]=c;
            new_i[mi+9]=ep;   new_i[mi+10]=ep+1;new_i[mi+11]=ep+2;
            ep+=3;
        }
        free(m->geom.vertices); free(m->geom.indices);
        m->geom.vertices=new_v; m->geom.vertex_count=ep;
        m->geom.indices=new_i;  m->geom.index_count=new_ni;
    }
    ccm_compute_normals(m);
    ccm_compute_tangents(m);
}

/* ══════════════════════════════════════════════════════════════════════
   SKELETON
   ══════════════════════════════════════════════════════════════════════ */

uint16_t ccm_add_bone(CCModel* m, const char* name, uint16_t parent,
                       const float pos[3], const float rot[4], const float scale[3]) {
    uint16_t idx=(uint16_t)m->skel.bone_count++;
    m->skel.bones=realloc(m->skel.bones, m->skel.bone_count*sizeof(CCMBone));
    CCMBone* b=&m->skel.bones[idx];
    memset(b,0,sizeof(*b));
    strncpy(b->name, name, 63);
    b->parent=parent;
    if (pos)   memcpy(b->bind_pos,   pos,   12);
    if (rot)   memcpy(b->bind_rot,   rot,   16); else {b->bind_rot[3]=1;}
    if (scale) memcpy(b->bind_scale, scale, 12); else {b->bind_scale[0]=b->bind_scale[1]=b->bind_scale[2]=1;}
    m->has_skel=true;
    return idx;
}

/* Compute inv_bind_mat for each bone from local bind poses */
void ccm_compute_inv_bind_poses(CCModel* m) {
    /* Build global bind matrices via depth-first traversal */
    uint16_t n=m->skel.bone_count;
    float* global=calloc(n*16,4);
    /* Identity */
    for (int i=0;i<n;i++){global[i*16]=global[i*16+5]=global[i*16+10]=global[i*16+15]=1;}
    /* Compute global = parent_global * local */
    for (uint16_t i=0;i<n;i++) {
        CCMBone* b=&m->skel.bones[i];
        /* Build local TRS matrix */
        float lm[16]={0};
        /* rotation */
        float qx=b->bind_rot[0],qy=b->bind_rot[1],qz=b->bind_rot[2],qw=b->bind_rot[3];
        lm[0]=1-2*(qy*qy+qz*qz); lm[1]=2*(qx*qy+qz*qw); lm[2]=2*(qx*qz-qy*qw);
        lm[4]=2*(qx*qy-qz*qw);   lm[5]=1-2*(qx*qx+qz*qz);lm[6]=2*(qy*qz+qx*qw);
        lm[8]=2*(qx*qz+qy*qw);   lm[9]=2*(qy*qz-qx*qw); lm[10]=1-2*(qx*qx+qy*qy);
        /* scale */
        lm[0]*=b->bind_scale[0]; lm[1]*=b->bind_scale[0]; lm[2]*=b->bind_scale[0];
        lm[4]*=b->bind_scale[1]; lm[5]*=b->bind_scale[1]; lm[6]*=b->bind_scale[1];
        lm[8]*=b->bind_scale[2]; lm[9]*=b->bind_scale[2]; lm[10]*=b->bind_scale[2];
        /* translation */
        lm[12]=b->bind_pos[0]; lm[13]=b->bind_pos[1]; lm[14]=b->bind_pos[2]; lm[15]=1;
        /* multiply with parent */
        if (b->parent!=CCM_BONE_NO_PARENT && b->parent<i) {
            float* pg=global+b->parent*16;
            float* og=global+i*16;
            /* og = pg * lm (column-major) */
            for (int r=0;r<4;r++) for (int c=0;c<4;c++) {
                og[c*4+r]=0;
                for (int k=0;k<4;k++) og[c*4+r]+=pg[k*4+r]*lm[c*4+k];
            }
        } else {
            memcpy(global+i*16, lm, 64);
        }
        /* Compute inverse (store in inv_bind_mat) — 4×4 inverse */
        float* g=global+i*16;
        float* inv=b->inv_bind_mat;
        /* Cofactor inverse for 4×4 */
        #define M(r,c) g[c*4+r]
        #define MINV(r,c) inv[c*4+r]
        float s0=M(0,0)*M(1,1)-M(1,0)*M(0,1); float s1=M(0,0)*M(1,2)-M(1,0)*M(0,2);
        float s2=M(0,0)*M(1,3)-M(1,0)*M(0,3); float s3=M(0,1)*M(1,2)-M(1,1)*M(0,2);
        float s4=M(0,1)*M(1,3)-M(1,1)*M(0,3); float s5=M(0,2)*M(1,3)-M(1,2)*M(0,3);
        float c0=M(2,0)*M(3,1)-M(3,0)*M(2,1); float c1=M(2,0)*M(3,2)-M(3,0)*M(2,2);
        float c2=M(2,0)*M(3,3)-M(3,0)*M(2,3); float c3=M(2,1)*M(3,2)-M(3,1)*M(2,2);
        float c4=M(2,1)*M(3,3)-M(3,1)*M(2,3); float c5=M(2,2)*M(3,3)-M(3,2)*M(2,3);
        float det=s0*c5-s1*c4+s2*c3+s3*c2-s4*c1+s5*c0;
        float idet=det!=0?1.0f/det:0;
        MINV(0,0)=( M(1,1)*c5-M(1,2)*c4+M(1,3)*c3)*idet;
        MINV(0,1)=(-M(0,1)*c5+M(0,2)*c4-M(0,3)*c3)*idet;
        MINV(0,2)=( M(3,1)*s5-M(3,2)*s4+M(3,3)*s3)*idet;
        MINV(0,3)=(-M(2,1)*s5+M(2,2)*s4-M(2,3)*s3)*idet;
        MINV(1,0)=(-M(1,0)*c5+M(1,2)*c2-M(1,3)*c1)*idet;
        MINV(1,1)=( M(0,0)*c5-M(0,2)*c2+M(0,3)*c1)*idet;
        MINV(1,2)=(-M(3,0)*s5+M(3,2)*s2-M(3,3)*s1)*idet;
        MINV(1,3)=( M(2,0)*s5-M(2,2)*s2+M(2,3)*s1)*idet;
        MINV(2,0)=( M(1,0)*c4-M(1,1)*c2+M(1,3)*c0)*idet;
        MINV(2,1)=(-M(0,0)*c4+M(0,1)*c2-M(0,3)*c0)*idet;
        MINV(2,2)=( M(3,0)*s4-M(3,1)*s2+M(3,3)*s0)*idet;
        MINV(2,3)=(-M(2,0)*s4+M(2,1)*s2-M(2,3)*s0)*idet;
        MINV(3,0)=(-M(1,0)*c3+M(1,1)*c1-M(1,2)*c0)*idet;
        MINV(3,1)=( M(0,0)*c3-M(0,1)*c1+M(0,2)*c0)*idet;
        MINV(3,2)=(-M(3,0)*s3+M(3,1)*s1-M(3,2)*s0)*idet;
        MINV(3,3)=( M(2,0)*s3-M(2,1)*s1+M(2,2)*s0)*idet;
        #undef M
        #undef MINV
    }
    free(global);
}

/* ══════════════════════════════════════════════════════════════════════
   ANIMATION
   ══════════════════════════════════════════════════════════════════════ */

CCMAnimChunk* ccm_add_anim(CCModel* m, const char* name, float duration, bool looping) {
    m->anims=realloc(m->anims,(m->anim_count+1)*sizeof(CCMAnimChunk));
    CCMAnimChunk* a=&m->anims[m->anim_count++];
    memset(a,0,sizeof(*a));
    strncpy(a->name, name, 63);
    a->duration=duration; a->looping=looping; a->fps=30.0f;
    return a;
}

CCMTrack* ccm_anim_add_track(CCMAnimChunk* anim, uint16_t bone,
                               CCMTrackTarget target, CCMInterpType interp) {
    anim->tracks=realloc(anim->tracks,(anim->track_count+1)*sizeof(CCMTrack));
    CCMTrack* t=&anim->tracks[anim->track_count++];
    memset(t,0,sizeof(*t));
    t->bone_index=bone; t->target=target; t->interp=interp;
    return t;
}

void ccm_track_add_key(CCMTrack* track, float time, float value,
                        float in_tan, float out_tan) {
    track->keys=realloc(track->keys,(track->key_count+1)*sizeof(CCMKeyframe));
    CCMKeyframe* k=&track->keys[track->key_count++];
    k->time=time; k->value=value; k->in_tan=in_tan; k->out_tan=out_tan;
}

CCMAnimChunk* ccm_find_anim(CCModel* m, const char* name) {
    for (uint32_t i=0;i<m->anim_count;i++)
        if (strcmp(m->anims[i].name,name)==0) return &m->anims[i];
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
   BLEND SHAPES
   ══════════════════════════════════════════════════════════════════════ */

CCMBlendShape* ccm_add_blend_shape(CCModel* m, const char* name) {
    m->bshp.shapes=realloc(m->bshp.shapes,(m->bshp.shape_count+1)*sizeof(CCMBlendShape));
    CCMBlendShape* bs=&m->bshp.shapes[m->bshp.shape_count++];
    memset(bs,0,sizeof(*bs));
    strncpy(bs->name,name,63);
    m->has_bshp=true;
    return bs;
}

void ccm_bshp_add_delta(CCMBlendShape* bs, uint32_t vi,
                         const float dp[3], const float dn[3]) {
    bs->deltas=realloc(bs->deltas,(bs->delta_count+1)*sizeof(CCMBlendDelta));
    CCMBlendDelta* d=&bs->deltas[bs->delta_count++];
    d->vertex_index=vi;
    memcpy(d->delta_pos,dp,12); memcpy(d->delta_normal,dn,12);
}

/* ══════════════════════════════════════════════════════════════════════
   MATERIALS
   ══════════════════════════════════════════════════════════════════════ */

uint32_t ccm_add_material_slot(CCModel* m, const char* name) {
    uint32_t idx=m->matl.slot_count++;
    m->matl.slots=realloc(m->matl.slots,m->matl.slot_count*sizeof(CCMMaterialSlot));
    CCMMaterialSlot* s=&m->matl.slots[idx];
    memset(s,0,sizeof(*s));
    strncpy(s->name,name,63);
    s->base_color[0]=s->base_color[1]=s->base_color[2]=s->base_color[3]=1.0f;
    s->roughness=0.5f;
    return idx;
}

CCMMaterialSlot* ccm_get_material_slot(CCModel* m, uint32_t slot) {
    return (slot<m->matl.slot_count)?&m->matl.slots[slot]:NULL;
}

/* ══════════════════════════════════════════════════════════════════════
   SAVE / LOAD
   ══════════════════════════════════════════════════════════════════════ */

static void write_u32(FILE* f, uint32_t v) { fwrite(&v,4,1,f); }
static void write_u16(FILE* f, uint16_t v) { fwrite(&v,2,1,f); }
static void write_f32(FILE* f, float v)    { fwrite(&v,4,1,f); }
static void write_str(FILE* f, const char* s, size_t maxlen) { fwrite(s,1,maxlen,f); }

bool ccm_save(const CCModel* m, const char* path) {
    FILE* f=fopen(path,"w+b");
    if(!f){fprintf(stderr,"ccm_save: cannot open %s\n",path);return false;}

    /* Write magic + version */
    fwrite(CCM_MAGIC,1,CCM_MAGIC_LEN,f);
    write_u16(f,CCM_VERSION);

    /* Count chunks */
    uint16_t nchunks=1+1; /* META + GEOM */
    if (m->has_skel) nchunks+=2; /* SKEL + SKIN */
    nchunks+=m->anim_count; /* ANIM × N */
    if (m->has_bshp) nchunks++;
    if (m->matl.slot_count) nchunks++;
    write_u16(f,nchunks);

    /* Flags, CRC placeholder, total size placeholder */
    uint32_t flags=0; write_u32(f,flags);
    long crc_pos=ftell(f); write_u32(f,0); /* CRC placeholder */
    long size_pos=ftell(f); write_u32(f,0); /* size placeholder */
    uint8_t reserved[40]={0}; fwrite(reserved,1,40,f);
    /* End of header: 64 bytes */

    /* Chunk table — we'll fill offsets as we write */
    long table_pos=ftell(f);
    CCMChunkEntry* entries=calloc(nchunks,sizeof(CCMChunkEntry));
    fwrite(entries,sizeof(CCMChunkEntry),nchunks,f);

    /* Helper to compress and write a chunk */
    int chunk_idx=0;
    #define WRITE_CHUNK(TYPE_ID, NAME_STR, raw_data, raw_size) do { \
        size_t bufsz=ZSTD_compressBound(raw_size); \
        void* cbuf=malloc(bufsz); \
        size_t csz=ZSTD_compress(cbuf,bufsz,raw_data,raw_size,9); \
        if(ZSTD_isError(csz)){csz=raw_size;memcpy(cbuf,raw_data,raw_size);} \
        entries[chunk_idx].type=TYPE_ID; \
        entries[chunk_idx].id=chunk_idx; \
        entries[chunk_idx].offset=(uint64_t)ftell(f); \
        entries[chunk_idx].size_compressed=(uint32_t)csz; \
        entries[chunk_idx].size_raw=(uint32_t)raw_size; \
        strncpy((char*)entries[chunk_idx].name,NAME_STR,31); \
        fwrite(cbuf,1,csz,f); free(cbuf); chunk_idx++; \
    } while(0)

    /* META chunk */
    char meta[256]={0}; snprintf(meta,256,"%s",m->name);
    WRITE_CHUNK(CCM_CHUNK_META,"meta",meta,strlen(meta)+1);

    /* GEOM chunk — pack: vert_count, idx_count, vertices, indices, submesh data */
    {
        size_t vsz=m->geom.vertex_count*sizeof(CCMVertex);
        size_t isz=m->geom.index_count*4;
        size_t rsz=4+4+4+vsz+isz+m->geom.submesh_count*12;
        uint8_t* buf=malloc(rsz); size_t off=0;
        memcpy(buf+off,&m->geom.vertex_count,4);off+=4;
        memcpy(buf+off,&m->geom.index_count,4);off+=4;
        memcpy(buf+off,&m->geom.submesh_count,4);off+=4;
        memcpy(buf+off,m->geom.vertices,vsz);off+=vsz;
        memcpy(buf+off,m->geom.indices,isz);off+=isz;
        for (uint32_t i=0;i<m->geom.submesh_count;i++){
            memcpy(buf+off,&m->geom.submesh_start[i],4);off+=4;
            memcpy(buf+off,&m->geom.submesh_count_arr[i],4);off+=4;
            memcpy(buf+off,&m->geom.submesh_material[i],4);off+=4;
        }
        WRITE_CHUNK(CCM_CHUNK_GEOM,"geometry",buf,off);
        free(buf);
    }

    /* SKEL chunk */
    if (m->has_skel) {
        size_t sz=2+m->skel.bone_count*sizeof(CCMBone);
        uint8_t* buf=malloc(sz);
        memcpy(buf,&m->skel.bone_count,2);
        memcpy(buf+2,m->skel.bones,m->skel.bone_count*sizeof(CCMBone));
        WRITE_CHUNK(CCM_CHUNK_SKEL,"skeleton",buf,sz); free(buf);
    }
    /* SKIN chunk */
    if (m->has_skin && m->skin.vertex_count) {
        size_t sz=4+m->skin.vertex_count*sizeof(CCMSkinVertex);
        uint8_t* buf=malloc(sz);
        memcpy(buf,&m->skin.vertex_count,4);
        memcpy(buf+4,m->skin.weights,m->skin.vertex_count*sizeof(CCMSkinVertex));
        WRITE_CHUNK(CCM_CHUNK_SKIN,"skin",buf,sz); free(buf);
    }

    /* ANIM chunks */
    for (uint32_t ai=0;ai<m->anim_count;ai++) {
        CCMAnimChunk* a=&m->anims[ai];
        /* Serialize: name(64)+dur(4)+fps(4)+loop(1)+track_count(4)+tracks... */
        size_t sz=64+4+4+1+4;
        for (uint32_t ti=0;ti<a->track_count;ti++)
            sz+=2+2+1+1+4+a->tracks[ti].key_count*sizeof(CCMKeyframe);
        uint8_t* buf=malloc(sz); size_t off=0;
        memcpy(buf+off,a->name,64);off+=64;
        memcpy(buf+off,&a->duration,4);off+=4;
        memcpy(buf+off,&a->fps,4);off+=4;
        buf[off++]=(uint8_t)a->looping;
        memcpy(buf+off,&a->track_count,4);off+=4;
        for (uint32_t ti=0;ti<a->track_count;ti++) {
            CCMTrack* tr=&a->tracks[ti];
            memcpy(buf+off,&tr->bone_index,2);off+=2;
            uint16_t bshp=tr->bshp_index; memcpy(buf+off,&bshp,2);off+=2;
            buf[off++]=(uint8_t)tr->target;
            buf[off++]=(uint8_t)tr->interp;
            memcpy(buf+off,&tr->key_count,4);off+=4;
            memcpy(buf+off,tr->keys,tr->key_count*sizeof(CCMKeyframe));
            off+=tr->key_count*sizeof(CCMKeyframe);
        }
        WRITE_CHUNK(CCM_CHUNK_ANIM,a->name,buf,off); free(buf);
    }

    /* BSHP chunk */
    if (m->has_bshp && m->bshp.shape_count) {
        size_t sz=4;
        for (uint32_t i=0;i<m->bshp.shape_count;i++) sz+=64+4+m->bshp.shapes[i].delta_count*sizeof(CCMBlendDelta);
        uint8_t* buf=malloc(sz); size_t off=0;
        memcpy(buf+off,&m->bshp.shape_count,4);off+=4;
        for (uint32_t i=0;i<m->bshp.shape_count;i++) {
            CCMBlendShape* bs=&m->bshp.shapes[i];
            memcpy(buf+off,bs->name,64);off+=64;
            memcpy(buf+off,&bs->delta_count,4);off+=4;
            memcpy(buf+off,bs->deltas,bs->delta_count*sizeof(CCMBlendDelta));
            off+=bs->delta_count*sizeof(CCMBlendDelta);
        }
        WRITE_CHUNK(CCM_CHUNK_BSHP,"blendshapes",buf,sz); free(buf);
    }

    /* MATL chunk */
    if (m->matl.slot_count) {
        size_t sz=4+m->matl.slot_count*sizeof(CCMMaterialSlot);
        uint8_t* buf=malloc(sz);
        memcpy(buf,&m->matl.slot_count,4);
        memcpy(buf+4,m->matl.slots,m->matl.slot_count*sizeof(CCMMaterialSlot));
        WRITE_CHUNK(CCM_CHUNK_MATL,"materials",buf,sz); free(buf);
    }
    #undef WRITE_CHUNK

    /* Rewrite chunk table with correct offsets */
    uint32_t total_size=(uint32_t)ftell(f);
    fseek(f,table_pos,SEEK_SET);
    fwrite(entries,sizeof(CCMChunkEntry),nchunks,f);

    /* Compute a real CRC32 over the file body (everything after the 64-byte
       header). Re-read the body from disk, hash it, then patch the header's
       CRC + size fields. */
    fflush(f);
    fseek(f,64,SEEK_SET);
    uint32_t body_len = total_size>64 ? total_size-64 : 0;
    uint32_t crc=0;
    if(body_len){
        uint8_t* body=malloc(body_len);
        size_t got=fread(body,1,body_len,f);
        crc=crc32(body,got);
        free(body);
    }
    fseek(f,crc_pos,SEEK_SET);  fwrite(&crc,4,1,f);         /* real CRC32 */
    fseek(f,size_pos,SEEK_SET); fwrite(&total_size,4,1,f);  /* total size  */

    free(entries);
    fclose(f);
    fprintf(stderr,"[ccmodel] Saved %s (%u bytes, crc=%08x, %u bones, %u anims)\n",
            path, total_size, crc, m->skel.bone_count, m->anim_count);
    return true;
}

CCModel* ccm_load(const char* path) {
    FILE* f=fopen(path,"rb");
    if(!f){fprintf(stderr,"ccm_load: cannot open %s\n",path);return NULL;}
    uint8_t magic[8]; fread(magic,1,8,f);
    if(memcmp(magic,CCM_MAGIC,8)!=0){fprintf(stderr,"ccm_load: bad magic\n");fclose(f);return NULL;}
    uint16_t ver; fread(&ver,2,1,f);
    uint16_t nchunks; fread(&nchunks,2,1,f);
    uint32_t flags; fread(&flags,4,1,f);
    uint32_t stored_crc; fread(&stored_crc,4,1,f);
    uint32_t stored_size; fread(&stored_size,4,1,f);
    fseek(f,40,SEEK_CUR); /* skip reserved[40]; header is 64 bytes total */
    /* verify CRC over the file body (bytes 64..end) */
    if(stored_crc!=0){
        long cur=ftell(f); fseek(f,0,SEEK_END); long fsz=ftell(f);
        uint32_t body_len = fsz>64 ? (uint32_t)(fsz-64) : 0;
        if(body_len){ fseek(f,64,SEEK_SET); uint8_t* body=malloc(body_len);
            size_t got=fread(body,1,body_len,f); uint32_t c=crc32(body,got); free(body);
            if(c!=stored_crc) fprintf(stderr,"ccm_load: WARNING crc mismatch (file %08x, computed %08x) — file may be corrupt\n",stored_crc,c);
        }
        fseek(f,cur,SEEK_SET);
    }
    (void)stored_size;
    CCMChunkEntry* entries=malloc(nchunks*sizeof(CCMChunkEntry));
    fread(entries,sizeof(CCMChunkEntry),nchunks,f);

    CCModel* m=ccm_model_new("loaded");
    for (uint16_t ci=0;ci<nchunks;ci++) {
        CCMChunkEntry* e=&entries[ci];
        fseek(f,(long)e->offset,SEEK_SET);
        uint8_t* cbuf=malloc(e->size_compressed);
        fread(cbuf,1,e->size_compressed,f);
        uint8_t* raw=malloc(e->size_raw);
        if (e->size_compressed==e->size_raw) memcpy(raw,cbuf,e->size_raw);
        else ZSTD_decompress(raw,e->size_raw,cbuf,e->size_compressed);
        free(cbuf);

        size_t off=0;
        switch(e->type) {
            case CCM_CHUNK_META:
                strncpy(m->name,(char*)raw,127); break;
            case CCM_CHUNK_GEOM: {
                uint32_t nv,ni,ns;
                memcpy(&nv,raw+off,4);off+=4;
                memcpy(&ni,raw+off,4);off+=4;
                memcpy(&ns,raw+off,4);off+=4;
                m->geom.vertex_count=nv; m->geom.index_count=ni; m->geom.submesh_count=ns;
                m->geom.vertices=malloc(nv*sizeof(CCMVertex)); memcpy(m->geom.vertices,raw+off,nv*sizeof(CCMVertex));off+=nv*sizeof(CCMVertex);
                m->geom.indices=malloc(ni*4); memcpy(m->geom.indices,raw+off,ni*4);off+=ni*4;
                m->geom.submesh_start=malloc(ns*4);m->geom.submesh_count_arr=malloc(ns*4);m->geom.submesh_material=malloc(ns*4);
                for (uint32_t i=0;i<ns;i++){memcpy(m->geom.submesh_start+i,raw+off,4);off+=4;memcpy(m->geom.submesh_count_arr+i,raw+off,4);off+=4;memcpy(m->geom.submesh_material+i,raw+off,4);off+=4;}
                break;
            }
            case CCM_CHUNK_SKEL: {
                memcpy(&m->skel.bone_count,raw+off,2);off+=2;
                m->skel.bones=malloc(m->skel.bone_count*sizeof(CCMBone));
                memcpy(m->skel.bones,raw+off,m->skel.bone_count*sizeof(CCMBone));
                m->has_skel=true; break;
            }
            case CCM_CHUNK_SKIN: {
                memcpy(&m->skin.vertex_count,raw+off,4);off+=4;
                m->skin.weights=malloc(m->skin.vertex_count*sizeof(CCMSkinVertex));
                memcpy(m->skin.weights,raw+off,m->skin.vertex_count*sizeof(CCMSkinVertex));
                m->has_skin=true; break;
            }
            case CCM_CHUNK_ANIM: {
                CCMAnimChunk* a=ccm_add_anim(m,"",0,false);
                memcpy(a->name,raw+off,64);off+=64;
                memcpy(&a->duration,raw+off,4);off+=4;
                memcpy(&a->fps,raw+off,4);off+=4;
                a->looping=(bool)raw[off++];
                memcpy(&a->track_count,raw+off,4);off+=4;
                a->tracks=calloc(a->track_count,sizeof(CCMTrack));
                for (uint32_t ti=0;ti<a->track_count;ti++){
                    CCMTrack* tr=&a->tracks[ti];
                    memcpy(&tr->bone_index,raw+off,2);off+=2;
                    memcpy(&tr->bshp_index,raw+off,2);off+=2;
                    tr->target=(CCMTrackTarget)raw[off++];
                    tr->interp=(CCMInterpType)raw[off++];
                    memcpy(&tr->key_count,raw+off,4);off+=4;
                    tr->keys=malloc(tr->key_count*sizeof(CCMKeyframe));
                    memcpy(tr->keys,raw+off,tr->key_count*sizeof(CCMKeyframe));
                    off+=tr->key_count*sizeof(CCMKeyframe);
                }
                break;
            }
            case CCM_CHUNK_BSHP: {
                memcpy(&m->bshp.shape_count,raw+off,4);off+=4;
                m->bshp.shapes=calloc(m->bshp.shape_count,sizeof(CCMBlendShape));
                for (uint32_t i=0;i<m->bshp.shape_count;i++){
                    CCMBlendShape* bs=&m->bshp.shapes[i];
                    memcpy(bs->name,raw+off,64);off+=64;
                    memcpy(&bs->delta_count,raw+off,4);off+=4;
                    bs->deltas=malloc(bs->delta_count*sizeof(CCMBlendDelta));
                    memcpy(bs->deltas,raw+off,bs->delta_count*sizeof(CCMBlendDelta));
                    off+=bs->delta_count*sizeof(CCMBlendDelta);
                }
                m->has_bshp=true; break;
            }
            case CCM_CHUNK_MATL: {
                memcpy(&m->matl.slot_count,raw+off,4);off+=4;
                m->matl.slots=malloc(m->matl.slot_count*sizeof(CCMMaterialSlot));
                memcpy(m->matl.slots,raw+off,m->matl.slot_count*sizeof(CCMMaterialSlot));
                break;
            }
        }
        free(raw);
    }
    free(entries);
    fclose(f);
    return m;
}

/* ══════════════════════════════════════════════════════════════════════
   TEXT FORMAT (.ccmodel v1) — human-readable, dependency-free
   ══════════════════════════════════════════════════════════════════════ */

bool ccm_save_text(const CCModel* m, const char* path) {
    FILE* f=fopen(path,"w");
    if(!f){ fprintf(stderr,"ccm_save_text: cannot open %s\n",path); return false; }
    fprintf(f,"ccmodel 1\n");
    fprintf(f,"name %s\n", m->name[0]?m->name:"model");
    /* geometry */
    fprintf(f,"verts %u\n", m->geom.vertex_count);
    for(uint32_t i=0;i<m->geom.vertex_count;i++){
        const CCMVertex* v=&m->geom.vertices[i];
        fprintf(f,"v %.6g %.6g %.6g %.5g %.5g %.5g %.6g %.6g %.5g %.5g %.5g %.5g %u %u %u %u\n",
            v->pos[0],v->pos[1],v->pos[2], v->normal[0],v->normal[1],v->normal[2],
            v->uv[0],v->uv[1], v->tangent[0],v->tangent[1],v->tangent[2],v->tangent[3],
            v->color[0],v->color[1],v->color[2],v->color[3]);
    }
    uint32_t tris=m->geom.index_count/3;
    fprintf(f,"tris %u\n", tris);
    for(uint32_t t=0;t<tris;t++)
        fprintf(f,"f %u %u %u\n", m->geom.indices[t*3],m->geom.indices[t*3+1],m->geom.indices[t*3+2]);
    for(uint32_t s=0;s<m->geom.submesh_count;s++)
        fprintf(f,"submesh %u %u %u\n",
            m->geom.submesh_start[s], m->geom.submesh_count_arr[s], m->geom.submesh_material[s]);
    /* materials */
    for(uint32_t s=0;s<m->matl.slot_count;s++){
        const CCMMaterialSlot* ms=&m->matl.slots[s];
        fprintf(f,"material %s\n", ms->name[0]?ms->name:"mat");
        fprintf(f,"  base_color %.5g %.5g %.5g %.5g\n", ms->base_color[0],ms->base_color[1],ms->base_color[2],ms->base_color[3]);
        fprintf(f,"  roughness %.5g\n", ms->roughness);
        fprintf(f,"  metallic %.5g\n", ms->metallic);
        if(ms->emissive[0]||ms->emissive[1]||ms->emissive[2])
            fprintf(f,"  emissive %.5g %.5g %.5g\n", ms->emissive[0],ms->emissive[1],ms->emissive[2]);
        if(ms->albedo_tex[0])     fprintf(f,"  albedo_tex %s\n", ms->albedo_tex);
        if(ms->normal_tex[0])     fprintf(f,"  normal_tex %s\n", ms->normal_tex);
        if(ms->roughmetal_tex[0]) fprintf(f,"  roughmetal_tex %s\n", ms->roughmetal_tex);
        if(ms->emissive_tex[0])   fprintf(f,"  emissive_tex %s\n", ms->emissive_tex);
        if(ms->ao_tex[0])         fprintf(f,"  ao_tex %s\n", ms->ao_tex);
        if(ms->alpha_cutoff>0)    fprintf(f,"  alpha_cutoff %.5g\n", ms->alpha_cutoff);
        if(ms->double_sided)      fprintf(f,"  double_sided 1\n");
        fprintf(f,"end\n");
    }
    /* ── skeleton ── */
    if(m->has_skel && m->skel.bone_count){
        fprintf(f,"skeleton %u\n", m->skel.bone_count);
        for(uint16_t b=0;b<m->skel.bone_count;b++){
            const CCMBone* bo=&m->skel.bones[b];
            fprintf(f,"bone %s %u  %.6g %.6g %.6g  %.6g %.6g %.6g %.6g  %.5g %.5g %.5g\n",
                bo->name[0]?bo->name:"bone", (unsigned)bo->parent,
                bo->bind_pos[0],bo->bind_pos[1],bo->bind_pos[2],
                bo->bind_rot[0],bo->bind_rot[1],bo->bind_rot[2],bo->bind_rot[3],
                bo->bind_scale[0],bo->bind_scale[1],bo->bind_scale[2]);
        }
    }
    /* ── skin weights ── */
    if(m->has_skin && m->skin.vertex_count){
        fprintf(f,"skin %u\n", m->skin.vertex_count);
        for(uint32_t i=0;i<m->skin.vertex_count;i++){
            const CCMSkinVertex* sw=&m->skin.weights[i];
            fprintf(f,"sw %u %u %u %u  %.6g %.6g %.6g %.6g\n",
                sw->joint[0],sw->joint[1],sw->joint[2],sw->joint[3],
                sw->weight[0],sw->weight[1],sw->weight[2],sw->weight[3]);
        }
    }
    /* ── animations ── */
    for(uint32_t ai=0;ai<m->anim_count;ai++){
        const CCMAnimChunk* a=&m->anims[ai];
        fprintf(f,"anim %s %.6g %.6g %d\n", a->name[0]?a->name:"anim", a->duration, a->fps, a->looping?1:0);
        for(uint32_t ti=0;ti<a->track_count;ti++){
            const CCMTrack* tr=&a->tracks[ti];
            fprintf(f,"track %u %u %d %d %u\n",
                tr->bone_index, tr->bshp_index, (int)tr->target, (int)tr->interp, tr->key_count);
            for(uint32_t ki=0;ki<tr->key_count;ki++){
                const CCMKeyframe* k=&tr->keys[ki];
                fprintf(f,"key %.6g %.6g %.6g %.6g\n", k->time,k->value,k->in_tan,k->out_tan);
            }
        }
        fprintf(f,"endanim\n");
    }
    /* ── blend shapes ── */
    if(m->has_bshp && m->bshp.shape_count){
        for(uint32_t si=0;si<m->bshp.shape_count;si++){
            const CCMBlendShape* bs=&m->bshp.shapes[si];
            fprintf(f,"blendshape %s %u\n", bs->name[0]?bs->name:"shape", bs->delta_count);
            for(uint32_t di=0;di<bs->delta_count;di++){
                const CCMBlendDelta* d=&bs->deltas[di];
                fprintf(f,"bd %u  %.6g %.6g %.6g  %.6g %.6g %.6g\n", d->vertex_index,
                    d->delta_pos[0],d->delta_pos[1],d->delta_pos[2],
                    d->delta_normal[0],d->delta_normal[1],d->delta_normal[2]);
            }
        }
    }
    fclose(f);
    return true;
}

/* strip a trailing comment ('#' to eol) and trailing whitespace, in place */
static void ccm_strip(char* s){
    for(char* p=s; *p; p++){ if(*p=='#'){ *p=0; break; } }
    size_t n=strlen(s);
    while(n && (s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' '||s[n-1]=='\t')) s[--n]=0;
}
/* first non-space token start; returns pointer past leading whitespace */
static char* ccm_skipws(char* s){ while(*s==' '||*s=='\t') s++; return s; }

CCModel* ccm_load_text(const char* path) {
    FILE* f=fopen(path,"rb");
    if(!f){ fprintf(stderr,"ccm_load_text: cannot open %s\n",path); return NULL; }
    /* auto-detect: binary magic → delegate to binary loader */
    uint8_t magic[8]; size_t got=fread(magic,1,8,f);
    if(got==8 && memcmp(magic,CCM_MAGIC,CCM_MAGIC_LEN)==0){ fclose(f); return ccm_load(path); }
    fseek(f,0,SEEK_SET);

    char line[1024];
    /* header line */
    int ok_header=0;
    while(fgets(line,sizeof(line),f)){
        ccm_strip(line); char* p=ccm_skipws(line); if(!*p) continue;
        int ver=0;
        if(sscanf(p,"ccmodel %d",&ver)==1 && ver==1){ ok_header=1; break; }
        fprintf(stderr,"ccm_load_text: bad header (expected 'ccmodel 1')\n"); fclose(f); return NULL;
    }
    if(!ok_header){ fclose(f); return NULL; }

    CCModel* m=ccm_model_new("model");
    CCMVertex* verts=NULL; uint32_t nv=0, vi=0;
    uint32_t* idx=NULL; uint32_t ni=0, ii=0;
    int cur_mat=-1;  /* index into m->matl.slots while inside a material block */
    /* submeshes are buffered and applied after geometry is committed */
    uint32_t sub_buf[512][3]; uint32_t sub_n=0;
    /* rig-parsing state */
    CCMAnimChunk* cur_anim=NULL; CCMTrack* cur_track=NULL;
    CCMBlendShape* cur_bshp=NULL;
    uint32_t skin_fill=0;   /* next skin-weight row to fill */

    while(fgets(line,sizeof(line),f)){
        ccm_strip(line);
        char* p=ccm_skipws(line);
        if(!*p) continue;
        char kw[64]={0};
        sscanf(p,"%63s",kw);

        if(!strcmp(kw,"name")){
            char* rest=ccm_skipws(p+4);
            strncpy(m->name,rest,sizeof(m->name)-1);
        } else if(!strcmp(kw,"verts")){
            sscanf(p,"verts %u",&nv);
            verts=calloc(nv?nv:1,sizeof(CCMVertex)); vi=0;
        } else if(!strcmp(kw,"v")){
            if(verts && vi<nv){
                CCMVertex* v=&verts[vi];
                unsigned c0=255,c1=255,c2=255,c3=255;
                sscanf(p,"v %f %f %f %f %f %f %f %f %f %f %f %f %u %u %u %u",
                    &v->pos[0],&v->pos[1],&v->pos[2], &v->normal[0],&v->normal[1],&v->normal[2],
                    &v->uv[0],&v->uv[1], &v->tangent[0],&v->tangent[1],&v->tangent[2],&v->tangent[3],
                    &c0,&c1,&c2,&c3);
                v->color[0]=(uint8_t)c0; v->color[1]=(uint8_t)c1; v->color[2]=(uint8_t)c2; v->color[3]=(uint8_t)c3;
                vi++;
            }
        } else if(!strcmp(kw,"tris")){
            sscanf(p,"tris %u",&ni); ni*=3;
            idx=calloc(ni?ni:1,sizeof(uint32_t)); ii=0;
        } else if(!strcmp(kw,"f")){
            if(idx && ii+3<=ni){
                sscanf(p,"f %u %u %u",&idx[ii],&idx[ii+1],&idx[ii+2]); ii+=3;
            }
        } else if(!strcmp(kw,"submesh")){
            uint32_t st=0,ct=0,mt=0; sscanf(p,"submesh %u %u %u",&st,&ct,&mt);
            if(sub_n<512){ sub_buf[sub_n][0]=st; sub_buf[sub_n][1]=ct; sub_buf[sub_n][2]=mt; sub_n++; }
        } else if(!strcmp(kw,"material")){
            char* rest=ccm_skipws(p+8);
            cur_mat=(int)ccm_add_material_slot(m, rest[0]?rest:"mat");
        } else if(!strcmp(kw,"base_color") && cur_mat>=0){
            CCMMaterialSlot* ms=ccm_get_material_slot(m,cur_mat);
            sscanf(p,"base_color %f %f %f %f",&ms->base_color[0],&ms->base_color[1],&ms->base_color[2],&ms->base_color[3]);
        } else if(!strcmp(kw,"roughness") && cur_mat>=0){
            ccm_get_material_slot(m,cur_mat)->roughness=(float)atof(ccm_skipws(p+9));
        } else if(!strcmp(kw,"metallic") && cur_mat>=0){
            ccm_get_material_slot(m,cur_mat)->metallic=(float)atof(ccm_skipws(p+8));
        } else if(!strcmp(kw,"emissive") && cur_mat>=0){
            CCMMaterialSlot* ms=ccm_get_material_slot(m,cur_mat);
            sscanf(p,"emissive %f %f %f",&ms->emissive[0],&ms->emissive[1],&ms->emissive[2]);
        } else if(!strcmp(kw,"albedo_tex") && cur_mat>=0){
            strncpy(ccm_get_material_slot(m,cur_mat)->albedo_tex,ccm_skipws(p+10),255);
        } else if(!strcmp(kw,"normal_tex") && cur_mat>=0){
            strncpy(ccm_get_material_slot(m,cur_mat)->normal_tex,ccm_skipws(p+10),255);
        } else if(!strcmp(kw,"roughmetal_tex") && cur_mat>=0){
            strncpy(ccm_get_material_slot(m,cur_mat)->roughmetal_tex,ccm_skipws(p+14),255);
        } else if(!strcmp(kw,"emissive_tex") && cur_mat>=0){
            strncpy(ccm_get_material_slot(m,cur_mat)->emissive_tex,ccm_skipws(p+12),255);
        } else if(!strcmp(kw,"ao_tex") && cur_mat>=0){
            strncpy(ccm_get_material_slot(m,cur_mat)->ao_tex,ccm_skipws(p+6),255);
        } else if(!strcmp(kw,"alpha_cutoff") && cur_mat>=0){
            ccm_get_material_slot(m,cur_mat)->alpha_cutoff=(float)atof(ccm_skipws(p+12));
        } else if(!strcmp(kw,"double_sided") && cur_mat>=0){
            ccm_get_material_slot(m,cur_mat)->double_sided=(atoi(ccm_skipws(p+12))!=0);
        } else if(!strcmp(kw,"end")){
            cur_mat=-1;
        } else if(!strcmp(kw,"skeleton")){
            /* count is informational; bones are added as they appear */
        } else if(!strcmp(kw,"bone")){
            char bn[64]={0}; unsigned parent=0xFFFF;
            float px,py,pz,qx,qy,qz,qw,sx,sy,sz;
            /* name may contain no spaces (writer uses bo->name as one token) */
            if(sscanf(p,"bone %63s %u %f %f %f %f %f %f %f %f %f %f",
                bn,&parent,&px,&py,&pz,&qx,&qy,&qz,&qw,&sx,&sy,&sz)==12){
                float pos[3]={px,py,pz}, rot[4]={qx,qy,qz,qw}, scl[3]={sx,sy,sz};
                ccm_add_bone(m,bn,(uint16_t)parent,pos,rot,scl);
            }
        } else if(!strcmp(kw,"skin")){
            uint32_t sc=0; sscanf(p,"skin %u",&sc);
            if(sc){
                m->skin.vertex_count=sc;
                m->skin.weights=calloc(sc,sizeof(CCMSkinVertex));
                m->has_skin=true; skin_fill=0;
            }
        } else if(!strcmp(kw,"sw")){
            if(m->skin.weights && skin_fill<m->skin.vertex_count){
                CCMSkinVertex* sv=&m->skin.weights[skin_fill];
                unsigned j0,j1,j2,j3; float w0,w1,w2,w3;
                if(sscanf(p,"sw %u %u %u %u %f %f %f %f",&j0,&j1,&j2,&j3,&w0,&w1,&w2,&w3)==8){
                    sv->joint[0]=(uint16_t)j0; sv->joint[1]=(uint16_t)j1;
                    sv->joint[2]=(uint16_t)j2; sv->joint[3]=(uint16_t)j3;
                    sv->weight[0]=w0; sv->weight[1]=w1; sv->weight[2]=w2; sv->weight[3]=w3;
                }
                skin_fill++;
            }
        } else if(!strcmp(kw,"anim")){
            char an[64]={0}; float dur=0,fps=30; int loop=0;
            sscanf(p,"anim %63s %f %f %d",an,&dur,&fps,&loop);
            cur_anim=ccm_add_anim(m,an,dur,loop!=0);
            if(cur_anim) cur_anim->fps=fps;
            cur_track=NULL;
        } else if(!strcmp(kw,"track") && cur_anim){
            unsigned bi=0,bshp=0; int target=0,interp=1; unsigned kc=0;
            sscanf(p,"track %u %u %d %d %u",&bi,&bshp,&target,&interp,&kc);
            cur_track=ccm_anim_add_track(cur_anim,(uint16_t)bi,(CCMTrackTarget)target,(CCMInterpType)interp);
            if(cur_track) cur_track->bshp_index=(uint16_t)bshp;
            (void)kc;
        } else if(!strcmp(kw,"key") && cur_track){
            float t=0,v=0,it=0,ot=0; sscanf(p,"key %f %f %f %f",&t,&v,&it,&ot);
            ccm_track_add_key(cur_track,t,v,it,ot);
        } else if(!strcmp(kw,"endanim")){
            cur_anim=NULL; cur_track=NULL;
        } else if(!strcmp(kw,"blendshape")){
            char sn[64]={0}; unsigned dc=0; sscanf(p,"blendshape %63s %u",sn,&dc);
            cur_bshp=ccm_add_blend_shape(m,sn); (void)dc;
        } else if(!strcmp(kw,"bd") && cur_bshp){
            unsigned vi2=0; float dpx,dpy,dpz,dnx,dny,dnz;
            if(sscanf(p,"bd %u %f %f %f %f %f %f",&vi2,&dpx,&dpy,&dpz,&dnx,&dny,&dnz)==7){
                float dp[3]={dpx,dpy,dpz}, dn[3]={dnx,dny,dnz};
                ccm_bshp_add_delta(cur_bshp,vi2,dp,dn);
            }
        }
        /* unknown keywords are ignored (forward-compatible) */
    }
    fclose(f);

    /* commit geometry, then apply buffered submeshes */
    if(m->geom.vertex_count==0 && verts) ccm_set_geometry(m,verts,vi,idx,ii);
    for(uint32_t s=0;s<sub_n;s++) ccm_add_submesh(m,sub_buf[s][0],sub_buf[s][1],sub_buf[s][2]);
    /* inverse bind poses aren't serialized in text — recompute from bind TRS */
    if(m->has_skel && m->skel.bone_count) ccm_compute_inv_bind_poses(m);
    free(verts); free(idx);
    return m;
}

bool ccm_peek(const char* path, char* out_name, uint32_t name_len,
              uint32_t* out_bones, uint32_t* out_anims, uint32_t* out_verts) {
    FILE* f=fopen(path,"rb"); if(!f) return false;
    uint8_t magic[8]; fread(magic,1,8,f);
    if(memcmp(magic,CCM_MAGIC,8)!=0){fclose(f);return false;}
    fseek(f,2,SEEK_CUR); /* version */
    uint16_t nchunks; fread(&nchunks,2,1,f);
    fseek(f,48,SEEK_CUR);
    CCMChunkEntry* entries=malloc(nchunks*sizeof(CCMChunkEntry));
    fread(entries,sizeof(CCMChunkEntry),nchunks,f);
    if (out_name)  out_name[0]=0;
    if (out_bones) *out_bones=0;
    if (out_anims) *out_anims=0;
    if (out_verts) *out_verts=0;
    uint32_t anim_count=0;
    for (uint16_t i=0;i<nchunks;i++) {
        if (entries[i].type==CCM_CHUNK_META && out_name) {
            fseek(f,(long)entries[i].offset,SEEK_SET);
            fread(out_name,1,name_len<entries[i].size_raw?name_len:entries[i].size_raw,f);
        }
        if (entries[i].type==CCM_CHUNK_SKEL && out_bones) {
            fseek(f,(long)entries[i].offset,SEEK_SET);
            uint8_t* tmp=malloc(entries[i].size_compressed);
            fread(tmp,1,entries[i].size_compressed,f);
            uint8_t* raw=malloc(entries[i].size_raw);
            if(entries[i].size_compressed==entries[i].size_raw) memcpy(raw,tmp,entries[i].size_raw);
            else ZSTD_decompress(raw,entries[i].size_raw,tmp,entries[i].size_compressed);
            memcpy(out_bones,raw,2); free(tmp);free(raw);
        }
        if (entries[i].type==CCM_CHUNK_ANIM) anim_count++;
        if (entries[i].type==CCM_CHUNK_GEOM && out_verts) {
            fseek(f,(long)entries[i].offset,SEEK_SET);
            uint8_t* tmp=malloc(entries[i].size_compressed);
            fread(tmp,1,entries[i].size_compressed,f);
            uint8_t* raw=malloc(entries[i].size_raw);
            if(entries[i].size_compressed==entries[i].size_raw) memcpy(raw,tmp,entries[i].size_raw);
            else ZSTD_decompress(raw,entries[i].size_raw,tmp,entries[i].size_compressed);
            memcpy(out_verts,raw,4); free(tmp);free(raw);
        }
    }
    if (out_anims) *out_anims=anim_count;
    free(entries); fclose(f);
    return true;
}

/* ══════════════════════════════════════════════════════════════════════
   PROCEDURAL BUILDERS
   ══════════════════════════════════════════════════════════════════════ */

CCModel* ccm_make_box(const char* name, float w, float h, float d) {
    w*=0.5f; h*=0.5f; d*=0.5f;
    CCMVertex verts[24];
    memset(verts,0,sizeof(verts));
    /* 6 faces × 4 verts */
    float faces[6][4][3]={
        {{-w,-h,-d},{w,-h,-d},{w,h,-d},{-w,h,-d}},/* -Z */
        {{w,-h,d},{-w,-h,d},{-w,h,d},{w,h,d}},     /* +Z */
        {{-w,-h,d},{-w,-h,-d},{-w,h,-d},{-w,h,d}}, /* -X */
        {{w,-h,-d},{w,-h,d},{w,h,d},{w,h,-d}},     /* +X */
        {{-w,-h,d},{w,-h,d},{w,-h,-d},{-w,-h,-d}}, /* -Y */
        {{-w,h,-d},{w,h,-d},{w,h,d},{-w,h,d}},     /* +Y */
    };
    float normals[6][3]={{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0}};
    float uvs[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for (int f=0;f<6;f++) for (int v=0;v<4;v++) {
        CCMVertex* vt=&verts[f*4+v];
        memcpy(vt->pos,faces[f][v],12);
        memcpy(vt->normal,normals[f],12);
        memcpy(vt->uv,uvs[v],8);
        vt->color[0]=vt->color[1]=vt->color[2]=vt->color[3]=255;
    }
    uint32_t idx[36];
    for (int f=0;f<6;f++){uint32_t b=f*4;idx[f*6+0]=b;idx[f*6+1]=b+1;idx[f*6+2]=b+2;idx[f*6+3]=b;idx[f*6+4]=b+2;idx[f*6+5]=b+3;}
    CCModel* m=ccm_model_new(name);
    ccm_set_geometry(m,verts,24,idx,36);
    ccm_compute_tangents(m);
    return m;
}

CCModel* ccm_make_sphere(const char* name, float r, uint32_t slices, uint32_t stacks) {
    uint32_t nv=(slices+1)*(stacks+1);
    uint32_t ni=slices*stacks*6;
    CCMVertex* verts=malloc(nv*sizeof(CCMVertex));
    uint32_t* idx=malloc(ni*4);
    uint32_t vi=0;
    for (uint32_t st=0;st<=stacks;st++) {
        float phi=(float)st/stacks*3.14159265f;
        for (uint32_t sl=0;sl<=slices;sl++) {
            float theta=(float)sl/slices*6.28318530f;
            float x=sinf(phi)*cosf(theta), y=cosf(phi), z=sinf(phi)*sinf(theta);
            CCMVertex* v=&verts[vi++];
            v->pos[0]=x*r; v->pos[1]=y*r; v->pos[2]=z*r;
            v->normal[0]=x; v->normal[1]=y; v->normal[2]=z;
            v->uv[0]=(float)sl/slices; v->uv[1]=(float)st/stacks;
            v->color[0]=v->color[1]=v->color[2]=v->color[3]=255;
        }
    }
    uint32_t ii=0;
    for (uint32_t st=0;st<stacks;st++) for (uint32_t sl=0;sl<slices;sl++) {
        uint32_t a=st*(slices+1)+sl, b=a+1, c=a+slices+1, dd=c+1;
        idx[ii++]=a;idx[ii++]=b;idx[ii++]=dd;
        idx[ii++]=a;idx[ii++]=dd;idx[ii++]=c;
    }
    CCModel* m=ccm_model_new(name);
    ccm_set_geometry(m,verts,nv,idx,ni);
    ccm_compute_tangents(m);
    free(verts); free(idx);
    return m;
}

CCModel* ccm_make_plane(const char* name, float w, float d, uint32_t divs) {
    uint32_t nv=(divs+1)*(divs+1), ni=divs*divs*6;
    CCMVertex* verts=malloc(nv*sizeof(CCMVertex));
    uint32_t* idx=malloc(ni*4);
    uint32_t vi=0;
    for (uint32_t z=0;z<=divs;z++) for (uint32_t x=0;x<=divs;x++) {
        CCMVertex* v=&verts[vi++];
        v->pos[0]=((float)x/divs-0.5f)*w; v->pos[1]=0; v->pos[2]=((float)z/divs-0.5f)*d;
        v->normal[0]=0; v->normal[1]=1; v->normal[2]=0;
        v->uv[0]=(float)x/divs; v->uv[1]=(float)z/divs;
        v->color[0]=v->color[1]=v->color[2]=v->color[3]=255;
    }
    uint32_t ii=0;
    for (uint32_t z=0;z<divs;z++) for (uint32_t x=0;x<divs;x++) {
        uint32_t a=z*(divs+1)+x,b=a+1,c=a+divs+1,dd=c+1;
        idx[ii++]=a;idx[ii++]=b;idx[ii++]=dd;idx[ii++]=a;idx[ii++]=dd;idx[ii++]=c;
    }
    CCModel* m=ccm_model_new(name);
    ccm_set_geometry(m,verts,nv,idx,ni);
    ccm_compute_tangents(m);
    free(verts); free(idx);
    return m;
}

CCModel* ccm_make_cylinder(const char* name, float r, float h, uint32_t segs) {
    /* side + caps */
    uint32_t nv=(segs+1)*2+segs*2+2;
    uint32_t ni=segs*2*3+segs*2*3;
    CCMVertex* verts=malloc(nv*sizeof(CCMVertex));
    uint32_t* idx=malloc(ni*4);
    uint32_t vi=0,ii=0;
    /* Sides */
    for (uint32_t s=0;s<=segs;s++) {
        float a=(float)s/segs*6.28318f;
        float x=cosf(a), z=sinf(a);
        for (int top=0;top<2;top++) {
            CCMVertex* v=&verts[vi++];
            v->pos[0]=x*r; v->pos[1]=top?h/2:-h/2; v->pos[2]=z*r;
            v->normal[0]=x; v->normal[1]=0; v->normal[2]=z;
            v->uv[0]=(float)s/segs; v->uv[1]=(float)top;
            v->color[0]=v->color[1]=v->color[2]=v->color[3]=255;
        }
    }
    for (uint32_t s=0;s<segs;s++){
        uint32_t b=s*2;
        idx[ii++]=b;idx[ii++]=b+2;idx[ii++]=b+3;
        idx[ii++]=b;idx[ii++]=b+3;idx[ii++]=b+1;
    }
    /* Caps */
    uint32_t cap_base=vi;
    for (int top=0;top<2;top++) {
        float ny=top?1:-1;
        uint32_t center=vi;
        CCMVertex* vc=&verts[vi++];
        vc->pos[0]=0;vc->pos[1]=top?h/2:-h/2;vc->pos[2]=0;
        vc->normal[0]=0;vc->normal[1]=ny;vc->normal[2]=0;
        vc->uv[0]=0.5f;vc->uv[1]=0.5f;
        vc->color[0]=vc->color[1]=vc->color[2]=vc->color[3]=255;
        for (uint32_t s=0;s<segs;s++) {
            float a=(float)s/segs*6.28318f, b2=(float)(s+1)/segs*6.28318f;
            CCMVertex* va=&verts[vi++]; CCMVertex* vb=&verts[vi++];
            va->pos[0]=cosf(a)*r;va->pos[1]=top?h/2:-h/2;va->pos[2]=sinf(a)*r;
            vb->pos[0]=cosf(b2)*r;vb->pos[1]=top?h/2:-h/2;vb->pos[2]=sinf(b2)*r;
            for(int k=0;k<2;k++){va->normal[k]=0;vb->normal[k]=0;}
            va->normal[1]=ny;vb->normal[1]=ny;
            va->color[0]=va->color[1]=va->color[2]=va->color[3]=255;
            vb->color[0]=vb->color[1]=vb->color[2]=vb->color[3]=255;
            if (top){idx[ii++]=center;idx[ii++]=(uint32_t)(vi-1);idx[ii++]=(uint32_t)(vi-2);}
            else    {idx[ii++]=center;idx[ii++]=(uint32_t)(vi-2);idx[ii++]=(uint32_t)(vi-1);}
        }
    }
    (void)cap_base;
    CCModel* m=ccm_model_new(name);
    ccm_set_geometry(m,verts,vi,idx,ii);
    ccm_compute_normals(m); ccm_compute_tangents(m);
    free(verts);free(idx);
    return m;
}

CCModel* ccm_make_capsule(const char* name, float r, float h, uint32_t segs) {
    /* Capsule = cylinder body (height h between cap centers) + two hemisphere
       caps of radius r. Built as a single lat/long sphere "split" and pushed
       apart by h/2, so the seams line up with the body rings exactly. */
    if(segs<3) segs=3;
    uint32_t rings=segs;                 /* longitudinal segments */
    uint32_t half=segs/2; if(half<2) half=2;  /* latitude bands per hemisphere */
    float halfh=h*0.5f;

    /* vertex grid: for each ring s in [0..rings], a column of vertices:
       top hemisphere (half+1 lats) + bottom hemisphere (half+1 lats).
       We emit top pole..equatorTop (shifted +halfh) then equatorBot..bottom pole
       (shifted -halfh). */
    uint32_t lat_total = (half+1)*2;     /* top hemi lats + bottom hemi lats */
    uint32_t nv=(rings+1)*lat_total;
    uint32_t ni=rings*(lat_total-1)*6;
    CCMVertex* verts=malloc(nv*sizeof(CCMVertex));
    uint32_t* idx=malloc(ni*sizeof(uint32_t));
    uint32_t vi=0, ii=0;

    for(uint32_t s=0;s<=rings;s++){
        float az=(float)s/rings*6.28318530718f; float cx=cosf(az), cz=sinf(az);
        for(uint32_t l=0;l<lat_total;l++){
            /* map l to a polar angle + which hemisphere/offset */
            float ny, rr, yoff;
            if(l<=half){ /* top hemisphere: pole(l=0)→equator(l=half) */
                float t=(float)l/half;                 /* 0..1 */
                float phi=t*1.57079632679f;             /* 0..pi/2 */
                ny=cosf(phi); rr=sinf(phi); yoff=halfh;
            } else {     /* bottom hemisphere: equator→pole */
                float t=(float)(l-half-1)/half;         /* 0..1 across bottom */
                float phi=t*1.57079632679f;             /* 0..pi/2 */
                ny=-sinf(phi); rr=cosf(phi); yoff=-halfh;
                if(l==half+1){ ny=0; rr=1; }            /* equator start */
            }
            CCMVertex* v=&verts[vi++];
            v->pos[0]=cx*rr*r; v->pos[1]=ny*r+yoff; v->pos[2]=cz*rr*r;
            /* normal: for the caps it's the radial dir from the cap center; for
               the body (equator band) it's horizontal — both handled by (cx*rr,ny,cz*rr). */
            float nlen=sqrtf(cx*rr*cx*rr+ny*ny+cz*rr*cz*rr); if(nlen<1e-6f)nlen=1;
            v->normal[0]=cx*rr/nlen; v->normal[1]=ny/nlen; v->normal[2]=cz*rr/nlen;
            v->uv[0]=(float)s/rings; v->uv[1]=(float)l/(lat_total-1);
            v->tangent[0]=1;v->tangent[1]=0;v->tangent[2]=0;v->tangent[3]=1;
            v->color[0]=v->color[1]=v->color[2]=v->color[3]=255;
        }
    }
    for(uint32_t s=0;s<rings;s++){
        for(uint32_t l=0;l<lat_total-1;l++){
            uint32_t a=s*lat_total+l, b=(s+1)*lat_total+l;
            idx[ii++]=a;   idx[ii++]=b;   idx[ii++]=b+1;
            idx[ii++]=a;   idx[ii++]=b+1; idx[ii++]=a+1;
        }
    }
    CCModel* m=ccm_model_new(name);
    ccm_set_geometry(m,verts,vi,idx,ii);
    free(verts); free(idx);
    return m;
}

/* ── Humanoid rig ─────────────────────────────────────────────────────── */
CCModel* ccm_make_humanoid(const char* name) {
    CCModel* m=ccm_model_new(name);

    /* Standard humanoid skeleton — 55 bones */
    float I[4]={0,0,0,1}; float S[3]={1,1,1};

    /* Core */
    float hip[3]={0,1.0f,0}, spine[3]={0,0.15f,0}, chest[3]={0,0.2f,0};
    float neck[3]={0,0.25f,0}, head[3]={0,0.18f,0};

    uint16_t b_root  =ccm_add_bone(m,"root",       CCM_BONE_NO_PARENT,(float[]){0,0,0},I,S);
    uint16_t b_hips  =ccm_add_bone(m,"hips",        b_root,hip,I,S);
    uint16_t b_spine =ccm_add_bone(m,"spine",        b_hips,spine,I,S);
    uint16_t b_chest =ccm_add_bone(m,"chest",        b_spine,chest,I,S);
    uint16_t b_upper =ccm_add_bone(m,"upper_chest",  b_chest,(float[]){0,0.15f,0},I,S);
    uint16_t b_neck  =ccm_add_bone(m,"neck",         b_upper,neck,I,S);
    uint16_t b_head  =ccm_add_bone(m,"head",         b_neck,head,I,S);

    /* Left arm */
    uint16_t b_sl=ccm_add_bone(m,"shoulder_l",b_upper,(float[]){0.1f,0.1f,0},I,S);
    uint16_t b_ul=ccm_add_bone(m,"upper_arm_l",b_sl,(float[]){0.15f,0,0},I,S);
    uint16_t b_ll=ccm_add_bone(m,"lower_arm_l",b_ul,(float[]){0.28f,0,0},I,S);
    uint16_t b_wl=ccm_add_bone(m,"hand_l",     b_ll,(float[]){0.25f,0,0},I,S);
    /* Left fingers */
    for (int fi=0;fi<5;fi++) {
        char fn[32]; snprintf(fn,32,"finger_%d_l_0",fi);
        float fx=0.08f*(fi-2)*0.4f;
        uint16_t f0=ccm_add_bone(m,fn,b_wl,(float[]){0.05f,0,fx},I,S);
        snprintf(fn,32,"finger_%d_l_1",fi); uint16_t f1=ccm_add_bone(m,fn,f0,(float[]){0.03f,0,0},I,S);
        snprintf(fn,32,"finger_%d_l_2",fi); ccm_add_bone(m,fn,f1,(float[]){0.025f,0,0},I,S);
    }

    /* Right arm (mirror) */
    uint16_t b_sr=ccm_add_bone(m,"shoulder_r",b_upper,(float[]){-0.1f,0.1f,0},I,S);
    uint16_t b_ur=ccm_add_bone(m,"upper_arm_r",b_sr,(float[]){-0.15f,0,0},I,S);
    uint16_t b_lr=ccm_add_bone(m,"lower_arm_r",b_ur,(float[]){-0.28f,0,0},I,S);
    uint16_t b_wr=ccm_add_bone(m,"hand_r",     b_lr,(float[]){-0.25f,0,0},I,S);
    for (int fi=0;fi<5;fi++) {
        char fn[32]; snprintf(fn,32,"finger_%d_r_0",fi);
        float fx=0.08f*(fi-2)*0.4f;
        uint16_t f0=ccm_add_bone(m,fn,b_wr,(float[]){-0.05f,0,fx},I,S);
        snprintf(fn,32,"finger_%d_r_1",fi); uint16_t f1=ccm_add_bone(m,fn,f0,(float[]){-0.03f,0,0},I,S);
        snprintf(fn,32,"finger_%d_r_2",fi); ccm_add_bone(m,fn,f1,(float[]){-0.025f,0,0},I,S);
    }

    /* Left leg */
    uint16_t b_ull=ccm_add_bone(m,"upper_leg_l",b_hips,(float[]){0.1f,-0.05f,0},I,S);
    uint16_t b_lll=ccm_add_bone(m,"lower_leg_l",b_ull,(float[]){0,-0.45f,0},I,S);
    uint16_t b_fl =ccm_add_bone(m,"foot_l",     b_lll,(float[]){0,-0.42f,0},I,S);
    ccm_add_bone(m,"toe_l",b_fl,(float[]){0,-0.05f,0.1f},I,S);

    /* Right leg */
    uint16_t b_ulr=ccm_add_bone(m,"upper_leg_r",b_hips,(float[]){-0.1f,-0.05f,0},I,S);
    uint16_t b_llr=ccm_add_bone(m,"lower_leg_r",b_ulr,(float[]){0,-0.45f,0},I,S);
    uint16_t b_fr =ccm_add_bone(m,"foot_r",     b_llr,(float[]){0,-0.42f,0},I,S);
    ccm_add_bone(m,"toe_r",b_fr,(float[]){0,-0.05f,0.1f},I,S);

    /* Suppress unused variable warnings */
    (void)b_hips;(void)b_spine;(void)b_chest;(void)b_neck;(void)b_head;
    (void)b_sl;(void)b_ul;(void)b_ll;(void)b_wl;
    (void)b_sr;(void)b_ur;(void)b_lr;(void)b_wr;
    (void)b_ull;(void)b_lll;(void)b_fl;
    (void)b_ulr;(void)b_llr;(void)b_fr;
    (void)b_root;(void)b_upper;

    /* Facial blend shapes */
    const char* shapes[]={"jaw_open","smile","frown","brow_raise_l","brow_raise_r",
        "brow_lower_l","brow_lower_r","eye_blink_l","eye_blink_r",
        "cheek_puff","lip_stretch","lip_pucker","nose_wrinkle"};
    for (int i=0;i<13;i++) ccm_add_blend_shape(m,shapes[i]);

    /* Default material slot */
    uint32_t mat=ccm_add_material_slot(m,"body");
    CCMMaterialSlot* ms=ccm_get_material_slot(m,mat);
    ms->base_color[0]=0.85f;ms->base_color[1]=0.7f;ms->base_color[2]=0.6f;ms->base_color[3]=1.0f;
    ms->roughness=0.6f;ms->metallic=0.0f;

    ccm_compute_inv_bind_poses(m);

    fprintf(stderr,"[ccmodel] humanoid: %d bones, %d blend shapes\n",m->skel.bone_count,m->bshp.shape_count);
    return m;
}

/* OBJ import */
CCModel* ccm_import_obj(const char* path, const char* name) {
    FILE* f=fopen(path,"r");
    if(!f){fprintf(stderr,"ccm_import_obj: cannot open %s\n",path);return NULL;}
    float* pos=malloc(1<<20); float* uvs=malloc(1<<20); float* nrm=malloc(1<<20);
    CCMVertex* verts=malloc(sizeof(CCMVertex)<<16); uint32_t* idx=malloc(4<<18);
    uint32_t np=0,nu=0,nn=0,nv=0,ni=0;
    char line[512];
    while(fgets(line,512,f)) {
        if(line[0]=='v'&&line[1]==' '){sscanf(line+2,"%f %f %f",&pos[np*3],&pos[np*3+1],&pos[np*3+2]);np++;}
        else if(line[0]=='v'&&line[1]=='t'){sscanf(line+3,"%f %f",&uvs[nu*2],&uvs[nu*2+1]);nu++;}
        else if(line[0]=='v'&&line[1]=='n'){sscanf(line+3,"%f %f %f",&nrm[nn*3],&nrm[nn*3+1],&nrm[nn*3+2]);nn++;}
        else if(line[0]=='f') {
            /* Parse up to 4 vertex refs (triangulate quads) */
            int pi[4]={0},ti[4]={0},ni2[4]={0}; int fc=0;
            char* p=line+2;
            while(*p&&fc<4) {
                int a,b=0,c=0;
                if(sscanf(p,"%d/%d/%d",&a,&b,&c)==3||sscanf(p,"%d//%d",&a,&c)==2||sscanf(p,"%d",&a)==1){
                    pi[fc]=a-1;ti[fc]=b-1;ni2[fc]=c-1;fc++;
                }
                while(*p&&*p!=' ')p++;while(*p==' ')p++;
            }
            /* Triangulate */
            for(int t=1;t<fc-1;t++){
                int tri[3]={0,t,t+1};
                for(int j=0;j<3;j++){
                    CCMVertex* v=&verts[nv];memset(v,0,sizeof(*v));
                    int pj=pi[tri[j]],tj=ti[tri[j]],nj=ni2[tri[j]];
                    if(pj>=0&&pj<(int)np){v->pos[0]=pos[pj*3];v->pos[1]=pos[pj*3+1];v->pos[2]=pos[pj*3+2];}
                    if(tj>=0&&tj<(int)nu){v->uv[0]=uvs[tj*2];v->uv[1]=uvs[tj*2+1];}
                    if(nj>=0&&nj<(int)nn){v->normal[0]=nrm[nj*3];v->normal[1]=nrm[nj*3+1];v->normal[2]=nrm[nj*3+2];}
                    v->color[0]=v->color[1]=v->color[2]=v->color[3]=255;
                    idx[ni++]=(uint32_t)(nv++);
                }
            }
        }
    }
    fclose(f);
    CCModel* m=ccm_model_new(name?name:path);
    ccm_set_geometry(m,verts,nv,idx,ni);
    if(nn==0)ccm_compute_normals(m);
    ccm_compute_tangents(m);
    free(pos);free(uvs);free(nrm);free(verts);free(idx);
    fprintf(stderr,"[ccmodel] OBJ import: %s — %u verts, %u tris\n",path,nv,ni/3);
    return m;
}

/* ─── Skin weights ───────────────────────────────────────────────────── */
void ccm_set_skin(CCModel* m, const CCMSkinVertex* weights, uint32_t count) {
    if (!m) return;
    free(m->skin.weights);
    m->skin.vertex_count = count;
    m->skin.weights = malloc(count * sizeof(CCMSkinVertex));
    memcpy(m->skin.weights, weights, count * sizeof(CCMSkinVertex));
    m->has_skin = true;
}

void ccm_normalize_weights(CCModel* m) {
    if (!m || !m->has_skin || !m->skin.weights) return;
    for (uint32_t i = 0; i < m->skin.vertex_count; i++) {
        CCMSkinVertex* sv = &m->skin.weights[i];
        float sum = sv->weight[0] + sv->weight[1] + sv->weight[2] + sv->weight[3];
        if (sum > 1e-8f) {
            float inv = 1.0f / sum;
            for (int j = 0; j < 4; j++) sv->weight[j] *= inv;
        } else {
            /* No influences — bind fully to bone 0 so the vertex still animates */
            sv->joint[0]=0; sv->weight[0]=1.0f;
            sv->weight[1]=sv->weight[2]=sv->weight[3]=0.0f;
        }
    }
}

/* ─── Merge two models into one (concatenate geometry, offset indices) ─── */
CCModel* ccm_merge(const CCModel* a, const CCModel* b, const char* name) {
    if (!a || !b) return NULL;
    CCModel* m = ccm_model_new(name ? name : "merged");

    uint32_t nv = a->geom.vertex_count + b->geom.vertex_count;
    uint32_t ni = a->geom.index_count  + b->geom.index_count;
    CCMVertex* verts = malloc(nv * sizeof(CCMVertex));
    uint32_t*  idx   = malloc(ni * sizeof(uint32_t));

    /* copy A verts + indices verbatim */
    memcpy(verts, a->geom.vertices, a->geom.vertex_count * sizeof(CCMVertex));
    memcpy(idx,   a->geom.indices,  a->geom.index_count  * sizeof(uint32_t));
    /* copy B verts after A, and B indices shifted by A's vertex count */
    memcpy(verts + a->geom.vertex_count, b->geom.vertices,
           b->geom.vertex_count * sizeof(CCMVertex));
    uint32_t base = a->geom.vertex_count;
    for (uint32_t i = 0; i < b->geom.index_count; i++)
        idx[a->geom.index_count + i] = b->geom.indices[i] + base;

    ccm_set_geometry(m, verts, nv, idx, ni);
    free(verts); free(idx);

    /* Build a submesh table: A as submesh 0, B as submesh 1, preserving material slots */
    m->geom.submesh_count = 2;
    m->geom.submesh_start    = malloc(2 * sizeof(uint32_t));
    m->geom.submesh_count_arr= malloc(2 * sizeof(uint32_t));
    m->geom.submesh_material = malloc(2 * sizeof(uint32_t));
    m->geom.submesh_start[0]    = 0;
    m->geom.submesh_count_arr[0]= a->geom.index_count;
    m->geom.submesh_material[0] = 0;
    m->geom.submesh_start[1]    = a->geom.index_count;
    m->geom.submesh_count_arr[1]= b->geom.index_count;
    m->geom.submesh_material[1] = (a->matl.slot_count > 0) ? a->matl.slot_count : 1;

    /* Merge material slots: A's slots then B's slots */
    uint32_t total_slots = a->matl.slot_count + b->matl.slot_count;
    if (total_slots > 0) {
        m->matl.slot_count = total_slots;
        m->matl.slots = malloc(total_slots * sizeof(CCMMaterialSlot));
        if (a->matl.slot_count)
            memcpy(m->matl.slots, a->matl.slots, a->matl.slot_count * sizeof(CCMMaterialSlot));
        if (b->matl.slot_count)
            memcpy(m->matl.slots + a->matl.slot_count, b->matl.slots,
                   b->matl.slot_count * sizeof(CCMMaterialSlot));
    }
    return m;
}

/* ─── glTF 2.0 export (spec-compliant .gltf + .bin) ──────────────────── */
/* Writes a standards-conformant glTF 2.0 file with an external .bin buffer
   containing interleaved-free (separate) POSITION, NORMAL, TEXCOORD_0 and
   indices. Verifiable by any glTF validator / viewer. */
bool ccm_export_gltf(const CCModel* m, const char* path) {
    if (!m || !path || m->geom.vertex_count == 0) return false;

    /* Build the .bin path (same stem, .bin extension) */
    char binpath[1024];
    snprintf(binpath, sizeof(binpath), "%s", path);
    char* dot = strrchr(binpath, '.');
    if (dot) strcpy(dot, ".bin"); else strcat(binpath, ".bin");
    /* bin filename only (for the JSON uri) */
    const char* binname = strrchr(binpath, '/');
    binname = binname ? binname + 1 : binpath;

    uint32_t nv = m->geom.vertex_count;
    uint32_t ni = m->geom.index_count;

    /* Buffer layout: [positions nv*12][normals nv*12][uv nv*8][indices ni*4] */
    uint32_t off_pos = 0;
    uint32_t off_nrm = off_pos + nv*12;
    uint32_t off_uv  = off_nrm + nv*12;
    uint32_t off_idx = off_uv  + nv*8;
    uint32_t total   = off_idx + ni*4;

    uint8_t* buf = malloc(total);
    /* min/max for POSITION accessor (required by spec) */
    float pmin[3] = { 1e30f, 1e30f, 1e30f}, pmax[3] = {-1e30f,-1e30f,-1e30f};
    for (uint32_t i = 0; i < nv; i++) {
        const CCMVertex* v = &m->geom.vertices[i];
        memcpy(buf + off_pos + i*12, v->pos, 12);
        memcpy(buf + off_nrm + i*12, v->normal, 12);
        memcpy(buf + off_uv  + i*8,  v->uv, 8);
        for (int k=0;k<3;k++){ if(v->pos[k]<pmin[k])pmin[k]=v->pos[k]; if(v->pos[k]>pmax[k])pmax[k]=v->pos[k]; }
    }
    memcpy(buf + off_idx, m->geom.indices, ni*4);

    FILE* bf = fopen(binpath, "wb");
    if (!bf) { free(buf); return false; }
    fwrite(buf, 1, total, bf);
    fclose(bf);
    free(buf);

    /* Write the JSON */
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "{\n");
    fprintf(f, "  \"asset\": { \"version\": \"2.0\", \"generator\": \"Chlorlite ccm_export_gltf\" },\n");
    fprintf(f, "  \"scene\": 0,\n");
    fprintf(f, "  \"scenes\": [ { \"nodes\": [ 0 ] } ],\n");
    fprintf(f, "  \"nodes\": [ { \"mesh\": 0, \"name\": \"%s\" } ],\n", m->name);
    fprintf(f, "  \"meshes\": [ { \"primitives\": [ { \"attributes\": "
               "{ \"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2 }, \"indices\": 3, \"mode\": 4 } ] } ],\n");
    /* accessors: 0=pos,1=nrm,2=uv,3=idx */
    fprintf(f, "  \"accessors\": [\n");
    fprintf(f, "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": %u, \"type\": \"VEC3\", "
               "\"min\": [%.7g,%.7g,%.7g], \"max\": [%.7g,%.7g,%.7g] },\n",
               nv, pmin[0],pmin[1],pmin[2], pmax[0],pmax[1],pmax[2]);
    fprintf(f, "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": %u, \"type\": \"VEC3\" },\n", nv);
    fprintf(f, "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": %u, \"type\": \"VEC2\" },\n", nv);
    fprintf(f, "    { \"bufferView\": 3, \"componentType\": 5125, \"count\": %u, \"type\": \"SCALAR\" }\n", ni);
    fprintf(f, "  ],\n");
    /* bufferViews */
    fprintf(f, "  \"bufferViews\": [\n");
    fprintf(f, "    { \"buffer\": 0, \"byteOffset\": %u, \"byteLength\": %u, \"target\": 34962 },\n", off_pos, nv*12);
    fprintf(f, "    { \"buffer\": 0, \"byteOffset\": %u, \"byteLength\": %u, \"target\": 34962 },\n", off_nrm, nv*12);
    fprintf(f, "    { \"buffer\": 0, \"byteOffset\": %u, \"byteLength\": %u, \"target\": 34962 },\n", off_uv, nv*8);
    fprintf(f, "    { \"buffer\": 0, \"byteOffset\": %u, \"byteLength\": %u, \"target\": 34963 }\n", off_idx, ni*4);
    fprintf(f, "  ],\n");
    fprintf(f, "  \"buffers\": [ { \"uri\": \"%s\", \"byteLength\": %u } ]\n", binname, total);
    fprintf(f, "}\n");
    fclose(f);
    return true;
}

/* ─── glTF 2.0 import (real parser, no external deps) ─────────────────── */
/* Minimal but correct glTF reader: parses the JSON well enough to extract the
   first mesh's POSITION / NORMAL / TEXCOORD_0 attributes and indices, resolves
   accessors → bufferViews → the external .bin buffer, and rebuilds geometry.
   Handles float VEC3/VEC2 attributes and both u16/u32 indices. */

/* tiny JSON scanner helpers — find "key": and read the following int */
static const char* gltf_find_key(const char* s, const char* key) {
    char pat[128]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    return strstr(s, pat);
}
static long gltf_int_after(const char* s) {
    if (!s) return -1;
    while (*s && *s != ':') s++;
    if (*s != ':') return -1;
    s++;
    while (*s == ' ' || *s == '\t' || *s=='\n') s++;
    return strtol(s, NULL, 10);
}
/* Read accessor #idx fields we need. glTF accessors are an array; we scan to the
   idx-th "{...}" object inside "accessors":[ ... ]. */
static const char* gltf_nth_object(const char* array_start, int n) {
    const char* p = array_start;
    while (*p && *p != '[') p++;
    if (*p != '[') return NULL;
    p++;
    int depth = 0, count = 0;
    const char* obj_start = NULL;
    for (; *p; p++) {
        if (*p == '{') { if (depth == 0) obj_start = p; depth++; }
        else if (*p == '}') { depth--; if (depth == 0) { if (count == n) return obj_start; count++; } }
        else if (*p == ']' && depth == 0) break;
    }
    return NULL;
}

/* Detect and unpack a binary glTF (.glb) container. On success, allocates and
 * returns the embedded JSON (NUL-terminated) via *out_json and the BIN chunk via
 * *out_bin/*out_binsz (both malloc'd), and returns true. Returns false if the
 * data is not a .glb (caller should treat it as text .gltf). Spec: 12-byte header
 * (magic 0x46546C67 "glTF", uint32 version, uint32 total length), then chunks of
 * [uint32 length][uint32 type][bytes]; type 0x4E4F534A "JSON", 0x004E4942 "BIN\0". */
static bool gltf_unpack_glb(const uint8_t* data, long size,
                            char** out_json, uint8_t** out_bin, long* out_binsz) {
    if (!data || size < 12) return false;
    uint32_t magic, version, length;
    memcpy(&magic, data + 0, 4);
    memcpy(&version, data + 4, 4);
    memcpy(&length, data + 8, 4);
    if (magic != 0x46546C67u) return false;         /* not "glTF" */
    (void)version;
    long total = (length && (long)length <= size) ? (long)length : size;

    char*    json = NULL;
    uint8_t* bin  = NULL;  long binsz = 0;
    long p = 12;
    while (p + 8 <= total) {
        uint32_t clen, ctype;
        memcpy(&clen, data + p, 4);
        memcpy(&ctype, data + p + 4, 4);
        p += 8;
        if (p + (long)clen > total) break;          /* truncated chunk */
        if (ctype == 0x4E4F534Au && !json) {        /* "JSON" */
            json = (char*)malloc(clen + 1);
            if (!json) break;
            memcpy(json, data + p, clen);
            json[clen] = 0;
        } else if (ctype == 0x004E4942u && !bin) {  /* "BIN\0" */
            bin = (uint8_t*)malloc(clen ? clen : 1);
            if (!bin) break;
            memcpy(bin, data + p, clen);
            binsz = clen;
        }
        p += clen;
        p = (p + 3) & ~3L;                          /* chunks are 4-byte aligned */
    }
    if (!json) { free(json); free(bin); return false; }
    *out_json = json; *out_bin = bin; *out_binsz = binsz;
    return true;
}

CCModel* ccm_import_gltf(const char* path, const char* name) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ccm_import_gltf: cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* raw = malloc(fsz > 0 ? fsz : 1);
    if (!raw || fread(raw, 1, fsz, f) != (size_t)fsz) { fclose(f); free(raw); return NULL; }
    fclose(f);

    char*    json = NULL;
    uint8_t* bin  = NULL;  long bsz = 0;
    bool is_glb = gltf_unpack_glb(raw, fsz, &json, &bin, &bsz);
    if (is_glb) {
        /* embedded binary already extracted; raw no longer needed */
        free(raw);
    } else {
        /* text .gltf: `raw` is the JSON; resolve the external .bin by uri */
        json = malloc(fsz + 1);
        memcpy(json, raw, fsz); json[fsz] = 0;
        free(raw);

        const char* buffers = gltf_find_key(json, "buffers");
        char binpath[1024] = {0};
        if (buffers) {
            const char* uri = gltf_find_key(buffers, "uri");
            if (uri) {
                const char* q1 = strchr(uri, ':'); if (q1) q1 = strchr(q1, '"');
                if (q1) { q1++; const char* q2 = strchr(q1, '"');
                    if (q2) {
                        char dir[1024]; snprintf(dir, sizeof(dir), "%s", path);
                        char* slash = strrchr(dir, '/');
                        if (slash) { *(slash+1) = 0; snprintf(binpath, sizeof(binpath), "%s%.*s", dir, (int)(q2-q1), q1); }
                        else snprintf(binpath, sizeof(binpath), "%.*s", (int)(q2-q1), q1);
                    }
                }
            }
        }
        if (!binpath[0]) { fprintf(stderr, "ccm_import_gltf: no buffer uri\n"); free(json); return NULL; }
        FILE* bf = fopen(binpath, "rb");
        if (!bf) { fprintf(stderr, "ccm_import_gltf: cannot open bin %s\n", binpath); free(json); return NULL; }
        fseek(bf, 0, SEEK_END); bsz = ftell(bf); fseek(bf, 0, SEEK_SET);
        bin = malloc(bsz > 0 ? bsz : 1);
        if (fread(bin, 1, bsz, bf) != (size_t)bsz) { fclose(bf); free(bin); free(json); return NULL; }
        fclose(bf);
    }
    (void)bsz;

    /* Read the first mesh primitive's accessor indices */
    const char* meshes = gltf_find_key(json, "meshes");
    const char* prim = meshes ? gltf_find_key(meshes, "attributes") : NULL;
    if (!prim) { fprintf(stderr, "ccm_import_gltf: no mesh attributes\n"); free(bin); free(json); return NULL; }
    int acc_pos = (int)gltf_int_after(gltf_find_key(prim, "POSITION"));
    int acc_nrm = (int)gltf_int_after(gltf_find_key(prim, "NORMAL"));
    int acc_uv  = (int)gltf_int_after(gltf_find_key(prim, "TEXCOORD_0"));
    int acc_idx = (int)gltf_int_after(gltf_find_key(meshes, "indices"));

    const char* accessors  = gltf_find_key(json, "accessors");
    const char* bufferView = gltf_find_key(json, "bufferViews");
    if (acc_pos < 0 || acc_idx < 0 || !accessors || !bufferView) {
        fprintf(stderr, "ccm_import_gltf: missing POSITION/indices accessor\n"); free(bin); free(json); return NULL;
    }

    /* helper: for accessor n, return (count, componentType, byteOffset into bin) */
    #define ACCESSOR_INFO(n, out_count, out_ctype, out_off) do { \
        const char* ao = gltf_nth_object(accessors, (n)); \
        (out_count) = (uint32_t)gltf_int_after(gltf_find_key(ao, "count")); \
        (out_ctype) = (int)gltf_int_after(gltf_find_key(ao, "componentType")); \
        int bv = (int)gltf_int_after(gltf_find_key(ao, "bufferView")); \
        long acc_bo = gltf_int_after(gltf_find_key(ao, "byteOffset")); if (acc_bo < 0) acc_bo = 0; \
        const char* bvo = gltf_nth_object(bufferView, bv); \
        long bv_bo = gltf_int_after(gltf_find_key(bvo, "byteOffset")); if (bv_bo < 0) bv_bo = 0; \
        (out_off) = (uint32_t)(bv_bo + acc_bo); \
    } while(0)

    uint32_t nv=0, ci=0, uvcount=0, idxcount=0; int ct_pos=0, ct_idx=0, ct_nrm=0, ct_uv=0;
    uint32_t off_pos=0, off_nrm=0, off_uv=0, off_idx=0;
    ACCESSOR_INFO(acc_pos, nv, ct_pos, off_pos);
    ACCESSOR_INFO(acc_idx, idxcount, ct_idx, off_idx);
    if (acc_nrm >= 0) ACCESSOR_INFO(acc_nrm, ci, ct_nrm, off_nrm);
    if (acc_uv  >= 0) ACCESSOR_INFO(acc_uv, uvcount, ct_uv, off_uv);

    CCMVertex* verts = calloc(nv, sizeof(CCMVertex));
    for (uint32_t i = 0; i < nv; i++) {
        memcpy(verts[i].pos, bin + off_pos + i*12, 12);
        if (acc_nrm >= 0) memcpy(verts[i].normal, bin + off_nrm + i*12, 12);
        if (acc_uv  >= 0) memcpy(verts[i].uv, bin + off_uv + i*8, 8);
        verts[i].color[0]=verts[i].color[1]=verts[i].color[2]=verts[i].color[3]=255;
        verts[i].tangent[3]=1;
    }
    uint32_t* idx = malloc(idxcount * 4);
    if (ct_idx == 5125) {            /* UNSIGNED_INT */
        memcpy(idx, bin + off_idx, idxcount*4);
    } else if (ct_idx == 5123) {     /* UNSIGNED_SHORT */
        const uint16_t* s = (const uint16_t*)(bin + off_idx);
        for (uint32_t i=0;i<idxcount;i++) idx[i] = s[i];
    } else if (ct_idx == 5121) {     /* UNSIGNED_BYTE */
        const uint8_t* s = bin + off_idx;
        for (uint32_t i=0;i<idxcount;i++) idx[i] = s[i];
    }

    CCModel* m = ccm_model_new(name ? name : "gltf_model");
    ccm_set_geometry(m, verts, nv, idx, idxcount);
    if (acc_nrm < 0) ccm_compute_normals(m);
    ccm_compute_tangents(m);
    free(verts); free(idx); free(bin); free(json);
    #undef ACCESSOR_INFO
    return m;
}

bool ccm_export_obj(const CCModel* m, const char* path) {
    FILE* f=fopen(path,"w");
    if(!f)return false;
    fprintf(f,"# Chlorlite OBJ export — %s\n",m->name);
    for(uint32_t i=0;i<m->geom.vertex_count;i++)
        fprintf(f,"v %f %f %f\n",m->geom.vertices[i].pos[0],m->geom.vertices[i].pos[1],m->geom.vertices[i].pos[2]);
    for(uint32_t i=0;i<m->geom.vertex_count;i++)
        fprintf(f,"vt %f %f\n",m->geom.vertices[i].uv[0],m->geom.vertices[i].uv[1]);
    for(uint32_t i=0;i<m->geom.vertex_count;i++)
        fprintf(f,"vn %f %f %f\n",m->geom.vertices[i].normal[0],m->geom.vertices[i].normal[1],m->geom.vertices[i].normal[2]);
    for(uint32_t i=0;i<m->geom.index_count;i+=3)
        fprintf(f,"f %u/%u/%u %u/%u/%u %u/%u/%u\n",
            m->geom.indices[i]+1,m->geom.indices[i]+1,m->geom.indices[i]+1,
            m->geom.indices[i+1]+1,m->geom.indices[i+1]+1,m->geom.indices[i+1]+1,
            m->geom.indices[i+2]+1,m->geom.indices[i+2]+1,m->geom.indices[i+2]+1);
    fclose(f);
    return true;
}

int ccm_validate(const CCModel* m, CCMValidMsg* msgs, int max) {
    int n=0;
    #define MSG(is_err, fmt, ...) do{ if(n<max){snprintf(msgs[n].msg,256,fmt,##__VA_ARGS__);msgs[n].is_error=is_err;n++;}}while(0)
    if(!m->geom.vertices||!m->geom.vertex_count) MSG(true,"No geometry");
    if(!m->geom.indices||!m->geom.index_count)   MSG(true,"No index buffer");
    if(m->geom.index_count%3!=0) MSG(true,"Index count not multiple of 3");
    if(m->has_skin&&m->skin.vertex_count!=m->geom.vertex_count) MSG(true,"Skin vertex count mismatch: %u vs %u",m->skin.vertex_count,m->geom.vertex_count);
    if(m->has_skel&&!m->has_skin) MSG(false,"Has skeleton but no skin weights");
    for(uint32_t i=0;i<m->anim_count;i++) {
        if(m->anims[i].duration<=0) MSG(true,"Animation '%s' has zero/negative duration",m->anims[i].name);
    }
    #undef MSG
    return n;
}
