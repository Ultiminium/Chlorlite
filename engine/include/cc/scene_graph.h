#pragma once
#include "ccmath.h"
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct CCSceneGraph CCSceneGraph;

CCSceneGraph* cc_scenegraph_create(void);
void          cc_scenegraph_destroy(CCSceneGraph* g);
void          cc_scenegraph_update(CCSceneGraph* g);   /* recompute world transforms */
uint32_t      cc_scenegraph_node_count(CCSceneGraph* g);

uint32_t cc_node_create(CCSceneGraph* g, const char* name, int32_t parent); /* parent -1 = root */
void     cc_node_destroy(CCSceneGraph* g, uint32_t id);
void     cc_node_set_position(CCSceneGraph* g, uint32_t id, CCVec3 p);
void     cc_node_set_rotation(CCSceneGraph* g, uint32_t id, CCQuat q);
void     cc_node_set_scale(CCSceneGraph* g, uint32_t id, CCVec3 s);
void     cc_node_set_mesh(CCSceneGraph* g, uint32_t id, uint32_t mesh, uint32_t material);
void     cc_node_set_parent(CCSceneGraph* g, uint32_t id, int32_t new_parent);

CCVec3       cc_node_world_position(CCSceneGraph* g, uint32_t id);
const float* cc_node_world_matrix(CCSceneGraph* g, uint32_t id);
uint32_t     cc_node_mesh(CCSceneGraph* g, uint32_t id, uint32_t* out_material);

#ifdef __cplusplus
}
#endif
