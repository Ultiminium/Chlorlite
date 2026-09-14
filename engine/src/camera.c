/*
 * camera.c — Chlorlite camera system implementation
 */
#include "cc/camera.h"
#include "cc/claudecore.h"
#include "cc/input.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ─── Internal tween ─────────────────────────────────────────────────── */
struct CCCamTween {
    CCVec3   start_pos, end_pos;
    CCQuat   start_rot, end_rot;
    float    start_fov, end_fov;
    float    duration, elapsed;
    CCCamEase ease;
    bool      done;
    CCCameraRig* rig;
};

/* ─── Lifecycle ──────────────────────────────────────────────────────── */

CCCameraRig* cc_camera_create(void) {
    CCCameraRig* r = calloc(1, sizeof(CCCameraRig));
    r->cam.type       = CC_CAM_PERSPECTIVE;
    r->cam.fov_deg    = 60.0f;
    r->cam.near_plane = 0.1f;
    r->cam.far_plane  = 1000.0f;
    r->cam.aspect     = 16.0f/9.0f;
    r->cam.exposure   = 1.0f;
    r->cam.position   = (CCVec3){0,0,5};
    r->cam.rotation   = quat_identity();
    r->shake.trauma_decay = 1.5f;
    r->shake.max_offset   = 0.1f;
    r->shake.max_angle_deg= 3.0f;
    r->shake.frequency    = 16.0f;
    r->active = true;
    return r;
}

void cc_camera_destroy(CCCameraRig* rig) { free(rig); }

/* ─── Matrix computation ─────────────────────────────────────────────── */

static void compute_matrices(CCCamera* cam) {
    /* View = lookAt(pos, pos+forward, up) */
    CCVec3 fwd = vec3_norm(quat_rotate(cam->rotation, (CCVec3){0,0,-1}));
    CCVec3 up  = vec3_norm(quat_rotate(cam->rotation, (CCVec3){0,1, 0}));
    CCVec3 right=vec3_norm(quat_rotate(cam->rotation, (CCVec3){1,0, 0}));
    cam->forward = fwd;
    cam->right   = right;
    cam->up      = up;
    cam->view    = mat4_look_at(cam->position, vec3_add(cam->position,fwd), up);

    if (cam->type == CC_CAM_PERSPECTIVE) {
        cam->proj = mat4_perspective(cam->fov_deg * CC_DEG2RAD, cam->aspect,
                                     cam->near_plane, cam->far_plane);
    } else {
        float h = cam->ortho_size;
        float w = h * cam->aspect;
        cam->proj = mat4_ortho(-w,w,-h,h,cam->near_plane,cam->far_plane);
    }
    cam->view_proj     = mat4_mul(cam->proj, cam->view);
    cam->inv_view_proj = mat4_inverse(cam->view_proj);
    cam->frustum       = frustum_from_vp(cam->view_proj);
}

/* ─── Ease functions ─────────────────────────────────────────────────── */

static float apply_ease(float t, CCCamEase e) {
    switch(e) {
        case CC_EASE_SMOOTH:   return cc_smoothstep(0,1,t);
        case CC_EASE_IN:       return cc_ease_in(t);
        case CC_EASE_OUT:      return cc_ease_out(t);
        case CC_EASE_IN_OUT:   return cc_ease_in_out(t);
        case CC_EASE_LINEAR:
        default:               return t;
    }
}

/* ─── Path sampling — Catmull-Rom ────────────────────────────────────── */

static CCVec3 catmull_rom_vec3(CCVec3 p0,CCVec3 p1,CCVec3 p2,CCVec3 p3,float t){
    float t2=t*t,t3=t2*t;
    CCVec3 r;
    r.x=0.5f*((2*p1.x)+(-p0.x+p2.x)*t+(2*p0.x-5*p1.x+4*p2.x-p3.x)*t2+(-p0.x+3*p1.x-3*p2.x+p3.x)*t3);
    r.y=0.5f*((2*p1.y)+(-p0.y+p2.y)*t+(2*p0.y-5*p1.y+4*p2.y-p3.y)*t2+(-p0.y+3*p1.y-3*p2.y+p3.y)*t3);
    r.z=0.5f*((2*p1.z)+(-p0.z+p2.z)*t+(2*p0.z-5*p1.z+4*p2.z-p3.z)*t2+(-p0.z+3*p1.z-3*p2.z+p3.z)*t3);
    return r;
}

static void path_sample(CCCamPath* path, float t, CCVec3* out_pos, CCQuat* out_rot, float* out_fov) {
    if (!path->keyframe_count) return;
    if (path->keyframe_count == 1) {
        *out_pos = path->keyframes[0].position;
        *out_rot = path->keyframes[0].rotation;
        *out_fov = path->keyframes[0].fov_deg;
        return;
    }
    /* Clamp */
    if (t <= path->keyframes[0].time) {
        *out_pos = path->keyframes[0].position;
        *out_rot = path->keyframes[0].rotation;
        *out_fov = path->keyframes[0].fov_deg;
        return;
    }
    uint32_t last = path->keyframe_count-1;
    if (t >= path->keyframes[last].time) {
        *out_pos = path->keyframes[last].position;
        *out_rot = path->keyframes[last].rotation;
        *out_fov = path->keyframes[last].fov_deg;
        return;
    }
    /* Find segment */
    uint32_t i=0;
    for (; i<last; i++) if (t < path->keyframes[i+1].time) break;
    float t0=path->keyframes[i].time, t1=path->keyframes[i+1].time;
    float alpha = (t-t0)/fmaxf(t1-t0, 1e-6f);
    alpha = apply_ease(alpha, path->keyframes[i].ease);

    if (path->use_catmull_rom && path->keyframe_count >= 4) {
        int im1 = i>0 ? i-1 : 0;
        int ip1 = i+1 < (int)path->keyframe_count ? i+1 : last;
        int ip2 = i+2 < (int)path->keyframe_count ? i+2 : last;
        *out_pos = catmull_rom_vec3(path->keyframes[im1].position,
                                    path->keyframes[i].position,
                                    path->keyframes[ip1].position,
                                    path->keyframes[ip2].position, alpha);
    } else {
        *out_pos = vec3_lerp(path->keyframes[i].position, path->keyframes[i+1].position, alpha);
    }
    *out_rot = quat_slerp(path->keyframes[i].rotation, path->keyframes[i+1].rotation, alpha);
    float f0 = path->keyframes[i].fov_deg ? path->keyframes[i].fov_deg : 60.0f;
    float f1 = path->keyframes[i+1].fov_deg ? path->keyframes[i+1].fov_deg : f0;
    *out_fov = cc_lerp(f0, f1, alpha);

    /* Look-at override */
    if (path->keyframes[i].has_look_at) {
        CCVec3 look_pos;
        if (path->keyframes[i+1].has_look_at)
            look_pos = vec3_lerp(path->keyframes[i].look_at, path->keyframes[i+1].look_at, alpha);
        else
            look_pos = path->keyframes[i].look_at;
        CCVec3 fwd = vec3_norm(vec3_sub(look_pos, *out_pos));
        CCVec3 up = {0,1,0};
        *out_rot = quat_look_at(*out_pos, look_pos, up);
    }
}

/* ─── Shake ──────────────────────────────────────────────────────────── */

static float _hash(float n){ n=sinf(n)*43758.5453f; return n-floorf(n); }
static float _smooth_noise(float x){float i=floorf(x); float f=x-i; f=f*f*(3-2*f); return cc_lerp(_hash(i),_hash(i+1),f);}

static void update_shake(CCCamShake* sh, CCCamera* cam, float dt) {
    if (sh->trauma <= 0) return;
    sh->trauma -= sh->trauma_decay * dt;
    if (sh->trauma < 0) sh->trauma = 0;
    float shake = sh->trauma * sh->trauma;
    sh->_seed += dt * sh->frequency;
    float ox = (_smooth_noise(sh->_seed*1.23f)*2-1) * sh->max_offset * shake;
    float oy = (_smooth_noise(sh->_seed*2.34f)*2-1) * sh->max_offset * shake;
    float oz = (_smooth_noise(sh->_seed*3.45f)*2-1) * sh->max_offset * shake;
    float rx = (_smooth_noise(sh->_seed*4.56f)*2-1) * sh->max_angle_deg * CC_DEG2RAD * shake;
    float ry = (_smooth_noise(sh->_seed*5.67f)*2-1) * sh->max_angle_deg * CC_DEG2RAD * shake;
    cam->position.x += ox; cam->position.y += oy; cam->position.z += oz;
    CCQuat sr = quat_from_euler(rx, ry, 0);
    cam->rotation = quat_mul(cam->rotation, sr);
}

/* ─── Controllers ────────────────────────────────────────────────────── */

static void update_free(CCCameraRig* rig, CCEngine* eng, float dt) {
    CCFreeCamCtrl* c = &rig->free_ctrl;
    /* Mouse look */
    int32_t mdx, mdy;
    cc_mouse_delta(eng, &mdx, &mdy);
    c->yaw   += mdx * c->mouse_sensitivity;
    c->pitch += (c->invert_y ? mdy : -mdy) * c->mouse_sensitivity;
    c->pitch  = cc_clamp(c->pitch, -89.0f, 89.0f);
    /* Build rotation from yaw+pitch */
    CCQuat qy = quat_from_axis_angle((CCVec3){0,1,0}, c->yaw   * CC_DEG2RAD);
    CCQuat qp = quat_from_axis_angle((CCVec3){1,0,0}, c->pitch * CC_DEG2RAD);
    rig->cam.rotation = quat_mul(qy, qp);
    /* Movement */
    float speed = c->move_speed * (cc_key_down(eng,QKEY_LSHIFT) ? c->sprint_mult : 1.0f);
    CCVec3 fwd  = vec3_norm(quat_rotate(rig->cam.rotation,(CCVec3){0,0,-1}));
    CCVec3 right= vec3_norm(quat_rotate(rig->cam.rotation,(CCVec3){1,0, 0}));
    CCVec3 up   = {0,1,0};
    CCVec3 move = {0,0,0};
    if (cc_key_down(eng,QKEY_W)) move=vec3_add(move,fwd);
    if (cc_key_down(eng,QKEY_S)) move=vec3_sub(move,fwd);
    if (cc_key_down(eng,QKEY_A)) move=vec3_sub(move,right);
    if (cc_key_down(eng,QKEY_D)) move=vec3_add(move,right);
    if (cc_key_down(eng,QKEY_E)) move=vec3_add(move,up);
    if (cc_key_down(eng,QKEY_Q)) move=vec3_sub(move,up);
    float ml = vec3_len(move);
    if (ml > CC_EPSILON)
        rig->cam.position = vec3_add(rig->cam.position, vec3_scale(vec3_scale(move,1/ml),speed*dt));
}

static void update_orbit(CCCameraRig* rig, CCEngine* eng, float dt) {
    CCOrbitCamCtrl* c = &rig->orbit_ctrl;
    /* Drag to orbit */
    if (cc_mouse_down(eng, QMOUSE_LEFT)) {
        int32_t mdx,mdy; cc_mouse_delta(eng,&mdx,&mdy);
        c->yaw   += mdx * c->orbit_speed;
        c->pitch += mdy * c->orbit_speed;
        c->pitch  = cc_clamp(c->pitch, c->min_pitch>-89?c->min_pitch:-89.0f,
                                        c->max_pitch< 89?c->max_pitch: 89.0f);
    }
    /* Scroll to zoom */
    float scroll_dx, scroll_dy;
    cc_mouse_scroll(eng, &scroll_dx, &scroll_dy);
    c->distance -= scroll_dy * c->zoom_speed;
    c->distance  = cc_clamp(c->distance,
                            c->min_dist>0?c->min_dist:0.1f,
                            c->max_dist>0?c->max_dist:1000.0f);
    /* Smooth */
    float sf = c->smooth_factor > 0 ? c->smooth_factor : 1.0f;
    float alpha = 1.0f - powf(sf, dt); /* frame-rate independent */
    c->_yaw_smooth   = cc_lerp(c->_yaw_smooth,   c->yaw,      alpha);
    c->_pitch_smooth = cc_lerp(c->_pitch_smooth,  c->pitch,    alpha);
    c->_dist_smooth  = cc_lerp(c->_dist_smooth,   c->distance, alpha);
    c->_target_smooth= vec3_lerp(c->_target_smooth, c->target,  alpha);
    /* Compute position from spherical coords */
    float yaw_r   = c->_yaw_smooth   * CC_DEG2RAD;
    float pitch_r = c->_pitch_smooth * CC_DEG2RAD;
    float d       = c->_dist_smooth;
    rig->cam.position = (CCVec3){
        c->_target_smooth.x + d * cosf(pitch_r) * sinf(yaw_r),
        c->_target_smooth.y + d * sinf(pitch_r),
        c->_target_smooth.z + d * cosf(pitch_r) * cosf(yaw_r)
    };
    rig->cam.rotation = quat_look_at(rig->cam.position, c->_target_smooth, (CCVec3){0,1,0});
}

static void update_follow(CCCameraRig* rig, float dt) {
    CCFollowCamCtrl* c = &rig->follow_ctrl;
    if (!c->target_pos) return;
    float lag = 1.0f - powf(c->lag_pos, dt);
    CCVec3 desired_pos = vec3_add(*c->target_pos, c->offset);
    c->_pos_smooth = vec3_lerp(c->_pos_smooth, desired_pos, lag);
    rig->cam.position = c->_pos_smooth;
    CCVec3 look = vec3_add(*c->target_pos,
        c->target_forward ? vec3_scale(*c->target_forward, c->look_ahead) : (CCVec3){0,0,0});
    rig->cam.rotation = quat_look_at(rig->cam.position, look, (CCVec3){0,1,0});
}

static void update_path(CCCameraRig* rig, float dt) {
    CCCamPath* p = &rig->path;
    if (!p->playing || !p->keyframe_count) return;
    p->current_time += dt;
    if (p->current_time >= p->total_duration) {
        if (p->loop) p->current_time = fmodf(p->current_time, p->total_duration);
        else { p->current_time = p->total_duration; p->playing = false; p->finished = true; }
    }
    CCVec3 pos; CCQuat rot; float fov;
    path_sample(p, p->current_time, &pos, &rot, &fov);
    rig->cam.position = pos;
    rig->cam.rotation = rot;
    if (fov > 0) rig->cam.fov_deg = fov;
}

/* ─── Main update ────────────────────────────────────────────────────── */

void cc_camera_update(CCCameraRig* rig, CCEngine* eng, float dt) {
    if (!rig || !rig->active) return;
    switch (rig->ctrl_type) {
        case CC_CAM_CTRL_FREE:      update_free(rig, eng, dt);   break;
        case CC_CAM_CTRL_ORBIT:     update_orbit(rig, eng, dt);  break;
        case CC_CAM_CTRL_FOLLOW:    update_follow(rig, dt);      break;
        case CC_CAM_CTRL_CINEMATIC: update_path(rig, dt);        break;
        case CC_CAM_CTRL_CUSTOM:    if(rig->custom_fn) rig->custom_fn(rig, eng, dt, rig->custom_state); break;
        default: break;
    }
    update_shake(&rig->shake, &rig->cam, dt);
    /* Auto aspect from engine */
    /* rig->cam.aspect = (float)eng_width / eng_height; -- set externally */
    compute_matrices(&rig->cam);
}

void cc_camera_apply(CCCameraRig* rig, CCEngine* eng) {
    if (!rig || !eng) return;
    /* Upload matrices to renderer via CCCameraDesc */
    CCCameraDesc desc;
    memcpy(desc.pos,    &rig->cam.position, 12);
    CCVec3 target = vec3_add(rig->cam.position, rig->cam.forward);
    memcpy(desc.target, &target, 12);
    memcpy(desc.up,     &rig->cam.up, 12);
    desc.fov_deg    = rig->cam.fov_deg;
    desc.near_plane = rig->cam.near_plane;
    desc.far_plane  = rig->cam.far_plane;
    desc.exposure   = rig->cam.exposure;
    desc.ortho_size = rig->cam.ortho_size;
    cc_camera_set(eng, &desc);
}


/* ─── Setup helpers ──────────────────────────────────────────────────── */

void cc_cam_set_perspective(CCCameraRig* r,float fov,float near,float far){
    r->cam.type=CC_CAM_PERSPECTIVE; r->cam.fov_deg=fov; r->cam.near_plane=near; r->cam.far_plane=far;
}
void cc_cam_set_ortho(CCCameraRig* r,float size,float near,float far){
    r->cam.type=CC_CAM_ORTHOGRAPHIC; r->cam.ortho_size=size; r->cam.near_plane=near; r->cam.far_plane=far;
}
void cc_cam_set_position(CCCameraRig* r,CCVec3 pos){ r->cam.position=pos; }
void cc_cam_set_rotation(CCCameraRig* r,CCQuat rot){ r->cam.rotation=rot; }
void cc_cam_look_at(CCCameraRig* r,CCVec3 target,CCVec3 up){
    r->cam.rotation=quat_look_at(r->cam.position,target,up);
}
void cc_cam_set_fov(CCCameraRig* r,float fov){ r->cam.fov_deg=fov; }

/* ─── Controllers setup ──────────────────────────────────────────────── */

void cc_cam_use_free(CCCameraRig* r,float speed,float sens){
    r->ctrl_type=CC_CAM_CTRL_FREE;
    r->free_ctrl.move_speed=speed; r->free_ctrl.mouse_sensitivity=sens;
    r->free_ctrl.sprint_mult=3.0f; r->free_ctrl.fly_mode=true;
    /* Init yaw/pitch from current rotation */
    CCVec3 fwd=vec3_norm(quat_rotate(r->cam.rotation,(CCVec3){0,0,-1}));
    r->free_ctrl.yaw  =atan2f(fwd.x,-fwd.z)*CC_RAD2DEG;
    r->free_ctrl.pitch=asinf(cc_clamp(fwd.y,-1,1))*CC_RAD2DEG;
}
void cc_cam_use_orbit(CCCameraRig* r,CCVec3 target,float dist,float yaw,float pitch){
    r->ctrl_type=CC_CAM_CTRL_ORBIT;
    r->orbit_ctrl.target=target; r->orbit_ctrl.distance=dist;
    r->orbit_ctrl.yaw=yaw; r->orbit_ctrl.pitch=pitch;
    r->orbit_ctrl.orbit_speed=0.4f; r->orbit_ctrl.zoom_speed=0.5f;
    r->orbit_ctrl.smooth_factor=0.1f; r->orbit_ctrl.min_pitch=-85.0f; r->orbit_ctrl.max_pitch=85.0f;
    r->orbit_ctrl._yaw_smooth=yaw; r->orbit_ctrl._pitch_smooth=pitch;
    r->orbit_ctrl._dist_smooth=dist; r->orbit_ctrl._target_smooth=target;
}
void cc_cam_use_follow(CCCameraRig* r,CCVec3* tpos,CCVec3* tfwd,CCVec3 offset,float lag){
    r->ctrl_type=CC_CAM_CTRL_FOLLOW;
    r->follow_ctrl.target_pos=tpos; r->follow_ctrl.target_forward=tfwd;
    r->follow_ctrl.offset=offset; r->follow_ctrl.lag_pos=lag;
    r->follow_ctrl._pos_smooth = tpos ? vec3_add(*tpos,offset) : offset;
}
void cc_cam_use_path(CCCameraRig* r){ r->ctrl_type=CC_CAM_CTRL_CINEMATIC; }

void cc_cam_use_custom(CCCameraRig* r, CCCamControllerFn fn, void* state){
    if(!r) return;
    r->custom_fn=fn; r->custom_state=state;
    r->ctrl_type = fn ? CC_CAM_CTRL_CUSTOM : CC_CAM_CTRL_NONE;
}

/* Orbit adjustments */
void cc_cam_orbit_set_target(CCCameraRig* r,CCVec3 t){ r->orbit_ctrl.target=t; }
void cc_cam_orbit_set_distance(CCCameraRig* r,float d){ r->orbit_ctrl.distance=d; }
void cc_cam_orbit_add_yaw(CCCameraRig* r,float deg){ r->orbit_ctrl.yaw+=deg; }
void cc_cam_orbit_add_pitch(CCCameraRig* r,float deg){
    r->orbit_ctrl.pitch=cc_clamp(r->orbit_ctrl.pitch+deg,-85,85);
}

/* ─── Path API ───────────────────────────────────────────────────────── */

void cc_cam_path_clear(CCCameraRig* r){
    memset(&r->path,0,sizeof(r->path));
}
void cc_cam_path_add(CCCameraRig* r,float time,CCVec3 pos,CCQuat rot,float fov,CCCamEase ease){
    if (r->path.keyframe_count>=CC_CAM_MAX_KEYFRAMES) return;
    CCCamKeyframe* kf=&r->path.keyframes[r->path.keyframe_count++];
    kf->time=time; kf->position=pos; kf->rotation=rot; kf->fov_deg=fov; kf->ease=ease;
    r->path.total_duration=fmaxf(r->path.total_duration,time);
}
void cc_cam_path_add_look_at(CCCameraRig* r,float time,CCVec3 pos,CCVec3 look_at,float fov,CCCamEase ease){
    if (r->path.keyframe_count>=CC_CAM_MAX_KEYFRAMES) return;
    CCCamKeyframe* kf=&r->path.keyframes[r->path.keyframe_count++];
    kf->time=time; kf->position=pos; kf->fov_deg=fov; kf->ease=ease;
    kf->has_look_at=true; kf->look_at=look_at;
    kf->rotation=quat_look_at(pos,look_at,(CCVec3){0,1,0});
    r->path.total_duration=fmaxf(r->path.total_duration,time);
}
void cc_cam_path_play(CCCameraRig* r){ r->path.playing=true; r->path.finished=false; r->ctrl_type=CC_CAM_CTRL_CINEMATIC; }
void cc_cam_path_stop(CCCameraRig* r){ r->path.playing=false; }
void cc_cam_path_seek(CCCameraRig* r,float t){ r->path.current_time=t; }
float cc_cam_path_duration(CCCameraRig* r){ return r->path.total_duration; }
bool  cc_cam_path_finished(CCCameraRig* r){ return r->path.finished; }

/* ─── Shake ──────────────────────────────────────────────────────────── */
void cc_cam_add_trauma(CCCameraRig* r,float t){ r->shake.trauma=cc_clamp(r->shake.trauma+t,0,1); }
void cc_cam_set_shake_params(CCCameraRig* r,float offset,float angle,float freq,float decay){
    r->shake.max_offset=offset; r->shake.max_angle_deg=angle;
    r->shake.frequency=freq; r->shake.trauma_decay=decay;
}

/* ─── Utility ────────────────────────────────────────────────────────── */
CCRay cc_cam_screen_to_ray(CCCameraRig* r,float sx,float sy,float sw,float sh){
    return ray_from_screen(sx,sy,sw,sh,r->cam.inv_view_proj,r->cam.near_plane,r->cam.far_plane);
}
CCVec3 cc_cam_world_to_screen(CCCameraRig* r,CCVec3 wp,float sw,float sh){
    CCVec4 clip=mat4_mul_vec4(r->cam.view_proj,(CCVec4){wp.x,wp.y,wp.z,1});
    CCVec3 ndc={(clip.x/clip.w+1)*0.5f*sw,(1-clip.y/clip.w)*0.5f*sh,clip.z/clip.w};
    return ndc;
}

/* ─── Tween ──────────────────────────────────────────────────────────── */
CCCamTween* cc_cam_tween_to(CCCameraRig* r,CCVec3 tpos,CCQuat trot,float tfov,float dur,CCCamEase ease){
    CCCamTween* tw=calloc(1,sizeof(CCCamTween));
    tw->start_pos=r->cam.position; tw->end_pos=tpos;
    tw->start_rot=r->cam.rotation; tw->end_rot=trot;
    tw->start_fov=r->cam.fov_deg;  tw->end_fov=tfov>0?tfov:r->cam.fov_deg;
    tw->duration=dur; tw->ease=ease; tw->rig=r;
    return tw;
}
/* Advance the tween by dt and apply to the camera. */
void cc_cam_tween_update(CCCamTween* tw, float dt){
    if (!tw || tw->done) return;
    CCCameraRig* r = tw->rig;
    tw->elapsed += dt;
    float t = tw->duration > 0.0f ? cc_clamp(tw->elapsed/tw->duration, 0, 1) : 1.0f;
    float et = apply_ease(t, tw->ease);
    r->cam.position = vec3_lerp(tw->start_pos, tw->end_pos, et);
    r->cam.rotation = quat_slerp(tw->start_rot, tw->end_rot, et);
    r->cam.fov_deg  = cc_lerp(tw->start_fov, tw->end_fov, et);
    if (t >= 1.0f) tw->done = true;
}
bool cc_cam_tween_finished(CCCamTween* tw){ return !tw || tw->done; }
void cc_cam_tween_destroy(CCCamTween* tw){ free(tw); }

/* Dolly zoom (Hitchcock / vertigo effect).
 * Keeps the subject the same apparent size while the FOV changes: the framed
 * height at the subject is 2*d*tan(fov/2); to hold it constant when fov goes
 * fov_start→fov_end, the distance must scale by tan(fov_start/2)/tan(fov_end/2).
 * With no persistent dolly state in the rig, this applies the END state
 * immediately: it sets fov_end and repositions the camera along its current
 * view direction to the distance that preserves the subject's framing. Drive it
 * per-frame (ramping fov_end yourself) or pair with a tween for animation.
 * `duration` is accepted for API symmetry; a 0/any value applies instantly. */
void cc_cam_dolly_zoom(CCCameraRig* r, CCVec3 subject, float duration,
                       float fov_start, float fov_end) {
    if (!r) return; (void)duration;
    if (fov_start <= 0.0f) fov_start = r->cam.fov_deg > 0 ? r->cam.fov_deg : 60.0f;
    if (fov_end   <= 0.0f) fov_end   = fov_start;

    /* current framed distance to subject along the view ray */
    CCVec3 fwd = vec3_norm(quat_rotate(r->cam.rotation, (CCVec3){0,0,-1}));
    CCVec3 to_subj = vec3_sub(subject, r->cam.position);
    float d0 = vec3_dot(to_subj, fwd);          /* signed distance along view */
    if (d0 < 1e-4f) d0 = vec3_len(to_subj);     /* fallback if behind/at cam */
    if (d0 < 1e-4f) d0 = 1e-4f;

    float ts = tanf(fov_start * 0.5f * CC_DEG2RAD);
    float te = tanf(fov_end   * 0.5f * CC_DEG2RAD);
    float d1 = (te > 1e-6f) ? d0 * (ts / te) : d0;

    /* place the camera at distance d1 from the subject, back along -fwd */
    r->cam.position = vec3_sub(subject, vec3_scale(fwd, d1));
    r->cam.fov_deg  = fov_end;
}
