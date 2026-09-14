/* canongrid — a FIXED canonical render space for exact, repeatable alignment.
 *
 * The idea (human's): every render is measured against ONE absolute frame with a
 * shared, immovable origin, so "is it centered / aligned / rotated right" stops
 * being a visual judgment and becomes an exact coordinate.
 *
 *   - The frame is ALWAYS 580 x 720 pixels. 1 cube = 1 pixel.
 *   - The origin C=0 is ALWAYS the center, pixel (290, 360). Not configurable.
 *   - Coordinates are SIGNED OFFSETS from center. X in [-290..+289], Y in [-360..+359].
 *     Y is UP (+Y toward the top of the image).
 *   - Notation "XYZ-ABC:rot":  XYZ = signed X offset, ABC = signed Y offset,
 *     rot = degrees 0..359 (0 = +X/right, CCW). e.g. "+050-+030:190".
 *   - An object's ID is computed from its ACTUAL world position (geometry/transform),
 *     projected into this fixed frame — the exact truth. A pixel-centroid read of the
 *     render is ALSO reported as a cross-check; if the two disagree, the render is
 *     placing the object somewhere other than the data says → a bug, surfaced.
 *   - A concentric-ring overlay at C=0 is the rotation guide (display addon).
 *
 * This is the alignment analog of modelcheck: data is the truth, the render is a
 * sanity check. The frame size and origin are locked precisely so results are
 * comparable across every render, every session.
 */
#include "cc/claudecore.h"
#include "cc/render.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ---- the immovable constants ---- */
#define CANON_W 580
#define CANON_H 720
#define CANON_CX 290    /* C=0 x  (CANON_W/2) */
#define CANON_CY 360    /* C=0 y  (CANON_H/2) */

typedef struct {
    int   x_off, y_off;     /* signed offset from C=0, in cubes/pixels, Y-up */
    int   rot_deg;          /* 0..359 */
    int   valid;            /* projected in front of camera */
    int   px_x_off, px_y_off; /* pixel-centroid cross-check offset (if measured) */
    int   has_pixel;        /* whether the centroid cross-check ran */
} CanonID;

/* project a world point into the FIXED canonical frame (origin-locked). Uses the
   engine's current camera VP — but the OUTPUT is always mapped onto 580x720 with
   C=0 at center, regardless of the actual render resolution, so IDs are canonical. */
static int canon_project(CCEngine* eng, CCVec3 w, float* out_x_off, float* out_y_off){
    extern CCRenderer* cc_engine_renderer(CCEngine*);
    extern void cc_renderer_camera_vp(CCRenderer*, float*);
    CCRenderer* r=cc_engine_renderer(eng); if(!r) return 0;
    float vp[16]; cc_renderer_camera_vp(r,vp);
    float x=vp[0]*w.x+vp[4]*w.y+vp[8]*w.z+vp[12];
    float y=vp[1]*w.x+vp[5]*w.y+vp[9]*w.z+vp[13];
    float wc=vp[3]*w.x+vp[7]*w.y+vp[11]*w.z+vp[15];
    if(wc<=0.0001f) return 0;
    float ndc_x = x/wc;           /* -1..+1 */
    float ndc_y = y/wc;           /* -1..+1, +Y up in NDC */
    /* map NDC to canonical signed offsets: NDC 0 → offset 0 (C=0). */
    *out_x_off = ndc_x * (CANON_W*0.5f);
    *out_y_off = ndc_y * (CANON_H*0.5f);   /* Y-up preserved */
    return 1;
}

/* Compute an object's canonical ID from its world position + a rotation (degrees).
   rotation is passed in by the caller (it's the object's own facing/spin — the grid
   doesn't invent it, it records it in the notation). */
CanonID cc_canon_id(CCEngine* eng, CCVec3 world_pos, int rotation_deg){
    CanonID id; memset(&id,0,sizeof id);
    float xo,yo;
    if(canon_project(eng, world_pos, &xo, &yo)){
        id.x_off=(int)lroundf(xo); id.y_off=(int)lroundf(yo); id.valid=1;
    }
    id.rot_deg = ((rotation_deg % 360)+360)%360;
    return id;
}

/* Pixel-centroid cross-check: read the rendered frame, find the subject's centroid,
   express it as a signed offset from C=0 (mapped onto the canonical 580x720). Fills
   px_x_off/px_y_off. Compares against the geometry ID; a mismatch is a signal. */
void cc_canon_pixel_check(CCEngine* eng, CanonID* id){
    uint32_t w,h; uint8_t* px=NULL; cc_frame_pixels(eng,&px,&w,&h);
    if(!px||!w||!h) return;
    double sx=0,sy=0; long n=0;
    for(uint32_t y=0;y<h;y++) for(uint32_t x=0;x<w;x++){
        const uint8_t* p=px+((size_t)y*w+x)*4;
        if(p[0]+p[1]+p[2] > 24){ sx+=x; sy+=y; n++; }
    }
    if(n<1) return;
    double cx=sx/n, cy=sy/n;
    /* map render-pixel centroid onto canonical frame (normalize by actual res → 580x720) */
    double nx = (cx/w)*2.0-1.0;          /* -1..1 */
    double ny = 1.0-(cy/h)*2.0;          /* -1..1, flip to Y-up */
    id->px_x_off=(int)lround(nx*(CANON_W*0.5));
    id->px_y_off=(int)lround(ny*(CANON_H*0.5));
    id->has_pixel=1;
}

/* format "XYZ-ABC:rot" with explicit signs, zero-padded to 3 digits.
   e.g. +050-+030:190 , -012--045:000 */
void cc_canon_format(const CanonID* id, char* buf, size_t n){
    if(!id->valid){ snprintf(buf,n,"OFFSCREEN"); return; }
    snprintf(buf,n,"%+04d-%+04d:%03d", id->x_off, id->y_off, id->rot_deg);
}

/* Draw the canonical guide overlay onto the current frame: center crosshair at C=0,
   the X/Y axes, and concentric rotation rings. This is the DISPLAY addon — the
   coordinate truth doesn't depend on it. Uses cc_draw_rect for pixel marks (works
   in the 2D overlay pass). ring_count concentric rings; radii evenly spaced. */
void cc_canon_overlay(CCEngine* eng, int ring_count){
    if(ring_count<1) ring_count=4;
    uint32_t axis_col=0x39ff8850, ring_col=0x39ff8830, ctr_col=0xff5a5aff;
    /* axes across the whole frame through C=0 */
    for(int x=0;x<CANON_W;x++) cc_draw_rect(eng, x, CANON_CY, 1,1, axis_col,0,0);
    for(int y=0;y<CANON_H;y++) cc_draw_rect(eng, CANON_CX, y, 1,1, axis_col,0,0);
    /* concentric rings */
    int maxr = (CANON_W<CANON_H?CANON_W:CANON_H)/2 - 4;
    for(int k=1;k<=ring_count;k++){
        int rr = maxr*k/ring_count;
        for(int a=0;a<360;a+=1){
            float th=a*3.14159265f/180.0f;
            int px=CANON_CX+(int)lroundf(cosf(th)*rr);
            int py=CANON_CY-(int)lroundf(sinf(th)*rr);   /* Y-up */
            if(px>=0&&px<CANON_W&&py>=0&&py<CANON_H) cc_draw_rect(eng,px,py,1,1,ring_col,0,0);
        }
    }
    /* rotation tick marks every 30° on the outer ring */
    for(int a=0;a<360;a+=30){
        float th=a*3.14159265f/180.0f;
        int px=CANON_CX+(int)lroundf(cosf(th)*maxr);
        int py=CANON_CY-(int)lroundf(sinf(th)*maxr);
        cc_draw_rect(eng,px-1,py-1,3,3,ctr_col,0,0);
    }
    /* C=0 marker */
    cc_draw_rect(eng, CANON_CX-2, CANON_CY-2, 5,5, ctr_col,0,0);
}

/* Report a single object: geometry ID + pixel cross-check + mismatch flag. */
void cc_canon_report(CCEngine* eng, const char* label, CCVec3 world_pos, int rot){
    CanonID id=cc_canon_id(eng, world_pos, rot);
    cc_canon_pixel_check(eng,&id);
    char s[64]; cc_canon_format(&id,s,sizeof s);
    printf("  %-16s geometry=%s", label?label:"(obj)", s);
    if(id.has_pixel){
        int dx=id.px_x_off-id.x_off, dy=id.px_y_off-id.y_off;
        int dist=(int)lround(sqrt((double)dx*dx+dy*dy));
        printf("   pixel-centroid=%+04d-%+04d  (mismatch %dpx%s)",
            id.px_x_off,id.px_y_off, dist, dist>20?" ⚠ render != data":"");
    }
    printf("\n");
}

/* ---- self-test / demo ---- */
#ifdef CANON_MAIN
int main(void){
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=CANON_W; cfg.height=CANON_H; cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); cc_aa_set_mode(e,CC_AA_OFF);
    CCMesh sph=cc_mesh_sphere(e,0.6f,24,24);
    CCMaterialDesc md={.base_color={0.8f,0.4f,0.2f,1},.roughness=0.6f,.tint={1,1,1,1}};
    CCMaterial mat=cc_material_create(e,&md);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.3f,-0.8f,-0.3f},.color={1,1,1},.intensity=2};
    cc_light_add(e,&sun); cc_light_set_ambient(e,0.3f,0.3f,0.3f,1);

    /* place three objects at known world positions */
    CCVec3 positions[3]={ {0,0,0}, {1.5f,0,0}, {0,1.0f,0} };
    int rots[3]={0,90,190};

    cc_frame_begin(e);
    CCCameraDesc cam={.pos={0,0,5},.target={0,0,0},.up={0,1,0},.fov_deg=55,.near_plane=0.1f,.far_plane=100,.exposure=1};
    cc_camera_set(e,&cam);
    for(int i=0;i<3;i++){ CCTransform3D xf={.pos={positions[i].x,positions[i].y,positions[i].z},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(e,sph,mat,&xf); }
    cc_canon_overlay(e,4);
    cc_frame_end(e);
    cc_screenshot(e,"/tmp/canon_demo.png");

    printf("=== canonical grid: 580x720, C=0 at (%d,%d), Y-up ===\n",CANON_CX,CANON_CY);
    for(int i=0;i<3;i++){
        char lbl[16]; snprintf(lbl,sizeof lbl,"obj%d",i);
        cc_canon_report(e,lbl,positions[i],rots[i]);
    }
    cc_shutdown(e); return 0;
}
#endif
