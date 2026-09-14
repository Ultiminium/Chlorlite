/* editmesh_test — the editable half-edge modeling system, rendered. Three
 * shapes, all STARTING from the same cube, show "add points anywhere + pull":
 *   left   — plain cube (8 corners)
 *   middle — a point added mid-face and pulled out into a pyramid-spike
 *   right  — every edge split + faces poked, points nudged → a faceted blob
 * Each editable mesh is baked to a GPU mesh (flat normals to show facets) and
 * lit. Proves the topology edits produce correct, renderable geometry. */
#include "cc/claudecore.h"
#include "cc/editmesh.h"
#include <math.h>
#include <stdio.h>

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/editmesh_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh ground=cc_mesh_plane(eng,40,40,4);
    CCMaterialDesc gd={.base_color={0.5f,0.52f,0.55f,1},.roughness=0.95f}; CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc md={.base_color={0.8f,0.45f,0.3f,1},.roughness=0.45f,.metallic=0.15f}; CCMaterial mm=cc_material_create(eng,&md);
    CCMaterialDesc bd={.base_color={0.4f,0.6f,0.85f,1},.roughness=0.4f,.metallic=0.2f}; CCMaterial mb=cc_material_create(eng,&bd);
    CCMaterialDesc yd={.base_color={0.85f,0.72f,0.35f,1},.roughness=0.35f,.metallic=0.35f}; CCMaterial my=cc_material_create(eng,&yd);

    cc_light_set_ambient(eng,0.15f,0.16f,0.19f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.45f,-0.8f,-0.4f},.color={1,0.96f,0.9f},.intensity=3.0f,.cast_shadows=true};
    cc_light_add(eng,&sun);
    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.3f; fx.bloom_intensity=0.08f;
    fx.vignette=true; fx.vignette_strength=0.28f; cc_postfx_set(eng,&fx);

    /* LEFT: plain cube */
    CCEditMesh* A=cc_editmesh_cube(1.6f);
    CCMesh meshA=cc_editmesh_bake(A,eng,false);

    /* MIDDLE: poke the top face, pull the center vertex up into a spike */
    CCEditMesh* B=cc_editmesh_cube(1.6f);
    uint32_t center=cc_editmesh_poke_face(B,1);          /* face 1 = top (+y) */
    CCVec3 cp=cc_editmesh_vertex_position(B,center);
    cc_editmesh_move_vertex(B,center,(CCVec3){cp.x,cp.y+1.6f,cp.z});  /* pull up */
    /* also split a bottom edge and pull it out sideways for asymmetry */
    uint32_t e=cc_editmesh_find_edge(B,0,1);
    uint32_t s=cc_editmesh_split_edge(B,e,0.5f);
    CCVec3 sp=cc_editmesh_vertex_position(B,s);
    cc_editmesh_move_vertex(B,s,(CCVec3){sp.x,sp.y-0.1f,sp.z-0.9f});
    char err[128]; printf("B valid=%d\n", cc_editmesh_validate(B,err,128));
    CCMesh meshB=cc_editmesh_bake(B,eng,false);

    /* RIGHT: Catmull-Clark subdivide the cube twice → smooth rounded form,
       plus bevel one corner of a fourth cube shown behind. */
    CCEditMesh* C=cc_editmesh_cube(1.8f);
    cc_editmesh_subdivide(C,2);           /* true Catmull-Clark → sphere-ish */
    uint32_t vc=cc_editmesh_vertex_count(C);
    printf("C: verts=%u faces=%u\n", vc, cc_editmesh_face_count(C));
    CCMesh meshC=cc_editmesh_bake(C,eng,true);   /* smooth normals */

    /* FOURTH: a beveled cube (corner truncated) */
    CCEditMesh* D=cc_editmesh_cube(1.5f);
    cc_editmesh_bevel_vertex(D,6,0.7f);
    cc_editmesh_bevel_vertex(D,0,0.7f);
    CCMesh meshD=cc_editmesh_bake(D,eng,false);

    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,3.4f,10.5f},.target={0,0.6f,0},.up={0,1,0},
                          .fov_deg=50,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D g={.pos={0,-1.2f,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,ground,mg,&g);
        CCTransform3D xa={.pos={-4.2f,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,meshA,mm,&xa);
        CCTransform3D xb={.pos={-1.4f,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,meshB,mb,&xb);
        CCTransform3D xc={.pos={1.6f,0.3f,0}, .rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,meshC,my,&xc);
        CCMaterialDesc pd={.base_color={0.55f,0.8f,0.45f,1},.roughness=0.4f,.metallic=0.1f}; static CCMaterial mp; if(!mp)mp=cc_material_create(eng,&pd);
        CCTransform3D xd={.pos={4.4f,0,0}, .rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(eng,meshD,mp,&xd);
        cc_frame_end(eng);
    }
    const char* sp2=cc_screenshot(eng,out);
    printf("screenshot: %s\n", sp2?sp2:"(null)");
    cc_editmesh_free(A);cc_editmesh_free(B);cc_editmesh_free(C);cc_editmesh_free(D);
    cc_shutdown(eng); return 0;
}
