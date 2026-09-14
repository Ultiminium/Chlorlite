/* Billboard test — a swarm of glowing spherical billboards (particles) and a
 * row of cylindrical billboards (impostor "trees"), over a lit ground plane.
 * Verifies both modes face the camera and that emissive billboards bloom. */
#include "cc/claudecore.h"
#include "cc/debug.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* soft radial gradient sprite: white core → transparent edge */
static void gen_particle(uint8_t* px, uint32_t w, uint32_t h, void* ud) {
    (void)ud;
    for (uint32_t y=0;y<h;y++) for (uint32_t x=0;x<w;x++) {
        float dx=(x+0.5f)/w-0.5f, dy=(y+0.5f)/h-0.5f;
        float d=sqrtf(dx*dx+dy*dy)*2.0f;      /* 0 center → 1 edge */
        float a=1.0f-d; if(a<0)a=0; a=a*a;      /* smooth falloff */
        uint8_t v=255;
        px[(y*w+x)*4+0]=v; px[(y*w+x)*4+1]=v; px[(y*w+x)*4+2]=v;
        px[(y*w+x)*4+3]=(uint8_t)(a*255);
    }
}

/* simple opaque "tree": green blob on a brown trunk, alpha-cut silhouette */
static void gen_tree(uint8_t* px, uint32_t w, uint32_t h, void* ud) {
    (void)ud;
    for (uint32_t y=0;y<h;y++) for (uint32_t x=0;x<w;x++) {
        float fx=(x+0.5f)/w, fy=(y+0.5f)/h;   /* fy: 0 top → 1 bottom */
        int r=40,g=30,b=25,a=0;
        /* canopy: circle in upper 2/3 */
        float cx=fx-0.5f, cy=fy-0.35f;
        if (sqrtf(cx*cx+cy*cy) < 0.34f) { r=40; g=120+(int)(30*fx); b=45; a=255; }
        /* trunk: narrow bar lower third */
        if (fy>0.62f && fabsf(fx-0.5f)<0.07f) { r=90; g=60; b=35; a=255; }
        px[(y*w+x)*4+0]=r; px[(y*w+x)*4+1]=g; px[(y*w+x)*4+2]=b; px[(y*w+x)*4+3]=a;
    }
}

int main(int argc, char** argv) {
    const char* out = (argc>1)?argv[1]:"/tmp/billboard_test.png";

    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width=1024; cfg.height=576; cfg.verbose=false;
    CCEngine* eng = cc_init(&cfg);
    if(!eng){ fprintf(stderr,"init failed\n"); return 1; }

    CCMesh ground = cc_mesh_plane(eng, 60.0f, 60.0f, 4);
    CCMaterialDesc gd={.base_color={0.20f,0.22f,0.20f,1}, .roughness=0.9f};
    CCMaterial mat_ground = cc_material_create(eng,&gd);

    CCTextureDesc ptd={.width=64,.height=64,.format=CC_FMT_RGBA8,.linear_filter=true};
    CCTexture tex_particle = cc_texture_proc(eng,&ptd,gen_particle,NULL);
    CCTextureDesc ttd={.width=128,.height=128,.format=CC_FMT_RGBA8,.linear_filter=true};
    CCTexture tex_tree = cc_texture_proc(eng,&ttd,gen_tree,NULL);

    cc_light_set_ambient(eng,0.06f,0.07f,0.09f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-1.0f,-0.35f},
                 .color={1.0f,0.95f,0.85f},.intensity=2.5f};
    cc_light_add(eng,&sun);

    CCPostFX fx=cc_postfx_default();
    fx.bloom=true; fx.bloom_threshold=0.8f; fx.bloom_intensity=0.15f;
    fx.vignette=true; fx.vignette_strength=0.25f;
    cc_postfx_set(eng,&fx);

    /* Particle swarm: 200 glowing spherical billboards in a rising column. */
    const int NP=200;
    CCBillboard* parts=(CCBillboard*)malloc(sizeof(CCBillboard)*NP);
    for(int i=0;i<NP;i++){
        float t=i/(float)NP;
        float ang=t*30.0f;
        float rad=1.5f+2.0f*t;
        parts[i].pos[0]=cosf(ang)*rad;
        parts[i].pos[1]=0.5f+t*6.0f;
        parts[i].pos[2]=sinf(ang)*rad - 3.0f;
        parts[i].size[0]=parts[i].size[1]=0.35f*(1.0f-0.5f*t);
        /* hot orange → yellow, >1 so they bloom */
        parts[i].color[0]=3.0f; parts[i].color[1]=1.2f+t*1.5f; parts[i].color[2]=0.3f;
        parts[i].color[3]=1.0f;
    }

    /* Tree row: 7 cylindrical billboards. */
    const int NT=7;
    CCBillboard trees[7];
    for(int i=0;i<NT;i++){
        trees[i].pos[0]=-9.0f+i*3.0f;
        trees[i].pos[1]=1.5f;      /* half-height above ground */
        trees[i].pos[2]=3.0f;
        trees[i].size[0]=2.2f; trees[i].size[1]=3.0f;
        trees[i].color[0]=trees[i].color[1]=trees[i].color[2]=trees[i].color[3]=1.0f;
    }

    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,5,14},.target={0,3,-2},.up={0,1,0},
                          .fov_deg=55.0f,.near_plane=0.1f,.far_plane=200.0f,.exposure=1.0f};
        cc_camera_set(eng,&cam);

        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mat_ground,&gxf);

        cc_draw_billboards(eng, tex_tree, trees, NT, CC_BILLBOARD_CYLINDRICAL);
        cc_draw_billboards(eng, tex_particle, parts, NP, CC_BILLBOARD_SPHERICAL);
        cc_frame_end(eng);
    }

    CCFrameStats st=cc_debug_stats(eng);
    printf("draw_calls=%u meshes_drawn=%u triangles=%u\n",
           st.draw_calls, st.meshes_drawn, st.triangles);
    const char* saved=cc_screenshot(eng,out);
    printf("screenshot: %s\n", saved?saved:"(null)");
    free(parts);
    cc_shutdown(eng);
    return 0;
}
