#pragma once
/*
 * .ccmodel — Chlorlite Model Format
 *
 * Magic: CCMDL\x01\x00\x00  (8 bytes)
 * Self-contained: geometry + skeleton + animations + blend shapes + material slots
 * Geometry chunk compressed with zstd level 9
 * All lengths varint-encoded
 * CRC32 checksum in header
 *
 * Layout:
 *   [Header 64 bytes]
 *   [Chunk table: N × ChunkEntry]
 *   [Chunk data: each chunk zstd-compressed]
 *
 * Chunk types:
 *   GEOM  — vertex buffer + index buffer
 *   SKEL  — skeleton (bone names, hierarchy, bind poses)
 *   SKIN  — skin weights (joint indices + weights per vertex)
 *   ANIM  — animation clip (name, duration, keyframe tracks)
 *   BSHP  — blend shape targets
 *   MATL  — material slots
 *   META  — metadata (name, author, timestamps)
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Magic & version ────────────────────────────────────────────────── */
#define CCM_MAGIC     "CCMDL\x01\x00\x00"
#define CCM_MAGIC_LEN 8
#define CCM_VERSION   1

/* ─── Chunk type IDs ─────────────────────────────────────────────────── */
typedef enum CCMChunkType {
    CCM_CHUNK_META = 0x4D455441,  /* 'META' */
    CCM_CHUNK_GEOM = 0x47454F4D,  /* 'GEOM' */
    CCM_CHUNK_SKEL = 0x534B454C,  /* 'SKEL' */
    CCM_CHUNK_SKIN = 0x534B494E,  /* 'SKIN' */
    CCM_CHUNK_ANIM = 0x414E494D,  /* 'ANIM' */
    CCM_CHUNK_BSHP = 0x42534850,  /* 'BSHP' */
    CCM_CHUNK_MATL = 0x4D41544C,  /* 'MATL' */
} CCMChunkType;

/* ─── File header ────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) CCMHeader {
    uint8_t  magic[8];        /* CCM_MAGIC */
    uint16_t version;         /* CCM_VERSION */
    uint16_t chunk_count;     /* number of chunks */
    uint32_t flags;           /* reserved */
    uint32_t crc32;           /* CRC32 of everything after header */
    uint32_t total_size;      /* total file size */
    uint8_t  reserved[40];    /* pad to 64 bytes */
} CCMHeader;

/* ─── Chunk table entry ──────────────────────────────────────────────── */
typedef struct __attribute__((packed)) CCMChunkEntry {
    uint32_t type;            /* CCMChunkType */
    uint32_t id;              /* chunk instance ID (for multiple ANIM chunks, etc.) */
    uint64_t offset;          /* byte offset from file start */
    uint32_t size_compressed; /* compressed size */
    uint32_t size_raw;        /* uncompressed size */
    uint8_t  name[32];        /* human name (animation name, etc.) null-terminated */
} CCMChunkEntry;

/* ─── Geometry chunk (GEOM) ──────────────────────────────────────────── */
/* Interleaved vertex: pos(12) + normal(12) + uv(8) + tangent(16) + color(4) = 52 bytes */
typedef struct __attribute__((packed)) CCMVertex {
    float   pos[3];
    float   normal[3];
    float   uv[2];
    float   tangent[4];   /* xyz=tangent, w=handedness */
    uint8_t color[4];     /* RGBA */
} CCMVertex;              /* 52 bytes */

typedef struct CCMGeomChunk {
    uint32_t   vertex_count;
    uint32_t   index_count;
    uint32_t   submesh_count;  /* for multi-material models */
    CCMVertex* vertices;       /* [vertex_count] */
    uint32_t*  indices;        /* [index_count] */
    /* Submesh table: [submesh_count × {start_index, count, material_slot}] */
    uint32_t*  submesh_start;
    uint32_t*  submesh_count_arr;
    uint32_t*  submesh_material;
} CCMGeomChunk;

/* ─── Skeleton chunk (SKEL) ──────────────────────────────────────────── */
#define CCM_MAX_BONES 256
#define CCM_BONE_NO_PARENT 0xFFFF

typedef struct CCMBone {
    char     name[64];
    uint16_t parent;           /* CCM_BONE_NO_PARENT if root */
    float    bind_pos[3];      /* local bind-pose position */
    float    bind_rot[4];      /* local bind-pose rotation (quaternion xyzw) */
    float    bind_scale[3];    /* local bind-pose scale */
    float    inv_bind_mat[16]; /* inverse bind-pose matrix (column-major) */
} CCMBone;

typedef struct CCMSkelChunk {
    uint16_t bone_count;
    CCMBone* bones;            /* [bone_count] */
} CCMSkelChunk;

/* ─── Skin chunk (SKIN) ──────────────────────────────────────────────── */
/* 4 bone influences per vertex — standard GPU skinning */
typedef struct CCMSkinVertex {
    uint16_t joint[4];   /* bone indices into skeleton */
    float    weight[4];  /* must sum to 1.0 */
} CCMSkinVertex;

typedef struct CCMSkinChunk {
    uint32_t      vertex_count;  /* must match GEOM vertex_count */
    CCMSkinVertex* weights;
} CCMSkinChunk;

/* ─── Animation chunk (ANIM) ─────────────────────────────────────────── */
typedef enum CCMInterpType {
    CCM_INTERP_STEP    = 0,  /* hold value until next key */
    CCM_INTERP_LINEAR  = 1,  /* linear interpolation */
    CCM_INTERP_CUBIC   = 2,  /* cubic Hermite spline */
} CCMInterpType;

typedef enum CCMTrackTarget {
    CCM_TRACK_POS_X = 0, CCM_TRACK_POS_Y, CCM_TRACK_POS_Z,
    CCM_TRACK_ROT_X,     CCM_TRACK_ROT_Y, CCM_TRACK_ROT_Z, CCM_TRACK_ROT_W,
    CCM_TRACK_SCALE_X,   CCM_TRACK_SCALE_Y, CCM_TRACK_SCALE_Z,
    CCM_TRACK_BSHP,      /* blend shape weight */
    CCM_TRACK_CUSTOM,
} CCMTrackTarget;

typedef struct CCMKeyframe {
    float time;     /* seconds */
    float value;
    float in_tan;   /* cubic: in tangent */
    float out_tan;  /* cubic: out tangent */
} CCMKeyframe;

typedef struct CCMTrack {
    uint16_t      bone_index;  /* skeleton bone, or 0xFFFF for blend shape */
    uint16_t      bshp_index;  /* blend shape index if bone_index==0xFFFF */
    CCMTrackTarget target;
    CCMInterpType  interp;
    uint32_t       key_count;
    CCMKeyframe*   keys;       /* [key_count], sorted by time */
} CCMTrack;

typedef struct CCMAnimChunk {
    char      name[64];
    float     duration;        /* seconds */
    float     fps;             /* authored FPS (for baking, not playback) */
    bool      looping;
    uint32_t  track_count;
    CCMTrack* tracks;          /* [track_count] */
} CCMAnimChunk;

/* ─── Blend shape chunk (BSHP) ───────────────────────────────────────── */
typedef struct CCMBlendDelta {
    uint32_t vertex_index;
    float    delta_pos[3];
    float    delta_normal[3];
} CCMBlendDelta;

typedef struct CCMBlendShape {
    char           name[64];   /* "smile", "brow_raise_l", "jaw_open", etc. */
    uint32_t       delta_count;
    CCMBlendDelta* deltas;
} CCMBlendShape;

typedef struct CCMBshpChunk {
    uint32_t       shape_count;
    CCMBlendShape* shapes;
} CCMBshpChunk;

/* ─── Material slot chunk (MATL) ─────────────────────────────────────── */
typedef struct CCMMaterialSlot {
    char     name[64];
    float    base_color[4];    /* RGBA 0-1 */
    float    roughness;
    float    metallic;
    float    emissive[3];
    char     albedo_tex[256];  /* relative path or asset ID */
    char     normal_tex[256];
    char     roughmetal_tex[256];
    char     emissive_tex[256];
    char     ao_tex[256];
    bool     double_sided;
    bool     alpha_blend;
    float    alpha_cutoff;
} CCMMaterialSlot;

typedef struct CCMMatlChunk {
    uint32_t          slot_count;
    CCMMaterialSlot*  slots;
} CCMMatlChunk;

/* ─── In-memory model ────────────────────────────────────────────────── */
typedef struct CCModel {
    char            name[128];
    CCMGeomChunk    geom;
    CCMSkelChunk    skel;
    CCMSkinChunk    skin;
    CCMBshpChunk    bshp;
    CCMMatlChunk    matl;
    uint32_t        anim_count;
    CCMAnimChunk*   anims;       /* [anim_count] */
    bool            has_skel;
    bool            has_skin;
    bool            has_bshp;
} CCModel;

/* ─── API ────────────────────────────────────────────────────────────── */

/* Create empty model */
CCModel* ccm_model_new(const char* name);
void     ccm_model_free(CCModel* m);

/* Geometry */
void ccm_set_geometry(CCModel* m, const CCMVertex* verts, uint32_t nv,
                      const uint32_t* idx, uint32_t ni);
void ccm_add_submesh(CCModel* m, uint32_t start, uint32_t count, uint32_t mat_slot);

/* Skeleton */
uint16_t ccm_add_bone(CCModel* m, const char* name, uint16_t parent,
                      const float pos[3], const float rot[4], const float scale[3]);
void     ccm_compute_inv_bind_poses(CCModel* m);

/* Skin weights */
void ccm_set_skin(CCModel* m, const CCMSkinVertex* weights, uint32_t count);
void ccm_normalize_weights(CCModel* m);

/* Animation */
CCMAnimChunk* ccm_add_anim(CCModel* m, const char* name, float duration, bool looping);
CCMTrack*     ccm_anim_add_track(CCMAnimChunk* anim, uint16_t bone, CCMTrackTarget target,
                                  CCMInterpType interp);
void          ccm_track_add_key(CCMTrack* track, float time, float value,
                                 float in_tan, float out_tan);
CCMAnimChunk* ccm_find_anim(CCModel* m, const char* name);

/* Blend shapes */
CCMBlendShape* ccm_add_blend_shape(CCModel* m, const char* name);
void           ccm_bshp_add_delta(CCMBlendShape* bs, uint32_t vert_idx,
                                   const float delta_pos[3], const float delta_normal[3]);

/* Materials */
uint32_t        ccm_add_material_slot(CCModel* m, const char* name);
CCMMaterialSlot* ccm_get_material_slot(CCModel* m, uint32_t slot);

/* I/O */
bool ccm_save(const CCModel* m, const char* path);
CCModel* ccm_load(const char* path);

/* ─── Text format (.ccmodel v1, human-readable) ──────────────────────────
 * A line-based, dependency-free text serialization of a model's geometry,
 * submeshes, and material slots (skeleton/skin/anim/blendshapes are NOT written
 * by the text writer yet — use the binary ccm_save for rigged models). This is
 * the format the engine LOADS scene geometry from. Round-trips through the same
 * CCModel struct. ccm_load_text auto-detects and also accepts the binary format
 * (so ccm_load_text is a safe general entry point); ccm_save_text always writes
 * text. Grammar (whitespace-separated tokens, '#' to end-of-line = comment):
 *   ccmodel 1
 *   name <string-to-eol>
 *   verts <N>
 *   v px py pz nx ny nz u v tx ty tz tw r g b a      (× N, color 0..255)
 *   tris <M>
 *   f i0 i1 i2                                        (× M)
 *   submesh <start> <count> <material_slot>           (optional, repeatable)
 *   material <name-to-eol>                            (optional, repeatable)
 *     base_color r g b a | roughness x | metallic x
 *     albedo_tex <path> | normal_tex <path> | roughmetal_tex <path>
 *     emissive r g b | alpha_cutoff x | double_sided 0|1
 *   end                                               (closes a material block)
 */
bool     ccm_save_text(const CCModel* m, const char* path);
CCModel* ccm_load_text(const char* path);

/* Peek at file metadata without full load */
bool ccm_peek(const char* path, char* out_name, uint32_t name_len,
              uint32_t* out_bone_count, uint32_t* out_anim_count,
              uint32_t* out_vertex_count);

/* Procedural model builders (Claude's primary creation path) */
CCModel* ccm_make_humanoid(const char* name);   /* full humanoid rig */
CCModel* ccm_make_box(const char* name, float w, float h, float d);
CCModel* ccm_make_sphere(const char* name, float r, uint32_t slices, uint32_t stacks);
CCModel* ccm_make_cylinder(const char* name, float r, float h, uint32_t segs);
CCModel* ccm_make_capsule(const char* name, float r, float h, uint32_t segs);
CCModel* ccm_make_plane(const char* name, float w, float d, uint32_t divs);

/* OBJ import */
CCModel* ccm_import_obj(const char* path, const char* name);

/* GLTF import (geometry + skeleton + animations) */
CCModel* ccm_import_gltf(const char* path, const char* name);

/* Export */
bool ccm_export_obj(const CCModel* m, const char* path);
bool ccm_export_gltf(const CCModel* m, const char* path);

/* Validation */
typedef struct { char msg[256]; bool is_error; } CCMValidMsg;
int  ccm_validate(const CCModel* m, CCMValidMsg* msgs, int max_msgs);

/* Merge two models into one (for instanced scene building) */
CCModel* ccm_merge(const CCModel* a, const CCModel* b, const char* name);

/* Edit operations (used by cc-edit command interface) */
void ccm_subdivide(CCModel* m, uint32_t iterations);
void ccm_compute_normals(CCModel* m);
void ccm_compute_tangents(CCModel* m);
void ccm_weld_vertices(CCModel* m, float threshold);
void ccm_center_pivot(CCModel* m);
void ccm_flip_normals(CCModel* m);
void ccm_scale_geometry(CCModel* m, float sx, float sy, float sz);
void ccm_translate_geometry(CCModel* m, float tx, float ty, float tz);

#ifdef __cplusplus
}
#endif
