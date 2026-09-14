#pragma once
/*
 * camera.h — Chlorlite camera system
 *
 * Camera types:
 *   Perspective  — standard 3D
 *   Orthographic — 2D or isometric
 *
 * Camera controllers:
 *   Free         — WASD + mouse look (FPS)
 *   Orbit        — rotate around target point
 *   Follow       — lag-follow a point/entity
 *   Cinematic    — keyframed path, splines, FOV animation
 *   Fixed        — static, no controller
 *
 * Camera animation:
 *   Linear, bezier cubic, Catmull-Rom spline paths
 *   Ease in/out, smooth-step
 *   Look-at tracking while on path
 *   Shake (trauma system)
 *   Dolly zoom (Hitchcock)
 */

#include "cc/ccmath.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCEngine CCEngine;

/* ─── Camera state ───────────────────────────────────────────────────── */
typedef enum CCCameraType {
    CC_CAM_PERSPECTIVE  = 0,
    CC_CAM_ORTHOGRAPHIC = 1,
} CCCameraType;

typedef struct {
    /* Position & orientation */
    CCVec3 position;
    CCQuat rotation;          /* world-space orientation */

    /* Projection */
    CCCameraType type;
    float fov_deg;            /* perspective: vertical FOV */
    float ortho_size;         /* ortho: half-height in world units */
    float near_plane;
    float far_plane;
    float aspect;             /* width/height, auto-set from window */
    float exposure;           /* tone mapping exposure */

    /* Derived (computed every frame, read-only) */
    CCMat4 view;
    CCMat4 proj;
    CCMat4 view_proj;
    CCMat4 inv_view_proj;
    CCVec3 forward;           /* camera forward direction */
    CCVec3 right;
    CCVec3 up;
    CCFrustum frustum;
} CCCamera;

/* ─── Camera controller types ────────────────────────────────────────── */
typedef enum CCCamControllerType {
    CC_CAM_CTRL_NONE     = 0,
    CC_CAM_CTRL_FREE,        /* prewired example: WASD+mouse fly            */
    CC_CAM_CTRL_ORBIT,       /* prewired example: orbit a target           */
    CC_CAM_CTRL_FOLLOW,      /* prewired example: chase a moving target    */
    CC_CAM_CTRL_CINEMATIC,   /* prewired example: keyframed path           */
    CC_CAM_CTRL_CUSTOM,      /* YOUR controller: cc_cam_use_custom(...)     */
} CCCamControllerType;

/* Free camera (FPS-style) */
typedef struct {
    float move_speed;
    float sprint_mult;
    float mouse_sensitivity;
    float yaw, pitch;         /* current Euler angles (degrees) */
    bool  invert_y;
    bool  fly_mode;           /* true = no gravity constraint */
} CCFreeCamCtrl;

/* Orbit camera */
typedef struct {
    CCVec3 target;
    float  yaw, pitch;        /* degrees */
    float  distance;
    float  min_pitch, max_pitch;
    float  min_dist, max_dist;
    float  orbit_speed;
    float  zoom_speed;
    float  smooth_factor;     /* 0=instant, 1=never arrives */
    /* Internal smoothed state */
    CCVec3 _target_smooth;
    float  _yaw_smooth, _pitch_smooth, _dist_smooth;
} CCOrbitCamCtrl;

/* Follow camera */
typedef struct {
    CCVec3* target_pos;       /* pointer to entity position (updated externally) */
    CCVec3* target_forward;
    CCVec3  offset;           /* local offset from target */
    float   lag_pos;          /* 0=instant, 1=never catches up */
    float   lag_rot;
    float   look_ahead;       /* how far ahead of target to look */
    CCVec3  _pos_smooth;
    CCQuat  _rot_smooth;
} CCFollowCamCtrl;

/* ─── Camera path (spline) ───────────────────────────────────────────── */
#define CC_CAM_MAX_KEYFRAMES 128

typedef enum CCCamEase {
    CC_EASE_LINEAR     = 0,
    CC_EASE_SMOOTH     = 1,   /* smoothstep */
    CC_EASE_IN         = 2,
    CC_EASE_OUT        = 3,
    CC_EASE_IN_OUT     = 4,
    CC_EASE_CUBIC      = 5,   /* bezier */
} CCCamEase;

typedef struct {
    float   time;             /* seconds from path start */
    CCVec3  position;
    CCQuat  rotation;
    float   fov_deg;          /* 0 = inherit from previous */
    float   exposure;
    CCCamEase ease;
    /* Look-at override (if look_target is set, rotation is ignored) */
    bool    has_look_at;
    CCVec3  look_at;
} CCCamKeyframe;

typedef struct {
    CCCamKeyframe keyframes[CC_CAM_MAX_KEYFRAMES];
    uint32_t      keyframe_count;
    float         total_duration;
    bool          loop;
    bool          use_catmull_rom;  /* smooth through all keyframes */
    /* Playback state */
    float         current_time;
    bool          playing;
    bool          finished;
} CCCamPath;

/* ─── Shake system ───────────────────────────────────────────────────── */
typedef struct {
    float trauma;             /* 0-1, decays each frame */
    float trauma_decay;       /* per second */
    float max_offset;         /* world units */
    float max_angle_deg;
    float frequency;          /* shake frequency Hz */
    float _seed;
} CCCamShake;

/* A custom camera controller: called every cc_camera_update with the rig, the
 * engine (for input), dt, and your state. Position/orient rig->cam however you
 * want (cc_cam_set_position / cc_cam_look_at / etc.). This is how you write a
 * camera behavior the engine doesn't ship — a lagging horror shoulder-cam, a
 * security-cam sweep, a spectator drone, a rail shooter path. */
typedef struct CCCameraRig CCCameraRig;
typedef void (*CCCamControllerFn)(CCCameraRig* rig, CCEngine* eng, float dt, void* state);

/* ─── Full camera rig ────────────────────────────────────────────────── */
typedef struct CCCameraRig {
    CCCamera              cam;
    CCCamControllerType   ctrl_type;
    union {
        CCFreeCamCtrl  free_ctrl;
        CCOrbitCamCtrl orbit_ctrl;
        CCFollowCamCtrl follow_ctrl;
    };
    CCCamPath  path;
    CCCamShake shake;
    bool       active;
    /* custom controller (ctrl_type == CC_CAM_CTRL_CUSTOM) */
    CCCamControllerFn custom_fn;
    void*             custom_state;
} CCCameraRig;

/* ─── Lifecycle ──────────────────────────────────────────────────────── */
CCCameraRig* cc_camera_create(void);
void         cc_camera_destroy(CCCameraRig* rig);

/* Per-frame update — call before drawing */
void cc_camera_update(CCCameraRig* rig, CCEngine* eng, float dt);

/* Apply this camera to the renderer (uploads view/proj to shaders) */
void cc_camera_apply(CCCameraRig* rig, CCEngine* eng);

/* ─── Camera setup helpers ───────────────────────────────────────────── */
void cc_cam_set_perspective(CCCameraRig* r, float fov_deg, float near, float far);
void cc_cam_set_ortho(CCCameraRig* r, float size, float near, float far);
void cc_cam_set_position(CCCameraRig* r, CCVec3 pos);
void cc_cam_set_rotation(CCCameraRig* r, CCQuat rot);
void cc_cam_look_at(CCCameraRig* r, CCVec3 target, CCVec3 up);
void cc_cam_set_fov(CCCameraRig* r, float fov_deg);

/* ─── Controllers ────────────────────────────────────────────────────── */
void cc_cam_use_free(CCCameraRig* r, float move_speed, float mouse_sens);
void cc_cam_use_orbit(CCCameraRig* r, CCVec3 target, float distance,
                      float yaw_deg, float pitch_deg);
void cc_cam_use_follow(CCCameraRig* r, CCVec3* target_pos,
                       CCVec3* target_fwd, CCVec3 offset, float lag);
void cc_cam_use_path(CCCameraRig* r);   /* switch to cinematic path mode */

/* Install YOUR OWN camera controller: `fn` runs every cc_camera_update with your
 * `state`. Switches the rig to CC_CAM_CTRL_CUSTOM. Pass fn=NULL to detach. The
 * engine still applies shake + uploads the result — you just drive the base
 * position/orientation. This is the open path; the use_free/orbit/follow/path
 * above are optional prewired examples. */
void cc_cam_use_custom(CCCameraRig* r, CCCamControllerFn fn, void* state);

/* Orbit control */
void cc_cam_orbit_set_target(CCCameraRig* r, CCVec3 target);
void cc_cam_orbit_set_distance(CCCameraRig* r, float dist);
void cc_cam_orbit_add_yaw(CCCameraRig* r, float deg);
void cc_cam_orbit_add_pitch(CCCameraRig* r, float deg);

/* ─── Camera path ────────────────────────────────────────────────────── */
void cc_cam_path_clear(CCCameraRig* r);
void cc_cam_path_add(CCCameraRig* r, float time, CCVec3 pos, CCQuat rot,
                     float fov_deg, CCCamEase ease);
void cc_cam_path_add_look_at(CCCameraRig* r, float time, CCVec3 pos,
                              CCVec3 look_at, float fov_deg, CCCamEase ease);
void cc_cam_path_play(CCCameraRig* r);
void cc_cam_path_stop(CCCameraRig* r);
void cc_cam_path_seek(CCCameraRig* r, float time);
float cc_cam_path_duration(CCCameraRig* r);
bool  cc_cam_path_finished(CCCameraRig* r);

/* ─── Shake ──────────────────────────────────────────────────────────── */
void cc_cam_add_trauma(CCCameraRig* r, float trauma);  /* 0-1 */
void cc_cam_set_shake_params(CCCameraRig* r, float max_offset,
                              float max_angle, float frequency, float decay);

/* ─── Smooth move-to (one-shot) ─────────────────────────────────────── */
/* Smoothly moves camera from current pos to target over `duration` seconds */
typedef struct CCCamTween CCCamTween;
CCCamTween* cc_cam_tween_to(CCCameraRig* r,
                             CCVec3 target_pos, CCQuat target_rot,
                             float target_fov, float duration,
                             CCCamEase ease);
void cc_cam_tween_update(CCCamTween* tw, float dt);  /* advance + apply; call each frame */
bool cc_cam_tween_finished(CCCamTween* tw);
void cc_cam_tween_destroy(CCCamTween* tw);

/* ─── Dolly zoom (Hitchcock effect) ─────────────────────────────────── */
/* Moves camera away from subject while zooming in to maintain subject size */
void cc_cam_dolly_zoom(CCCameraRig* r, CCVec3 subject, float duration,
                        float fov_start, float fov_end);

/* ─── Utility ────────────────────────────────────────────────────────── */
CCRay cc_cam_screen_to_ray(CCCameraRig* r, float screen_x, float screen_y,
                             float screen_w, float screen_h);
CCVec3 cc_cam_world_to_screen(CCCameraRig* r, CCVec3 world_pos,
                               float screen_w, float screen_h);

#ifdef __cplusplus
}
#endif
