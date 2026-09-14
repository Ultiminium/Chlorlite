#include "cc/anim.h"
#include "cc/render.h"
typedef struct CCRenderer CCRenderer;
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════════
   MATH
   ══════════════════════════════════════════════════════════════════════ */

Vec3 cca_vec3_add(Vec3 a, Vec3 b)      { return (Vec3){a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 vec3_sub(Vec3 a, Vec3 b)      { return (Vec3){a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 cca_vec3_scale(Vec3 a, float s)   { return (Vec3){a.x*s,a.y*s,a.z*s}; }
float cca_vec3_dot(Vec3 a, Vec3 b)     { return a.x*b.x+a.y*b.y+a.z*b.z; }
float cca_vec3_len(Vec3 v)             { return sqrtf(cca_vec3_dot(v,v)); }
Vec3 cca_vec3_normalize(Vec3 v)        { float l=cca_vec3_len(v); return l>1e-7f?cca_vec3_scale(v,1/l):v; }
Vec3 cca_vec3_cross(Vec3 a, Vec3 b)    { return (Vec3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
Vec3 cca_vec3_lerp(Vec3 a, Vec3 b, float t){ return (Vec3){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t}; }

Quat cca_quat_identity(void) { return (Quat){0,0,0,1}; }
Quat cca_quat_normalize(Quat q) {
    float l=sqrtf(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(l<1e-7f) return cca_quat_identity();
    return (Quat){q.x/l,q.y/l,q.z/l,q.w/l};
}
Quat cca_quat_conjugate(Quat q) { return (Quat){-q.x,-q.y,-q.z,q.w}; }
Quat cca_quat_mul(Quat a, Quat b) {
    return (Quat){
        a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z
    };
}
Quat cca_quat_slerp(Quat a, Quat b, float t) {
    float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0){b.x=-b.x;b.y=-b.y;b.z=-b.z;b.w=-b.w;dot=-dot;}
    if(dot>0.9995f) return cca_quat_normalize((Quat){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});
    float theta0=acosf(dot), theta=theta0*t;
    float sa=sinf(theta)/sinf(theta0), sb=cosf(theta)-dot*sa;
    /* nb: sb is sin(theta)*cos(theta0)/sin(theta0) */
    float ia=sinf(theta0-theta)/sinf(theta0);
    float ib=sinf(theta)/sinf(theta0);
    return cca_quat_normalize((Quat){a.x*ia+b.x*ib,a.y*ia+b.y*ib,a.z*ia+b.z*ib,a.w*ia+b.w*ib});
    (void)sa;(void)sb;
}
Quat cca_quat_from_axis_angle(Vec3 axis, float rad) {
    float s=sinf(rad*0.5f);
    return cca_quat_normalize((Quat){axis.x*s,axis.y*s,axis.z*s,cosf(rad*0.5f)});
}
Quat cca_quat_from_euler_deg(float x, float y, float z) {
    float cx=cosf(x*0.00872665f),sx=sinf(x*0.00872665f); /* deg→rad/2 */
    float cy=cosf(y*0.00872665f),sy=sinf(y*0.00872665f);
    float cz=cosf(z*0.00872665f),sz=sinf(z*0.00872665f);
    return (Quat){
        sx*cy*cz+cx*sy*sz,
        cx*sy*cz-sx*cy*sz,
        cx*cy*sz+sx*sy*cz,
        cx*cy*cz-sx*sy*sz
    };
}
Vec3 cca_quat_rotate(Quat q, Vec3 v) {
    Vec3 u={q.x,q.y,q.z};
    float s=q.w;
    return cca_vec3_add(cca_vec3_add(cca_vec3_scale(u,2*cca_vec3_dot(u,v)),cca_vec3_scale(v,s*s-cca_vec3_dot(u,u))),cca_vec3_scale(cca_vec3_cross(u,v),2*s));
}

TRS cca_trs_identity(void) { return (TRS){.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; }
TRS cca_trs_lerp(TRS a, TRS b, float t) {
    return (TRS){
        .pos   = cca_vec3_lerp(a.pos,b.pos,t),
        .rot   = cca_quat_slerp(a.rot,b.rot,t),
        .scale = cca_vec3_lerp(a.scale,b.scale,t)
    };
}

void cca_mat4_identity(Mat4* m) {
    memset(m->m,0,64);
    m->m[0]=m->m[5]=m->m[10]=m->m[15]=1;
}
Mat4 cca_trs_to_mat4(TRS t) {
    Mat4 m; cca_mat4_identity(&m);
    float qx=t.rot.x,qy=t.rot.y,qz=t.rot.z,qw=t.rot.w;
    float sx=t.scale.x,sy=t.scale.y,sz=t.scale.z;
    m.m[0]=(1-2*(qy*qy+qz*qz))*sx; m.m[1]=2*(qx*qy+qz*qw)*sx;   m.m[2]=2*(qx*qz-qy*qw)*sx;
    m.m[4]=2*(qx*qy-qz*qw)*sy;   m.m[5]=(1-2*(qx*qx+qz*qz))*sy; m.m[6]=2*(qy*qz+qx*qw)*sy;
    m.m[8]=2*(qx*qz+qy*qw)*sz;   m.m[9]=2*(qy*qz-qx*qw)*sz;   m.m[10]=(1-2*(qx*qx+qy*qy))*sz;
    m.m[12]=t.pos.x; m.m[13]=t.pos.y; m.m[14]=t.pos.z; m.m[15]=1;
    return m;
}
void cca_mat4_mul(Mat4* out, const Mat4* a, const Mat4* b) {
    Mat4 tmp; memset(&tmp,0,sizeof(tmp));
    for(int r=0;r<4;r++) for(int c=0;c<4;c++) for(int k=0;k<4;k++)
        tmp.m[c*4+r]+=a->m[k*4+r]*b->m[c*4+k];
    *out=tmp;
}

/* ══════════════════════════════════════════════════════════════════════
   POSE
   ══════════════════════════════════════════════════════════════════════ */

CCPose* cc_pose_new(uint16_t bone_count) {
    CCPose* p=malloc(sizeof(CCPose));
    p->bone_count=bone_count;
    p->bones=malloc(bone_count*sizeof(TRS));
    for(uint16_t i=0;i<bone_count;i++) p->bones[i]=cca_trs_identity();
    return p;
}
void cc_pose_free(CCPose* p) { if(p){free(p->bones);free(p);} }
void cc_pose_copy(CCPose* dst, const CCPose* src) {
    uint16_t n=dst->bone_count<src->bone_count?dst->bone_count:src->bone_count;
    memcpy(dst->bones,src->bones,n*sizeof(TRS));
}
CCPose* cc_pose_bind(const CCMSkelChunk* skel) {
    CCPose* p=cc_pose_new(skel->bone_count);
    for(uint16_t i=0;i<skel->bone_count;i++) {
        CCMBone* b=&skel->bones[i];
        p->bones[i].pos=(Vec3){b->bind_pos[0],b->bind_pos[1],b->bind_pos[2]};
        p->bones[i].rot=(Quat){b->bind_rot[0],b->bind_rot[1],b->bind_rot[2],b->bind_rot[3]};
        p->bones[i].scale=(Vec3){b->bind_scale[0],b->bind_scale[1],b->bind_scale[2]};
    }
    return p;
}

void cc_pose_compute_global(const CCMSkelChunk* skel, const CCPose* local, Mat4* global) {
    for(uint16_t i=0;i<skel->bone_count;i++) {
        Mat4 lm=cca_trs_to_mat4(local->bones[i]);
        uint16_t par=skel->bones[i].parent;
        if(par==CCM_BONE_NO_PARENT||par>=skel->bone_count) global[i]=lm;
        else cca_mat4_mul(&global[i],&global[par],&lm);
    }
}

void cc_pose_compute_skinning(const CCMSkelChunk* skel, const Mat4* global, Mat4* skinning) {
    for(uint16_t i=0;i<skel->bone_count;i++) {
        /* skinning[i] = global[i] × inv_bind_mat[i] */
        Mat4 inv; memcpy(inv.m,skel->bones[i].inv_bind_mat,64);
        cca_mat4_mul(&skinning[i],&global[i],&inv);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   KEYFRAME SAMPLING
   ══════════════════════════════════════════════════════════════════════ */

float ccm_track_sample(const CCMTrack* track, float time) {
    if(!track->key_count) return 0;
    if(track->key_count==1) return track->keys[0].value;
    /* Find bracket */
    uint32_t lo=0,hi=track->key_count-1;
    for(uint32_t i=0;i<track->key_count-1;i++){if(track->keys[i+1].time>time){lo=i;hi=i+1;break;}}
    CCMKeyframe* ka=&track->keys[lo], *kb=&track->keys[hi];
    if(ka->time>=kb->time) return ka->value;
    float t=(time-ka->time)/(kb->time-ka->time);
    switch(track->interp) {
        case CCM_INTERP_STEP:   return ka->value;
        case CCM_INTERP_LINEAR: return ka->value+(kb->value-ka->value)*t;
        case CCM_INTERP_CUBIC: {
            /* Cubic Hermite */
            float t2=t*t,t3=t2*t;
            float h00=2*t3-3*t2+1, h10=t3-2*t2+t, h01=-2*t3+3*t2, h11=t3-t2;
            float dt=kb->time-ka->time;
            return h00*ka->value+h10*dt*ka->out_tan+h01*kb->value+h11*dt*kb->in_tan;
        }
    }
    return ka->value;
}

void cc_anim_sample(const CCMAnimChunk* clip, const CCMSkelChunk* skel,
                    float time, CCPose* out) {
    /* Start from bind pose */
    for(uint16_t i=0;i<out->bone_count&&i<skel->bone_count;i++) {
        CCMBone* b=&skel->bones[i];
        out->bones[i].pos=(Vec3){b->bind_pos[0],b->bind_pos[1],b->bind_pos[2]};
        out->bones[i].rot=(Quat){b->bind_rot[0],b->bind_rot[1],b->bind_rot[2],b->bind_rot[3]};
        out->bones[i].scale=(Vec3){b->bind_scale[0],b->bind_scale[1],b->bind_scale[2]};
    }
    /* Apply tracks */
    for(uint32_t ti=0;ti<clip->track_count;ti++) {
        const CCMTrack* tr=&clip->tracks[ti];
        uint16_t bi=tr->bone_index;
        if(bi>=out->bone_count) continue;
        float v=ccm_track_sample(tr,time);
        switch(tr->target) {
            case CCM_TRACK_POS_X: out->bones[bi].pos.x=v; break;
            case CCM_TRACK_POS_Y: out->bones[bi].pos.y=v; break;
            case CCM_TRACK_POS_Z: out->bones[bi].pos.z=v; break;
            case CCM_TRACK_ROT_X: out->bones[bi].rot.x=v; break;
            case CCM_TRACK_ROT_Y: out->bones[bi].rot.y=v; break;
            case CCM_TRACK_ROT_Z: out->bones[bi].rot.z=v; break;
            case CCM_TRACK_ROT_W: out->bones[bi].rot.w=v; break;
            case CCM_TRACK_SCALE_X: out->bones[bi].scale.x=v; break;
            case CCM_TRACK_SCALE_Y: out->bones[bi].scale.y=v; break;
            case CCM_TRACK_SCALE_Z: out->bones[bi].scale.z=v; break;
            default: break;
        }
    }
    /* Normalize all rotations */
    for(uint16_t i=0;i<out->bone_count;i++)
        out->bones[i].rot=cca_quat_normalize(out->bones[i].rot);
}

/* ══════════════════════════════════════════════════════════════════════
   BLEND TREE
   ══════════════════════════════════════════════════════════════════════ */

CCBlendNode* cc_blend_clip(CCMAnimChunk* clip, float speed, bool loop) {
    CCBlendNode* n=calloc(1,sizeof(CCBlendNode));
    n->type=CC_BLEND_CLIP; n->clip=clip; n->clip_speed=speed; n->clip_loop=loop;
    return n;
}
CCBlendNode* cc_blend_lerp(CCBlendNode* a, CCBlendNode* b, float w) {
    CCBlendNode* n=calloc(1,sizeof(CCBlendNode));
    n->type=CC_BLEND_LERP; n->child_a=a; n->child_b=b; n->weight=w;
    return n;
}
CCBlendNode* cc_blend_additive(CCBlendNode* base, CCBlendNode* delta) {
    CCBlendNode* n=calloc(1,sizeof(CCBlendNode));
    n->type=CC_BLEND_ADDITIVE; n->child_a=base; n->child_b=delta;
    return n;
}
CCBlendNode* cc_blend_1d(CCMAnimChunk** clips, float* thresholds, uint32_t nc) {
    CCBlendNode* n=calloc(1,sizeof(CCBlendNode));
    n->type=CC_BLEND_1D; n->n_clips=nc;
    n->clips_1d=malloc(nc*sizeof(void*)); memcpy(n->clips_1d,clips,nc*sizeof(void*));
    n->thresholds=malloc(nc*4); memcpy(n->thresholds,thresholds,nc*4);
    n->times_1d=calloc(nc,4);
    return n;
}

CCBlendNode* cc_blend_2d(CCMAnimChunk** clips, float* xs, float* ys, uint32_t nc) {
    CCBlendNode* n=calloc(1,sizeof(CCBlendNode));
    n->type=CC_BLEND_2D; n->n_clips=nc;
    n->clips_1d=malloc(nc*sizeof(void*)); memcpy(n->clips_1d,clips,nc*sizeof(void*));
    n->params_2d_x=malloc(nc*4); memcpy(n->params_2d_x,xs,nc*4);
    n->params_2d_y=malloc(nc*4); memcpy(n->params_2d_y,ys,nc*4);
    n->times_1d=calloc(nc,4);
    return n;
}

CCBlendNode* cc_blend_mask(CCBlendNode* child, const bool* bone_mask, uint16_t count) {
    CCBlendNode* n=calloc(1,sizeof(CCBlendNode));
    n->type=CC_BLEND_MASK; n->child_a=child;
    n->bone_mask=malloc(count*sizeof(bool));
    memcpy(n->bone_mask,bone_mask,count*sizeof(bool));
    return n;
}

static void blend_eval_internal(CCBlendNode* node, const CCMSkelChunk* skel,
                                  float dt, CCPose* a, CCPose* b, CCPose* out);

void cc_blend_evaluate(CCBlendNode* node, const CCMSkelChunk* skel,
                        float dt, CCPose* out_pose) {
    if(!node||!skel||!out_pose) return;
    uint16_t bn=skel->bone_count;
    switch(node->type) {
        case CC_BLEND_CLIP:
            node->clip_time+=dt*node->clip_speed;
            if(node->clip_loop && node->clip->duration>0)
                while(node->clip_time>node->clip->duration) node->clip_time-=node->clip->duration;
            cc_anim_sample(node->clip,skel,node->clip_time,out_pose);
            break;
        case CC_BLEND_LERP: {
            CCPose* pa=cc_pose_new(bn), *pb=cc_pose_new(bn);
            cc_blend_evaluate(node->child_a,skel,dt,pa);
            cc_blend_evaluate(node->child_b,skel,dt,pb);
            for(uint16_t i=0;i<bn;i++) out_pose->bones[i]=cca_trs_lerp(pa->bones[i],pb->bones[i],node->weight);
            cc_pose_free(pa); cc_pose_free(pb);
            break;
        }
        case CC_BLEND_ADDITIVE: {
            CCPose* base=cc_pose_new(bn), *delta=cc_pose_new(bn);
            cc_blend_evaluate(node->child_a,skel,dt,base);
            cc_blend_evaluate(node->child_b,skel,dt,delta);
            for(uint16_t i=0;i<bn;i++) {
                out_pose->bones[i].pos=cca_vec3_add(base->bones[i].pos,delta->bones[i].pos);
                out_pose->bones[i].rot=cca_quat_mul(base->bones[i].rot,delta->bones[i].rot);
                out_pose->bones[i].scale=cca_vec3_add(base->bones[i].scale,delta->bones[i].scale);
            }
            cc_pose_free(base); cc_pose_free(delta);
            break;
        }
        case CC_BLEND_1D: {
            /* Find two adjacent clips bracketing param_1d */
            float p=node->param_1d;
            uint32_t lo=0;
            for(uint32_t i=0;i<node->n_clips-1;i++){if(node->thresholds[i+1]>p){lo=i;break;}}
            uint32_t hi=lo+1<node->n_clips?lo+1:lo;
            float range=node->thresholds[hi]-node->thresholds[lo];
            float t=range>1e-6f?(p-node->thresholds[lo])/range:0;
            t=t<0?0:t>1?1:t;
            /* Advance both clip times */
            node->times_1d[lo]+=dt; node->times_1d[hi]+=dt;
            if(node->clips_1d[lo]->duration>0) while(node->times_1d[lo]>node->clips_1d[lo]->duration) node->times_1d[lo]-=node->clips_1d[lo]->duration;
            if(node->clips_1d[hi]->duration>0) while(node->times_1d[hi]>node->clips_1d[hi]->duration) node->times_1d[hi]-=node->clips_1d[hi]->duration;
            CCPose* pa=cc_pose_new(bn),*pb=cc_pose_new(bn);
            cc_anim_sample(node->clips_1d[lo],skel,node->times_1d[lo],pa);
            cc_anim_sample(node->clips_1d[hi],skel,node->times_1d[hi],pb);
            for(uint16_t i=0;i<bn;i++) out_pose->bones[i]=cca_trs_lerp(pa->bones[i],pb->bones[i],t);
            cc_pose_free(pa); cc_pose_free(pb);
            break;
        }
        case CC_BLEND_MASK: {
            /* Evaluate child; keep it only on masked bones, else bind pose. */
            CCPose* pc=cc_pose_new(bn);
            cc_blend_evaluate(node->child_a,skel,dt,pc);
            for(uint16_t i=0;i<bn;i++){
                if(node->bone_mask && node->bone_mask[i]) out_pose->bones[i]=pc->bones[i];
                else {
                    CCMBone* b=&skel->bones[i];
                    out_pose->bones[i].pos=(Vec3){b->bind_pos[0],b->bind_pos[1],b->bind_pos[2]};
                    out_pose->bones[i].rot=(Quat){b->bind_rot[0],b->bind_rot[1],b->bind_rot[2],b->bind_rot[3]};
                    out_pose->bones[i].scale=(Vec3){b->bind_scale[0],b->bind_scale[1],b->bind_scale[2]};
                }
            }
            cc_pose_free(pc);
            break;
        }
        case CC_BLEND_2D: {
            /* Inverse-distance weighted blend of all clips by (x,y) proximity to
               (param_2d_x, param_2d_y). Gradient-band style, robust + simple. */
            if(node->n_clips==0) break;
            float wsum=0; float* w=alloca(node->n_clips*sizeof(float));
            for(uint32_t i=0;i<node->n_clips;i++){
                float dx=node->params_2d_x[i]-node->param_2d_x;
                float dy=node->params_2d_y[i]-node->param_2d_y;
                float d2=dx*dx+dy*dy;
                w[i]=1.0f/(d2+1e-4f);   /* closer clip → larger weight */
                wsum+=w[i];
            }
            /* accumulate weighted pose */
            for(uint16_t b=0;b<bn;b++){ out_pose->bones[b].pos=(Vec3){0,0,0};
                out_pose->bones[b].scale=(Vec3){0,0,0}; out_pose->bones[b].rot=(Quat){0,0,0,0}; }
            for(uint32_t i=0;i<node->n_clips;i++){
                float wi=w[i]/wsum;
                node->times_1d[i]+=dt;
                if(node->clips_1d[i]->duration>0)
                    while(node->times_1d[i]>node->clips_1d[i]->duration) node->times_1d[i]-=node->clips_1d[i]->duration;
                CCPose* p=cc_pose_new(bn);
                cc_anim_sample(node->clips_1d[i],skel,node->times_1d[i],p);
                for(uint16_t b=0;b<bn;b++){
                    out_pose->bones[b].pos=cca_vec3_add(out_pose->bones[b].pos,cca_vec3_scale(p->bones[b].pos,wi));
                    out_pose->bones[b].scale=cca_vec3_add(out_pose->bones[b].scale,cca_vec3_scale(p->bones[b].scale,wi));
                    /* nlerp accumulation for rotation (weighted sum + normalize at end) */
                    Quat q=p->bones[b].rot;
                    /* hemisphere align to first non-zero */
                    if(out_pose->bones[b].rot.w!=0||out_pose->bones[b].rot.x!=0){
                        if(cca_vec3_dot((Vec3){q.x,q.y,q.z},(Vec3){out_pose->bones[b].rot.x,out_pose->bones[b].rot.y,out_pose->bones[b].rot.z})
                           + q.w*out_pose->bones[b].rot.w < 0){ q.x=-q.x;q.y=-q.y;q.z=-q.z;q.w=-q.w; }
                    }
                    out_pose->bones[b].rot.x+=q.x*wi; out_pose->bones[b].rot.y+=q.y*wi;
                    out_pose->bones[b].rot.z+=q.z*wi; out_pose->bones[b].rot.w+=q.w*wi;
                }
                cc_pose_free(p);
            }
            for(uint16_t b=0;b<bn;b++) out_pose->bones[b].rot=cca_quat_normalize(out_pose->bones[b].rot);
            break;
        }
    }
}

static void blend_eval_internal(CCBlendNode* node, const CCMSkelChunk* skel,
                                  float dt, CCPose* a, CCPose* b, CCPose* out) {
    (void)a;(void)b; cc_blend_evaluate(node,skel,dt,out);
}

void cc_blend_node_free(CCBlendNode* n) {
    if(!n) return;
    cc_blend_node_free(n->child_a);
    cc_blend_node_free(n->child_b);
    free(n->clips_1d); free(n->thresholds); free(n->times_1d); free(n->bone_mask);
    free(n);
}

/* ══════════════════════════════════════════════════════════════════════
   ANIMATION STATE MACHINE
   ══════════════════════════════════════════════════════════════════════ */

CCAnimStateMachine* cc_asm_new(const CCMSkelChunk* skel) {
    CCAnimStateMachine* sm=calloc(1,sizeof(CCAnimStateMachine));
    sm->skel=skel;
    sm->pose_current=cc_pose_new(skel->bone_count);
    sm->pose_next   =cc_pose_new(skel->bone_count);
    sm->pose_out    =cc_pose_new(skel->bone_count);
    return sm;
}

void cc_asm_free(CCAnimStateMachine* sm) {
    if(!sm) return;
    cc_pose_free(sm->pose_current);
    cc_pose_free(sm->pose_next);
    cc_pose_free(sm->pose_out);
    /* Note: states and transitions are user-managed */
    free(sm->params);
    free(sm);
}

CCASMState* cc_asm_add_state(CCAnimStateMachine* sm, const char* name, CCBlendNode* tree) {
    sm->states=realloc(sm->states,(sm->state_count+1)*sizeof(CCASMState));
    CCASMState* s=&sm->states[sm->state_count++];
    memset(s,0,sizeof(*s));
    strncpy(s->name,name,63);
    s->blend_tree=tree; s->speed=1.0f;
    if(!sm->current) { sm->current=s; }
    return s;
}

void cc_asm_set_entry(CCAnimStateMachine* sm, const char* name) {
    for(uint32_t i=0;i<sm->state_count;i++)
        if(!strcmp(sm->states[i].name,name)){sm->current=&sm->states[i];return;}
}

static CCASMParam* find_param(CCAnimStateMachine* sm, const char* name) {
    for(uint32_t i=0;i<sm->param_count;i++)
        if(!strcmp(sm->params[i].name,name)) return &sm->params[i];
    sm->params=realloc(sm->params,(sm->param_count+1)*sizeof(CCASMParam));
    CCASMParam* p=&sm->params[sm->param_count++];
    memset(p,0,sizeof(*p));
    strncpy(p->name,name,63);
    return p;
}

void cc_asm_set_float  (CCAnimStateMachine* sm, const char* p, float v)  { find_param(sm,p)->value=v; }
void cc_asm_set_bool   (CCAnimStateMachine* sm, const char* p, bool v)   { find_param(sm,p)->value=v?1:0; }
void cc_asm_set_trigger(CCAnimStateMachine* sm, const char* p)            { CCASMParam* pp=find_param(sm,p); pp->is_trigger=true; pp->triggered=true; }

void cc_asm_add_transition(CCAnimStateMachine* sm, const char* from, const char* to,
                            float exit_time, float fade_dur, bool has_exit_time) {
    CCASMState* fs=NULL, *ts=NULL;
    for(uint32_t i=0;i<sm->state_count;i++){
        if(!strcmp(sm->states[i].name,from)) fs=&sm->states[i];
        if(!strcmp(sm->states[i].name,to))   ts=&sm->states[i];
    }
    if(!fs||!ts) return;
    fs->transitions=realloc(fs->transitions,(fs->trans_count+1)*sizeof(CCASMTransition));
    CCASMTransition* t=&fs->transitions[fs->trans_count++];
    memset(t,0,sizeof(*t));
    t->target=ts; t->exit_time=exit_time; t->fade_duration=fade_dur; t->has_exit_time=has_exit_time;
}

void cc_asm_add_condition(CCAnimStateMachine* sm, const char* from, const char* to,
                           const char* param, CCConditionOp op, float value) {
    CCASMState* fs=NULL;
    for(uint32_t i=0;i<sm->state_count;i++) if(!strcmp(sm->states[i].name,from)){fs=&sm->states[i];break;}
    if(!fs) return;
    /* Find the existing transition to 'to' or create one */
    CCASMTransition* trans=NULL;
    for(uint32_t i=0;i<fs->trans_count;i++)
        if(!strcmp(fs->transitions[i].target->name,to)){trans=&fs->transitions[i];break;}
    if(!trans){cc_asm_add_transition(sm,from,to,1.0f,0.2f,false);trans=&fs->transitions[fs->trans_count-1];}
    trans->conditions=realloc(trans->conditions,(trans->cond_count+1)*sizeof(CCTransitionCondition));
    CCTransitionCondition* c=&trans->conditions[trans->cond_count++];
    c->param_name=param; c->op=op; c->value=value;
}

static bool eval_condition(CCAnimStateMachine* sm, const CCTransitionCondition* c) {
    CCASMParam* p=find_param(sm,c->param_name);
    switch(c->op){
        case CC_COND_GREATER:   return p->value>c->value;
        case CC_COND_LESS:      return p->value<c->value;
        case CC_COND_EQUAL:     return fabsf(p->value-c->value)<0.0001f;
        case CC_COND_NOT_EQUAL: return fabsf(p->value-c->value)>=0.0001f;
        case CC_COND_TRIGGER:   if(p->triggered){p->triggered=false;return true;}return false;
    }
    return false;
}

void cc_asm_update(CCAnimStateMachine* sm, float dt) {
    if(!sm->current) return;

    /* Evaluate current state */
    if(sm->current->blend_tree)
        cc_blend_evaluate(sm->current->blend_tree,sm->skel,dt*sm->current->speed,sm->pose_current);

    /* Check transitions */
    if(!sm->next) {
        for(uint32_t ti=0;ti<sm->current->trans_count;ti++) {
            CCASMTransition* tr=&sm->current->transitions[ti];
            bool ok=true;
            for(uint32_t ci=0;ci<tr->cond_count;ci++)
                if(!eval_condition(sm,&tr->conditions[ci])){ok=false;break;}
            if(ok){
                sm->next=tr->target;
                sm->fade_dur=tr->fade_duration>0?tr->fade_duration:0.001f;
                sm->fade_t=0;
                break;
            }
        }
    }

    /* Crossfade */
    if(sm->next) {
        if(sm->next->blend_tree)
            cc_blend_evaluate(sm->next->blend_tree,sm->skel,dt*sm->next->speed,sm->pose_next);
        sm->fade_t+=dt/sm->fade_dur;
        if(sm->fade_t>=1.0f) {
            cc_pose_copy(sm->pose_out,sm->pose_next);
            sm->current=sm->next; sm->next=NULL; sm->fade_t=0;
        } else {
            for(uint16_t i=0;i<sm->skel->bone_count;i++)
                sm->pose_out->bones[i]=cca_trs_lerp(sm->pose_current->bones[i],sm->pose_next->bones[i],sm->fade_t);
        }
    } else {
        cc_pose_copy(sm->pose_out,sm->pose_current);
    }
}

const CCPose* cc_asm_pose(CCAnimStateMachine* sm) { return sm->pose_out; }
const char* cc_asm_current_state(CCAnimStateMachine* sm) { return sm->current?sm->current->name:""; }

/* ══════════════════════════════════════════════════════════════════════
   IK SOLVERS
   ══════════════════════════════════════════════════════════════════════ */

void cc_ik_solve_limb(CCIKLimb* limb, Vec3 target,
                       const Mat4* global_in, Mat4* global_out,
                       CCPose* pose_out, const CCMSkelChunk* skel) {
    /* Two-bone analytic IK */
    Mat4* root_m = &global_out[limb->root_bone];
    *root_m = global_in[limb->root_bone];

    Vec3 root_pos = {root_m->m[12],root_m->m[13],root_m->m[14]};
    float la=limb->chain_len_a, lb=limb->chain_len_b;
    float total=la+lb;

    Vec3 to_target=vec3_sub(target,root_pos);
    float dist=cca_vec3_len(to_target);
    if(dist<0.0001f) return;
    if(dist>total*0.9999f) dist=total*0.9999f;

    /* Law of cosines: angle at root */
    float cos_a=(dist*dist+la*la-lb*lb)/(2*dist*la);
    cos_a=cos_a<-1?-1:cos_a>1?1:cos_a;
    float angle_a=acosf(cos_a);

    Vec3 dir=cca_vec3_normalize(to_target);
    Vec3 pole=limb->use_pole?cca_vec3_normalize(vec3_sub(limb->pole_target,root_pos)):(Vec3){0,1,0};
    Vec3 right=cca_vec3_normalize(cca_vec3_cross(dir,pole));
    Vec3 up=cca_vec3_cross(right,dir);

    /* Root rotation */
    Vec3 mid_dir={(float)(cosf(angle_a)*dir.x+sinf(angle_a)*up.x),
                  (float)(cosf(angle_a)*dir.y+sinf(angle_a)*up.y),
                  (float)(cosf(angle_a)*dir.z+sinf(angle_a)*up.z)};
    Vec3 mid_pos=cca_vec3_add(root_pos,cca_vec3_scale(mid_dir,la));

    /* Write mid bone global */
    global_out[limb->mid_bone]=global_in[limb->mid_bone];
    global_out[limb->mid_bone].m[12]=mid_pos.x;
    global_out[limb->mid_bone].m[13]=mid_pos.y;
    global_out[limb->mid_bone].m[14]=mid_pos.z;

    /* Write end bone */
    global_out[limb->end_bone]=global_in[limb->end_bone];
    global_out[limb->end_bone].m[12]=target.x;
    global_out[limb->end_bone].m[13]=target.y;
    global_out[limb->end_bone].m[14]=target.z;

    /* Back-solve local rotations */
    (void)pose_out; (void)skel;
}

void cc_ik_solve_fabrik(CCIKFABRIK* ik, Vec3 target,
                          const Mat4* global_in, Mat4* global_out,
                          CCPose* pose_out, const CCMSkelChunk* skel) {
    if(!ik->chain_len) return;
    Vec3* positions=malloc(ik->chain_len*sizeof(Vec3));
    float* lengths=malloc(ik->chain_len*4);
    for(uint32_t i=0;i<ik->chain_len;i++){
        positions[i]=(Vec3){global_in[ik->bones[i]].m[12],global_in[ik->bones[i]].m[13],global_in[ik->bones[i]].m[14]};
        lengths[i]=i>0?cca_vec3_len(vec3_sub(positions[i],positions[i-1])):0;
    }
    Vec3 root=positions[0];
    for(uint32_t iter=0;iter<ik->max_iter;iter++){
        /* Forward pass */
        positions[ik->chain_len-1]=target;
        for(int i=(int)ik->chain_len-2;i>=0;i--){
            Vec3 dir=cca_vec3_normalize(vec3_sub(positions[i],positions[i+1]));
            positions[i]=cca_vec3_add(positions[i+1],cca_vec3_scale(dir,lengths[i+1]));
        }
        /* Backward pass */
        positions[0]=root;
        for(uint32_t i=1;i<ik->chain_len;i++){
            Vec3 dir=cca_vec3_normalize(vec3_sub(positions[i],positions[i-1]));
            positions[i]=cca_vec3_add(positions[i-1],cca_vec3_scale(dir,lengths[i]));
        }
        if(cca_vec3_len(vec3_sub(positions[ik->chain_len-1],target))<ik->tolerance) break;
    }
    for(uint32_t i=0;i<ik->chain_len;i++){
        global_out[ik->bones[i]]=global_in[ik->bones[i]];
        global_out[ik->bones[i]].m[12]=positions[i].x;
        global_out[ik->bones[i]].m[13]=positions[i].y;
        global_out[ik->bones[i]].m[14]=positions[i].z;
    }
    free(positions); free(lengths);
    (void)pose_out; (void)skel;
}

void cc_ik_solve_fullbody(CCFullBodyIK* fbik, CCPose* pose, const CCMSkelChunk* skel) {
    uint16_t bn=skel->bone_count;
    Mat4* global=malloc(bn*sizeof(Mat4));
    Mat4* out=malloc(bn*sizeof(Mat4));
    cc_pose_compute_global(skel,pose,global);
    memcpy(out,global,bn*sizeof(Mat4));
    if(fbik->w_left_hand>0.01f)  cc_ik_solve_limb(&fbik->left_arm,  fbik->left_hand_target,  global,out,pose,skel);
    if(fbik->w_right_hand>0.01f) cc_ik_solve_limb(&fbik->right_arm, fbik->right_hand_target, global,out,pose,skel);
    if(fbik->w_left_foot>0.01f)  cc_ik_solve_limb(&fbik->left_leg,  fbik->left_foot_target,  global,out,pose,skel);
    if(fbik->w_right_foot>0.01f) cc_ik_solve_limb(&fbik->right_leg, fbik->right_foot_target, global,out,pose,skel);
    free(global); free(out);
}

/* ══════════════════════════════════════════════════════════════════════
   FACIAL CONTROLLER
   ══════════════════════════════════════════════════════════════════════ */

CCFacialController* cc_facial_new(const CCMBshpChunk* bshp) {
    CCFacialController* fc=calloc(1,sizeof(CCFacialController));
    fc->bshp=bshp;
    fc->shape_weights=calloc(bshp->shape_count,4);
    return fc;
}
void cc_facial_free(CCFacialController* fc) {
    if(!fc) return;
    free(fc->params); free(fc->shape_weights); free(fc);
}
void cc_facial_set(CCFacialController* fc, const char* param, float weight) {
    /* Find or create param */
    for(uint32_t i=0;i<fc->param_count;i++){
        if(!strcmp(fc->params[i].param_name,param)){fc->params[i].target_weight=weight;return;}
    }
    fc->params=realloc(fc->params,(fc->param_count+1)*sizeof(CCFacialParam));
    CCFacialParam* p=&fc->params[fc->param_count++];
    memset(p,0,sizeof(*p));
    strncpy(p->param_name,param,63);
    p->target_weight=weight; p->speed=5.0f;
    /* Find shape index */
    for(uint32_t i=0;i<fc->bshp->shape_count;i++){
        if(!strcmp(fc->bshp->shapes[i].name,param)){p->shape_index=i;break;}
    }
}
void cc_facial_update(CCFacialController* fc, float dt) {
    for(uint32_t i=0;i<fc->param_count;i++){
        CCFacialParam* p=&fc->params[i];
        float diff=p->target_weight-p->weight;
        float step=p->speed*dt;
        if(fabsf(diff)<step) p->weight=p->target_weight;
        else p->weight+=step*(diff>0?1:-1);
        fc->shape_weights[p->shape_index]=p->weight;
    }
}
void cc_facial_apply(const CCFacialController* fc, const CCMBshpChunk* bshp,
                      const CCMVertex* base, uint32_t nv, CCMVertex* out) {
    memcpy(out,base,nv*sizeof(CCMVertex));
    for(uint32_t si=0;si<bshp->shape_count;si++){
        float w=fc->shape_weights[si]; if(w<0.0001f) continue;
        const CCMBlendShape* bs=&bshp->shapes[si];
        for(uint32_t di=0;di<bs->delta_count;di++){
            const CCMBlendDelta* d=&bs->deltas[di];
            if(d->vertex_index>=nv) continue;
            out[d->vertex_index].pos[0]+=d->delta_pos[0]*w;
            out[d->vertex_index].pos[1]+=d->delta_pos[1]*w;
            out[d->vertex_index].pos[2]+=d->delta_pos[2]*w;
            out[d->vertex_index].normal[0]+=d->delta_normal[0]*w;
            out[d->vertex_index].normal[1]+=d->delta_normal[1]*w;
            out[d->vertex_index].normal[2]+=d->delta_normal[2]*w;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   ANIMATOR COMPONENT
   ══════════════════════════════════════════════════════════════════════ */

CCAnimator* cc_animator_new(struct CCEngine* eng, const CCModel* model) {
    CCAnimator* a=calloc(1,sizeof(CCAnimator));
    a->model=model;
    a->state_machine=cc_asm_new(&model->skel);
    if(model->has_bshp) a->facial=cc_facial_new(&model->bshp);
    /* Add default states for each animation */
    for(uint32_t i=0;i<model->anim_count;i++) {
        CCBlendNode* node=cc_blend_clip(&model->anims[i],1.0f,model->anims[i].looping);
        cc_asm_add_state(a->state_machine,model->anims[i].name,node);
    }
    (void)eng;
    return a;
}
void cc_animator_free(CCAnimator* a) {
    if(!a) return;
    cc_asm_free(a->state_machine);
    if(a->facial) cc_facial_free(a->facial);
    free(a);
}
void cc_animator_update(CCAnimator* a, float dt) {
    cc_asm_update(a->state_machine,dt);
    if(a->facial) cc_facial_update(a->facial,dt);
    if(a->fbik_enabled&&a->fbik) cc_ik_solve_fullbody(a->fbik,(CCPose*)cc_asm_pose(a->state_machine),&a->model->skel);
}
void cc_animator_play(CCAnimator* a, const char* state)    { cc_asm_set_entry(a->state_machine,state); }
void cc_animator_set_speed(CCAnimator* a, float speed)     { cc_asm_set_float(a->state_machine,"speed",speed); }
void cc_animator_set_facial(CCAnimator* a, const char* p, float w) { if(a->facial) cc_facial_set(a->facial,p,w); }
void cc_animator_set_look_target(CCAnimator* a, Vec3 t)    { if(a->fbik)a->fbik->look_target=t; }
void cc_animator_set_foot_target(CCAnimator* a, bool left, Vec3 t) {
    if(!a->fbik)return;
    if(left)a->fbik->left_foot_target=t;else a->fbik->right_foot_target=t;
    if(left)a->fbik->w_left_foot=1;else a->fbik->w_right_foot=1;
}
void cc_animator_set_hand_target(CCAnimator* a, bool left, Vec3 t) {
    if(!a->fbik)return;
    if(left)a->fbik->left_hand_target=t;else a->fbik->right_hand_target=t;
    if(left)a->fbik->w_left_hand=1;else a->fbik->w_right_hand=1;
}
/* Compute the current skinning-matrix palette from the animator's live pose.
   out must hold at least model->skel.bone_count Mat4. Returns the count. */
uint32_t cc_animator_skinning_matrices(CCAnimator* a, Mat4* out) {
    if (!a || !a->model || !a->model->has_skel) return 0;
    const CCMSkelChunk* skel = &a->model->skel;
    const CCPose* pose = cc_asm_pose(a->state_machine);
    if (!pose) return 0;
    Mat4 global[256];
    uint16_t n = skel->bone_count < 256 ? skel->bone_count : 256;
    cc_pose_compute_global(skel, pose, global);
    cc_pose_compute_skinning(skel, global, out);
    return n;
}

void cc_animator_draw(CCAnimator* a, struct CCEngine* eng, const Mat4* model_mat) {
    if (!a || !eng || !a->model) return;
    extern CCRenderer* cc_engine_renderer(struct CCEngine*);
    extern void cc_renderer_set_bones(CCRenderer*, const float*, uint32_t);
    CCRenderer* r = cc_engine_renderer(eng);
    if (!r) return;

    /* Compute + upload the skinning palette so any subsequent skinned draw uses
       this animator's current pose. */
    Mat4 palette[256];
    uint32_t n = cc_animator_skinning_matrices(a, palette);
    if (n == 0) {
        /* No skeleton: upload identity so the skin shader is a no-op passthrough */
        Mat4 ident; cca_mat4_identity(&ident);
        cc_renderer_set_bones(r, ident.m, 1);
        return;
    }
    cc_renderer_set_bones(r, (const float*)palette, n);
    (void)model_mat; /* model transform is applied at the draw_skinned call site */
}

/* ══════════════════════════════════════════════════════════════════════
   SKINNED MESH RENDERER (CCSkin)
   Thin wrapper over the engine's working skinned-draw path: builds a GPU mesh
   from a CCModel's geometry, attaches its joint/weight skin data, and drives
   cc_renderer_set_bones + cc_renderer_draw_skinned. This is the drawable the
   animator/state-machine feed their computed skinning palette into.
   ══════════════════════════════════════════════════════════════════════ */

struct CCSkin {
    struct CCEngine* eng;
    const CCModel*   model;
    CCMesh           mesh;         /* GPU mesh handle (with skin attached) */
    CCMaterial       material;     /* default material slot */
    uint32_t         bone_count;
};

CCSkin* cc_skin_create(struct CCEngine* eng, const CCModel* model) {
    if (!eng || !model) return NULL;
    extern CCRenderer* cc_engine_renderer(struct CCEngine*);
    extern CCMesh cc_renderer_mesh_create(CCRenderer*, const CCVertex*, uint32_t,
                                          const uint32_t*, uint32_t, CCMeshUsage);
    extern void cc_renderer_mesh_attach_skin(CCRenderer*, CCMesh, const uint16_t*,
                                             const float*, uint32_t);
    extern CCMaterial cc_renderer_material_create(CCRenderer*, const CCMaterialDesc*);
    CCRenderer* r = cc_engine_renderer(eng);
    if (!r) return NULL;
    const CCMGeomChunk* g = &model->geom;
    if (!g->vertices || !g->vertex_count) return NULL;

    /* CCMVertex and CCVertex share an identical field layout (pos/normal/uv/
       tangent/color) — copy straight across into the renderer vertex format. */
    CCVertex* verts = malloc(g->vertex_count * sizeof(CCVertex));
    for (uint32_t i=0;i<g->vertex_count;i++) {
        const CCMVertex* s=&g->vertices[i]; CCVertex* d=&verts[i];
        memcpy(d->pos,s->pos,12); memcpy(d->normal,s->normal,12);
        memcpy(d->uv,s->uv,8);    memcpy(d->tangent,s->tangent,16);
        memcpy(d->color,s->color,4);
    }
    CCSkin* sk = calloc(1,sizeof(CCSkin));
    sk->eng=eng; sk->model=model;
    sk->mesh = cc_renderer_mesh_create(r, verts, g->vertex_count,
                                       g->indices, g->index_count, CC_MESH_STATIC);
    free(verts);

    /* Attach skin: expand CCMSkinVertex (joint[4]/weight[4]) into the parallel
       joints/weights arrays the renderer expects. */
    if (model->has_skin && model->skin.weights && model->skin.vertex_count==g->vertex_count) {
        uint16_t* joints = malloc(g->vertex_count*4*sizeof(uint16_t));
        float*    weights= malloc(g->vertex_count*4*sizeof(float));
        for (uint32_t i=0;i<g->vertex_count;i++) {
            const CCMSkinVertex* w=&model->skin.weights[i];
            for (int k=0;k<4;k++){ joints[i*4+k]=w->joint[k]; weights[i*4+k]=w->weight[k]; }
        }
        cc_renderer_mesh_attach_skin(r, sk->mesh, joints, weights, g->vertex_count);
        free(joints); free(weights);
    }
    sk->bone_count = model->has_skel ? model->skel.bone_count : 0;

    CCMaterialDesc md; memset(&md,0,sizeof md);
    md.base_color[0]=md.base_color[1]=md.base_color[2]=md.base_color[3]=1.0f;
    md.roughness=0.6f; md.metallic=0.0f;
    sk->material = cc_renderer_material_create(r, &md);
    return sk;
}

void cc_skin_destroy(CCSkin* s) { if (s) free(s); }

void cc_skin_update(CCSkin* s, const Mat4* skinning_mats, uint32_t bone_count) {
    if (!s || !skinning_mats) return;
    extern CCRenderer* cc_engine_renderer(struct CCEngine*);
    extern void cc_renderer_set_bones(CCRenderer*, const float*, uint32_t);
    CCRenderer* r = cc_engine_renderer(s->eng);
    if (r) cc_renderer_set_bones(r, (const float*)skinning_mats, bone_count);
}

void cc_skin_draw(CCSkin* s, struct CCEngine* eng, uint32_t material_slot,
                  const Mat4* model_matrix) {
    if (!s || !eng) return;
    extern CCRenderer* cc_engine_renderer(struct CCEngine*);
    extern void cc_renderer_draw_skinned(CCRenderer*, CCMesh, CCMaterial, const CCTransform3D*);
    CCRenderer* r = cc_engine_renderer(eng);
    if (!r) return;
    (void)material_slot;
    /* Decompose the incoming model matrix into a TRS transform. We only need
       translation + the (assumed uniform) scale + rotation for the draw call. */
    CCTransform3D xf; memset(&xf,0,sizeof xf);
    if (model_matrix) {
        xf.pos[0]=model_matrix->m[12]; xf.pos[1]=model_matrix->m[13]; xf.pos[2]=model_matrix->m[14];
        /* extract scale from column lengths */
        float sx=cca_vec3_len((Vec3){model_matrix->m[0],model_matrix->m[1],model_matrix->m[2]});
        float sy=cca_vec3_len((Vec3){model_matrix->m[4],model_matrix->m[5],model_matrix->m[6]});
        float sz=cca_vec3_len((Vec3){model_matrix->m[8],model_matrix->m[9],model_matrix->m[10]});
        xf.scale[0]=sx; xf.scale[1]=sy; xf.scale[2]=sz;
        /* rotation quaternion from the normalized upper 3x3 */
        Mat4 rm=*model_matrix;
        if(sx>1e-6f){rm.m[0]/=sx;rm.m[1]/=sx;rm.m[2]/=sx;}
        if(sy>1e-6f){rm.m[4]/=sy;rm.m[5]/=sy;rm.m[6]/=sy;}
        if(sz>1e-6f){rm.m[8]/=sz;rm.m[9]/=sz;rm.m[10]/=sz;}
        float t=rm.m[0]+rm.m[5]+rm.m[10];
        Quat q;
        if(t>0){float k=sqrtf(t+1.0f)*2.0f; q.w=0.25f*k;
            q.x=(rm.m[6]-rm.m[9])/k; q.y=(rm.m[8]-rm.m[2])/k; q.z=(rm.m[1]-rm.m[4])/k;}
        else { q=(Quat){0,0,0,1}; }
        xf.rot[0]=q.x; xf.rot[1]=q.y; xf.rot[2]=q.z; xf.rot[3]=q.w;
    } else { xf.rot[3]=1; xf.scale[0]=xf.scale[1]=xf.scale[2]=1; }
    cc_renderer_draw_skinned(r, s->mesh, s->material, &xf);
}

void cc_skin_draw_posed(CCSkin* s, struct CCEngine* eng,
                        const CCPose* pose, const CCMSkelChunk* skel,
                        const Mat4* model_matrix) {
    if (!s || !eng || !pose || !skel) return;
    Mat4 global[256], palette[256];
    cc_pose_compute_global(skel, pose, global);
    cc_pose_compute_skinning(skel, global, palette);
    cc_skin_update(s, palette, skel->bone_count);
    cc_skin_draw(s, eng, 0, model_matrix);
}

void cc_skin_apply_facial(CCSkin* s, const float* shape_weights, uint32_t shape_count) {
    /* Apply weighted blend-shape (morph target) deltas to the base geometry and
       re-upload. Each shape contributes weight_i * delta to the vertices it
       touches; the mesh is rebuilt from the morphed vertices (the old GPU mesh
       is destroyed to avoid a leak) and the skin re-attached. */
    if (!s || !s->model || !shape_weights) return;
    const CCModel* model = s->model;
    if (!model->has_bshp || model->bshp.shape_count==0) return;
    const CCMGeomChunk* g = &model->geom;
    if (!g->vertices || !g->vertex_count) return;

    extern CCRenderer* cc_engine_renderer(struct CCEngine*);
    extern CCMesh cc_renderer_mesh_create(CCRenderer*, const CCVertex*, uint32_t, const uint32_t*, uint32_t, CCMeshUsage);
    extern void  cc_renderer_mesh_destroy(CCRenderer*, CCMesh);
    extern void  cc_renderer_mesh_attach_skin(CCRenderer*, CCMesh, const uint16_t*, const float*, uint32_t);
    CCRenderer* r = cc_engine_renderer(s->eng);
    if (!r) return;

    /* start from base geometry */
    CCVertex* verts = malloc(g->vertex_count * sizeof(CCVertex));
    for (uint32_t i=0;i<g->vertex_count;i++){ const CCMVertex* sv=&g->vertices[i]; CCVertex* d=&verts[i];
        memcpy(d->pos,sv->pos,12); memcpy(d->normal,sv->normal,12); memcpy(d->uv,sv->uv,8);
        memcpy(d->tangent,sv->tangent,16); memcpy(d->color,sv->color,4); }

    /* accumulate weighted deltas from each active shape */
    uint32_t nshapes = shape_count < model->bshp.shape_count ? shape_count : model->bshp.shape_count;
    for (uint32_t si=0; si<nshapes; si++){
        float w = shape_weights[si]; if (w==0.0f) continue;
        const CCMBlendShape* bs=&model->bshp.shapes[si];
        for (uint32_t d=0; d<bs->delta_count; d++){
            const CCMBlendDelta* dd=&bs->deltas[d];
            if (dd->vertex_index>=g->vertex_count) continue;
            CCVertex* v=&verts[dd->vertex_index];
            v->pos[0]+=dd->delta_pos[0]*w; v->pos[1]+=dd->delta_pos[1]*w; v->pos[2]+=dd->delta_pos[2]*w;
            v->normal[0]+=dd->delta_normal[0]*w; v->normal[1]+=dd->delta_normal[1]*w; v->normal[2]+=dd->delta_normal[2]*w;
        }
    }

    /* rebuild + re-skin, swap handle */
    CCMesh newmesh = cc_renderer_mesh_create(r, verts, g->vertex_count, g->indices, g->index_count, CC_MESH_DYNAMIC);
    free(verts);
    if (model->has_skin && model->skin.weights && model->skin.vertex_count==g->vertex_count){
        uint16_t* joints=malloc(g->vertex_count*4*sizeof(uint16_t));
        float* weights=malloc(g->vertex_count*4*sizeof(float));
        for (uint32_t i=0;i<g->vertex_count;i++){ const CCMSkinVertex* wv=&model->skin.weights[i];
            for(int k=0;k<4;k++){ joints[i*4+k]=wv->joint[k]; weights[i*4+k]=wv->weight[k]; } }
        cc_renderer_mesh_attach_skin(r, newmesh, joints, weights, g->vertex_count);
        free(joints); free(weights);
    }
    if (s->mesh) cc_renderer_mesh_destroy(r, s->mesh);
    s->mesh = newmesh;
}
