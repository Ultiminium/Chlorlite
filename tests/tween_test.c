/* tween_test — tween + timer system. Data checks for tween completion, easing
 * monotonicity, on-complete callbacks, one-shot + repeating timers, and cancel;
 * plus a visual render of several objects tweened into place with different
 * easing curves. */
#include "cc/claudecore.h"
#include "cc/tween.h"
#include <stdio.h>
#include <math.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)
static int feq(float a,float b){ return fabsf(a-b)<1e-3f; }

static int g_done=0;
static void on_done(void* ud){ (void)ud; g_done++; }
static int g_tick=0;
static void on_tick(void* ud){ (void)ud; g_tick++; }
static int g_once=0;
static void on_once(void* ud){ (void)ud; g_once++; }

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/tween_test.png";

    /* ── data checks (no engine needed for these) ── */
    CCTweens* tw=cc_tweens_create();

    /* tween a value 0->10 over 1s linear; step in 0.1s increments */
    float v=0;
    CCTweenId id=cc_tween_to(tw,&v,0,10,1.0f,CC_EASE_LINEAR,on_done,NULL);
    CHECK(cc_tween_active(tw,id),"tween active after create");
    CHECK(feq(v,0),"tween initial value applied");
    float prev=v; int mono=1;
    for(int i=0;i<10;i++){ cc_tweens_update(tw,0.1f); if(v<prev-1e-4f) mono=0; prev=v; }
    CHECK(feq(v,10),"tween reached target");
    CHECK(mono,"tween monotonic for linear 0->10");
    CHECK(g_done==1,"on_done fired once");
    CHECK(!cc_tween_active(tw,id),"tween removed after completion");

    /* easing curve sanity: ease-in slower at start than linear */
    CHECK(cc_ease(CC_EASE_IN,0.25f) < 0.25f,"ease-in below linear early");
    CHECK(cc_ease(CC_EASE_OUT,0.25f) > 0.25f,"ease-out above linear early");
    CHECK(feq(cc_ease(CC_EASE_LINEAR,0.5f),0.5f),"linear midpoint");
    CHECK(feq(cc_ease(CC_EASE_IN_OUT,0.0f),0.0f) && feq(cc_ease(CC_EASE_IN_OUT,1.0f),1.0f),"ease-in-out endpoints");

    /* repeating timer every 0.5s: over 2.05s should fire 4 times */
    g_tick=0;
    CCTimerId tid=cc_timer_every(tw,0.5f,on_tick,NULL);
    cc_tweens_update(tw,2.05f);   /* single big dt: catch-up loop fires 4x */
    CHECK(g_tick==4,"repeating timer fired 4x over 2.05s");
    cc_timer_cancel(tw,tid);
    CHECK(!cc_timer_active(tw,tid),"timer cancelled");
    g_tick=0; cc_tweens_update(tw,2.0f);
    CHECK(g_tick==0,"cancelled timer does not fire");

    /* one-shot timer after 0.3s */
    g_once=0;
    cc_timer_after(tw,0.3f,on_once,NULL);
    cc_tweens_update(tw,0.2f); CHECK(g_once==0,"one-shot not yet");
    cc_tweens_update(tw,0.2f); CHECK(g_once==1,"one-shot fired");
    cc_tweens_update(tw,1.0f); CHECK(g_once==1,"one-shot fires only once");

    /* cancel an in-flight tween */
    float v2=0; CCTweenId id2=cc_tween_to(tw,&v2,0,100,1.0f,CC_EASE_LINEAR,NULL,NULL);
    cc_tweens_update(tw,0.3f);
    cc_tween_cancel(tw,id2);
    float held=v2; cc_tweens_update(tw,0.5f);
    CHECK(feq(v2,held),"cancelled tween stops updating");
    CHECK(cc_tweens_active_count(tw)==0,"all cleaned up");

    printf("tween data checks: %s\n", fails?"SOME FAILED":"all passed");

    /* ── visual: tween objects into place with different easings ── */
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=420;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e){ return fails?2:0; }
    CCMesh cube=cc_mesh_cube(e,0.8f);
    CCMesh ground=cc_mesh_plane(e,40,40,4);
    CCMaterialDesc md={.base_color={0.85f,0.5f,0.3f,1},.roughness=0.4f,.metallic=0.2f,.tint={1,1,1,1}};
    CCMaterial mat=cc_material_create(e,&md);
    CCMaterialDesc gd={.base_color={0.3f,0.32f,0.35f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);

    CCTweens* vtw=cc_tweens_create();
    CCEaseType eases[5]={CC_EASE_LINEAR,CC_EASE_IN,CC_EASE_OUT,CC_EASE_IN_OUT,CC_EASE_CUBIC};
    float ypos[5]; for(int i=0;i<5;i++){ ypos[i]=6.0f; cc_tween_to(vtw,&ypos[i],6.0f,0.5f,1.0f,eases[i],NULL,NULL); }
    /* advance to ~70% so the different curves are at visibly different heights */
    for(int s=0;s<42;s++) cc_tweens_update(vtw,0.016f);

    cc_light_set_ambient(e,0.16f,0.17f,0.2f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.7f,-0.5f},.color={1,0.96f,0.9f},.intensity=2.9f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.3f,0.45f,0.7f},h[3]={0.6f,0.64f,0.7f},g[3]={0.2f,0.2f,0.2f};
    cc_light_set_sky_colors(e,z,h,g,1.0f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.2f; fx.taa=true;
    cc_postfx_set(e,&fx);

    for(int f=0;f<6;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,3.0f,9},.target={0,1.2f,0},.up={0,1,0},.fov_deg=54,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        for(int i=0;i<5;i++){
            CCTransform3D t={.pos={(i-2)*1.6f, ypos[i]+0.4f, 0},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(e,cube,mat,&t);
        }
        cc_frame_end(e);
    }
    const char* s=cc_screenshot(e,out);
    printf("screenshot: %s (5 cubes mid-tween: linear/in/out/inout/cubic)\n", s?s:"(null)");

    cc_tweens_destroy(tw); cc_tweens_destroy(vtw); cc_shutdown(e);
    if(fails){ printf("TWEEN TEST: %d FAILURE(S)\n",fails); return 2; }
    printf("TWEEN TEST: all checks passed\n");
    return 0;
}
