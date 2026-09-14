#include "voxel/voxel.h"
#include <stdio.h>
int main(void){
    VoxelTerrainParams tp={.seed=42,.ground_height=8,.hill_amp=4.5f,.hill_freq=0.1f,.cave_freq=0.1f,.cave_threshold=0.14f};
    VoxelRegion R={.min={0,0,0},.max={32,16,32},.nx=64,.ny=32,.nz=64,.iso=0};
    VoxelMesh m; int rc=voxel_generate(&R,voxel_terrain_density,&tp,&m);
    printf("voxel %s: rc=%d verts=%u tris=%u\n",voxel_version(),rc,m.vertex_count,m.index_count/3);
    voxel_mesh_free(&m);
    printf("%s\n",(rc==0&&m.vertex_count==0)?"OK(freed)":"OK");
    return rc;
}
