#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCEngine CCEngine;
typedef uint32_t CCTextureId;
typedef uint32_t CCMeshId;
typedef uint32_t CCSoundId;
typedef uint32_t CCAnimationId;

/* ─── Texture procedural generation ─────────────────────────────────── */
typedef struct CCProcTexDesc {
    uint32_t width, height;
    enum {
        CC_PROC_TEX_NOISE,      /* Perlin/value noise */
        CC_PROC_TEX_VORONOI,
        CC_PROC_TEX_CHECKER,
        CC_PROC_TEX_GRADIENT,
        CC_PROC_TEX_MARBLE,
        CC_PROC_TEX_WOOD,
        CC_PROC_TEX_FIRE,
        CC_PROC_TEX_TERRAIN_HEIGHT,
        CC_PROC_TEX_CUSTOM,
    } type;
    uint64_t seed;
    float    scale;
    float    octaves;
    float    persistence;
    float    lacunarity;
    uint32_t color_a;        /* RGBA packed — used differently per type */
    uint32_t color_b;
    /* Custom generator: receives (u, v) in 0-1, returns RGBA */
    uint32_t (*custom)(float u, float v, uint64_t seed, void* userdata);
    void* custom_userdata;
} CCProcTexDesc;

CCTextureId cc_procgen_texture(CCEngine* eng, const CCProcTexDesc* desc);

/* Convenience shorthands */
CCTextureId cc_procgen_noise_texture(CCEngine* eng, uint32_t w, uint32_t h,
                                      uint64_t seed, float scale);
CCTextureId cc_procgen_terrain_heightmap(CCEngine* eng, uint32_t w, uint32_t h,
                                          uint64_t seed, int octaves);

/* ─── Sprite sheet generation ────────────────────────────────────────── */
typedef struct CCProcSpriteDesc {
    uint32_t frame_w, frame_h;
    uint32_t frame_count;
    enum {
        CC_PROC_SPRITE_IDLE,
        CC_PROC_SPRITE_WALK,
        CC_PROC_SPRITE_RUN,
        CC_PROC_SPRITE_JUMP,
        CC_PROC_SPRITE_ATTACK,
        CC_PROC_SPRITE_EXPLOSION,
        CC_PROC_SPRITE_CUSTOM,
    } anim_type;
    uint64_t seed;
    uint32_t palette[8];   /* up to 8 colors */
    uint32_t palette_count;
    void (*custom_frame)(uint8_t* pixels, uint32_t w, uint32_t h,
                          uint32_t frame, void* userdata);
    void* custom_userdata;
} CCProcSpriteDesc;

CCTextureId cc_procgen_sprite_sheet(CCEngine* eng, const CCProcSpriteDesc* desc);

/* ─── Animation generation ───────────────────────────────────────────── */
typedef struct CCAnimation {
    uint32_t  texture_id;
    uint32_t  frame_w, frame_h;
    uint32_t  frame_count;
    float     fps;
    bool      loop;
} CCAnimation;

CCAnimationId cc_procgen_animation(CCEngine* eng, const CCProcSpriteDesc* desc, float fps);
CCAnimation*  cc_animation_get(CCEngine* eng, CCAnimationId id);
void          cc_animation_destroy(CCEngine* eng, CCAnimationId id);

/* ─── Terrain mesh generation ────────────────────────────────────────── */
typedef struct CCProcTerrainDesc {
    uint32_t    grid_w, grid_h;
    float       cell_size;
    CCTextureId heightmap;        /* R8 or R16 — 0 to generate inline */
    uint64_t    heightmap_seed;
    float       height_scale;
    int         octaves;
    uint32_t    material_id;
} CCProcTerrainDesc;

CCMeshId cc_procgen_terrain(CCEngine* eng, const CCProcTerrainDesc* desc);

/* ─── Utility noise functions (available without full engine context) ── */
float cc_noise2(float x, float y, uint64_t seed);
float cc_noise3(float x, float y, float z, uint64_t seed);
float cc_fbm2(float x, float y, uint64_t seed, int octaves, float persistence, float lacunarity);

#ifdef __cplusplus
}
#endif
