#pragma once
/*
 * Chlorlite Animation System
 *
 * Stack:
 *   CCPose            — local-space transforms for all bones
 *   CCAnimSampler     — sample a CCMAnimChunk at a given time → CCPose
 *   CCBlendTree       — weighted blend of N poses
 *   CCAnimStateMachine— state graph driving blend tree
 *   CCIKSolver        — two-bone IK + FABRIK chain + full-body IK
 *   CCFacialController— blend shape parameter → weight mapping
 *   CCSkinRenderer    — GPU bone UBO upload + skinned draw call
 */

#include "ccmodel.h"

/* Forward declaration — the full CCEngine is defined in the engine core.
   Declared at file scope so the pointer type matches across all TUs. */
struct CCEngine;
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Math types (column-major) ──────────────────────────────────────── */
typedef struct { float x,y,z; }       Vec3;
typedef struct { float x,y,z,w; }     Quat;
typedef struct { Vec3 pos; Quat rot; Vec3 scale; } TRS;
typedef struct { float m[16]; }       Mat4;

Vec3 cca_vec3_add(Vec3 a, Vec3 b);
Vec3 cca_vec3_scale(Vec3 a, float s);
Vec3 cca_vec3_lerp(Vec3 a, Vec3 b, float t);
Vec3 cca_vec3_normalize(Vec3 v);
float cca_vec3_dot(Vec3 a, Vec3 b);
Vec3 cca_vec3_cross(Vec3 a, Vec3 b);
float cca_vec3_len(Vec3 v);

Quat cca_quat_identity(void);
Quat cca_quat_from_axis_angle(Vec3 axis, float rad);
Quat cca_quat_from_euler_deg(float x, float y, float z);
Quat cca_quat_mul(Quat a, Quat b);
Quat cca_quat_slerp(Quat a, Quat b, float t);
Quat cca_quat_normalize(Quat q);
Quat cca_quat_conjugate(Quat q);
Vec3 cca_quat_rotate(Quat q, Vec3 v);
void cca_quat_to_mat4(Quat q, Mat4* out);

TRS  cca_trs_identity(void);
TRS  cca_trs_lerp(TRS a, TRS b, float t);   /* lerp pos+scale, slerp rot */
Mat4 cca_trs_to_mat4(TRS t);
void cca_mat4_mul(Mat4* out, const Mat4* a, const Mat4* b);
void cca_mat4_identity(Mat4* m);
void cca_mat4_inverse(Mat4* out, const Mat4* m);

/* ─── Pose ───────────────────────────────────────────────────────────── */
/* One TRS per bone, in local (parent) space */
typedef struct {
    uint16_t bone_count;
    TRS*     bones;         /* [bone_count] */
} CCPose;

CCPose* cc_pose_new(uint16_t bone_count);
void    cc_pose_free(CCPose* p);
void    cc_pose_copy(CCPose* dst, const CCPose* src);
CCPose* cc_pose_bind(const CCMSkelChunk* skel);  /* pose = bind pose */
void    cc_pose_compute_global(const CCMSkelChunk* skel, const CCPose* local,
                                Mat4* out_global_mats);  /* [bone_count] */
void    cc_pose_compute_skinning(const CCMSkelChunk* skel, const Mat4* global_mats,
                                  Mat4* out_skinning_mats);  /* inv_bind × global */

/* ─── Keyframe evaluation ────────────────────────────────────────────── */
float ccm_track_sample(const CCMTrack* track, float time);

/* Sample a full animation clip into a pose */
void cc_anim_sample(const CCMAnimChunk* clip, const CCMSkelChunk* skel,
                    float time, CCPose* out_pose);

/* ─── Blend tree ─────────────────────────────────────────────────────── */
typedef enum CCBlendNodeType {
    CC_BLEND_CLIP    = 0,   /* leaf: one animation clip */
    CC_BLEND_LERP    = 1,   /* blend A and B by weight */
    CC_BLEND_ADDITIVE= 2,   /* add delta pose on top of base */
    CC_BLEND_MASK    = 3,   /* apply child only to masked bones */
    CC_BLEND_1D      = 4,   /* 1D blend space (e.g. speed → walk/run) */
    CC_BLEND_2D      = 5,   /* 2D blend space (direction + speed) */
} CCBlendNodeType;

typedef struct CCBlendNode CCBlendNode;
struct CCBlendNode {
    CCBlendNodeType type;
    /* Leaf */
    CCMAnimChunk*  clip;
    float          clip_time;
    float          clip_speed;
    bool           clip_loop;
    /* Internal */
    CCBlendNode*   child_a;
    CCBlendNode*   child_b;
    float          weight;    /* 0=A, 1=B for LERP */
    /* Mask */
    bool*          bone_mask; /* [bone_count] — true = apply child */
    /* 1D blend space */
    float*         thresholds;   /* [n_clips] */
    CCMAnimChunk** clips_1d;     /* [n_clips] */
    float*         times_1d;
    uint32_t       n_clips;
    float          param_1d;     /* e.g. speed 0..1 */
    /* 2D blend space */
    float*         params_2d_x;  /* [n_clips] */
    float*         params_2d_y;
    float          param_2d_x;
    float          param_2d_y;
};

CCBlendNode* cc_blend_clip(CCMAnimChunk* clip, float speed, bool loop);
CCBlendNode* cc_blend_lerp(CCBlendNode* a, CCBlendNode* b, float weight);
CCBlendNode* cc_blend_additive(CCBlendNode* base, CCBlendNode* delta);
CCBlendNode* cc_blend_mask(CCBlendNode* child, const bool* bone_mask, uint16_t count);
CCBlendNode* cc_blend_1d(CCMAnimChunk** clips, float* thresholds, uint32_t n);
CCBlendNode* cc_blend_2d(CCMAnimChunk** clips, float* xs, float* ys, uint32_t n);
void         cc_blend_evaluate(CCBlendNode* node, const CCMSkelChunk* skel,
                                float dt, CCPose* out_pose);
void         cc_blend_node_free(CCBlendNode* node);

/* ─── Animation state machine ────────────────────────────────────────── */
typedef enum CCConditionOp {
    CC_COND_GREATER, CC_COND_LESS, CC_COND_EQUAL,
    CC_COND_NOT_EQUAL, CC_COND_TRIGGER,
} CCConditionOp;

typedef struct {
    const char*  param_name;
    CCConditionOp op;
    float        value;
} CCTransitionCondition;

typedef struct CCASMState CCASMState;
typedef struct CCASMTransition {
    CCASMState*          target;
    float                exit_time;    /* 0-1, normalized clip time — -1 = ignore */
    float                fade_duration;/* crossfade seconds */
    bool                 has_exit_time;
    uint32_t             cond_count;
    CCTransitionCondition* conditions;
} CCASMTransition;

struct CCASMState {
    char           name[64];
    CCBlendNode*   blend_tree;
    uint32_t       trans_count;
    CCASMTransition* transitions;
    float          speed;
};

typedef struct {
    char    name[64];
    float   value;
    bool    is_trigger;
    bool    triggered;
} CCASMParam;

typedef struct CCAnimStateMachine {
    CCASMState*   states;
    uint32_t      state_count;
    CCASMState*   current;
    CCASMState*   next;           /* during crossfade */
    float         fade_t;         /* 0-1 crossfade progress */
    float         fade_dur;
    CCASMParam*   params;
    uint32_t      param_count;
    const CCMSkelChunk* skel;
    CCPose*       pose_current;
    CCPose*       pose_next;
    CCPose*       pose_out;
} CCAnimStateMachine;

CCAnimStateMachine* cc_asm_new(const CCMSkelChunk* skel);
void cc_asm_free(CCAnimStateMachine* sm);
CCASMState* cc_asm_add_state(CCAnimStateMachine* sm, const char* name, CCBlendNode* tree);
void cc_asm_add_transition(CCAnimStateMachine* sm, const char* from, const char* to,
                            float exit_time, float fade_dur, bool has_exit_time);
void cc_asm_add_condition(CCAnimStateMachine* sm, const char* from, const char* to,
                           const char* param, CCConditionOp op, float value);
void cc_asm_set_entry(CCAnimStateMachine* sm, const char* state_name);
void cc_asm_set_float(CCAnimStateMachine* sm, const char* param, float value);
void cc_asm_set_bool(CCAnimStateMachine* sm, const char* param, bool value);
void cc_asm_set_trigger(CCAnimStateMachine* sm, const char* param);
void cc_asm_update(CCAnimStateMachine* sm, float dt);
const CCPose* cc_asm_pose(CCAnimStateMachine* sm);
const char* cc_asm_current_state(CCAnimStateMachine* sm);

/* ─── Inverse kinematics ─────────────────────────────────────────────── */

/* Two-bone IK — limb (arm/leg) */
typedef struct {
    uint16_t root_bone;   /* shoulder / hip */
    uint16_t mid_bone;    /* elbow / knee */
    uint16_t end_bone;    /* wrist / ankle */
    float    chain_len_a; /* root → mid */
    float    chain_len_b; /* mid → end */
    Vec3     pole_target; /* hint for bend direction */
    bool     use_pole;
} CCIKLimb;

void cc_ik_solve_limb(CCIKLimb* limb, Vec3 target_world,
                       const Mat4* global_in, Mat4* global_out,
                       CCPose* pose_out, const CCMSkelChunk* skel);

/* FABRIK — arbitrary bone chain */
typedef struct {
    uint16_t* bones;      /* [chain_len] from root to tip */
    uint32_t  chain_len;
    uint32_t  max_iter;   /* default 10 */
    float     tolerance;  /* default 0.001 */
} CCIKFABRIK;

void cc_ik_solve_fabrik(CCIKFABRIK* ik, Vec3 target_world,
                         const Mat4* global_in, Mat4* global_out,
                         CCPose* pose_out, const CCMSkelChunk* skel);

/* Full-body IK (FBIK) — grounded feet, hand targets, spine */
typedef struct {
    CCIKLimb  left_arm, right_arm;
    CCIKLimb  left_leg, right_leg;
    uint16_t  spine_bones[8];
    uint32_t  spine_count;
    uint16_t  head_bone;
    /* Targets (world space) */
    Vec3      left_hand_target,  right_hand_target;
    Vec3      left_foot_target,  right_foot_target;
    Vec3      look_target;
    /* Weights (0 = ignore, 1 = full) */
    float     w_left_hand, w_right_hand;
    float     w_left_foot, w_right_foot;
    float     w_look;
} CCFullBodyIK;

void cc_ik_solve_fullbody(CCFullBodyIK* fbik, CCPose* pose,
                           const CCMSkelChunk* skel);

/* ─── Facial controller ──────────────────────────────────────────────── */
/* Maps named float parameters to blend shape weights */
typedef struct {
    char    param_name[64];  /* "smile", "brow_raise_l", "jaw_open" */
    uint32_t shape_index;    /* index into CCMBshpChunk.shapes */
    float    weight;         /* 0-1 */
    float    target_weight;  /* animated target */
    float    speed;          /* blend speed units/sec */
} CCFacialParam;

typedef struct CCFacialController {
    CCFacialParam*  params;
    uint32_t        param_count;
    const CCMBshpChunk* bshp;
    float*          shape_weights;  /* [bshp->shape_count] — output */
} CCFacialController;

CCFacialController* cc_facial_new(const CCMBshpChunk* bshp);
void cc_facial_free(CCFacialController* fc);
void cc_facial_set(CCFacialController* fc, const char* param, float weight);
void cc_facial_update(CCFacialController* fc, float dt);
/* Apply facial deltas to a base vertex buffer */
void cc_facial_apply(const CCFacialController* fc, const CCMBshpChunk* bshp,
                      const CCMVertex* base_verts, uint32_t vert_count,
                      CCMVertex* out_verts);

/* ─── Skinned mesh renderer ──────────────────────────────────────────── */
typedef struct CCSkin CCSkin;

/* Upload skinned model to GPU */
CCSkin* cc_skin_create(struct CCEngine* eng, const CCModel* model);
void    cc_skin_destroy(CCSkin* s);

/* Per-frame: upload computed skinning matrices, draw */
void cc_skin_update(CCSkin* s, const Mat4* skinning_mats, uint32_t bone_count);
void cc_skin_draw(CCSkin* s, struct CCEngine* eng, uint32_t material_slot,
                   const Mat4* model_matrix);

/* Full pipeline: pose → skinning matrices → GPU upload → draw */
void cc_skin_draw_posed(CCSkin* s, struct CCEngine* eng,
                         const CCPose* pose, const CCMSkelChunk* skel,
                         const Mat4* model_matrix);

/* Apply facial deltas on GPU (texture-based morphing) */
void cc_skin_apply_facial(CCSkin* s, const float* shape_weights,
                           uint32_t shape_count);

/* ─── Animation component (ECS integration) ──────────────────────────── */
typedef struct CCAnimator {
    CCAnimStateMachine* state_machine;
    CCFacialController* facial;
    CCFullBodyIK*       fbik;          /* NULL if not using full-body IK */
    CCSkin*             skin;
    const CCModel*      model;
    bool                fbik_enabled;
} CCAnimator;

CCAnimator* cc_animator_new(struct CCEngine* eng, const CCModel* model);
void        cc_animator_free(CCAnimator* a);
void        cc_animator_update(CCAnimator* a, float dt);
void        cc_animator_draw(CCAnimator* a, struct CCEngine* eng, const Mat4* model_mat);
uint32_t    cc_animator_skinning_matrices(CCAnimator* a, Mat4* out);  /* fill palette, return count */

/* Shortcuts for common animation commands */
void cc_animator_play(CCAnimator* a, const char* state);
void cc_animator_set_speed(CCAnimator* a, float speed);
void cc_animator_set_look_target(CCAnimator* a, Vec3 target);
void cc_animator_set_foot_target(CCAnimator* a, bool left, Vec3 target);
void cc_animator_set_hand_target(CCAnimator* a, bool left, Vec3 target);
void cc_animator_set_facial(CCAnimator* a, const char* param, float weight);

#ifdef __cplusplus
}
#endif
