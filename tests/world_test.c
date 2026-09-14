/* World test — spatial culling. A large field of cubes is registered in a
 * CCWorld; the octree frustum-cull returns only the subset inside the camera
 * frustum, and we draw ONLY those. A HUD-ish inset isn't available, so instead
 * we colour objects by LOD (green=LOD0 near, yellow=LOD1 mid, red=LOD2 far) so
 * the LOD bands are visible as concentric rings, and print cull stats proving
 * most of the field was pruned. */
#include "cc/claudecore.h"
#include "cc/world.h"
#include <math.h>
#include <stdio.h>

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/world_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh ground = cc_mesh_plane(eng,400,400,4);
    CCMesh cube_hi = cc_mesh_cube(eng,1.0f);
    CCMesh cube_lo = cc_mesh_cube(eng,1.0f);   /* same mesh; colour conveys LOD here */

    CCMaterialDesc gd={.base_color={0.5f,0.52f,0.55f,1},.roughness=0.95f};
    CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc l0={.base_color={0.4f,0.85f,0.4f,1},.roughness=0.5f};  /* near  */
    CCMaterialDesc l1={.base_color={0.9f,0.8f,0.35f,1},.roughness=0.5f};  /* mid   */
    CCMaterialDesc l2={.base_color={0.9f,0.4f,0.3f,1},.roughness=0.5f};   /* far   */
    CCMaterial ml[3]={cc_material_create(eng,&l0),cc_material_create(eng,&l1),cc_material_create(eng,&l2)};

    cc_light_set_ambient(eng,0.2f,0.21f,0.24f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.9f,-0.35f},
                 .color={1,0.96f,0.9f},.intensity=2.8f,.cast_shadows=true};
    cc_light_add(eng,&sun);
    CCPostFX fx=cc_postfx_default(); fx.vignette=true; fx.vignette_strength=0.3f;
    cc_postfx_set(eng,&fx);

    /* build a big grid field of cubes */
    CCWorld* w=cc_world_create();
    const int G=40; const float S=6.0f;       /* 40x40 = 1600 cubes, 6 units apart */
    for (int gz=0; gz<G; gz++) for (int gx=0; gx<G; gx++){
        float x=(gx-G/2)*S, z=(gz-G/2)*S;
        CCVec3 p={x,0.5f,z};
        cc_world_add(w,cube_hi,ml[0], p,(CCVec3){x-0.5f,0,z-0.5f},(CCVec3){x+0.5f,1,z+0.5f});
    }
    (void)cube_lo;

    /* camera looking across the field */
    CCVec3 cam_pos={0,14,70};
    CCVec3 cam_tgt={0,0,-40};
    CCMat4 view=mat4_look_at(cam_pos,cam_tgt,(CCVec3){0,1,0});
    CCMat4 proj=mat4_perspective(48.0f*3.14159f/180.0f,(float)cfg.width/cfg.height,0.5f,400.0f);
    CCMat4 vp=mat4_mul(proj,view);

    /* cull */
    static uint32_t vis[4096];
    uint32_t nv=cc_world_cull(w,vp,vis,4096);
    CCWorldCullStats st=cc_world_cull_stats(w);

    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={cam_pos.x,cam_pos.y,cam_pos.z},
                          .target={cam_tgt.x,cam_tgt.y,cam_tgt.z},.up={0,1,0},
                          .fov_deg=48,.near_plane=0.5f,.far_plane=400,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mg,&gxf);

        /* draw ONLY the culled-visible set; colour by distance band (LOD proxy) */
        for(uint32_t i=0;i<nv;i++){
            const CCWorldObject* o=cc_world_get(w,vis[i]);
            if(!o) continue;
            float dx=cam_pos.x-o->position.x, dz=cam_pos.z-o->position.z;
            float d=sqrtf(dx*dx+dz*dz);
            int band = d<45?0 : d<90?1 : 2;
            CCTransform3D xf={.pos={o->position.x,o->position.y,o->position.z},
                              .rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(eng,cube_hi,ml[band],&xf);
        }
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s  field=%u visible=%u tested=%u pruned=%.0f%% nodes=%u\n",
        s?s:"(null)", cc_world_count(w), nv, st.tested,
        100.0*(1.0-(double)st.tested/cc_world_count(w)), st.octree_nodes);
    cc_world_destroy(w); cc_shutdown(eng); return 0;
}
