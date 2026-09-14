/* particles_test — the particle system. Renders several emitters (smoke, fire,
 * a sparks burst, magic) as billboards over a lit scene, and asserts the
 * simulation behaves: continuous emission grows the pool, a burst spawns N at
 * once, and particles age out to zero when emission stops. */
#include "cc/claudecore.h"
#include "cc/particles.h"
#include <stdio.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/particles_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=560;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    /* ── data-only simulation checks first ── */
    {
        CCParticles* p=cc_particles_create(500);
        CCEmitterDesc d=cc_emitter_smoke(); d.rate=100; cc_particles_config(p,&d);
        CHECK(cc_particles_alive(p)==0,"starts empty");
        for(int i=0;i<10;i++) cc_particles_update(p,0.016f);   /* ~0.16s @100/s ≈ 16 */
        CHECK(cc_particles_alive(p)>0,"continuous emission spawns");
        uint32_t after_emit=cc_particles_alive(p);
        cc_particles_burst(p,50);
        CHECK(cc_particles_alive(p)==after_emit+50,"burst adds exactly 50");
        /* stop emitting and run long enough for all to die (smoke life<=2.8s) */
        cc_particles_set_emitting(p,false);
        for(int i=0;i<300;i++) cc_particles_update(p,0.02f);   /* 6s */
        CHECK(cc_particles_alive(p)==0,"all particles age out");
        cc_particles_destroy(p);
    }

    /* ── visual: four emitters ── */
    CCParticles* smoke=cc_particles_create(1500);
    CCEmitterDesc sd=cc_emitter_smoke(); sd.position=(CCVec3){-3.0f,0.2f,0}; cc_particles_config(smoke,&sd);
    CCParticles* fire=cc_particles_create(1500);
    CCEmitterDesc fd=cc_emitter_fire(); fd.position=(CCVec3){-1.0f,0.1f,0}; cc_particles_config(fire,&fd);
    CCParticles* magic=cc_particles_create(1500);
    CCEmitterDesc md=cc_emitter_magic(); md.position=(CCVec3){1.2f,0.6f,0}; cc_particles_config(magic,&md);
    CCParticles* sparks=cc_particles_create(1500);
    CCEmitterDesc kd=cc_emitter_sparks(); kd.position=(CCVec3){3.0f,0.4f,0}; cc_particles_config(sparks,&kd);

    CCMaterialDesc gd={.base_color={0.22f,0.24f,0.27f,1},.roughness=0.9f,.tint={1,1,1,1}};
    CCMaterial mg=cc_material_create(e,&gd);
    CCMesh ground=cc_mesh_plane(e,40,40,4);

    cc_light_set_ambient(e,0.1f,0.11f,0.14f,1);
    CCLight k={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.7f,-0.5f},.color={1,0.96f,0.9f},.intensity=2.4f,.cast_shadows=true};
    cc_light_add(e,&k);
    float z[3]={0.1f,0.13f,0.2f},h[3]={0.2f,0.22f,0.28f},g[3]={0.08f,0.08f,0.1f};
    cc_light_set_sky_colors(e,z,h,g,0.6f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.auto_exposure=true; fx.ae_key=0.2f; fx.taa=true;
    fx.bloom=true; fx.bloom_threshold=1.1f; fx.bloom_intensity=0.18f;   /* fire/sparks glow */
    fx.vignette=true; fx.vignette_strength=0.3f;
    cc_postfx_set(e,&fx);

    /* warm up the continuous emitters so they have a full plume, then draw */
    for(int i=0;i<50;i++){
        cc_particles_update(smoke,0.02f);
        cc_particles_update(fire,0.02f);
        cc_particles_update(magic,0.02f);
    }
    cc_particles_burst(sparks,250);
    for(int i=0;i<8;i++) cc_particles_update(sparks,0.02f);  /* let sparks arc out a bit */

    for(int f=0;f<8;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,2.2f,8.5f},.target={0,1.4f,0},.up={0,1,0},.fov_deg=54,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(e,&cam);
        CCTransform3D g2={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}}; cc_draw_mesh(e,ground,mg,&g2);
        cc_particles_draw(smoke,e,0);
        cc_particles_draw(fire,e,0);
        cc_particles_draw(magic,e,0);
        cc_particles_draw(sparks,e,0);
        cc_frame_end(e);
    }
    const char* s=cc_screenshot(e,out);
    printf("emitters alive: smoke=%u fire=%u magic=%u sparks=%u\n",
        cc_particles_alive(smoke),cc_particles_alive(fire),cc_particles_alive(magic),cc_particles_alive(sparks));
    printf("screenshot: %s (smoke / fire / magic / sparks)\n", s?s:"(null)");

    cc_particles_destroy(smoke); cc_particles_destroy(fire);
    cc_particles_destroy(magic); cc_particles_destroy(sparks);
    if(fails){ printf("PARTICLES TEST: %d FAILURE(S)\n",fails); cc_shutdown(e); return 2; }
    printf("PARTICLES TEST: all checks passed\n");
    cc_shutdown(e); return 0;
}
