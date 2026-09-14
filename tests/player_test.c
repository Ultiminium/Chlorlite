/* Player test — kinematic character controller.
 * Drives a character with a scripted input sequence (deterministic, headless):
 * walk forward, auto-step up onto a raised ledge, then jump. A follow-camera
 * tracks it. We stamp a fading trail of "ghost" capsules at sampled positions
 * so the whole motion path (including the jump arc + the step-up) is visible in
 * a single screenshot. Also exercises cc_character_transform + the ground query. */
#include "cc/claudecore.h"
#include "cc/camera.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Ground: flat at y=0, but a raised block (ledge) of height 0.3 for z in
 * [-9,-4] and |x|<3 — low enough for the controller's step_offset to auto-climb.
 * (Forward at yaw=0 is -Z, matching the camera convention, so the character
 * walks toward -Z into the ledge.) */
static float ground_fn(float x, float z, void* u){
    (void)u;
    if (z < -4.0f && z > -9.0f && x > -3.0f && x < 3.0f) return 0.3f;
    return 0.0f;
}

typedef struct { float x,y,z, yaw, a; } Ghost;

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/player_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh ground = cc_mesh_plane(eng,60,60,4);
    CCMesh ledge  = cc_mesh_cube(eng,1.0f);
    CCMesh body   = cc_mesh_capsule(eng,0.4f,1.0f,20);

    CCMaterialDesc gd={.base_color={0.55f,0.57f,0.6f,1},.roughness=0.92f};
    CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc ld={.base_color={0.45f,0.5f,0.55f,1},.roughness=0.85f};
    CCMaterial ml=cc_material_create(eng,&ld);
    CCMaterialDesc pd={.base_color={0.9f,0.45f,0.2f,1},.roughness=0.4f,.metallic=0.1f};
    CCMaterial mp=cc_material_create(eng,&pd);
    CCMaterialDesc td={.base_color={0.35f,0.55f,0.85f,1},.roughness=0.5f};
    CCMaterial mt=cc_material_create(eng,&td);

    cc_light_set_ambient(eng,0.15f,0.16f,0.19f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-0.85f,-0.35f},
                 .color={1.0f,0.96f,0.88f},.intensity=3.0f,.cast_shadows=true};
    cc_light_add(eng,&sun);

    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.3f; fx.bloom_intensity=0.1f;
    fx.vignette=true; fx.vignette_strength=0.28f;
    cc_postfx_set(eng,&fx);

    /* ── character ─────────────────────────────────────────────────────── */
    CCCharacter pc; cc_character_init(&pc, cc_character_default());
    pc.position=(CCVec3){0,0,4};

    /* Scripted input timeline: (t_end, move_z, jump_at_this_step) */
    const float dt=1.0f/60.0f;
    const int   STEPS=210;          /* 3.5 s */
    Ghost trail[STEPS]; int ntrail=0;

    for(int s=0;s<STEPS;s++){
        float t=s*dt;
        CCCharInput in={0};
        in.yaw_deg = 0.0f;          /* face +Z (forward) */
        in.move_z  = 1.0f;          /* always walking forward */
        in.sprint  = (t>2.3f);      /* sprint near the end */
        /* jump once, just before reaching the ledge, to show an arc */
        in.jump    = (s==132);      /* ~2.2s */
        cc_character_update(&pc,&in,ground_fn,NULL,dt);

        /* sample a ghost every 12 steps on the ground, but every 6 while
           airborne so the jump arc is densely traced */
        int stride = pc.grounded ? 12 : 6;
        if (s % stride == 0 || s==STEPS-1){
            float p[3],r[4]; cc_character_transform(&pc,p,r);
            trail[ntrail++] = (Ghost){p[0],p[1],p[2], pc.facing_yaw, 0};
        }
    }
    /* assign fading alpha to the trail (older = fainter) */
    for(int i=0;i<ntrail;i++) trail[i].a = 0.25f + 0.75f*((float)i/(ntrail-1));

    /* follow camera aimed at the final player position, slightly behind/above */
    CCVec3 pcpos=pc.position;

    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        /* side-profile camera looking down +X at the travel plane, so the jump
           ARC (Y) and the ledge STEP-UP are both visible against Z. */
        CCCameraDesc cam={.pos={14,4.0f,-6.5f},.target={0,1.1f,-7.0f},.up={0,1,0},
                          .fov_deg=50,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);

        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mg,&gxf);

        /* draw the ledge as a flat slab (scale a unit cube) */
        CCTransform3D lxf={.pos={0,0.15f,-6.5f},.rot={0,0,0,1},.scale={6,0.3f,5}};
        cc_draw_mesh(eng,ledge,ml,&lxf);

        /* motion trail: faded ghosts, brightest = current */
        for(int i=0;i<ntrail;i++){
            CCQuat q=quat_from_axis_angle((CCVec3){0,1,0},trail[i].yaw*CC_DEG2RAD);
            CCTransform3D gx={.pos={trail[i].x,trail[i].y,trail[i].z},
                              .rot={q.x,q.y,q.z,q.w},.scale={1,1,1}};
            cc_draw_mesh(eng, body, (i==ntrail-1)?mp:mt, &gx);
        }
        cc_frame_end(eng);
    }
    const char* sp=cc_screenshot(eng,out); printf("screenshot: %s  final_pos=(%.2f,%.2f,%.2f) grounded=%d\n",
        sp?sp:"(null)", pc.position.x,pc.position.y,pc.position.z, pc.grounded);
    cc_shutdown(eng); return 0;
}
