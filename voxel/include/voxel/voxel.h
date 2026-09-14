#ifndef VOXEL_H
#define VOXEL_H
/*
 * voxel — a standalone Marching Cubes mesher. Turns a 3D DENSITY FIELD into a
 * triangle mesh, so you get REAL 3D terrain: overhangs, arches, and CAVES —
 * things a heightmap fundamentally cannot represent (a heightmap is one height per
 * point; a density field is solid-or-air at every point in space).
 *
 * Standalone + primitive: you provide a density function f(x,y,z) that returns
 * < 0 for solid (rock) and > 0 for empty (air). The mesher walks a 3D grid, and
 * wherever the surface (density = 0) crosses, it emits triangles. Output is flat
 * float arrays (positions + normals + indices) you upload to any engine. No engine
 * types, depends only on libm.
 *
 * Carve caves by making the density positive (air) along tunnels; make solid ground
 * by making it negative below the surface. See voxel_terrain_density() for a ready
 * example that produces hilly ground riddled with winding caves.
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* density callback: return <0 = solid, >0 = air. user is your context. */
typedef float (*VoxelDensityFn)(float x, float y, float z, void* user);

/* The generated mesh. Arrays are malloc'd by voxel_generate; free with
 * voxel_mesh_free. Positions/normals are 3 floats each; indices are triangles. */
typedef struct {
    float*    positions;   /* [vertex_count*3] xyz */
    float*    normals;     /* [vertex_count*3] xyz (computed from the field gradient) */
    uint32_t* indices;     /* [index_count]   triangles */
    uint32_t  vertex_count;
    uint32_t  index_count;
} VoxelMesh;

/* Region to mesh, in world units, sampled on a grid of (nx,ny,nz) cells.
 * Larger grid = finer detail + more triangles + slower. iso is the surface level
 * (0 for the <0 solid / >0 air convention above). */
typedef struct {
    float    min[3], max[3];     /* world-space bounds of the region */
    uint32_t nx, ny, nz;         /* grid resolution (cells per axis) */
    float    iso;                /* surface threshold (usually 0) */
} VoxelRegion;

/* Generate a mesh for `region` from `density`. Returns 0 on success, fills `out`.
 * Non-zero on allocation failure. Empty regions produce a 0-triangle mesh (ok). */
int  voxel_generate(const VoxelRegion* region, VoxelDensityFn density, void* user,
                    VoxelMesh* out);
void voxel_mesh_free(VoxelMesh* m);

/* A ready-made terrain+caves density function you can pass straight to
 * voxel_generate (cast a VoxelTerrainParams* as the user pointer). Solid rolling
 * ground below a surface, with winding tunnels carved through it. */
typedef struct {
    uint64_t seed;
    float    ground_height;   /* base surface height (world y) */
    float    hill_amp;        /* how tall the hills are */
    float    hill_freq;       /* how wide the hills are (smaller = broader) */
    float    cave_freq;       /* cave tunnel scale */
    float    cave_threshold;  /* how much of the rock is hollowed into caves (0..1) */
} VoxelTerrainParams;
float voxel_terrain_density(float x, float y, float z, void* params);

#define VOXEL_VERSION "0.1.0"
const char* voxel_version(void);

#ifdef __cplusplus
}
#endif
#endif /* VOXEL_H */
