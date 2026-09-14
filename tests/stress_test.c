/* stress_test — deliberately VARIED geometry to expose bugs that spheres hide:
 * hard edges (cube), thin tips (cone), holes/self-occlusion (torus), curved
 * caps (capsule/cylinder), a flat card (quad, backface test), and an edited
 * mesh (bevel+subdiv). Mixed materials incl. sharp metal (worst case for normal
 * seams) and rough dielectric. Two lights + shadows. Scrutinize edges, seams,
 * silhouettes, the torus hole, and the cone tip. */
#include "cc/claudecore.h"
#include "cc/editmesh.h"
#include <math.h>
#include <stdio.h>

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/stress_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1280;cfg.height=560;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh ground=cc_mesh_plane(eng,60,60,1);
    CCMesh cube=cc_mesh_cube(eng,1.3f);
    CCMesh cyl=cc_mesh_cylinder(eng,0.6f,1.6f,32);
    CCMesh cone=cc_mesh_cone(eng,0.7f,1.6f,32);
    CCMesh cap=cc_mesh_capsule(eng,0.5f,1.4f,24);
    CCMesh torus=cc_mesh_torus(eng,0.7f,0.28f,32);
    CCMesh quad=cc_mesh_quad(eng);

    /* edited mesh: cube → bevel two corners + one subdivide (varied topology) */
    CCEditMesh* em=cc_editmesh_cube(1.3f);
    cc_editmesh_bevel_vertex(em,6,0.5f);
    cc_editmesh_bevel_vertex(em,0,0.5f);
    CCMesh edited=cc_editmesh_bake(em,eng,false);

    /* materials: sharp metal (harsh on seams), rough dielectric, mid plastic */
    CCMaterialDesc metal={.base_color={0.9f,0.9f,0.92f,1},.roughness=0.18f,.metallic=1.0f,.tint={1,1,1,1}};
    CCMaterialDesc gold ={.base_color={1.0f,0.78f,0.34f,1},.roughness=0.28f,.metallic=1.0f,.tint={1,1,1,1}};
    CCMaterialDesc red  ={.base_color={0.85f,0.3f,0.28f,1},.roughness=0.55f,.metallic=0.0f,.tint={1,1,1,1}};
    CCMaterialDesc blue ={.base_color={0.3f,0.5f,0.85f,1},.roughness=0.4f,.metallic=0.2f,.tint={1,1,1,1}};
    CCMaterialDesc green={.base_color={0.4f,0.7f,0.4f,1},.roughness=0.6f,.metallic=0.0f,.tint={1,1,1,1}};
    CCMaterialDesc white={.base_color={0.9f,0.5f,0.5f,1},.roughness=0.5f,.double_sided=true,.tint={1,1,1,1}};
    CCMaterial mMetal=cc_material_create(eng,&metal), mGold=cc_material_create(eng,&gold);
    CCMaterial mRed=cc_material_create(eng,&red), mBlue=cc_material_create(eng,&blue);
    CCMaterial mGreen=cc_material_create(eng,&green), mWhite=cc_material_create(eng,&white);
    CCMaterialDesc gd={.base_color={0.55f,0.55f,0.58f,1},.roughness=0.85f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(eng,&gd);

    cc_light_set_ambient(eng,0.10f,0.11f,0.14f,1.0f);
    CCLight key={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-0.7f,-0.45f},.color={1,0.96f,0.88f},.intensity=2.8f,.cast_shadows=true};
    cc_light_add(eng,&key);
    CCLight fill={.type=CC_LIGHT_POINT,.pos={4,3,3},.color={0.5f,0.6f,1.0f},.intensity=8.0f,.range=20};
    cc_light_add(eng,&fill);
    float z[3]={0.16f,0.26f,0.5f},h2[3]={0.4f,0.46f,0.56f},g2[3]={0.14f,0.15f,0.14f};
    cc_light_set_sky_colors(eng,z,h2,g2,0.6f);
    CCPostFX fx=cc_postfx_default(); fx.auto_exposure=true; fx.ae_key=0.2f;
    fx.bloom=true; fx.bloom_threshold=1.3f; fx.bloom_intensity=0.07f;
    cc_postfx_set(eng,&fx);

    /* lay out 7 objects in a row, varied rotations */
    struct { CCMesh m; CCMaterial mat; float x,y,z,rot; } items[] = {
        {cube,  mMetal,  -6.0f, 0.65f, 0, 0.5f},
        {cyl,   mRed,    -4.0f, 0.8f,  0, 0.0f},
        {cone,  mGold,   -2.0f, 0.8f,  0, 0.0f},
        {cap,   mBlue,    0.0f, 0.9f,  0, 0.3f},
        {torus, mGreen,   2.2f, 0.9f,  0, 1.2f},
        {edited,mWhite,   4.4f, 0.7f,  0, 0.7f},
        {quad,  mWhite,   6.4f, 1.0f,  0, 0.0f},
    };
    for(int frame=0;frame<6;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,2.6f,10},.target={0,0.7f,0},.up={0,1,0},
                          .fov_deg=52,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D g={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,ground,mg,&g);
        for(int i=0;i<7;i++){ float hr=items[i].rot*0.5f;
            CCTransform3D t={.pos={items[i].x,items[i].y,items[i].z},
                             .rot={0,sinf(hr),0,cosf(hr)},.scale={1,1,1}};
            cc_draw_mesh(eng,items[i].m,items[i].mat,&t); }
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s (cube/cyl/cone/capsule/torus/edited/quad; metal+dielectric; 2 lights)\n", s?s:"(null)");
    cc_editmesh_free(em); cc_shutdown(eng); return 0;
}
