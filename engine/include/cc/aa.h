#pragma once
/*
 * CCAntiAliasing — the anti-aliasing system. Like every other game-logic system
 * in CC, this is a MECHANISM with optional presets, not a fixed menu: pick one of
 * the built-in modes, OR install your own resolve with cc_aa_use_custom.
 *
 * Anti-aliasing removes the jagged "stair-step" edges (aliasing) that appear
 * wherever a high-contrast edge crosses the pixel grid. This engine does it the
 * one way that actually works cleanly: SUPERSAMPLING — render the scene at a
 * higher internal resolution, then box-average every sub-sample down to the
 * display. The more samples per output pixel, the more the staircase dissolves
 * into a true gradient (more, smaller "waves" converge to a straight line). No
 * post-process approximations (FXAA/SMAA) — those only ever soften the image
 * while leaving the edges visible.
 *
 *   OFF        — no AA (fastest, sharpest, aliased).
 *   SSAA_1_5X  — supersample at 1.5x  (2.25 samples/pixel).
 *   SSAA_2X    — supersample at 2x    (4 samples/pixel).
 *   SSAA_3X    — supersample at 3x    (9 samples/pixel).
 *   USD1       — high-density 4x      (16 samples/pixel).
 *   USD2       — high-density 6x      (36 samples/pixel).
 *   USD3       — high-density 8x      (64 samples/pixel) — maximum quality.
 *   CUSTOM     — your own resolve callback (cc_aa_use_custom). You receive the
 *                rendered color texture + resolution and write the anti-aliased
 *                result; define any AA technique you want.
 *
 * Cost scales with the square of the factor (8x = 64x the pixels), so USD3 is
 * expensive but reference-quality. Switching modes is cheap and can be done at
 * runtime (e.g. from a settings menu or a cvar).
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCEngine CCEngine;

/* Built-in AA modes (presets). CC_AA_CUSTOM selects a user resolve.
 *
 * Every real mode here is SUPERSAMPLING: render at a higher internal resolution
 * and box-average down. There is no post-process approximation (FXAA/SMAA were
 * removed — they only ever softened the image while leaving edges visible). More
 * samples per output pixel = the jagged staircase converges to a straight line,
 * the way more, smaller waves converge to a line.
 *
 *   SSAA 1.5x/2x/3x — supersample at 1.5/2/3x (2.25/4/9 samples per pixel).
 *   USD1/2/3        — high-density supersample at 4x/6x/8x (16/36/64 samples per
 *                     pixel). "Upscale-Downscale" taken to the density where the
 *                     edge is effectively gradient-smooth. USD3 (8x, 64 samples)
 *                     is the maximum quality. */
typedef enum CCAAMode {
    CC_AA_OFF = 0,
    CC_AA_ANALYTIC,      /* shaped-pixel analytic coverage (pixel = diagonal) —
                            exact edge, no supersample, no fade */
    CC_AA_PDAA1,         /* Pixel Divide AA — finer hard-sampled grid (no gray).
                            PDAA1/2/3 = rising division. Cheap. PDAA1 is default. */
    CC_AA_PDAA2,
    CC_AA_PDAA3,
    CC_AA_SSAA_1_5X,
    CC_AA_SSAA_2X,
    CC_AA_SSAA_3X,
    CC_AA_USD1,          /* smooth supersampling. USD1/2/3 = 2x/3x/4x by default; */
    CC_AA_USD2,          /* the scale is user-settable (cc_aa_set_usd_scale) up to */
    CC_AA_USD3,          /* a very high cap. */
    CC_AA_CUSTOM = -1,
} CCAAMode;

/* PDAA default division counts for the three presets (finer = smoother hard
 * staircase, still cheap because it's nearest point-sampling, not shading). */
#define CC_PDAA1_DIV 4
#define CC_PDAA2_DIV 8
#define CC_PDAA3_DIV 16
/* USD scale bounds. Presets are 2x/3x/4x; the user may set any scale up to the
 * (absurd, "you can but you'll regret it") cap. */
#define CC_USD_SCALE_MAX 128746258.0f

/* What a custom AA resolve receives. The engine binds `src_tex` (the rendered,
 * post-processed color at src_w×src_h) and a destination framebuffer already
 * bound at display size; the callback draws the resolved image. `state` is your
 * userdata. A full-screen-quad VAO id is provided for convenience. */
typedef struct CCAAContext {
    uint32_t src_tex;      /* GL texture: input color (render resolution)        */
    uint32_t src_w, src_h; /* input resolution (may be > display for SSAA/USD)   */
    uint32_t dst_w, dst_h; /* display resolution to write                        */
    uint32_t fsq_vao;      /* a fullscreen-quad VAO (TRIANGLE_FAN, 4 verts)      */
    void*    state;        /* your userdata                                      */
} CCAAContext;

typedef void (*CCAAResolveFn)(CCEngine* eng, const CCAAContext* ctx);

/* Internal AA state — embedded in the engine. Treat as opaque from game code;
 * use the cc_aa_* functions below. Exposed here so the engine can embed it. */
typedef struct CCAAState {
    CCAAMode      mode;
    float         usd_smoothing;   /* legacy, unused (kept for ABI)              */
    float         usd_scale;       /* 0 = per-preset default; else override      */
    uint32_t      pdaa_divisions;  /* 0 = per-preset default; else override      */
    CCAAResolveFn custom_fn;
    void*         custom_state;
    float         custom_scale;
} CCAAState;

/* Initialize AA state to defaults (mode = FXAA, smoothing = 1). */
void cc_aa_state_init(CCAAState* s);
/* Mode helpers usable without an engine (for the renderer + tests). */
float       cc_aa_mode_scale(CCAAMode m);   /* internal render-scale of a mode   */
int         cc_aa_usd_rounds(CCAAMode m);   /* USD upscale+smooth rounds (0 if not USD) */

/* ─── mode selection ──────────────────────────────────────────────────────── */
/* Select a built-in AA mode. Applies immediately; SSAA/USD adjust render scale. */
void      cc_aa_set_mode(CCEngine* eng, CCAAMode mode);
CCAAMode  cc_aa_get_mode(const CCEngine* eng);
/* Human-readable name for a mode ("FXAA", "SSAA 2x", "USD2", ...). */
const char* cc_aa_mode_name(CCAAMode mode);
/* Number of presets and the i-th preset (for building a settings dropdown).
 * Excludes CC_AA_CUSTOM. */
uint32_t  cc_aa_preset_count(void);
CCAAMode  cc_aa_preset_at(uint32_t i);

/* ─── USD tuning ──────────────────────────────────────────────────────────── */
/* Override the supersample scale for USD modes (default 2x/3x/4x for USD1/2/3).
 * Clamped to [1, CC_USD_SCALE_MAX]. Higher = smoother but quadratically more
 * expensive — you can set it absurdly high, but your GPU will not thank you. */
void  cc_aa_set_usd_scale(CCEngine* eng, float scale);
float cc_aa_get_usd_scale(const CCEngine* eng);   /* 0 = use the per-preset default */

/* ─── PDAA tuning ─────────────────────────────────────────────────────────── */
/* Override the division count for PDAA modes (default 4/8/16 for PDAA1/2/3).
 * More divisions = finer, more accurate HARD staircase (no gray). Cheap: this is
 * nearest point-sampling on a finer grid, not extra shading. */
void     cc_aa_set_pdaa_divisions(CCEngine* eng, uint32_t divisions);
uint32_t cc_aa_get_pdaa_divisions(const CCEngine* eng);  /* 0 = use preset default */

/* ─── the mechanism: your own AA ──────────────────────────────────────────── */
/* Install a custom resolve. Sets the mode to CC_AA_CUSTOM. `render_scale` tells
 * the engine what internal resolution to render at for your resolve (1.0 = display
 * size; 2.0 = render at 2x like SSAA, if your technique wants supersampled input).
 * Pass fn=NULL to detach (reverts to CC_AA_OFF). */
void cc_aa_use_custom(CCEngine* eng, CCAAResolveFn fn, void* state, float render_scale);

/* ─── introspection ───────────────────────────────────────────────────────── */
/* The internal render-scale the current mode implies (1.0, 1.5, 2.0, 3.0). Useful
 * for HUD/debug and for allocating render targets. */
float cc_aa_render_scale(const CCEngine* eng);

#ifdef __cplusplus
}
#endif
