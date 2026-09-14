/* trail_test — motion trails. A glowing sphere flies in an arc; each frame we
 * push its position into a CCTrail and draw the fading streak behind it. Also
 * asserts the trail's point count grows then caps, and that clear() empties it. */
#include "cc/claudecore.h"
#include "cc/trail.h"
#include "cc/debug.h"
#include <stdio.h>
#include <math.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/trail_test.png";

    /* ── data checks ── */
    CCTrail* dt=cc_trail_create(8, 1.0f);
    CHECK(cc_trail_point_count(dt)==0,"empty at start");
    for(int i=0;i<5;i++){ cc_trail_push(dt,(float)i,0,0); }
    CHECK(cc_trail_point_count(dt)==5,"5 points after 5 pushes");
    cc_trail_push(dt,4.0f,0,0);   /* duplicate of last → ignored */
    CHECK(cc_trail_point_count(dt)==5,"duplicate push ignored");
    for(int i=0;i<20;i++) cc_trail_push(dt,(float)(100+i),0,0);
    CHECK(cc_trail_point_count(dt)==8,"capped at capacity 8");
    cc_trail_update(dt,2.0f);     /* all older than 1s lifetime → dropped */
    CHECK(cc_trail_point_count(dt)==0,"all expire after lifetime");
    cc_trail_push(dt,0,0,0); cc_trail_push(dt,1,0,0);
    cc_trail_clear(dt);
    CHECK(cc_trail_point_count(dt)==0,"clear empties");
    cc_trail_destroy(dt);
    printf("trail data checks: %s\n", fails?"SOME FAILED":"all passed");

    /* ── visual: arcing projectile with a trail ── */
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=480;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e){ return fails?2:0; }
    CCMesh ball=cc_mesh_sphere(e,0.28f,20,16);
    CCMesh ground=cc_mesh_plane(e,60,60,4);
    CCMaterialDesc bd={.base_color={1.0f,0.7f,0.2f,1},.roughness=0.3f,.emissive={2.0f,1.2f,0.3f},.tint={1,1,1,1}};
    CCMaterial bmat=cc_material_create(e,&bd);
    CCMaterialDesc gd={.base_color={0.28f,0.3f,0.33f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);

    cc_light_set_ambient(e,0.14f,0.15f,0.18f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.7f,-0.5f},.color={1,0.96f,0.9f},.intensity=2.6f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.28f,0.4f,0.62f},h[3]={0.5f,0.55f,0.62f},g[3]={0.2f,0.2f,0.2f};
    cc_light_set_sky_colors(e,z,h,g,1.0f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.2f; fx.taa=true;
    fx.bloom=true; fx.bloom_threshold=1.1f; fx.bloom_intensity=0.12f;
    cc_postfx_set(e,&fx);

    CCTrail* trail=cc_trail_create(48, 0.9f);
    float tsec=0;
    /* advance the projectile over ~30 frames BEFORE the final capture so the
       trail is well-formed; we render every frame but only the last is saved */
    const char* s=NULL;
    for(int f=0;f<34;f++){
        tsec += 0.03f;
        /* arc: x moves left→right, y is a parabola */
        float px=-5.0f + tsec*4.0f;
        float py= 0.4f + 4.5f*tsec*(1.6f-tsec);   /* parabola peaking mid-flight */
        float pz= 0.0f;
        cc_trail_push(trail, px, py, pz);
        cc_trail_update(trail, 0.03f);

        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,3.0f,11},.target={0,2.2f,0},.up={0,1,0},.fov_deg=52,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        CCTransform3D bt={.pos={px,py,pz},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ball,bmat,&bt);
        cc_trail_draw(e, trail, 1.0f, 0.75f, 0.3f);   /* warm fading streak */
        cc_debug_overlay(e);   /* flush world-space gizmo lines (incl. the trail) */
        cc_frame_end(e);
    }
    s=cc_screenshot(e,out);
    printf("trail points at capture: %u\n", cc_trail_point_count(trail));
    printf("screenshot: %s (arcing projectile + fading trail)\n", s?s:"(null)");

    cc_trail_destroy(trail);
    cc_shutdown(e);
    if(fails){ printf("TRAIL TEST: %d FAILURE(S)\n",fails); return 2; }
    printf("TRAIL TEST: all checks passed\n");
    return 0;
}
