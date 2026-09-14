/* game_striker — end-to-end COMBAT demo: the payoff that wires together the whole
 * stack this session built, and the concrete answer to "AI can write functional
 * combat but not combat that feels good."
 *
 *   TIMELINE (cc/timeline.h)  — the attack MOVE as frame data: startup → active →
 *                               recovery, with a 'hitbox' window inside active and
 *                               a 'cancel' window in late recovery.
 *   COMBAT FEEL (cc/combat.h) — on a connect: hitstop, screenshake, knockback,
 *                               hitstun, damage event. All tunable.
 *   CHARACTER (cc/player.h)   — a kinematic player you move + a dummy target.
 *   LOOP (cc_run split)       — on_tick simulates at fixed dt (feeds the hitstop-
 *                               scaled dt to the move timeline so the swing freezes
 *                               during impact); on_render draws.
 *   PIXELS                    — headless mode records frames so the hit can be seen.
 *
 * Feel note: hitstop is fed into BOTH the sim and the timeline, so on a connect the
 * attacker's swing visibly freezes for a few frames — that's the weight the quote
 * says can't be authored. Every number is in TUNE below for human-in-the-loop feel.
 *
 * (Uses primitive meshes for the fighters: the systems under test are combat, not
 * art. Real animated glTF models drop in via cc_model_load where noted — the demo
 * is structured so the fighter's transform is all the renderer needs.)
 *
 *   striker            -> windowed, play (WASD move, J attack)
 *   striker <out_dir>  -> headless: scripted approach+attack, records frames
 */
#include "cc/claudecore.h"
#include "cc/render.h"
#include "cc/camera.h"
#include "cc/player.h"
#include "cc/actor.h"
#include "cc/timeline.h"
#include "cc/combat.h"
#include "cc/aa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── tunable feel/frame-data knobs (the human-in-the-loop surface) ──────── */
static struct {
    float move_duration, startup_end, active_end;  /* frame data (seconds) */
    float hitbox_start, hitbox_end;
    float reach;        /* attack range (world units) */
    float arc_dot;      /* facing tolerance: cos of half-arc (0.5 = 60° each side) */
} TUNE = {
    .move_duration=0.55f, .startup_end=0.18f, .active_end=0.32f,
    .hitbox_start=0.20f,  .hitbox_end=0.30f,
    .reach=2.6f, .arc_dot=0.35f,
};

/* ── world ──────────────────────────────────────────────────────────────── */
static CCEngine*    E;
static CCScene*     SCENE;
static CCFont       FONT;
static CCCameraRig* CAMRIG;
static CCCombat*    CB;
static CCTimeline*  MOVE;
static CCCharacter  PC;
static int          W=900, H=520;

static CCActor A_PLAYER, A_ENEMY, A_GROUND;
static CCMesh  M_BODY, M_GROUND;
static CCMaterial MAT_PLAYER, MAT_ENEMY, MAT_ENEMY_HIT, MAT_FLOOR;

static float ENEMY_POS[3] = {0, 0, -4};
static float ENEMY_HP = 100.0f;
static int   attacking = 0;
static int   hit_landed_this_move = 0;   /* one hit per swing */
static float enemy_flash = 0;            /* brief material flash on hit */
static int   total_hits = 0;

static float ground_fn(float x,float z,void* u){(void)x;(void)z;(void)u;return 0;}

/* the attack timeline callback: when the hitbox window opens, we DON'T hit here —
   we mark that hits are live; actual overlap test happens in sim (needs positions).
   We use the window purely as the authoritative "hitbox active" gate. */
static void on_move_event(const CCTimelineEvent* ev, void* u){
    (void)u;
    if(ev->type==CC_TL_FINISHED){ attacking=0; }
}

static void start_attack(void){
    if(attacking) return;
    attacking=1; hit_landed_this_move=0;
    cc_timeline_play(MOVE);
}

/* try to connect: only during the 'hitbox' window, once per swing, if the enemy is
   within reach and inside the facing arc. On connect → full feel cascade. */
static void try_hit(void){
    if(!hit_landed_this_move && cc_timeline_window_open_name(MOVE,"hitbox")){
        float dx=ENEMY_POS[0]-PC.position.x, dz=ENEMY_POS[2]-PC.position.z;
        float dist=sqrtf(dx*dx+dz*dz);
        if(dist<=TUNE.reach && dist>1e-3f){
            /* facing check: player yaw vs direction to enemy */
            float fyaw=PC.facing_yaw*3.14159265f/180.0f;
            float fx=sinf(fyaw), fz=-cosf(fyaw);      /* forward vector */
            float ndx=dx/dist, ndz=dz/dist;
            if(fx*ndx+fz*ndz >= TUNE.arc_dot){
                CCHit h={ .attacker=1,.victim=2,.damage=12,.strength=1.2f,
                          .dir_x=ndx,.dir_z=ndz };
                cc_combat_register_hit(CB,&h);
                hit_landed_this_move=1; total_hits++;
                ENEMY_HP-=h.damage; if(ENEMY_HP<0)ENEMY_HP=0;
                enemy_flash=0.12f;
                /* knockback the (non-physics) dummy by the computed impulse */
                float kx,ky,kz; cc_combat_last_knockback(CB,&kx,&ky,&kz);
                ENEMY_POS[0]+=kx*0.10f; ENEMY_POS[2]+=kz*0.10f;
            }
        }
    }
}

/* ── build ──────────────────────────────────────────────────────────────── */
static void build(void){
    SCENE=cc_scene_create(E,"striker");
    cc_aa_set_mode(E, CC_AA_OFF);   /* headless deferred perf */

    M_GROUND=cc_mesh_plane(E,40,40,6);
    M_BODY  =cc_mesh_sphere(E,0.6f,28,28);   /* fighter stand-in (→ glTF model here) */

    CCMaterialDesc floor={.base_color={0.10f,0.11f,0.14f,1},.roughness=0.9f,.tint={1,1,1,1}};
    MAT_FLOOR=cc_material_create(E,&floor);
    CCMaterialDesc pl={.base_color={0.30f,0.65f,1.0f,1},.roughness=0.4f,.metallic=0.2f,.tint={1,1,1,1}};
    MAT_PLAYER=cc_material_create(E,&pl);
    CCMaterialDesc en={.base_color={0.9f,0.35f,0.3f,1},.roughness=0.5f,.tint={1,1,1,1}};
    MAT_ENEMY=cc_material_create(E,&en);
    CCMaterialDesc enh={.base_color={1.0f,1.0f,1.0f,1},.roughness=0.3f,.emissive={2,2,2},.tint={1,1,1,1}};
    MAT_ENEMY_HIT=cc_material_create(E,&enh);

    A_GROUND=cc_actor_spawn(SCENE,M_GROUND,MAT_FLOOR,0,0,0);
    A_PLAYER=cc_actor_spawn(SCENE,M_BODY,MAT_PLAYER,0,0.6f,2);
    A_ENEMY =cc_actor_spawn(SCENE,M_BODY,MAT_ENEMY,ENEMY_POS[0],0.6f,ENEMY_POS[2]);

    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.8f,-0.4f},
                 .color={1.0f,0.9f,0.75f},.intensity=2.6f,.cast_shadows=true,.shadow_map_size=2048};
    cc_light_add(E,&sun);
    cc_light_set_ambient(E,0.08f,0.09f,0.12f,1.0f);
    float z[3]={0.10f,0.14f,0.22f},h[3]={0.22f,0.25f,0.32f},g[3]={0.04f,0.04f,0.05f};
    cc_light_set_sky_colors(E,z,h,g,0.5f);
    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; fx.bloom=true; fx.bloom_intensity=0.12f;
    fx.vignette=true; fx.vignette_strength=0.4f; fx.contrast=1.1f; fx.saturation=1.15f;
    cc_postfx_set(E,&fx);

    CAMRIG=cc_camera_create();
    cc_cam_set_perspective(CAMRIG,55,0.05f,300);
    cc_cam_set_shake_params(CAMRIG, 0.5f, 4.0f, 30.0f, 2.5f);  /* punchy, fast-decaying shake */

    CB=cc_combat_create(E);
    cc_combat_set_camera(CB,CAMRIG);   /* screenshake target */
    /* melee/action feel defaults are exactly right here; tune knockback a touch up */
    CCFeelProfile* fp=cc_combat_profile(CB);
    fp->knockback_base=9.0f; fp->hitstop_base=0.10f; fp->shake_trauma=0.55f;

    /* the attack move as frame data */
    MOVE=cc_timeline_create(TUNE.move_duration,false);
    cc_timeline_set_callback(MOVE,on_move_event,NULL);
    cc_timeline_add_phase(MOVE,1,"startup", 0.0f,           TUNE.startup_end);
    cc_timeline_add_phase(MOVE,2,"active",  TUNE.startup_end,TUNE.active_end);
    cc_timeline_add_phase(MOVE,3,"recovery",TUNE.active_end, TUNE.move_duration);
    cc_timeline_add_window(MOVE,10,"hitbox", TUNE.hitbox_start,TUNE.hitbox_end);
    cc_timeline_add_window(MOVE,11,"cancel", TUNE.active_end+0.05f, TUNE.move_duration);

    CCCharConfig cc=cc_character_default(); cc.walk_speed=5.0f;
    cc_character_init(&PC,cc);
    PC.position=(CCVec3){0,0,2};
}

/* ── sim + render ───────────────────────────────────────────────────────── */
static void sim(float dt, float mx, float mz, int atk){
    if(atk) start_attack();
    /* hitstop: freeze BOTH the sim and the move timeline for weight on impact */
    float sdt=cc_combat_begin_frame(CB,dt);

    /* movement (allowed unless in startup/active — you commit to a swing) */
    int locked = attacking && (cc_timeline_current_phase(MOVE)==1 || cc_timeline_current_phase(MOVE)==2);
    CCCharInput in={.move_x=locked?0:mx,.move_z=locked?0:mz,
                    .yaw_deg=0, .sprint=false,.jump=false};
    /* face the enemy while attacking so hits land readably */
    if(attacking){
        float dx=ENEMY_POS[0]-PC.position.x, dz=ENEMY_POS[2]-PC.position.z;
        PC.facing_yaw=atan2f(dx,-dz)*180.0f/3.14159265f;
    }
    cc_character_update(&PC,&in,ground_fn,NULL,sdt);

    if(attacking){
        cc_timeline_advance(MOVE,sdt);   /* frozen dt during hitstop → swing pauses */
        try_hit();
    }
    if(enemy_flash>0) enemy_flash-=dt;

    /* sync actors */
    cc_actor_set_position(A_PLAYER, PC.position.x,0.6f,PC.position.z);
    cc_actor_set_position(A_ENEMY, ENEMY_POS[0],0.6f,ENEMY_POS[2]);
    cc_actor_set_material(A_ENEMY, enemy_flash>0?MAT_ENEMY_HIT:MAT_ENEMY);
}

static void draw(void){
    /* camera frames BOTH fighters: sit back on +Z, raised, looking at the midpoint
       between player and enemy so the whole engagement stays on screen. */
    float mx=(PC.position.x+ENEMY_POS[0])*0.5f;
    float mz=(PC.position.z+ENEMY_POS[2])*0.5f;
    cc_cam_set_position(CAMRIG,(CCVec3){mx, 4.0f, mz+7.5f});
    cc_cam_look_at(CAMRIG,(CCVec3){mx,0.7f,mz},(CCVec3){0,1,0});
    cc_camera_update(CAMRIG,E,1.0f/60.0f);   /* advances shake decay */
    cc_camera_apply(CAMRIG,E);

    cc_scene_render(E,SCENE);

    char hud[128];
    const char* ph = attacking? (cc_timeline_current_phase_name(MOVE)?cc_timeline_current_phase_name(MOVE):"-") : "idle";
    snprintf(hud,sizeof(hud),"ENEMY HP %.0f   hits:%d   move:%s%s", ENEMY_HP, total_hits, ph,
             cc_combat_hitstop_active(CB)?"  [HITSTOP]":"");
    cc_draw_text(E,FONT,hud,16,14,20,0xdfe8ffff);
    if(cc_timeline_window_open_name(MOVE,"hitbox"))
        cc_draw_text(E,FONT,"HITBOX ACTIVE",16,40,18,0xff5a4aff);
}

static void on_tick(CCEngine* e,double dt,void* u){(void)u;
    float mx=0,mz=0;
    if(cc_key_down(e,QKEY_W))mz+=1; if(cc_key_down(e,QKEY_S))mz-=1;
    if(cc_key_down(e,QKEY_A))mx-=1; if(cc_key_down(e,QKEY_D))mx+=1;
    sim((float)dt,mx,mz, cc_key_pressed(e,QKEY_J));
}
static void on_render(CCEngine* e,double a,void* u){(void)e;(void)a;(void)u; draw();}
/* headless scripted loop drives its own frame, so wrap draw in begin/end */
static void draw_headless(void){ cc_frame_begin(E); draw(); cc_frame_end(E); }

int main(int argc,char**argv){
    int headless=(argc>1); const char* out=headless?argv[1]:NULL;
    CCEngineConfig cfg=headless?cc_sandbox_config():cc_default_config();
    cfg.width=W;cfg.height=H;cfg.verbose=false;cfg.title="STRIKER";cfg.target_fps=0;
    cfg.tick_rate=60.0; cfg.on_tick=on_tick; cfg.on_render=on_render;
    E=cc_init(&cfg); if(!E){printf("init failed\n");return 1;}
    FONT=cc_font_builtin(E); build();

    if(!headless){ cc_run(E); cc_shutdown(E); return 0; }

    /* headless: scripted — walk into range, then throw 3 attacks with spacing so
       the hitstop/knockback on each connect is visible in the recorded frames. */
    const double dt=1.0/60.0; char path[512]; int shot=0;
    for(int f=0; f<300; f++){
        float mx=0,mz=0; int atk=0;
        float gap = sqrtf((ENEMY_POS[0]-PC.position.x)*(ENEMY_POS[0]-PC.position.x)+
                          (ENEMY_POS[2]-PC.position.z)*(ENEMY_POS[2]-PC.position.z));
        if(gap>TUNE.reach-0.4f && f<70) mz=1;   /* approach only until in range */
        else if(f==80||f==150||f==220) atk=1;   /* three swings */
        sim((float)dt,mx,mz,atk);
        draw_headless();
        if((f%2)==0){ snprintf(path,sizeof(path),"%s/frame_%06d.png",out,shot++); cc_screenshot(E,path); }
    }
    printf("striker: done — enemy hp=%.0f, total hits=%d\n", ENEMY_HP, total_hits);
    cc_shutdown(E);
    return 0;
}
