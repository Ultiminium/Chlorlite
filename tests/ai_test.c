/* AI test — A* grid pathfinding + steering path-follow, plus a small flock that
 * uses separation. A grid with wall obstacles; one "hero" agent plans an A*
 * route from corner to corner and walks it with cc_steer_path_follow (rendered
 * as a trail). A cluster of follower agents seek the hero while separating from
 * each other so they spread into a formation instead of overlapping. All
 * simulated headless, then rendered top-down-ish. */
#include "cc/claudecore.h"
#include "cc/ai.h"
#include <math.h>
#include <stdio.h>

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/ai_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh m_ground = cc_mesh_plane(eng,40,40,4);
    CCMesh m_wall   = cc_mesh_cube(eng,1.0f);
    CCMesh m_hero   = cc_mesh_capsule(eng,0.35f,0.8f,18);
    CCMesh m_dot    = cc_mesh_sphere(eng,0.25f,16,16);
    CCMesh m_node   = cc_mesh_cube(eng,0.2f);

    CCMaterialDesc gd={.base_color={0.5f,0.52f,0.55f,1},.roughness=0.95f};
    CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc wd={.base_color={0.32f,0.34f,0.4f,1},.roughness=0.85f};
    CCMaterial mw=cc_material_create(eng,&wd);
    CCMaterialDesc hd={.base_color={0.9f,0.4f,0.2f,1},.roughness=0.4f,.metallic=0.2f};
    CCMaterial mh=cc_material_create(eng,&hd);
    CCMaterialDesc fd={.base_color={0.35f,0.6f,0.9f,1},.roughness=0.45f};
    CCMaterial mf=cc_material_create(eng,&fd);
    CCMaterialDesc nd={.base_color={0.3f,0.85f,0.45f,1},.roughness=0.5f,.emissive={0.05f,0.2f,0.1f}};
    CCMaterial mn=cc_material_create(eng,&nd);

    cc_light_set_ambient(eng,0.16f,0.17f,0.2f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.9f,-0.3f},
                 .color={1.0f,0.96f,0.9f},.intensity=3.0f,.cast_shadows=true};
    cc_light_add(eng,&sun);
    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.2f; fx.bloom_intensity=0.1f;
    fx.vignette=true; fx.vignette_strength=0.28f;
    cc_postfx_set(eng,&fx);

    /* ── grid + obstacles ──────────────────────────────────────────────── */
    const uint32_t GW=16, GH=16; const float CELL=1.6f;
    CCGrid* grid=cc_grid_create(GW,GH,CELL);
    /* three staggered walls forming an S-maze */
    for (uint32_t y=0;y<11;y++) cc_grid_set_blocked(grid, 5, y, true);
    for (uint32_t y=5;y<16;y++) cc_grid_set_blocked(grid,10, y, true);
    for (uint32_t x=10;x<14;x++) cc_grid_set_blocked(grid, x, 5, true);

    /* ── A* route: bottom-left → top-right ─────────────────────────────── */
    CCCell path[512];
    uint32_t plen=cc_astar(grid, 1,1, 14,14, true, path, 512);

    /* ── hero walks the path ───────────────────────────────────────────── */
    float sx,sz; cc_grid_cell_to_world(grid,1,1,&sx,&sz);
    CCAgent hero=cc_agent_make((CCVec3){sx,0.4f,sz}, 5.0f, 9.0f);
    uint32_t wp=0; bool done=false;

    /* ── follower flock chasing the hero ───────────────────────────────── */
    #define NF 6
    CCAgent flock[NF];
    for (int i=0;i<NF;i++){
        float ang=i*(6.2831853f/NF);
        flock[i]=cc_agent_make((CCVec3){sx+cosf(ang)*1.5f,0.3f,sz+sinf(ang)*1.5f}, 5.0f, 16.0f);
    }

    /* trail of hero positions */
    CCVec3 trail[400]; int nt=0;

    const float dt=1.0f/60.0f;
    for (int step=0; step<1200; step++){
        CCVec3 s=cc_steer_path_follow(&hero,grid,path,plen,&wp,CELL*0.9f,&done);
        cc_agent_integrate(&hero,s,dt);
        if (step%6==0 && nt<400) trail[nt++]=hero.position;
        /* flock: seek hero + separate from each other */
        for (int i=0;i<NF;i++){
            CCVec3 seek=cc_steer_seek(&flock[i], hero.position);
            CCVec3 sep =cc_steer_separation(&flock[i], flock, NF, 1.2f);
            CCVec3 total=vec3_add(seek, vec3_scale(sep, 2.0f));
            cc_agent_integrate(&flock[i], total, dt);
        }
        if (done) break;   /* stop once hero reaches the goal */
    }

    /* ── render ────────────────────────────────────────────────────────── */
    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,26,20},.target={0,0,0},.up={0,1,0},
                          .fov_deg=45,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);

        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,m_ground,mg,&gxf);

        /* walls */
        for (uint32_t y=0;y<GH;y++) for (uint32_t x=0;x<GW;x++){
            if (!cc_grid_is_blocked(grid,x,y)) continue;
            float wx,wz; cc_grid_cell_to_world(grid,x,y,&wx,&wz);
            CCTransform3D wxf={.pos={wx,0.6f,wz},.rot={0,0,0,1},.scale={CELL*0.95f,1.2f,CELL*0.95f}};
            cc_draw_mesh(eng,m_wall,mw,&wxf);
        }
        /* A* path nodes (green) */
        for (uint32_t i=0;i<plen;i++){
            float wx,wz; cc_grid_cell_to_world(grid,path[i].x,path[i].y,&wx,&wz);
            CCTransform3D nx={.pos={wx,0.2f,wz},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(eng,m_node,mn,&nx);
        }
        /* hero trail */
        for (int i=0;i<nt;i++){
            CCTransform3D tx={.pos={trail[i].x,0.25f,trail[i].z},.rot={0,0,0,1},.scale={0.6f,0.6f,0.6f}};
            cc_draw_mesh(eng,m_dot,mh,&tx);
        }
        /* hero */
        CCTransform3D hx={.pos={hero.position.x,0.5f,hero.position.z},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,m_hero,mh,&hx);
        /* flock */
        for (int i=0;i<NF;i++){
            CCTransform3D fx2={.pos={flock[i].position.x,0.35f,flock[i].position.z},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(eng,m_dot,mf,&fx2);
        }
        cc_frame_end(eng);
    }
    const char* sp=cc_screenshot(eng,out);
    printf("screenshot: %s  path_cells=%u hero_done=%d hero=(%.1f,%.1f)\n",
           sp?sp:"(null)", plen, done, hero.position.x, hero.position.z);
    cc_grid_destroy(grid); cc_shutdown(eng); return 0;
}
