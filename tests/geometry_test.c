/* Geometry test — the new primitive set (cylinder, cone, capsule, torus) lined
 * up on a lit, shadowed ground, plus a smooth-vs-flat pair built from the SAME
 * raw vertex buffer to prove cc_geometry_recompute_normals + weld. */
#include "cc/claudecore.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Build a low-poly icosphere-ish sphere by hand (unwelded, flat) so we can
 * demonstrate weld + smooth-normal recompute turning it into a smooth mesh.
 * Here we just reuse a coarse UV sphere emitted as independent triangles. */
#define TAU 6.28318530717958647692f
#define PI  3.14159265358979323846f
static CCMesh coarse_ball(CCEngine* eng, float r, uint32_t seg, bool smooth) {
    uint32_t slices=seg, stacks=seg;
    uint32_t tri = slices*stacks*2;
    uint32_t nv  = tri*3;               /* fully unwelded: 3 verts per tri */
    CCVertex* V = malloc(nv*sizeof(CCVertex));
    uint32_t*  I = malloc(nv*sizeof(uint32_t));
    uint32_t vi=0;
    /* sph(i,j) → xyz into a float[3] */
    #define SPH(dst,ii,jj) do{ \
        float ph=PI*(jj)/stacks-PI*0.5f, th=TAU*(ii)/slices; \
        (dst)[0]=r*cosf(ph)*cosf(th); (dst)[1]=r*sinf(ph); (dst)[2]=r*cosf(ph)*sinf(th); \
    }while(0)
    for (uint32_t j=0;j<stacks;j++) for (uint32_t i=0;i<slices;i++){
        float a[3],b[3],c[3],d[3];
        SPH(a,i,j); SPH(b,i,j+1); SPH(c,i+1,j+1); SPH(d,i+1,j);
        float* quad[6]={a,b,c, a,c,d};
        float uv[6][2]={{(float)i/slices,(float)j/stacks},{(float)i/slices,(float)(j+1)/stacks},
                        {(float)(i+1)/slices,(float)(j+1)/stacks},{(float)i/slices,(float)j/stacks},
                        {(float)(i+1)/slices,(float)(j+1)/stacks},{(float)(i+1)/slices,(float)j/stacks}};
        for(int k=0;k<6;k++){
            CCVertex* v=&V[vi];
            v->pos[0]=quad[k][0];v->pos[1]=quad[k][1];v->pos[2]=quad[k][2];
            v->normal[0]=0;v->normal[1]=0;v->normal[2]=0;
            v->uv[0]=uv[k][0];v->uv[1]=uv[k][1];
            v->tangent[0]=1;v->tangent[1]=0;v->tangent[2]=0;v->tangent[3]=1;
            v->color[0]=v->color[1]=v->color[2]=v->color[3]=255;
            I[vi]=vi; vi++;
        }
    }
    #undef SPH
    uint32_t final_nv=nv;
    if (smooth) {
        /* weld coincident verts, then average normals across shared faces */
        cc_geometry_weld(V,&final_nv,I,nv,1e-4f);
        cc_geometry_recompute_normals(V,final_nv,I,nv,true);
    } else {
        cc_geometry_recompute_normals(V,final_nv,I,nv,false); /* flat / faceted */
    }
    cc_geometry_recompute_tangents(V,final_nv,I,nv);
    CCMesh m=cc_mesh_create(eng,V,final_nv,I,nv,CC_MESH_STATIC);
    free(V);free(I);
    return m;
}

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/geometry_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    CCMesh ground   = cc_mesh_plane(eng,60,60,4);
    CCMesh cylinder = cc_mesh_cylinder(eng,0.8f,2.2f,32);
    CCMesh cone     = cc_mesh_cone(eng,1.0f,2.4f,32);
    CCMesh capsule  = cc_mesh_capsule(eng,0.7f,1.6f,24);
    CCMesh torus    = cc_mesh_torus(eng,1.0f,0.35f,32);
    CCMesh ball_flat   = coarse_ball(eng,0.95f,10,false);
    CCMesh ball_smooth = coarse_ball(eng,0.95f,10,true);

    CCMaterialDesc gd={.base_color={0.55f,0.56f,0.6f,1},.roughness=0.92f};
    CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc md={.base_color={0.80f,0.35f,0.28f,1},.roughness=0.42f,.metallic=0.1f};
    CCMaterial mr=cc_material_create(eng,&md);
    CCMaterialDesc bd={.base_color={0.35f,0.55f,0.85f,1},.roughness=0.35f,.metallic=0.25f};
    CCMaterial mb=cc_material_create(eng,&bd);
    CCMaterialDesc yd={.base_color={0.85f,0.72f,0.30f,1},.roughness=0.3f,.metallic=0.6f};
    CCMaterial my=cc_material_create(eng,&yd);

    cc_light_set_ambient(eng,0.14f,0.15f,0.18f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.5f,-0.85f,-0.35f},
                 .color={1.0f,0.96f,0.88f},.intensity=3.1f,.cast_shadows=true};
    cc_light_add(eng,&sun);

    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.3f; fx.bloom_intensity=0.1f;
    fx.vignette=true; fx.vignette_strength=0.28f;
    cc_postfx_set(eng,&fx);

    #define Q(yy) {0,sinf(yy*0.5f),0,cosf(yy*0.5f)}
    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,5.5f,13},.target={0,0.6f,0},.up={0,1,0},
                          .fov_deg=52,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);

        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mg,&gxf);

        /* front row: the four new primitives */
        CCTransform3D t_cyl ={.pos={-4.5f,1.1f,1.5f},.rot=Q(0.3f),.scale={1,1,1}};
        cc_draw_mesh(eng,cylinder,mr,&t_cyl);
        CCTransform3D t_cone={.pos={-1.5f,1.2f,1.5f},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,cone,mb,&t_cone);
        CCTransform3D t_cap ={.pos={1.5f,1.5f,1.5f},.rot=Q(0.5f),.scale={1,1,1}};
        cc_draw_mesh(eng,capsule,my,&t_cap);
        CCTransform3D t_tor ={.pos={4.5f,1.0f,1.5f},.rot=Q(1.1f),.scale={1,1,1}};
        cc_draw_mesh(eng,torus,mr,&t_tor);

        /* back row: SAME source buffer, flat (left) vs smooth (right) */
        CCTransform3D t_bf={.pos={-1.6f,1.0f,-2.5f},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ball_flat,mb,&t_bf);
        CCTransform3D t_bs={.pos={1.6f,1.0f,-2.5f},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ball_smooth,mb,&t_bs);

        cc_frame_end(eng);
    }
    #undef Q
    const char* s=cc_screenshot(eng,out); printf("screenshot: %s\n",s?s:"(null)");
    cc_shutdown(eng); return 0;
}
