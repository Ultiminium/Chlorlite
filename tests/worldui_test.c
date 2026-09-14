/* worldui_test — screen-space UI anchored to world positions. Three "characters"
 * (cubes) each get a floating health bar + nameplate; damage numbers pop up over
 * them and rise/fade. Verifies projection (a point in front projects on-screen,
 * a point behind the camera reports not-visible) and the floater lifecycle. */
#include "cc/claudecore.h"
#include "cc/worldui.h"
#include <stdio.h>
#include <math.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)
#define RGBA(r,g,b,a) (((uint32_t)(r)<<24)|((uint32_t)(g)<<16)|((uint32_t)(b)<<8)|(uint32_t)(a))

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/worldui_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=560;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    CCMesh cube=cc_mesh_cube(e,1.0f);
    CCMesh ground=cc_mesh_plane(e,40,40,4);
    CCMaterialDesc md={.base_color={0.6f,0.45f,0.4f,1},.roughness=0.5f,.tint={1,1,1,1}};
    CCMaterial mat=cc_material_create(e,&md);
    CCMaterialDesc gd={.base_color={0.3f,0.32f,0.35f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);

    cc_light_set_ambient(e,0.16f,0.17f,0.2f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.7f,-0.5f},.color={1,0.96f,0.9f},.intensity=2.9f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.3f,0.45f,0.7f},h[3]={0.6f,0.64f,0.7f},g[3]={0.2f,0.2f,0.2f};
    cc_light_set_sky_colors(e,z,h,g,1.0f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.2f; fx.taa=true;
    cc_postfx_set(e,&fx);

    /* three characters at different depths */
    float cx[3]={-3.0f, 0.0f, 3.2f};
    float cz[3]={ 1.0f,-1.0f, 0.5f};
    float hp[3]={0.85f, 0.4f, 0.65f};
    const char* names[3]={"Knight","Goblin","Mage"};

    CCFloaters* floaters=cc_floaters_create();
    cc_floaters_spawn(floaters, cx[0],1.4f,cz[0], "-12", 1.0f,0.9f,0.3f);
    cc_floaters_spawn(floaters, cx[1],1.4f,cz[1], "-45", 1.0f,0.3f,0.2f);
    cc_floaters_spawn(floaters, cx[2],1.4f,cz[2], "crit!", 1.0f,0.5f,0.9f);
    CHECK(cc_floaters_active(floaters)==3,"3 floaters spawned");

    /* set the camera once so projection is valid for the pre-draw assertions */
    cc_frame_begin(e);
    CCCameraDesc cam={.pos={0,3.5f,9},.target={0,0.6f,0},.up={0,1,0},.fov_deg=54,.near_plane=0.1f,.far_plane=100,.exposure=1};
    cc_camera_set(e,&cam);
    /* projection checks */
    float sx,sy; bool vis;
    bool infront=cc_world_to_screen(e, 0,0.6f,0, &sx,&sy,&vis);
    CHECK(infront && vis,"point in front projects on-screen");
    CHECK(sx>0 && sx<1000 && sy>0 && sy<560,"projected coords within viewport");
    bool behind=cc_world_to_screen(e, 0,0.6f,20, &sx,&sy,&vis);  /* behind camera (z=+20, cam looks -z) */
    CHECK(!(behind && vis),"point behind camera not visible");
    cc_frame_end(e);

    /* advance floaters partway so they've risen/faded a bit */
    for(int i=0;i<12;i++) cc_floaters_update(floaters,0.03f);

    const char* s=NULL;
    for(int f=0;f<6;f++){
        cc_frame_begin(e);
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        for(int c=0;c<3;c++){
            CCTransform3D t={.pos={cx[c],0.5f,cz[c]},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_mesh(e,cube,mat,&t);
        }
        /* world-space UI over each character (top of head ~ y=1.1) */
        for(int c=0;c<3;c++){
            uint32_t barcol = hp[c]>0.5f ? RGBA(80,200,90,255)
                            : (hp[c]>0.25f? RGBA(220,190,60,255) : RGBA(210,70,60,255));
            cc_worldui_bar(e, cx[c],1.1f,cz[c], hp[c], 54, 7, 46, barcol, RGBA(30,30,36,220));
            cc_worldui_label(e, cx[c],1.1f,cz[c], names[c], 15, 58, RGBA(235,238,245,255));
        }
        cc_floaters_draw(e, floaters);
        cc_frame_end(e);
    }
    s=cc_screenshot(e,out);
    printf("screenshot: %s (3 health bars + nameplates + floating damage numbers)\n", s?s:"(null)");
    printf("floaters still active: %u\n", cc_floaters_active(floaters));

    cc_floaters_destroy(floaters);
    cc_shutdown(e);
    if(fails){ printf("WORLDUI TEST: %d FAILURE(S)\n",fails); return 2; }
    printf("WORLDUI TEST: all checks passed\n");
    return 0;
}
