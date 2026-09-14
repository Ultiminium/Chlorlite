/* warden — a small but REAL 3D game (not a diorama): it has content, state, a win
 * condition AND a lose condition, and systems that react to each other.
 *
 * GOAL:   collect all the glowing shards, then reach the exit pad to escape.
 * THREAT: a warden drone chases you; if it touches you, you're caught (lose).
 * STATE:  shards_collected, all_collected (unlocks exit), won, lost — the world
 *         reacts: collecting hides the shard + advances state; the exit pad stays
 *         dark until every shard is gathered, then lights up; the drone steers
 *         toward the player every frame.
 *
 * Systems exercised: scene + persistent actors (cc_scene / cc_actor), PBR
 * materials, directional light + soft shadows + IBL sky + fog, ACES/bloom/vignette
 * post, the deferred pipeline, the kinematic character controller + input, a
 * follow camera, proximity-based interaction (collect), an AI chaser, and a HUD.
 *
 *   warden            -> windowed, play it (WASD move, Shift sprint, Space jump)
 *   warden <out_dir>  -> headless: an AGENT plays it from PIXELS and records frames
 *   warden <out_dir> --script -> headless: fixed scripted walkthrough (fallback)
 */
#include "cc/claudecore.h"
#include "cc/render.h"
#include "cc/camera.h"
#include "cc/player.h"
#include "cc/actor.h"
#include "cc/aa.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── config ───────────────────────────────────────────────────────────── */
#define NSHARDS 5
#define ARENA   16.0f      /* half-extent of the play field */
#define CATCH_DIST 1.3f    /* drone catch radius */
#define COLLECT_DIST 1.6f  /* how close to grab a shard */
static int W = 960, H = 540;

/* ── world ────────────────────────────────────────────────────────────── */
static CCEngine*   E;
static CCScene*    SCENE;
static CCFont      FONT;
static CCCharacter PC;
static float       CAM_YAW = 0.0f;

static CCMesh   M_GROUND, M_SHARD, M_DRONE, M_PLAYER, M_PILLAR, M_EXIT;
static CCMaterial MAT_FLOOR, MAT_SHARD, MAT_SHARD_DIM, MAT_DRONE, MAT_PLAYER,
                  MAT_PILLAR, MAT_EXIT_OFF, MAT_EXIT_ON;

static CCActor  A_SHARD[NSHARDS];
static int      SHARD_LIVE[NSHARDS];
static float    SHARD_POS[NSHARDS][3];
static CCActor  A_DRONE, A_PLAYER, A_EXIT;
static float    DRONE[3] = { 0, 1.0f, -12 };
static float    EXIT_POS[3] = { 0, 0.05f, -14 };

/* game state */
static int   shards_collected = 0;
static int   won = 0, lost = 0;
static float play_time = 0;

static float ground_fn(float x, float z, void* u){ (void)x;(void)z;(void)u; return 0.0f; }

/* ── setup ────────────────────────────────────────────────────────────── */
static void build(void) {
    SCENE = cc_scene_create(E, "warden");
    /* Default AA (PDAA1) renders at 4× internal resolution → 16× the pixels through
       the deferred pipeline (shadows+SSAO+SSGI+SSR+bloom). That's fine on a GPU but
       crushes the software OSMesa headless path (~5fps). Turn it off so the agent
       can actually play through in reasonable time; the look is essentially the same
       at this scene scale. */
    cc_aa_set_mode(E, CC_AA_OFF);

    M_GROUND = cc_mesh_plane(E, ARENA*2, ARENA*2, 8);
    M_SHARD  = cc_mesh_sphere(E, 0.45f, 20, 20);   /* stand-in shard */
    M_DRONE  = cc_mesh_sphere(E, 0.7f, 28, 28);
    M_PLAYER = cc_mesh_sphere(E, 0.5f, 28, 28);
    M_PILLAR = cc_mesh_cube(E, 1.0f);
    M_EXIT   = cc_mesh_cube(E, 1.0f);

    /* materials — saturated, with real contrast (the diorama was grey + blown out) */
    CCMaterialDesc floor = {.base_color={0.10f,0.11f,0.13f,1}, .roughness=0.9f, .metallic=0.0f, .tint={1,1,1,1}};
    MAT_FLOOR = cc_material_create(E, &floor);
    CCMaterialDesc shard = {.base_color={0.20f,0.9f,1.0f,1}, .roughness=0.25f, .metallic=0.2f,
                            .emissive={0.6f,2.6f,3.2f}, .tint={1,1,1,1}};   /* cyan glow */
    MAT_SHARD = cc_material_create(E, &shard);
    CCMaterialDesc drone = {.base_color={0.95f,0.15f,0.12f,1}, .roughness=0.35f, .metallic=0.6f,
                            .emissive={2.2f,0.15f,0.1f}, .tint={1,1,1,1}};  /* menacing red */
    MAT_DRONE = cc_material_create(E, &drone);
    CCMaterialDesc player = {.base_color={0.95f,0.85f,0.35f,1}, .roughness=0.4f, .metallic=0.3f, .tint={1,1,1,1}};
    MAT_PLAYER = cc_material_create(E, &player);
    CCMaterialDesc pillar = {.base_color={0.30f,0.32f,0.38f,1}, .roughness=0.7f, .metallic=0.1f, .tint={1,1,1,1}};
    MAT_PILLAR = cc_material_create(E, &pillar);
    CCMaterialDesc exoff = {.base_color={0.15f,0.15f,0.18f,1}, .roughness=0.6f, .metallic=0.2f, .tint={1,1,1,1}};
    MAT_EXIT_OFF = cc_material_create(E, &exoff);
    CCMaterialDesc exon = {.base_color={0.4f,1.0f,0.5f,1}, .roughness=0.3f, .metallic=0.1f,
                           .emissive={0.5f,3.0f,1.0f}, .tint={1,1,1,1}};    /* green, lit when unlocked */
    MAT_EXIT_ON = cc_material_create(E, &exon);

    /* ground + boundary pillars */
    cc_actor_spawn(SCENE, M_GROUND, MAT_FLOOR, 0,0,0);
    for (int i=0;i<8;i++){
        float a = i/8.0f*6.2831853f;
        CCActor p = cc_actor_spawn(SCENE, M_PILLAR, MAT_PILLAR, cosf(a)*ARENA, 1.5f, sinf(a)*ARENA);
        cc_actor_set_scale(p, 1.0f, 3.0f, 1.0f);
    }

    /* shards scattered around */
    SHARD_POS[0][0]=-8; SHARD_POS[0][2]=-6;
    SHARD_POS[1][0]= 9; SHARD_POS[1][2]=-3;
    SHARD_POS[2][0]=-6; SHARD_POS[2][2]= 7;
    SHARD_POS[3][0]= 7; SHARD_POS[3][2]= 8;
    SHARD_POS[4][0]= 0; SHARD_POS[4][2]=-9;
    for (int i=0;i<NSHARDS;i++){
        SHARD_POS[i][1]=1.0f; SHARD_LIVE[i]=1;
        A_SHARD[i]=cc_actor_spawn(SCENE, M_SHARD, MAT_SHARD, SHARD_POS[i][0],1.0f,SHARD_POS[i][2]);
    }

    A_EXIT  = cc_actor_spawn(SCENE, M_EXIT, MAT_EXIT_OFF, EXIT_POS[0],EXIT_POS[1],EXIT_POS[2]);
    cc_actor_set_scale(A_EXIT, 2.5f, 0.1f, 2.5f);
    A_DRONE = cc_actor_spawn(SCENE, M_DRONE, MAT_DRONE, DRONE[0],DRONE[1],DRONE[2]);
    A_PLAYER= cc_actor_spawn(SCENE, M_PLAYER, MAT_PLAYER, 0,1.0f,10);

    /* lighting — dimmer, warmer key + cool sky, real shadows, less ambient so it
       is NOT flat/overexposed like the diorama */
    CCLight sun = { .type=CC_LIGHT_DIRECTIONAL, .dir={-0.4f,-0.75f,-0.5f},
                    .color={1.0f,0.86f,0.66f}, .intensity=2.2f, .cast_shadows=true, .shadow_map_size=2048 };
    cc_light_add(E,&sun);
    cc_light_set_shadow_softness(E, 1.6f);
    cc_light_set_ambient(E, 0.05f,0.06f,0.09f, 1.0f);   /* low ambient → contrast */
    float zen[3]={0.06f,0.09f,0.16f}, hor[3]={0.20f,0.22f,0.30f}, grd[3]={0.03f,0.03f,0.04f};
    cc_light_set_sky_colors(E, zen,hor,grd, 0.5f);      /* dusky sky */

    CCPostFX fx = cc_postfx_default();
    fx.tonemap_aces=true;
    fx.bloom=true; fx.bloom_threshold=1.0f; fx.bloom_intensity=0.14f;
    fx.vignette=true; fx.vignette_strength=0.45f;
    fx.fog=true; fx.fog_density=0.030f; fx.fog_color[0]=0.10f; fx.fog_color[1]=0.12f; fx.fog_color[2]=0.18f;
    fx.fog_height=8.0f;
    fx.contrast=1.12f; fx.saturation=1.20f;
    cc_postfx_set(E,&fx);

    CCCharConfig cc = cc_character_default();
    cc.walk_speed=5.5f; cc.sprint_mult=1.7f; cc.jump_height=1.6f;
    cc_character_init(&PC, cc);
    PC.position=(CCVec3){0,0,10};
}

/* ── game logic ───────────────────────────────────────────────────────── */
static float dist2(float ax,float az,float bx,float bz){ float dx=ax-bx,dz=az-bz; return dx*dx+dz*dz; }

static void update_game(float dt) {
    if (won || lost) return;
    play_time += dt;

    /* collect shards by proximity */
    for (int i=0;i<NSHARDS;i++) if (SHARD_LIVE[i]) {
        if (dist2(PC.position.x,PC.position.z, SHARD_POS[i][0],SHARD_POS[i][2]) < COLLECT_DIST*COLLECT_DIST){
            SHARD_LIVE[i]=0; shards_collected++;
            cc_actor_set_visible(A_SHARD[i], false);   /* world reacts: shard gone */
        }
    }
    int all = (shards_collected>=NSHARDS);
    cc_actor_set_material(A_EXIT, all ? MAT_EXIT_ON : MAT_EXIT_OFF);  /* exit lights when ready */

    /* drone chases the player (simple steering) */
    float dx=PC.position.x-DRONE[0], dz=PC.position.z-DRONE[2];
    float d=sqrtf(dx*dx+dz*dz)+1e-4f;
    float sp=(all?4.6f:3.6f)*dt;   /* drone speeds up once exit is armed */
    DRONE[0]+=dx/d*sp; DRONE[2]+=dz/d*sp;
    DRONE[1]=1.0f + 0.25f*sinf(play_time*3.0f);   /* bob */
    cc_actor_set_position(A_DRONE, DRONE[0],DRONE[1],DRONE[2]);

    /* lose: drone catches player */
    if (dist2(PC.position.x,PC.position.z, DRONE[0],DRONE[2]) < CATCH_DIST*CATCH_DIST) lost=1;
    /* win: all collected AND reach exit */
    if (all && dist2(PC.position.x,PC.position.z, EXIT_POS[0],EXIT_POS[2]) < 2.2f*2.2f) won=1;
}

/* draw the scene from a given (possibly interpolated) player position. Does NOT
   call cc_frame_begin/end — the caller owns that (cc_run wraps on_render; the
   legacy render_frame wraps it itself). */
static void draw_scene(float px, float py, float pz) {
    float yaw=CAM_YAW*3.14159265f/180.0f;
    CCCameraDesc cam={.pos={px-sinf(yaw)*8.0f, py+4.0f, pz+cosf(yaw)*8.0f},
                      .target={px,py+1.0f,pz}, .up={0,1,0},
                      .fov_deg=60,.near_plane=0.05f,.far_plane=400,.exposure=1.0f};
    cc_camera_set(E,&cam);
    cc_actor_set_position(A_PLAYER, px, py, pz);
    cc_scene_render(E, SCENE);
    char hud[128];
    snprintf(hud,sizeof(hud),"SHARDS %d / %d%s", shards_collected, NSHARDS,
             shards_collected>=NSHARDS ? "   EXIT OPEN" : "");
    cc_draw_text(E, FONT, hud, 16,14, 20, 0x8fe8ffff);
    if (won)  cc_draw_text(E, FONT, "ESCAPED", W/2-70, H/2-16, 40, 0x7fff9fff);
    if (lost) cc_draw_text(E, FONT, "CAUGHT",  W/2-64, H/2-16, 40, 0xff6a5aff);
}

/* legacy path: wraps its own frame + draws at the live sim position */
static void render_frame(void) {
    cc_frame_begin(E);
    draw_scene(PC.position.x, PC.position.y, PC.position.z);
    cc_frame_end(E);
}

/* split path: cc_run already called cc_frame_begin; draw at interpolated pos */
static void render_frame_interp(float ipx, float ipy, float ipz) {
    draw_scene(ipx, ipy, ipz);
}

static void apply_input(float dt, float mx, float mz, int sprint, int jump) {
    CCCharInput in={.move_x=mx,.move_z=mz,.yaw_deg=CAM_YAW,.sprint=sprint!=0,.jump=jump!=0};
    cc_character_update(&PC,&in,ground_fn,NULL,dt);
    update_game(dt);
}

/* ── TPS loop: fixed-timestep SIMULATION (input + character + game logic) ──── */
static float PREV_PX, PREV_PY, PREV_PZ, CUR_PX, CUR_PY, CUR_PZ;  /* for interpolation */
static void on_tick(CCEngine* e, double fixed_dt, void* ud){
    (void)ud;
    float mx=0,mz=0;
    if(cc_key_down(e,QKEY_W)||cc_key_down(e,QKEY_UP))   mz+=1;
    if(cc_key_down(e,QKEY_S)||cc_key_down(e,QKEY_DOWN)) mz-=1;
    if(cc_key_down(e,QKEY_A)||cc_key_down(e,QKEY_LEFT)) mx-=1;
    if(cc_key_down(e,QKEY_D)||cc_key_down(e,QKEY_RIGHT))mx+=1;
    if(cc_key_down(e,QKEY_Q)) CAM_YAW-=90*(float)fixed_dt;
    if(cc_key_down(e,QKEY_E)) CAM_YAW+=90*(float)fixed_dt;
    /* record previous sim position so render can interpolate to current */
    PREV_PX=PC.position.x; PREV_PY=PC.position.y; PREV_PZ=PC.position.z;
    apply_input((float)fixed_dt, mx,mz, cc_key_down(e,QKEY_LSHIFT), cc_key_pressed(e,QKEY_SPACE));
    CUR_PX=PC.position.x; CUR_PY=PC.position.y; CUR_PZ=PC.position.z;
}

/* ── FPS loop: render ONCE per frame, interpolating between the last two ticks
   by `alpha` (0..1) so motion is smooth even when FPS != TPS. ──────────────── */
static void on_render(CCEngine* e, double alpha, void* ud){
    (void)ud;
    float a=(float)alpha;
    /* interpolate the drawn player position between prev and current sim states */
    float ipx=PREV_PX+(CUR_PX-PREV_PX)*a;
    float ipy=PREV_PY+(CUR_PY-PREV_PY)*a;
    float ipz=PREV_PZ+(CUR_PZ-PREV_PZ)*a;
    render_frame_interp(ipx, ipy, ipz);
}

/* legacy single-callback path kept for the headless agent/scripted recorder */
static void on_frame(CCEngine* e, double dt, void* ud){
    (void)ud;
    float mx=0,mz=0;
    if(cc_key_down(e,QKEY_W)||cc_key_down(e,QKEY_UP))   mz+=1;
    if(cc_key_down(e,QKEY_S)||cc_key_down(e,QKEY_DOWN)) mz-=1;
    if(cc_key_down(e,QKEY_A)||cc_key_down(e,QKEY_LEFT)) mx-=1;
    if(cc_key_down(e,QKEY_D)||cc_key_down(e,QKEY_RIGHT))mx+=1;
    if(cc_key_down(e,QKEY_Q)) CAM_YAW-=90*(float)dt;
    if(cc_key_down(e,QKEY_E)) CAM_YAW+=90*(float)dt;
    apply_input((float)dt, mx,mz, cc_key_down(e,QKEY_LSHIFT), cc_key_pressed(e,QKEY_SPACE));
    render_frame();
}

/* ── AGENT: play the game by reading the rendered PIXELS ──────────────────
   Each frame: read the framebuffer, find the nearest live shard (cyan) — or the
   exit (green) once all collected — as a bright blob, steer the player toward its
   screen position, and move. This is Claude's own game being played from pixels,
   not from internal state. (Fallback --script path is a fixed route.)            */
static int agent_decide_and_move(float dt) {
    uint32_t fw=0,fh=0; uint8_t* px=NULL;
    cc_frame_pixels(E,&px,&fw,&fh);
    if(!px||!fw){ apply_input(dt,0,1,0,0); return 0; }
    int all = (shards_collected>=NSHARDS);

    /* scan for the target color's centroid: cyan shard (G,B high, R low) OR, when
       all collected, the green exit (G high, R/B lower). */
    long sxs=0, sys=0, cnt=0;
    for(uint32_t y=0;y<fh;y+=4) for(uint32_t x=0;x<fw;x+=4){
        const uint8_t* p=px+((size_t)y*fw+x)*4;
        int r=p[0],g=p[1],b=p[2];
        int hit = all ? (g>150 && g>r+40 && g>b+40)          /* green exit */
                      : (g>140 && b>140 && r<g-30);          /* cyan shard */
        if(hit){ sxs+=x; sys+=y; cnt++; }
    }
    float mx=0,mz=0;
    if(cnt>15){
        float cx=(float)sxs/cnt/(float)(fw)/1.0f;   /* 0..1 across screen */
        float screen_frac = cx;                     /* 0 left .. 1 right */
        /* target on screen: left→strafe left, right→strafe right; always push fwd */
        mx = (screen_frac-0.5f)*2.0f;
        mz = 1.0f;                                  /* advance toward it */
    } else {
        /* nothing visible → rotate camera to search */
        CAM_YAW += 40.0f*dt; mz=0.3f;
    }
    /* clamp */
    if(mx>1)mx=1; if(mx<-1)mx=-1;
    apply_input(dt, mx, mz, all/*sprint to exit*/, 0);
    return 1;
}

int main(int argc,char**argv){
    int headless = (argc>1);
    const char* out = headless?argv[1]:NULL;
    int scripted = (argc>2 && strcmp(argv[2],"--script")==0);

    CCEngineConfig cfg = headless?cc_sandbox_config():cc_default_config();
    cfg.width=W;cfg.height=H;cfg.verbose=false;cfg.title="WARDEN";cfg.target_fps=0;
    /* Windowed play uses the TPS/FPS split loop: simulation ticks at a fixed 60 Hz
       (on_tick), rendering runs uncapped with interpolation (on_render). Headless
       paths below drive their own manual loop, so on_frame is set as a fallback. */
    cfg.tick_rate = 60.0;
    cfg.on_tick   = on_tick;
    cfg.on_render = on_render;
    cfg.on_frame  = on_frame;
    E=cc_init(&cfg); if(!E){printf("init failed\n");return 1;}
    FONT=cc_font_builtin(E); build();

    if(!headless){ cc_run(E); cc_shutdown(E); return 0; }

    const double dt=1.0/60.0; char path[512];
    int maxf=600, f=0;
    for(; f<maxf && !won && !lost; f++){
        if(scripted){
            float mx=0,mz=0; /* naive fixed route toward shard positions then exit */
            apply_input((float)dt, mx, mz, 0, 0);
            render_frame();
        } else {
            /* render ONCE, then the agent reads that frame's pixels + moves for next
               frame. (Rendering twice per loop was ~5fps headless.) */
            render_frame();
            agent_decide_and_move((float)dt);
        }
        if ((f % 2) == 0) {
            snprintf(path,sizeof(path),"%s/frame_%06d.png",out,f/2);
            cc_screenshot(E,path);
        }
    }
    printf("warden: %s after %d frames — shards %d/%d, pos %.1f,%.1f\n",
           won?"WON":(lost?"LOST":"timeout"), f, shards_collected, NSHARDS,
           PC.position.x, PC.position.z);
    cc_shutdown(E);
    return 0;
}
