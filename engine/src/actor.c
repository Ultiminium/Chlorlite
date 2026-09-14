#include "cc/actor.h"
#include "cc/ecs.h"
#include <math.h>
#include <string.h>
#include <stddef.h>

/* Reuse the same built-in component ids the renderer uses, so actors ARE the
   entities cc_scene_render() already draws. These helpers are defined in
   scene_render.c and registered idempotently. */
extern CCComponentId cc_transform_component(void);
extern CCComponentId cc_meshrenderer_component(void);

/* A tiny per-actor flags component (visibility + stashed mesh id while hidden).
   Registered lazily. Keeping it separate means plain ECS entities that never use
   the actor API don't pay for it. */
typedef struct CCActorFlags {
    uint8_t  visible;
    uint32_t hidden_mesh;   /* stashed mesh_id while hidden (restored on show) */
} CCActorFlags;

static CCComponentId actor_flags_component(void){
    static CCComponentId c = 0;
    if (!c) c = cc_component_register("CCActorFlags", sizeof(CCActorFlags));
    return c;
}

/* ── helpers ── */
static CCTransform* actor_tr(CCActor a){
    if (!a.scene || !a.id) return NULL;
    return (CCTransform*)cc_component_get(a.scene, a.id, cc_transform_component());
}
static CCMeshComp* actor_mc(CCActor a){
    if (!a.scene || !a.id) return NULL;
    return (CCMeshComp*)cc_component_get(a.scene, a.id, cc_meshrenderer_component());
}

/* quaternion (xyzw) from Euler degrees (ZYX order to match typical usage) */
static void euler_to_quat(float rx,float ry,float rz,float* q){
    float hx=rx*0.5f*3.14159265f/180.0f;
    float hy=ry*0.5f*3.14159265f/180.0f;
    float hz=rz*0.5f*3.14159265f/180.0f;
    float cx=cosf(hx),sx=sinf(hx), cy=cosf(hy),sy=sinf(hy), cz=cosf(hz),sz=sinf(hz);
    /* ZYX */
    q[0]=sx*cy*cz - cx*sy*sz;
    q[1]=cx*sy*cz + sx*cy*sz;
    q[2]=cx*cy*sz - sx*sy*cz;
    q[3]=cx*cy*cz + sx*sy*sz;
}

/* ── lifecycle ── */
CCActor cc_actor_spawn(CCScene* scene, CCMesh mesh, CCMaterial mat, float x,float y,float z){
    CCActor a = { scene, 0 };
    if (!scene) return a;
    a.id = cc_entity_create(scene);
    CCTransform* tr = (CCTransform*)cc_component_add(scene, a.id, cc_transform_component());
    if (tr){ tr->x=x;tr->y=y;tr->z=z; tr->rx=tr->ry=tr->rz=0; tr->sx=tr->sy=tr->sz=1.0f; }
    CCMeshComp* mc = (CCMeshComp*)cc_component_add(scene, a.id, cc_meshrenderer_component());
    if (mc){ mc->mesh_id=mesh; mc->material_id=mat; mc->cast_shadow=true; mc->receive_shadow=true; }
    CCActorFlags* fl = (CCActorFlags*)cc_component_add(scene, a.id, actor_flags_component());
    if (fl){ fl->visible=1; fl->hidden_mesh=0; }
    return a;
}

CCActor cc_actor_spawn_empty(CCScene* scene, float x,float y,float z){
    CCActor a = { scene, 0 };
    if (!scene) return a;
    a.id = cc_entity_create(scene);
    CCTransform* tr = (CCTransform*)cc_component_add(scene, a.id, cc_transform_component());
    if (tr){ tr->x=x;tr->y=y;tr->z=z; tr->rx=tr->ry=tr->rz=0; tr->sx=tr->sy=tr->sz=1.0f; }
    CCActorFlags* fl = (CCActorFlags*)cc_component_add(scene, a.id, actor_flags_component());
    if (fl){ fl->visible=1; fl->hidden_mesh=0; }
    return a;
}

void cc_actor_destroy(CCActor a){
    if (a.scene && a.id) cc_entity_destroy(a.scene, a.id);
}
bool cc_actor_valid(CCActor a){
    return a.scene && a.id && cc_entity_alive(a.scene, a.id);
}

/* ── identity ── */
void cc_actor_set_name(CCActor a, const char* name){
    if (a.scene && a.id) cc_entity_set_name(a.scene, a.id, name);
}
const char* cc_actor_name(CCActor a){
    return (a.scene && a.id) ? cc_entity_name(a.scene, a.id) : NULL;
}
CCActor cc_actor_find(CCScene* scene, const char* name){
    CCActor a = { scene, 0 };
    if (scene && name) a.id = cc_entity_find(scene, name);
    return a;
}

/* ── transform ── */
void cc_actor_set_position(CCActor a, float x,float y,float z){
    CCTransform* t=actor_tr(a); if(t){ t->x=x;t->y=y;t->z=z; }
}
void cc_actor_get_position(CCActor a, float* x,float* y,float* z){
    CCTransform* t=actor_tr(a);
    if(t){ if(x)*x=t->x; if(y)*y=t->y; if(z)*z=t->z; }
    else { if(x)*x=0; if(y)*y=0; if(z)*z=0; }
}
void cc_actor_translate(CCActor a, float dx,float dy,float dz){
    CCTransform* t=actor_tr(a); if(t){ t->x+=dx;t->y+=dy;t->z+=dz; }
}
void cc_actor_set_rotation(CCActor a, float rx,float ry,float rz){
    CCTransform* t=actor_tr(a); if(t){ t->rx=rx;t->ry=ry;t->rz=rz; }
}
void cc_actor_set_rotation_y(CCActor a, float deg){
    CCTransform* t=actor_tr(a); if(t){ t->ry=deg; }
}
void cc_actor_rotate_y(CCActor a, float deg){
    CCTransform* t=actor_tr(a); if(t){ t->ry+=deg; }
}
void cc_actor_set_scale(CCActor a, float sx,float sy,float sz){
    CCTransform* t=actor_tr(a); if(t){ t->sx=sx;t->sy=sy;t->sz=sz; }
}
void cc_actor_set_uniform_scale(CCActor a, float s){
    CCTransform* t=actor_tr(a); if(t){ t->sx=t->sy=t->sz=s; }
}
bool cc_actor_get_transform(CCActor a, CCTransform3D* out){
    CCTransform* t=actor_tr(a); if(!t||!out) return false;
    out->pos[0]=t->x; out->pos[1]=t->y; out->pos[2]=t->z;
    out->scale[0]=t->sx; out->scale[1]=t->sy; out->scale[2]=t->sz;
    euler_to_quat(t->rx,t->ry,t->rz,out->rot);
    return true;
}

/* ── appearance ── */
void cc_actor_set_mesh(CCActor a, CCMesh mesh){
    CCMeshComp* m=actor_mc(a);
    if(m){
        /* if currently hidden, update the stash instead of the live id */
        CCActorFlags* fl=(a.scene&&a.id)?(CCActorFlags*)cc_component_get(a.scene,a.id,actor_flags_component()):NULL;
        if(fl && !fl->visible) fl->hidden_mesh=mesh; else m->mesh_id=mesh;
    }
}
void cc_actor_set_material(CCActor a, CCMaterial mat){
    CCMeshComp* m=actor_mc(a); if(m){ m->material_id=mat; }
}
CCMesh cc_actor_mesh(CCActor a){
    CCMeshComp* m=actor_mc(a);
    if(!m) return 0;
    CCActorFlags* fl=(a.scene&&a.id)?(CCActorFlags*)cc_component_get(a.scene,a.id,actor_flags_component()):NULL;
    if(fl && !fl->visible) return fl->hidden_mesh;
    return m->mesh_id;
}
CCMaterial cc_actor_material(CCActor a){
    CCMeshComp* m=actor_mc(a); return m? m->material_id : 0;
}
void cc_actor_set_visible(CCActor a, bool visible){
    if(!a.scene||!a.id) return;
    CCMeshComp* m=actor_mc(a);
    CCActorFlags* fl=(CCActorFlags*)cc_component_get(a.scene,a.id,actor_flags_component());
    if(!m||!fl) return;
    if(visible && !fl->visible){
        m->mesh_id = fl->hidden_mesh;   /* restore */
        fl->hidden_mesh = 0; fl->visible = 1;
    } else if(!visible && fl->visible){
        fl->hidden_mesh = m->mesh_id;   /* stash + zero so renderer skips it */
        m->mesh_id = 0; fl->visible = 0;
    }
}
bool cc_actor_visible(CCActor a){
    if(!a.scene||!a.id) return false;
    CCActorFlags* fl=(CCActorFlags*)cc_component_get(a.scene,a.id,actor_flags_component());
    return fl ? (fl->visible!=0) : true;
}
void cc_actor_set_shadow(CCActor a, bool cast_shadow, bool receive_shadow){
    CCMeshComp* m=actor_mc(a);
    if(m){ m->cast_shadow=cast_shadow; m->receive_shadow=receive_shadow; }
}
