/*
 * scene_render.c — ECS → renderer draw path
 *
 * cc_scene_render() queries every entity that has both a CCTransform and a
 * CCMeshComp (the built-in components declared in ecs.h) and issues draw calls.
 * This bridges the ECS to the renderer so a populated scene renders itself,
 * instead of requiring manual immediate-mode draw calls.
 */
#include "cc/ecs.h"
#include "cc/render.h"
#include "cc/claudecore.h"
#include <string.h>
#include <math.h>

static CCComponentId s_transform_comp = 0;
static CCComponentId s_mesh_comp = 0;

CCComponentId cc_transform_component(void) {
    if (!s_transform_comp) s_transform_comp = cc_component_register("CCTransform", sizeof(CCTransform));
    return s_transform_comp;
}
CCComponentId cc_meshrenderer_component(void) {
    if (!s_mesh_comp) s_mesh_comp = cc_component_register("CCMeshComp", sizeof(CCMeshComp));
    return s_mesh_comp;
}

/* Euler (degrees) → quaternion xyzw */
static void euler_to_quat(float rx, float ry, float rz, float* q) {
    float dr = 0.01745329252f;
    float cx=cosf(rx*dr*0.5f), sx=sinf(rx*dr*0.5f);
    float cy=cosf(ry*dr*0.5f), sy=sinf(ry*dr*0.5f);
    float cz=cosf(rz*dr*0.5f), sz=sinf(rz*dr*0.5f);
    q[0] = sx*cy*cz - cx*sy*sz;   /* x */
    q[1] = cx*sy*cz + sx*cy*sz;   /* y */
    q[2] = cx*cy*sz - sx*sy*cz;   /* z */
    q[3] = cx*cy*cz + sx*sy*sz;   /* w */
}

typedef struct {
    CCEngine* eng;
    uint32_t drawn;
    CCComponentId tc, mc;
    uint32_t idx_tr, idx_mesh;   /* which comps[] slot each maps to */
} RenderCtx;

static void render_entity_cb(CCEntityId id, void** comps, void* userdata) {
    (void)id;
    RenderCtx* ctx = (RenderCtx*)userdata;
    CCTransform* tr = (CCTransform*)comps[ctx->idx_tr];
    CCMeshComp*  mc = (CCMeshComp*)comps[ctx->idx_mesh];
    if (!mc->mesh_id) return;
    CCTransform3D xf;
    xf.pos[0]=tr->x; xf.pos[1]=tr->y; xf.pos[2]=tr->z;
    euler_to_quat(tr->rx, tr->ry, tr->rz, xf.rot);
    xf.scale[0]=tr->sx?tr->sx:1.0f; xf.scale[1]=tr->sy?tr->sy:1.0f; xf.scale[2]=tr->sz?tr->sz:1.0f;
    cc_draw_mesh(ctx->eng, mc->mesh_id, mc->material_id, &xf);
    ctx->drawn++;
}

uint32_t cc_scene_render(CCEngine* eng, CCScene* scene) {
    if (!eng || !scene) return 0;
    CCComponentId tc = cc_transform_component();
    CCComponentId mc = cc_meshrenderer_component();
    /* cc_query_run returns comps in SORTED component-id order — compute which
       slot each of our components lands in so the callback reads correctly. */
    RenderCtx ctx = { eng, 0, tc, mc, 0, 0 };
    if (tc < mc) { ctx.idx_tr = 0; ctx.idx_mesh = 1; }
    else         { ctx.idx_tr = 1; ctx.idx_mesh = 0; }
    CCComponentId types[2] = { tc, mc };
    CCQuery q = {0};
    q.types = types;
    q.count = 2;
    q.fn = render_entity_cb;
    q.userdata = &ctx;
    cc_query_run(scene, &q);
    return ctx.drawn;
}

CCEntityId cc_spawn_mesh(CCEngine* eng, CCScene* scene, CCMesh mesh, CCMaterial mat,
                          float x, float y, float z) {
    (void)eng;
    CCComponentId tc = cc_transform_component();
    CCComponentId mc = cc_meshrenderer_component();
    CCEntityId e = cc_entity_create(scene);
    CCTransform* tr = cc_component_add(scene, e, tc);
    tr->x=x; tr->y=y; tr->z=z;
    tr->rx=tr->ry=tr->rz=0;
    tr->sx=tr->sy=tr->sz=1.0f;
    CCMeshComp* m = cc_component_add(scene, e, mc);
    m->mesh_id = mesh; m->material_id = mat; m->cast_shadow = true; m->receive_shadow = true;
    return e;
}
