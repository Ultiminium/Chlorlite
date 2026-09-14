# voxel — a standalone Marching Cubes mesher (real 3D terrain + caves)

Turns a 3D **density field** into a triangle mesh, giving you terrain a heightmap
**cannot** produce: overhangs, arches, and real explorable **caves**. (A heightmap is
one height per point — no tunnels. A density field is solid-or-air at every point in
space.)

Standalone and dependency-free (libm only), primitive types only — drop it into any
engine. You provide `float density(x,y,z)` returning <0 for solid rock, >0 for air;
the mesher walks a 3D grid and emits triangles where the surface crosses.

## Build
```sh
gcc -O2 -std=c11 -Iinclude -Isrc src/voxel.c yourprogram.c -lm
```

## Use
```c
VoxelTerrainParams tp={.seed=42,.ground_height=8,.hill_amp=4.5f,
                       .hill_freq=0.10f,.cave_freq=0.10f,.cave_threshold=0.14f};
VoxelRegion R={.min={0,0,0},.max={40,18,40},.nx=80,.ny=40,.nz=80,.iso=0};
VoxelMesh m; voxel_generate(&R, voxel_terrain_density, &tp, &m);
/* m.positions / m.normals / m.indices → upload to your renderer */
voxel_mesh_free(&m);
```
Or pass your own density function to carve any shape/cave system you like.

## Status
v0.1.0. Core mesher proven (generates solid terrain with real cave mouths + tunnels,
verified by rendering). NOT yet included: chunking for infinite/large worlds, and
collision against the generated mesh — both needed to make a big explorable map.
