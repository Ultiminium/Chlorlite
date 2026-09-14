#pragma once
/*
 * cc/assetpack.h — asset loading + shareable bundles.
 *
 * Assets you hand to the project live in the in-development library (assets-dev/
 * fonts|models|textures|anims in the CC skill). Reference them in a game with
 * cc_asset("name.ext") (see claudecore.h) — it resolves to the dev library while
 * building and to the shipped assets/ folder at runtime, and the bundler copies
 * exactly the ones you reference into the shipped game.
 *
 * This header adds the loading + bundle plumbing under that:
 *   PER-FILE  cc_asset_load(eng, path)  — load one asset, dispatched by extension.
 *   BUNDLE    a .ccpak packs many assets into ONE shareable file. cc_pack_write
 *             exports; cc_pack_load opens + loads every entry; cc_pack_open/entry/
 *             extract give fine-grained access. Useful for moving an asset set
 *             around as a single file.
 *
 * Format is deliberately simple and dependency-free (raw store) so a .ccpak is
 * trivially portable and inspectable.
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CCEngine;

/* what a loaded asset turned out to be */
typedef enum {
    CC_ASSET_UNKNOWN = 0,
    CC_ASSET_FONT,
    CC_ASSET_MODEL,
    CC_ASSET_TEXTURE,
    CC_ASSET_SCENE,
    CC_ASSET_PACK,       /* a .ccpak — load expands into many assets */
} CCAssetType;

/* result of loading one asset. The concrete handle is in the union by type; the
   caller reads the field matching `type`. id 0 / NULL means load failed. */
typedef struct {
    CCAssetType type;
    const char* name;    /* basename it was loaded as */
    union {
        uint32_t font;      /* CCFont     */
        uint32_t texture;   /* CCTexture  */
        void*    model;     /* CCModel*   */
        void*    scene;     /* CCSceneAsset* */
    } as;
    bool ok;
} CCAssetResult;

/* Guess the asset type from a filename extension (no I/O). */
CCAssetType cc_asset_type_from_path(const char* path);

/* Load a single asset file, dispatched by extension. A .ccpak loads the whole
   bundle (returns type=PACK, ok reflecting whether all entries loaded). */
CCAssetResult cc_asset_load(struct CCEngine* eng, const char* path);

/* ── .ccpak bundles ─────────────────────────────────────────────────────── */
typedef struct CCPack CCPack;

/* WRITE: pack a set of files into one shareable .ccpak. `files` are source paths;
   each is stored under its basename. Returns bytes written, 0 on failure. */
uint64_t cc_pack_write(const char* out_path, const char* const* files, uint32_t nfiles);

/* OPEN (no loading): read a .ccpak's directory so you can inspect/extract entries. */
CCPack*  cc_pack_open(const char* path);
void     cc_pack_close(CCPack* p);
uint32_t cc_pack_count(const CCPack* p);
const char* cc_pack_entry_name(const CCPack* p, uint32_t i);
CCAssetType cc_pack_entry_type(const CCPack* p, uint32_t i);
/* Extract one entry to a destination path (raw bytes). Returns bytes written. */
uint64_t cc_pack_extract(const CCPack* p, uint32_t i, const char* dest_path);

/* LOAD ALL: open a .ccpak and load every entry into the engine (extracting to a
   temp dir as needed). Fills `out` (up to `max`) with per-entry results; returns
   the number loaded. This is the "drop a bundle, get all its assets" path. */
uint32_t cc_pack_load(struct CCEngine* eng, const char* path,
                      CCAssetResult* out, uint32_t max);

#ifdef __cplusplus
}
#endif
