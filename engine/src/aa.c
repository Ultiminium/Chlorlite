/* aa.c — CCAntiAliasing: preset modes + custom mechanism. See cc/aa.h.
 *
 * State + mode logic live here; the actual GL resolve passes are driven from the
 * renderer (which owns the GL context, the fullscreen quad, and the color
 * targets) via the cc_aa_resolve_* entry points below. SSAA/USD report a
 * render-scale the renderer uses to size its internal targets; FXAA/SMAA/USD-
 * smoothing/custom run as resolve passes on the final color texture.
 *
 * This file is GL-free where possible: it holds the mode/params and the shader
 * SOURCE strings + the resolve algorithm structure. The renderer calls
 * cc_aa_state() to read config and uses the provided shader sources.
 */
#include "cc/aa.h"
#include <stddef.h>
#include <string.h>

/* CCAAState is defined in cc/aa.h (embedded in the engine). */

/* ─── mode metadata ───────────────────────────────────────────────────────── */
static const CCAAMode k_presets[] = {
    CC_AA_OFF, CC_AA_ANALYTIC,
    CC_AA_PDAA1, CC_AA_PDAA2, CC_AA_PDAA3,
    CC_AA_SSAA_1_5X, CC_AA_SSAA_2X, CC_AA_SSAA_3X,
    CC_AA_USD1, CC_AA_USD2, CC_AA_USD3,
};

const char* cc_aa_mode_name(CCAAMode m) {
    switch (m) {
        case CC_AA_OFF:        return "Off";
        case CC_AA_ANALYTIC:   return "Analytic";
        case CC_AA_PDAA1:      return "PDAA1";
        case CC_AA_PDAA2:      return "PDAA2";
        case CC_AA_PDAA3:      return "PDAA3";
        case CC_AA_SSAA_1_5X:  return "SSAA 1.5x";
        case CC_AA_SSAA_2X:    return "SSAA 2x";
        case CC_AA_SSAA_3X:    return "SSAA 3x";
        case CC_AA_USD1:       return "USD1";
        case CC_AA_USD2:       return "USD2";
        case CC_AA_USD3:       return "USD3";
        case CC_AA_CUSTOM:     return "Custom";
        default:               return "Unknown";
    }
}

uint32_t cc_aa_preset_count(void) { return (uint32_t)(sizeof(k_presets)/sizeof(k_presets[0])); }
CCAAMode cc_aa_preset_at(uint32_t i) {
    if (i >= cc_aa_preset_count()) return CC_AA_OFF;
    return k_presets[i];
}

/* render-scale implied by a mode (internal resolution multiplier) */
float cc_aa_mode_scale(CCAAMode m) {
    switch (m) {
        case CC_AA_ANALYTIC:  return 1.0f;              /* display-res resolve */
        case CC_AA_PDAA1:     return (float)CC_PDAA1_DIV; /* 4  */
        case CC_AA_PDAA2:     return (float)CC_PDAA2_DIV; /* 8  */
        case CC_AA_PDAA3:     return (float)CC_PDAA3_DIV; /* 16 */
        case CC_AA_SSAA_1_5X: return 1.5f;
        case CC_AA_SSAA_2X:   return 2.0f;
        case CC_AA_SSAA_3X:   return 3.0f;
        case CC_AA_USD1:      return 2.0f;
        case CC_AA_USD2:      return 3.0f;
        case CC_AA_USD3:      return 4.0f;
        default:              return 1.0f;
    }
}
static bool mode_is_pdaa(CCAAMode m){ return m==CC_AA_PDAA1||m==CC_AA_PDAA2||m==CC_AA_PDAA3; }
static bool mode_is_usd(CCAAMode m){ return m==CC_AA_USD1||m==CC_AA_USD2||m==CC_AA_USD3; }

/* USD is now pure supersampling (no smoothing rounds). Kept for API stability. */
int cc_aa_usd_rounds(CCAAMode m) { (void)m; return 0; }

/* ─── state accessors (the engine owns the CCAAState; these operate on it) ──── */
void cc_aa_state_init(CCAAState* s) {
    if (!s) return;
    s->mode = CC_AA_PDAA1;      /* Pixel Divide AA (finest-cheap) is the default */
    s->usd_smoothing = 1.0f;
    s->usd_scale = 0.0f;        /* 0 = per-preset default */
    s->pdaa_divisions = 0;      /* 0 = per-preset default */
    s->custom_fn = NULL;
    s->custom_state = NULL;
    s->custom_scale = 1.0f;
}

/* These are defined in engine.c-style glue via cc_engine_aa_state(eng). To keep
 * aa.c decoupled from the full CCEngine layout, the engine provides this getter. */
CCAAState* cc_engine_aa_state(CCEngine* eng);   /* implemented in engine.c */

void cc_aa_set_mode(CCEngine* eng, CCAAMode mode) {
    CCAAState* s = cc_engine_aa_state(eng);
    if (!s) return;
    s->mode = mode;
    if (mode != CC_AA_CUSTOM) { s->custom_fn = NULL; s->custom_state = NULL; }
}
CCAAMode cc_aa_get_mode(const CCEngine* eng) {
    CCAAState* s = cc_engine_aa_state((CCEngine*)eng);
    return s ? s->mode : CC_AA_OFF;
}

void cc_aa_set_usd_scale(CCEngine* eng, float scale) {
    CCAAState* s = cc_engine_aa_state(eng);
    if (!s) return;
    if (scale < 1.0f) scale = 1.0f;
    if (scale > CC_USD_SCALE_MAX) scale = CC_USD_SCALE_MAX;
    s->usd_scale = scale;
}
float cc_aa_get_usd_scale(const CCEngine* eng) {
    CCAAState* s = cc_engine_aa_state((CCEngine*)eng);
    return s ? s->usd_scale : 0.0f;
}
void cc_aa_set_pdaa_divisions(CCEngine* eng, uint32_t divisions) {
    CCAAState* s = cc_engine_aa_state(eng);
    if (!s) return;
    if (divisions < 1) divisions = 1;
    if (divisions > (uint32_t)CC_USD_SCALE_MAX) divisions = (uint32_t)CC_USD_SCALE_MAX;
    s->pdaa_divisions = divisions;
}
uint32_t cc_aa_get_pdaa_divisions(const CCEngine* eng) {
    CCAAState* s = cc_engine_aa_state((CCEngine*)eng);
    return s ? s->pdaa_divisions : 0;
}

void cc_aa_use_custom(CCEngine* eng, CCAAResolveFn fn, void* state, float render_scale) {
    CCAAState* s = cc_engine_aa_state(eng);
    if (!s) return;
    if (!fn) { s->mode = CC_AA_OFF; s->custom_fn = NULL; s->custom_state = NULL; return; }
    s->custom_fn = fn;
    s->custom_state = state;
    s->custom_scale = render_scale > 0 ? render_scale : 1.0f;
    s->mode = CC_AA_CUSTOM;
}

float cc_aa_render_scale(const CCEngine* eng) {
    CCAAState* s = cc_engine_aa_state((CCEngine*)eng);
    if (!s) return 1.0f;
    if (s->mode == CC_AA_CUSTOM) return s->custom_scale;
    /* apply user overrides: USD scale, PDAA divisions */
    if (mode_is_usd(s->mode) && s->usd_scale >= 1.0f) return s->usd_scale;
    if (mode_is_pdaa(s->mode) && s->pdaa_divisions >= 1) return (float)s->pdaa_divisions;
    return cc_aa_mode_scale(s->mode);
}

/* ─── accessors used by the renderer's resolve stage ──────────────────────── */
CCAAMode      cc_aa_state_mode(const CCAAState* s)       { return s ? s->mode : CC_AA_OFF; }
float         cc_aa_state_usd_smoothing(const CCAAState* s){ return s ? s->usd_smoothing : 0.0f; }
CCAAResolveFn cc_aa_state_custom_fn(const CCAAState* s)  { return s ? s->custom_fn : NULL; }
void*         cc_aa_state_custom_ud(const CCAAState* s)  { return s ? s->custom_state : NULL; }
