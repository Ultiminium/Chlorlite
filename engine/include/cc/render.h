#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cc/ecs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCEngine CCEngine;

/* ─── Opaque handles ─────────────────────────────────────────────────── */
typedef uint32_t CCTexture;
typedef uint32_t CCMesh;
typedef uint32_t CCMaterial;
typedef uint32_t CCShader;
typedef uint32_t CCRenderTarget;
typedef uint32_t CCFont;
#define CC_NULL 0

/* ─── Renderer backend ───────────────────────────────────────────────── */
typedef enum CCRendererBackend {
    CC_RENDERER_AUTO     = 0, /* Vulkan → OpenGL → OSMesa (headless) */
    CC_RENDERER_VULKAN,
    CC_RENDERER_OPENGL,
    CC_RENDERER_OSMESA,       /* Software GL — no GPU needed, Claude sandbox */
    CC_RENDERER_HEADLESS,     /* Same as OSMESA */
    CC_RENDERER_NULL,
} CCRendererBackend;

/* ─── Pixel formats ──────────────────────────────────────────────────── */
typedef enum CCPixelFmt {
    CC_FMT_RGBA8   = 0,
    CC_FMT_RGB8,
    CC_FMT_R8,
    CC_FMT_RGBA16F,
    CC_FMT_RG16F,
    CC_FMT_R32F,
    CC_FMT_DEPTH24_STENCIL8,
    CC_FMT_DEPTH32F,
    /* sRGB-encoded RGBA: the GPU decodes sRGB→linear ON SAMPLE (before
     * filtering/mipmapping), which is the physically-correct way to feed
     * color/albedo maps into a linear-space PBR pipeline. Use this for ALBEDO
     * and EMISSIVE color maps (which are authored in sRGB); keep normal,
     * roughness, metallic and AO maps as plain RGBA8/R8 (they are linear data,
     * NOT color, and must not be gamma-decoded). */
    CC_FMT_SRGB8_ALPHA8,
} CCPixelFmt;

/* ─── Texture ────────────────────────────────────────────────────────── */
typedef struct CCTextureDesc {
    uint32_t   width, height;
    CCPixelFmt format;
    bool       mipmaps;
    bool       linear_filter;
    bool       wrap_repeat;
    bool       is_cubemap;
} CCTextureDesc;

CCTexture cc_texture_load(CCEngine* eng, const char* path);
/* Load a COLOR map (albedo/emissive) with sRGB→linear decode on sample. This is
 * the correct loader for anything that represents color; cc_texture_load stays
 * linear and is correct for normal/roughness/metallic/AO data maps. */
CCTexture cc_texture_load_srgb(CCEngine* eng, const char* path);
CCTexture cc_texture_from_memory(CCEngine* eng, const void* data, size_t size);
CCTexture cc_texture_create(CCEngine* eng, const CCTextureDesc* d, const void* pixels);
CCTexture cc_texture_proc(CCEngine* eng, const CCTextureDesc* d,
    void (*gen)(uint8_t* px, uint32_t w, uint32_t h, void* ud), void* ud);
void      cc_texture_update(CCEngine* eng, CCTexture t, const void* pixels);
void      cc_texture_destroy(CCEngine* eng, CCTexture t);

/* ─── Mesh ───────────────────────────────────────────────────────────── */
/* Interleaved vertex: pos(3) normal(3) uv(2) tangent(4) color(4) = 64 bytes */
typedef struct CCVertex {
    float   pos[3];
    float   normal[3];
    float   uv[2];
    float   tangent[4];  /* xyz=tangent w=handedness */
    uint8_t color[4];    /* RGBA */
} CCVertex;

typedef enum CCMeshUsage { CC_MESH_STATIC=0, CC_MESH_DYNAMIC, CC_MESH_STREAM } CCMeshUsage;

CCMesh cc_mesh_create(CCEngine* eng,
                      const CCVertex* verts, uint32_t nv,
                      const uint32_t* idx,   uint32_t ni,
                      CCMeshUsage usage);
CCMesh cc_mesh_load_obj(CCEngine* eng, const char* path);
CCMesh cc_mesh_load_gltf(CCEngine* eng, const char* path);
/* Build a renderable mesh from a loaded .ccmodel's geometry (see cc/ccmodel.h,
 * ccm_load_text / ccm_load). Uploads the model's vertex+index buffers as one
 * mesh; submesh/material slots are carried on the CCModel and can be queried
 * separately. This is the bridge that makes loading a model from disk drawable.
 * Forward-declared as void* to avoid a hard ccmodel.h dependency in render.h. */
CCMesh cc_mesh_from_model(CCEngine* eng, const void* ccmodel);
/* Convenience: load a .ccmodel text (or binary) file straight to a mesh. Returns
 * CC_NULL on failure. Frees the intermediate CCModel. */
CCMesh cc_mesh_load_ccmodel(CCEngine* eng, const char* path);
/* Build a CCMaterial from a model's material slot #slot (see cc/ccmodel.h MATL).
 * Textures named in the slot are loaded with the correct color space (albedo +
 * emissive via the sRGB path, normal/roughmetal/ao linear) and resolved relative
 * to base_dir (pass the model file's directory, or NULL for cwd). Falls back to
 * the slot's scalar base_color/roughness/metallic where no map is named. Returns
 * CC_NULL if the model has no such slot. */
CCMaterial cc_material_from_model_slot(CCEngine* eng, const void* ccmodel,
                                       uint32_t slot, const char* base_dir);
/* Build every material slot of a model into out[], up to max. Returns the count
 * written. If a model has no slots, writes one sensible default and returns 1. */
uint32_t cc_materials_from_model(CCEngine* eng, const void* ccmodel,
                                 CCMaterial* out, uint32_t max, const char* base_dir);
void   cc_mesh_update(CCEngine* eng, CCMesh m,
                      const CCVertex* verts, uint32_t nv,
                      const uint32_t* idx,   uint32_t ni);
void   cc_mesh_destroy(CCEngine* eng, CCMesh m);

/* Procedural geometry */
CCMesh cc_mesh_quad(CCEngine* eng);
CCMesh cc_mesh_cube(CCEngine* eng, float s);
CCMesh cc_mesh_sphere(CCEngine* eng, float r, uint32_t slices, uint32_t stacks);
CCMesh cc_mesh_plane(CCEngine* eng, float w, float h, uint32_t divs);
CCMesh cc_mesh_capsule(CCEngine* eng, float r, float h, uint32_t segs);
CCMesh cc_mesh_torus(CCEngine* eng, float r_major, float r_minor, uint32_t segs);
CCMesh cc_mesh_cylinder(CCEngine* eng, float r, float h, uint32_t segs);
CCMesh cc_mesh_cone(CCEngine* eng, float r, float h, uint32_t segs);

/* ─── Geometry toolkit (CPU vertex/index processing) ─────────────────────
 * Operate on a raw interleaved CCVertex buffer + index buffer BEFORE upload
 * via cc_mesh_create. Buffers are caller-owned; ops that change vertex count
 * (weld) return the new count and compact in place. All are headless-safe
 * (no GL) so they can run in tools, tests, or game init identically. */

/* Recompute per-vertex normals. If smooth, area-weighted normals are averaged
 * across all faces sharing a position (welded implicitly by position); if not,
 * each vertex gets its face normal (call after a flat/unwelded build). */
void cc_geometry_recompute_normals(CCVertex* verts, uint32_t nv,
                                   const uint32_t* idx, uint32_t ni, bool smooth);

/* Recompute tangents (xyz + handedness in .w) from positions/normals/UVs via
 * the Lengyel method. Requires valid normals + a non-degenerate UV set. */
void cc_geometry_recompute_tangents(CCVertex* verts, uint32_t nv,
                                    const uint32_t* idx, uint32_t ni);

/* Weld vertices closer than `epsilon` (position only) into shared vertices,
 * rewriting `idx` to point at the survivors and compacting `verts`. Returns the
 * new vertex count. `ni` is unchanged (indices only remap). */
uint32_t cc_geometry_weld(CCVertex* verts, uint32_t* nv_inout,
                          uint32_t* idx, uint32_t ni, float epsilon);

/* Axis-aligned bounds of a vertex buffer. min/max may be NULL. */
void cc_geometry_bounds(const CCVertex* verts, uint32_t nv,
                        float out_min[3], float out_max[3]);

/* Flip winding + normals (turn a mesh inside-out, e.g. for skyboxes). */
void cc_geometry_flip(CCVertex* verts, uint32_t nv, uint32_t* idx, uint32_t ni);

/* ─── Shader ─────────────────────────────────────────────────────────── */
CCShader cc_shader_load(CCEngine* eng, const char* vert_path, const char* frag_path);
CCShader cc_shader_load_src(CCEngine* eng, const char* vert_src, const char* frag_src);
CCShader cc_shader_load_spirv(CCEngine* eng, const void* vert, size_t vsz,
                                             const void* frag, size_t fsz);
void     cc_shader_set_int(CCEngine* eng, CCShader s, const char* name, int v);
void     cc_shader_set_float(CCEngine* eng, CCShader s, const char* name, float v);
void     cc_shader_set_vec3(CCEngine* eng, CCShader s, const char* name, float x, float y, float z);
void     cc_shader_set_vec4(CCEngine* eng, CCShader s, const char* name, float x, float y, float z, float w);
void     cc_shader_set_mat4(CCEngine* eng, CCShader s, const char* name, const float* m4x4);
void     cc_shader_destroy(CCEngine* eng, CCShader s);
void     cc_post_custom(CCEngine* eng, CCShader s);  /* run custom fullscreen shader pass */

/* ─── Material (PBR) ─────────────────────────────────────────────────── */
typedef struct CCMaterialDesc {
    /* Albedo */
    float     base_color[4];   /* RGBA 0-1, multiplied with albedo_map */
    CCTexture albedo_map;
    /* PBR */
    float     roughness;       /* 0=mirror 1=matte */
    float     metallic;        /* 0=dielectric 1=metal */
    CCTexture roughness_metallic_map; /* R=roughness G=metallic */
    /* Normal */
    CCTexture normal_map;
    /* Detail normal: a second, high-frequency normal map tiled at detail_scale ×
     * the base UVs and blended over the base normal. Adds close-up micro-surface
     * detail (pores, weave, grain) so surfaces hold up when the camera is near —
     * a hallmark of the photorealistic look. 0 map = disabled. */
    CCTexture detail_normal_map;
    float     detail_normal_scale;   /* UV tiling multiplier (0 => default 8.0) */
    float     detail_normal_strength;/* blend weight 0..1 (0 => default 1.0)     */
    /* Emissive */
    float     emissive[3];
    CCTexture emissive_map;
    /* AO */
    CCTexture ao_map;
    /* Misc */
    bool      double_sided;
    bool      alpha_blend;
    float     alpha_cutoff;    /* 0 = disabled */
    CCShader  custom_shader;   /* 0 = built-in PBR */
    float     tint[4];         /* per-material color multiply (0 => treated as 1,1,1,1) */
    bool      unlit;           /* skip lighting: emit albedo+emissive directly */
    /* ─── Realism axis: shading model (hyper-real ↔ non-real) ───────────
     * Selects HOW light is turned into color for this material. PBR is the
     * physically-based (hyper-real) end; TOON/FLAT/RIM move toward stylized
     * (non-real). All share the same PBR G-buffer inputs, so you can mix
     * realistic and stylized objects in one scene. */
    int       shading_model;   /* CCShadingModel; 0 = PBR (default) */
    int       toon_bands;      /* TOON: number of quantized light steps (2..8; 0=default 4) */
    float     toon_specular;   /* TOON: 0=no spec highlight, 1=full stepped highlight */
    float     rim_strength;    /* RIM/all: fresnel rim-light amount (0=off) */
    float     rim_power;       /* RIM: rim falloff exponent (0=default 3.0) */
    float     rim_color[3];    /* RIM: rim light color (0,0,0 => uses light color) */
} CCMaterialDesc;

/* Shading models along the realism axis. */
typedef enum CCShadingModel {
    CC_SHADE_PBR      = 0,  /* physically-based Cook-Torrance (hyper-real)      */
    CC_SHADE_TOON     = 1,  /* quantized cel bands + optional stepped specular  */
    CC_SHADE_FLAT     = 2,  /* single-step lambert, no specular (matte stylized)*/
    CC_SHADE_RIM      = 3,  /* lambert + strong fresnel rim (non-real accent)   */
    CC_SHADE_COUNT
} CCShadingModel;

CCMaterial cc_material_create(CCEngine* eng, const CCMaterialDesc* d);
/* ─── PBR texture-set loading ─────────────────────────────────────────────
 * Load a full scanned-PBR material from a directory of maps, the way real
 * asset packs ship them (e.g. a "rusted_metal/" folder with basecolor +
 * normal + roughness + metallic + ao PNGs). The loader probes conventional
 * filename stems (case-insensitive) for each channel and wires them into a
 * material with the CORRECT color space per map:
 *   albedo/basecolor/diffuse/color/col    → sRGB
 *   normal/nrm/normalgl                    → linear
 *   roughness/rough/rgh                    → linear (packed R)
 *   metallic/metal/metalness/mtl           → linear (packed G)
 *   orm/arm/roughmetal (combined)          → linear (remapped to R=rough,G=metal)
 *   ao/ambientocclusion/occlusion          → linear
 *   emissive/emission/emit                 → sRGB
 * Roughness and metallic, if supplied as separate single-channel files, are
 * packed CPU-side into one RG texture (R=rough, G=metal) matching the
 * gbuffer's uRoughMetal expectation. Missing maps are left unbound and fall
 * back to the scalar roughness/metallic in `base`. Supported extensions:
 * png, jpg, jpeg, tga, bmp. Returns 0 if the directory has no albedo map.
 * `base` supplies defaults (base_color, scalar roughness/metallic, shading
 * model, etc.); NULL means neutral defaults. */
CCMaterial cc_material_load_pbr(CCEngine* eng, const char* dir, const CCMaterialDesc* base);
void       cc_material_set_base_color(CCEngine* eng, CCMaterial m, float r, float g, float b, float a);
void       cc_material_set_roughness(CCEngine* eng, CCMaterial m, float v);
void       cc_material_set_metallic(CCEngine* eng, CCMaterial m, float v);
void       cc_material_destroy(CCEngine* eng, CCMaterial m);

/* ─── Render target ──────────────────────────────────────────────────── */
CCRenderTarget cc_rt_create(CCEngine* eng, uint32_t w, uint32_t h,
                            CCPixelFmt color_fmt, bool depth, uint32_t msaa);
void           cc_rt_destroy(CCEngine* eng, CCRenderTarget rt);
CCTexture      cc_rt_color_texture(CCEngine* eng, CCRenderTarget rt);
CCTexture      cc_rt_depth_texture(CCEngine* eng, CCRenderTarget rt);
void           cc_rt_read_pixels(CCEngine* eng, CCRenderTarget rt,
                                  void* out_rgba8, uint32_t* w, uint32_t* h);
/* Copy the current composited frame into the RT's color buffer (for
 * camera-monitor / mirror / feedback effects). Call after cc_frame_end. */
void           cc_rt_capture(CCEngine* eng, CCRenderTarget rt);

/* ─── Lighting ───────────────────────────────────────────────────────── */
typedef enum CCLightType {
    CC_LIGHT_DIRECTIONAL = 0,
    CC_LIGHT_POINT,
    CC_LIGHT_SPOT,
} CCLightType;

typedef struct CCLight {
    CCLightType type;
    float pos[3];
    float dir[3];
    float color[3];
    float intensity;
    float range;          /* point/spot */
    float inner_angle;    /* spot, radians */
    float outer_angle;    /* spot, radians */
    bool  cast_shadows;
    uint32_t shadow_map_size; /* 0=default(1024) */
} CCLight;

typedef uint32_t CCLightId;
CCLightId cc_light_add(CCEngine* eng, const CCLight* l);
void      cc_light_update(CCEngine* eng, CCLightId id, const CCLight* l);
void      cc_light_remove(CCEngine* eng, CCLightId id);
void      cc_light_set_ambient(CCEngine* eng, float r, float g, float b, float intensity);
/* PCSS shadow softness: light size in shadow-map UV units. Larger = softer,
 * wider penumbrae; smaller = crisper. Default 3.0. */
void      cc_light_set_shadow_softness(CCEngine* eng, float softness);
void      cc_light_set_sky(CCEngine* eng, CCTexture hdr_cubemap);
/* Style the procedural sky + IBL (also enables it). Colors are linear RGB.
   Pass NULL for any stop to leave it unchanged; intensity<=0 leaves it as-is. */
void      cc_light_set_sky_colors(CCEngine* eng, const float zenith[3],
                                  const float horizon[3], const float ground[3],
                                  float intensity);

/* ─── Camera ─────────────────────────────────────────────────────────── */
typedef struct CCCameraDesc {
    float pos[3];
    float target[3];
    float up[3];
    float fov_deg;     /* 0 = orthographic */
    float ortho_size;
    float near_plane;
    float far_plane;
    float exposure;    /* tone mapping */
} CCCameraDesc;

void cc_camera_set(CCEngine* eng, const CCCameraDesc* cam);
void cc_camera_get(CCEngine* eng, CCCameraDesc* out);

/* ─── 3D draw calls ──────────────────────────────────────────────────── */
typedef struct CCTransform3D {
    float pos[3];
    float rot[4];  /* quaternion xyzw */
    float scale[3];
} CCTransform3D;

void cc_draw_mesh(CCEngine* eng, CCMesh mesh, CCMaterial mat, const CCTransform3D* xf);
void cc_mesh_attach_skin(CCEngine* eng, CCMesh mesh, const uint16_t* joints4, const float* weights4, uint32_t vertex_count);
void cc_set_bones(CCEngine* eng, const float* mats16, uint32_t bone_count);
void cc_draw_skinned(CCEngine* eng, CCMesh mesh, CCMaterial mat, const CCTransform3D* xf);
void cc_draw_mesh_instanced(CCEngine* eng, CCMesh mesh, CCMaterial mat,
                             const CCTransform3D* xforms, uint32_t count);
void cc_draw_mesh_wireframe(CCEngine* eng, CCMesh mesh, const CCTransform3D* xf,
                             float r, float g, float b);
void cc_draw_bounds(CCEngine* eng, const CCTransform3D* xf,
                    float r, float g, float b);

/* ─── Billboards ─────────────────────────────────────────────────────────
   Camera-facing textured quads in world space. Spherical billboards face the
   camera fully (particles, sprites, impostors); cylindrical ones rotate only
   about the world +Y axis (trees, grass, foliage). Drawn instanced through the
   deferred pipeline, so they receive lighting and contribute to bloom. Each
   billboard emits as unlit * tint * texture; supply an emissive-style bright
   tint for glowing particles. */
typedef enum {
    CC_BILLBOARD_SPHERICAL = 0,   /* full camera-facing */
    CC_BILLBOARD_CYLINDRICAL      /* yaw-only about world +Y */
} CCBillboardMode;

typedef struct CCBillboard {
    float pos[3];       /* world-space center */
    float size[2];      /* width, height in world units */
    float color[4];     /* linear RGBA multiplied with the texture */
} CCBillboard;

/* Draw `count` billboards sharing one texture (CC_NULL = solid color quad). */
void cc_draw_billboards(CCEngine* eng, CCTexture tex,
                        const CCBillboard* billboards, uint32_t count,
                        CCBillboardMode mode);

/* ─── Decals ─────────────────────────────────────────────────────────────
   Deferred projected decals: a box volume is projected onto whatever geometry
   already occupies the G-buffer, stamping a texture onto those surfaces (bullet
   holes, blood, cracks, logos, puddles). Applied after geometry, before lighting,
   so decals are lit exactly like the surface they land on. Submit during the
   frame; they flush at frame end. The box is centered at `pos`, oriented by a
   quaternion, sized by `size` (x,z span the stamped area, y is projection depth).
   `angle_fade` (radians) discards where the surface normal deviates too far from
   the decal's -Y axis, preventing stretching on steep faces (0 = no culling). */
typedef struct CCDecal {
    float pos[3];        /* box center in world space */
    float rot[4];        /* orientation quaternion xyzw */
    float size[3];       /* x,z = footprint; y = projection thickness */
    float color[4];      /* linear RGBA multiplied with the texture */
    float emissive;      /* >0 makes the decal glow (and bloom) */
    float angle_fade;    /* radians; 0 disables normal-based culling */
} CCDecal;

/* Submit one decal for this frame (CC_NULL tex = solid color box stamp). */
void cc_draw_decal(CCEngine* eng, CCTexture tex, const CCDecal* decal);

/* ─── 2D draw calls ──────────────────────────────────────────────────── */
/* All 2D coords in pixels from top-left */
void cc_draw_sprite(CCEngine* eng, CCTexture tex,
                    float x, float y, float w, float h,
                    float angle_deg, uint32_t tint);
void cc_draw_sprite_ex(CCEngine* eng, CCTexture tex,
                       float x, float y, float w, float h,
                       float u0, float v0, float u1, float v1,
                       float angle_deg, uint32_t tint, int layer);
void cc_draw_rect(CCEngine* eng, float x, float y, float w, float h,
                  uint32_t fill, float border_px, uint32_t border_col);
void cc_draw_circle(CCEngine* eng, float cx, float cy, float r,
                    uint32_t fill, float border_px, uint32_t border_col);
void cc_draw_line(CCEngine* eng, float x0, float y0, float x1, float y1,
                  float width, uint32_t col);
void cc_draw_triangle(CCEngine* eng, float ax, float ay, float bx, float by,
                      float cx2, float cy2, uint32_t col);

/* ─── Text ───────────────────────────────────────────────────────────── */
CCFont cc_font_load(CCEngine* eng, const char* path, float px_size);
CCFont cc_font_builtin(CCEngine* eng);  /* embedded fallback font, always works */
void   cc_draw_text(CCEngine* eng, CCFont font, const char* text,
                    float x, float y, float size, uint32_t color);
void   cc_draw_text_wrap(CCEngine* eng, CCFont font, const char* text,
                          float x, float y, float w, float size, uint32_t color);
float  cc_text_width(CCEngine* eng, CCFont font, const char* text, float size);
float  cc_font_line_height(CCEngine* eng, CCFont font, float size);
void   cc_font_destroy(CCEngine* eng, CCFont f);

/* ─── GUI popups (immediate mode) ───────────────────────────────────── */
/* Rendered on top of everything, captured in screenshots */
void  cc_gui_begin_frame(CCEngine* eng);
bool  cc_gui_window(CCEngine* eng, const char* title,
                    float x, float y, float w, float h);
void  cc_gui_label(CCEngine* eng, const char* text);
bool  cc_gui_button(CCEngine* eng, const char* label);
float cc_gui_slider(CCEngine* eng, const char* label, float val, float mn, float mx);
bool  cc_gui_checkbox(CCEngine* eng, const char* label, bool* val);
void  cc_gui_separator(CCEngine* eng);
void  cc_gui_progress(CCEngine* eng, const char* label, float frac, uint32_t rgba);
void  cc_gui_spacer(CCEngine* eng, float pixels);
bool  cc_gui_button_at(CCEngine* eng, const char* label, float x, float y, float w, float h);
void  cc_gui_get_cursor(CCEngine* eng, float* x, float* y);
void  cc_gui_end_window(CCEngine* eng);
void  cc_gui_end_frame(CCEngine* eng);

/* ─── Frame / screenshot ─────────────────────────────────────────────── */
void        cc_frame_begin(CCEngine* eng);
void        cc_frame_end(CCEngine* eng);    /* submit + swap */
const char* cc_screenshot(CCEngine* eng, const char* path); /* PNG */
void        cc_frame_pixels(CCEngine* eng, uint8_t** px, uint32_t* w, uint32_t* h);
/* Resize the offscreen buffer (headless mode) */
void        cc_resize(CCEngine* eng, uint32_t w, uint32_t h);

/* ─── Post-processing ────────────────────────────────────────────────── */
typedef struct CCPostFX {
    bool  bloom;
    float bloom_threshold;
    float bloom_intensity;
    bool  tonemap_aces;     /* ACES filmic tone mapping */
    bool  fxaa;
    float gamma;
    float saturation;
    float contrast;
    bool  vignette;
    float vignette_strength;
    bool  chromatic_aberration;
    float ca_strength;
    bool  film_grain;
    float grain_strength;
    bool  scanlines;
    float scanline_strength;
    bool  fog;
    float fog_density;      /* exponential distance fog */
    float fog_color[3];
    float fog_height;       /* height-based falloff origin (0=disabled) */
    bool  ssao;             /* screen-space ambient occlusion */
    float ssao_radius;      /* sample radius in world units (0=default 0.5) */
    float ssao_intensity;   /* occlusion strength (0=default 1.0) */
    bool  ssr;              /* screen-space reflections */
    float ssr_intensity;    /* reflection strength (0=default 1.0) */
    float ssr_max_distance; /* max ray march distance, world units (0=default 20) */
    /* SSGI — screen-space global illumination (one indirect bounce + color bleed).
     * The biggest realism cue after direct lighting: fills shadowed/indirect areas
     * and bleeds surface color onto neighbors. Screen-space approximation. */
    bool  ssgi;             /* enable screen-space GI */
    float ssgi_intensity;   /* indirect strength (0=default 1.0) */
    float ssgi_radius;      /* gather radius in world units (0=default 2.0) */
    /* TAA — temporal anti-aliasing. Sub-pixel jitter accumulated across frames:
     * the biggest single reduction of the "CG look" (clean edges, no shimmer),
     * and it denoises SSGI/shadows/specular for free. Best with several frames
     * rendered per shot (the headless perceive->act loop already renders 6-8). */
    bool  taa;              /* enable temporal anti-aliasing */
    float taa_blend;        /* history weight 0..0.97 (0=default 0.9) */
    bool  outline;          /* edge-detection outlines (depth+normal Sobel) */
    float outline_color[3]; /* line color (linear RGB) */
    float outline_thickness;/* edge sample offset in pixels (0=default 1.0) */
    float outline_depth_sensitivity;  /* 0=default 1.0 */
    float outline_normal_sensitivity; /* 0=default 1.0 */
    /* ─── Depth of field (camera focus) ─────────────────────────────────
     * A real lens focuses at one distance; everything nearer/farther blurs by
     * an amount set by aperture. Its ABSENCE (everything perfectly sharp) is a
     * classic "this is CG" tell. This is an artist-friendly (not physical
     * f-stop) model: pick a focus distance and a focal range that stays sharp;
     * beyond that, blur ramps up to dof_max_blur. Depth-aware so sharp
     * foreground doesn't smear onto blurred background. Runs on the final
     * resolved image; 2D/UI drawn afterward stays crisp. */
    bool  dof;              /* enable depth of field */
    float dof_focus_dist;   /* world distance in perfect focus (0=default 8)   */
    float dof_focus_range;  /* half-width of the sharp zone, world u (0=def 3) */
    float dof_max_blur;     /* max blur radius in pixels (0=default 6)         */
    /* ─── Auto-exposure (eye adaptation) ────────────────────────────────
     * When on, the renderer measures average scene luminance each frame and
     * drives exposure automatically toward a middle-grey key — scenes expose
     * correctly without hand-tuning camera.exposure (which still multiplies on
     * top as a manual bias). This is the biggest single realism win for varied
     * lighting: bright and dark scenes both land in a filmic range. */
    bool  auto_exposure;
    float ae_key;           /* target middle-grey (0=default 0.18)             */
    float ae_speed;         /* adaptation rate per second (0=default 3.0)      */
    float ae_min;           /* clamp adapted luminance floor (0=default 0.03)  */
    float ae_max;           /* clamp adapted luminance ceiling (0=default 8.0) */
} CCPostFX;

void cc_postfx_set(CCEngine* eng, const CCPostFX* fx);
CCPostFX cc_postfx_default(void);

#ifdef __cplusplus
}
#endif

/* ─── ECS → renderer draw path ───────────────────────────────────────── */
CCComponentId cc_transform_component(void);      /* standard CCTransform component id */
CCComponentId cc_meshrenderer_component(void);   /* standard CCMeshRenderer component id */
uint32_t   cc_scene_render(CCEngine* eng, CCScene* scene);  /* draw all renderable entities */
CCEntityId cc_spawn_mesh(CCEngine* eng, CCScene* scene, CCMesh mesh, CCMaterial mat, float x, float y, float z);
