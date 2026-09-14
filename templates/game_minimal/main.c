/*
 * Chlorlite — Minimal Game Template
 * Replace this with your game logic.
 *
 * Build:  bash SKILL_DIR/scripts/build_game.sh game_minimal/
 * Run:    bash SKILL_DIR/scripts/run_headless.sh game --ticks 120 --screenshot 1
 */

#include "cc/claudecore.h"
#include <stdio.h>
#include <math.h>

/* ── Game state ──────────────────────────────────────────────────────── */
typedef struct {
    CCEngine*  eng;
    CCScene*   scene;
    CCTexture  tex_player;
    CCMesh     mesh_floor;
    CCMaterial mat_floor;
    CCLightId  sun;
    float      time;
    float      player_x, player_y;
    float      player_speed;
} Game;

static Game g;

/* Player texture: white square with a colored border. A named C function —
   cc_texture_proc takes a plain function pointer (this engine is C/gnu17, so
   no Clang blocks or lambdas). */
static void gen_player_tex(uint8_t* px, uint32_t w, uint32_t h, void* ud) {
    (void)ud;
    for (uint32_t y=0;y<h;y++) for (uint32_t x=0;x<w;x++) {
        bool border = x<3||y<3||x>=w-3||y>=h-3;
        uint8_t r=border?0x2a:0xff, gv=border?0x8c:0xff, b=0xff;
        px[(y*w+x)*4+0]=r; px[(y*w+x)*4+1]=gv;
        px[(y*w+x)*4+2]=b; px[(y*w+x)*4+3]=0xff;
    }
}

/* ── Init ────────────────────────────────────────────────────────────── */
void cc_game_init(CCEngine* eng) {
    g.eng   = eng;
    g.scene = cc_scene_active(eng);
    g.player_speed = 200.0f;
    g.player_x = 640.0f;
    g.player_y = 360.0f;

    /* Procedural white texture for the player */
    CCTextureDesc td = {
        .width=64, .height=64, .format=CC_FMT_RGBA8,
        .mipmaps=true, .linear_filter=true, .wrap_repeat=true
    };
    g.tex_player = cc_texture_proc(eng, &td, gen_player_tex, NULL);

    /* Sun light */
    CCLight sun_light = {
        .type=CC_LIGHT_DIRECTIONAL,
        .dir={-0.5f,-1.0f,-0.5f},
        .color={1.0f,0.95f,0.85f},
        .intensity=3.0f,
        .cast_shadows=true,
    };
    g.sun = cc_light_add(eng, &sun_light);
    cc_light_set_ambient(eng, 0.1f, 0.12f, 0.15f, 1.0f);

    CC_INFO("Game initialized");
}

/* ── Tick ────────────────────────────────────────────────────────────── */
void cc_game_tick(CCEngine* eng, double dt) {
    g.time += (float)dt;

    /* Input */
    float dx=0, dy=0;
    if (cc_key_down(eng, QKEY_W) || cc_key_down(eng, QKEY_UP))    dy -= 1;
    if (cc_key_down(eng, QKEY_S) || cc_key_down(eng, QKEY_DOWN))  dy += 1;
    if (cc_key_down(eng, QKEY_A) || cc_key_down(eng, QKEY_LEFT))  dx -= 1;
    if (cc_key_down(eng, QKEY_D) || cc_key_down(eng, QKEY_RIGHT)) dx += 1;
    if (cc_key_pressed(eng, QKEY_ESCAPE)) cc_quit(eng);

    float len = dx*dx + dy*dy;
    if (len > 0.001f) { float inv = 1.0f/sqrtf(len); dx*=inv; dy*=inv; }
    g.player_x += dx * g.player_speed * (float)dt;
    g.player_y += dy * g.player_speed * (float)dt;

    /* Draw */
    /* 2D sprite for player */
    float pw=64, ph=64;
    float pulse = 0.85f + 0.15f * sinf(g.time * 4.0f);
    uint32_t tint = 0xFFFFFFFF;
    cc_draw_sprite(eng, g.tex_player,
                   g.player_x - pw/2, g.player_y - ph/2, pw*pulse, ph*pulse,
                   g.time * 45.0f, tint);

    /* Background */
    uint32_t bg = 0x1a1a2eff;
    cc_draw_rect(eng, 0, 0, 1280, 720, bg, 0, 0);
    /* Simple grid */
    for (int gx=0; gx<1280; gx+=64)
        cc_draw_rect(eng, (float)gx, 0, 1, 720, 0x2a2a4a88, 0, 0);
    for (int gy=0; gy<720; gy+=64)
        cc_draw_rect(eng, 0, (float)gy, 1280, 1, 0x2a2a4a88, 0, 0);
}

/* ── Shutdown ────────────────────────────────────────────────────────── */
void cc_game_shutdown(CCEngine* eng) {
    (void)eng;
    CC_INFO("Game shutdown");
}
