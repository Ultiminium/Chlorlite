/* Physics test — the injectable rigid-body world with the full collision
 * matrix. Drops a mix of spheres, boxes, and capsules onto a ground plane and
 * a static ramp; they collide with each other (sphere-sphere, sphere-box,
 * box-box, capsule-*, capsule-capsule) and settle into a pile. Simulated
 * headless for a fixed number of steps, then rendered. Exercises restitution,
 * friction, and Baumgarte positional correction resting stability. */
#include "cc/claudecore.h"
#include "cc/physics.h"
#include <math.h>
#include <stdio.h>

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/physics_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    /* ── meshes ────────────────────────────────────────────────────────── */
    CCMesh m_ground = cc_mesh_plane(eng,50,50,4);
    CCMesh m_sphere = cc_mesh_sphere(eng,0.5f,24,24);
    CCMesh m_box    = cc_mesh_cube(eng,1.0f);
    CCMesh m_caps   = cc_mesh_capsule(eng,0.4f,1.0f,20);

    CCMaterialDesc gd={.base_color={0.5f,0.52f,0.55f,1},.roughness=0.95f};
    CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc sd={.base_color={0.85f,0.35f,0.28f,1},.roughness=0.4f,.metallic=0.1f};
    CCMaterial ms=cc_material_create(eng,&sd);
    CCMaterialDesc bd={.base_color={0.35f,0.55f,0.85f,1},.roughness=0.45f,.metallic=0.2f};
    CCMaterial mb=cc_material_create(eng,&bd);
    CCMaterialDesc cd={.base_color={0.85f,0.72f,0.3f,1},.roughness=0.35f,.metallic=0.4f};
    CCMaterial mc=cc_material_create(eng,&cd);

    cc_light_set_ambient(eng,0.15f,0.16f,0.19f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-0.85f,-0.4f},
                 .color={1.0f,0.96f,0.88f},.intensity=3.0f,.cast_shadows=true};
    cc_light_add(eng,&sun);
    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.3f; fx.bloom_intensity=0.08f;
    fx.vignette=true; fx.vignette_strength=0.28f;
    cc_postfx_set(eng,&fx);

    /* ── physics world ─────────────────────────────────────────────────── */
    CCPhysicsWorld* w=cc_physics_create();
    cc_physics_set_gravity(w,(CCVec3){0,-14.0f,0});
    cc_physics_set_solver_iterations(w,12);

    CCCollider plane={.type=CC_SHAPE_PLANE,.plane={.normal={0,1,0},.offset=0}};
    cc_body_create(w,CC_BODY_STATIC,plane,cc_body_material_default(),(CCVec3){0,0,0});

    /* a static box acting as a low wall/step the pile leans against */
    CCCollider wall={.type=CC_SHAPE_BOX,.box={.half_extents={4.0f,0.5f,0.6f}}};
    CCBodyMaterial wm=cc_body_material_default(); wm.friction=0.8f;
    cc_body_create(w,CC_BODY_STATIC,wall,wm,(CCVec3){0,0.5f,3.0f});

    /* falling bodies: track (id, kind) for drawing */
    enum { SPH, BOX, CAP };
    typedef struct { CCBodyId id; int kind; } Obj;
    Obj objs[64]; int nobj=0;

    CCBodyMaterial dm=cc_body_material_default();
    dm.restitution=0.25f; dm.friction=0.6f; dm.mass=1.0f;

    /* a grid of mixed bodies raining down from staggered heights */
    unsigned seed=12345u;
    for (int i=0;i<24;i++){
        seed = seed*1664525u + 1013904223u;
        float rx = ((seed>>16)&0xff)/255.0f*6.0f - 3.0f;
        float rz = ((seed>>8)&0xff)/255.0f*4.0f - 2.0f;
        float ry = 3.0f + i*0.45f;
        int kind = i%3;
        CCCollider col;
        if (kind==SPH)      col=(CCCollider){.type=CC_SHAPE_SPHERE,.sphere={.radius=0.5f}};
        else if (kind==BOX) col=(CCCollider){.type=CC_SHAPE_BOX,.box={.half_extents={0.5f,0.5f,0.5f}}};
        else                col=(CCCollider){.type=CC_SHAPE_CAPSULE,.capsule={.radius=0.4f,.height=1.0f}};
        CCBodyId id=cc_body_create(w,CC_BODY_DYNAMIC,col,dm,(CCVec3){rx,ry,rz});
        objs[nobj++]=(Obj){id,kind};
    }

    /* ── simulate to settle ────────────────────────────────────────────── */
    for (int step=0; step<300; step++) cc_physics_step(w,1.0f/60.0f);

    /* ── render the settled scene ──────────────────────────────────────── */
    int resting=0;
    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,6,12},.target={0,1.0f,1.0f},.up={0,1,0},
                          .fov_deg=52,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);

        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,m_ground,mg,&gxf);
        CCTransform3D wxf={.pos={0,0.5f,3.0f},.rot={0,0,0,1},.scale={8,1,1.2f}};
        cc_draw_mesh(eng,m_box,mg,&wxf);

        resting=0;
        for(int i=0;i<nobj;i++){
            CCBodyState st=cc_body_get_state(w,objs[i].id);
            if (fabsf(st.linear_velocity.y)<0.05f && st.position.y<3.0f) resting++;
            CCTransform3D xf;
            xf.pos[0]=st.position.x; xf.pos[1]=st.position.y; xf.pos[2]=st.position.z;
            xf.rot[0]=st.orientation.x; xf.rot[1]=st.orientation.y;
            xf.rot[2]=st.orientation.z; xf.rot[3]=st.orientation.w;
            xf.scale[0]=xf.scale[1]=xf.scale[2]=1.0f;
            if (objs[i].kind==SPH)      cc_draw_mesh(eng,m_sphere,ms,&xf);
            else if (objs[i].kind==BOX) cc_draw_mesh(eng,m_box,mb,&xf);
            else                        cc_draw_mesh(eng,m_caps,mc,&xf);
        }
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s  bodies=%u resting=%d/%d\n", s?s:"(null)",
           cc_physics_body_count(w), resting, nobj);
    cc_physics_destroy(w); cc_shutdown(eng); return 0;
}
