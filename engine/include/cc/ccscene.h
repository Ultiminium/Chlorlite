#pragma once
/*
 * .cclist — Chlorlite Scene Manifest (text)
 *
 * A whole scene loaded from one text file: which .ccmodel files to load, and
 * where to place each instance (position / scale / Y-rotation), with optional
 * per-instance material-slot selection. Models are loaded and cached by path
 * (each unique model file → one CCMesh + its material set, built once), so many
 * instances of the same model are cheap. The loader returns a flat list of
 * ready-to-draw instances (mesh + material + transform); the caller just loops
 * and calls cc_draw_mesh.
 *
 * Grammar ('#' to end-of-line = comment; tokens whitespace-separated):
 *   cclist 1
 *   name <scene-name-to-eol>                     (optional)
 *   model <path.ccmodel>                          → loads + becomes "current"
 *     at   <x> <y> <z>                            (first instance transform)
 *     scale <sx> <sy> <sz>                        (applies to the pending instance)
 *     rot_y <degrees>                             (applies to the pending instance)
 *     slot <material_slot_index>                  (which material slot to draw with)
 *   instance <x y z> <sx sy sz> <rot_y_degrees>   (another instance of current model)
 *
 * A bare 'model' line with a following 'at' creates one instance; each extra
 * 'instance' line adds another. Texture paths inside the models resolve relative
 * to the directory of the .cclist file.
 */
#include "cc/render.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCSceneAssetInstance {
    CCMesh        mesh;
    CCMaterial    material;
    CCTransform3D xform;
    /* authoring source (so a scene can be serialized back to .cclist) */
    char          model_path[256];  /* the .ccmodel this instance came from */
    uint32_t      material_slot;    /* which slot of that model */
} CCSceneAssetInstance;

typedef struct CCSceneAsset {
    char             name[128];
    char             base_dir[256];   /* for resolving relative model paths on save/reload */
    CCSceneAssetInstance* instances;
    uint32_t         instance_count;
    uint32_t         instance_cap;
    /* owned unique resources (freed by cc_sceneasset_free) */
    CCMesh*          meshes;      uint32_t mesh_count;
    CCMaterial*      materials;   uint32_t material_count;
    void*            _build_cache; /* internal: builder model cache (opaque) */
} CCSceneAsset;

/* Load a .cclist manifest. Returns NULL on failure. Records each instance's
 * source model path + slot so the scene can be re-saved. */
CCSceneAsset* cc_sceneasset_load(CCEngine* eng, const char* path);

/* ─── Scene authoring (build a level in memory, draw it, save it) ─────────
 * Create an empty scene, add instances of .ccmodel files at transforms, then
 * either draw it or serialize it to a hand-editable .cclist. Meshes/materials
 * for each unique model path are loaded and cached on first use. base_dir (may
 * be NULL/"") is prefixed to relative model paths and written into saved files. */
CCSceneAsset* cc_sceneasset_new(const char* name, const char* base_dir);
/* Add an instance of model_path at (pos/scale/rot_y_degrees) using material
 * slot. Loads+caches the model's mesh/materials as needed. Returns the new
 * instance index, or UINT32_MAX on load failure. */
uint32_t cc_sceneasset_add(CCEngine* eng, CCSceneAsset* scene, const char* model_path,
                      float px, float py, float pz,
                      float sx, float sy, float sz,
                      float rot_y_degrees, uint32_t material_slot);
/* Serialize the scene to a .cclist text file (one 'model' block per unique
 * model path, with its instances). Returns false on write failure. */
bool cc_sceneasset_save(const CCSceneAsset* scene, const char* path);

/* Draw every instance in one call (loops cc_draw_mesh). */
void cc_sceneasset_draw(CCEngine* eng, const CCSceneAsset* scene);

/* Free the scene struct (does not destroy GPU meshes/materials by default —
 * pass destroy_gpu=true to also release them via the engine). */
void cc_sceneasset_free(CCEngine* eng, CCSceneAsset* scene, bool destroy_gpu);

#ifdef __cplusplus
}
#endif
