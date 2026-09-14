#include "cc/prefab.h"
#include <string.h>
#include <stdio.h>

CCPrefab cc_prefab_new(const char* name){
    CCPrefab p;
    memset(&p,0,sizeof(p));
    snprintf(p.name,sizeof(p.name),"%s", (name&&name[0])?name:"prefab");
    p.mesh=0; p.material=0;
    p.scale[0]=p.scale[1]=p.scale[2]=1.0f;
    p.rot_euler[0]=p.rot_euler[1]=p.rot_euler[2]=0.0f;
    p.cast_shadow=true; p.receive_shadow=true; p.visible=true;
    p.spawn_count=0;
    return p;
}

CCPrefab cc_prefab_from_actor(CCActor a){
    CCPrefab p=cc_prefab_new(cc_actor_name(a));
    p.mesh=cc_actor_mesh(a);
    p.material=cc_actor_material(a);
    CCTransform3D xf;
    if(cc_actor_get_transform(a,&xf)){
        p.scale[0]=xf.scale[0]; p.scale[1]=xf.scale[1]; p.scale[2]=xf.scale[2];
        /* base rotation left at 0; per-instance yaw is applied at spawn. Capturing
           a full 3-axis rotation from the quaternion isn't needed for templating. */
    }
    p.visible=cc_actor_visible(a);
    return p;
}

void cc_prefab_set_mesh(CCPrefab* p, CCMesh mesh){ if(p) p->mesh=mesh; }
void cc_prefab_set_material(CCPrefab* p, CCMaterial mat){ if(p) p->material=mat; }
void cc_prefab_set_scale(CCPrefab* p, float sx,float sy,float sz){
    if(p){ p->scale[0]=sx; p->scale[1]=sy; p->scale[2]=sz; }
}
void cc_prefab_set_rotation(CCPrefab* p, float rx,float ry,float rz){
    if(p){ p->rot_euler[0]=rx; p->rot_euler[1]=ry; p->rot_euler[2]=rz; }
}
void cc_prefab_set_shadow(CCPrefab* p, bool cast_shadow, bool receive_shadow){
    if(p){ p->cast_shadow=cast_shadow; p->receive_shadow=receive_shadow; }
}
void cc_prefab_set_visible(CCPrefab* p, bool visible){ if(p) p->visible=visible; }

CCActor cc_prefab_spawn_ex(CCScene* world, CCPrefab* p, float x,float y,float z,
                           float extra_yaw_deg, float scale_mul){
    if(!world||!p) return CC_ACTOR_NULL;
    CCActor a=cc_actor_spawn(world, p->mesh, p->material, x,y,z);
    if(!cc_actor_valid(a)) return a;
    if(scale_mul<=0) scale_mul=1.0f;
    cc_actor_set_scale(a, p->scale[0]*scale_mul, p->scale[1]*scale_mul, p->scale[2]*scale_mul);
    cc_actor_set_rotation(a, p->rot_euler[0], p->rot_euler[1]+extra_yaw_deg, p->rot_euler[2]);
    cc_actor_set_shadow(a, p->cast_shadow, p->receive_shadow);
    if(!p->visible) cc_actor_set_visible(a, false);
    /* unique instance name: "<prefab>_<n>" */
    char nm[96]; snprintf(nm,sizeof(nm),"%s_%u", p->name, p->spawn_count++);
    cc_actor_set_name(a, nm);
    return a;
}

CCActor cc_prefab_spawn(CCScene* world, CCPrefab* p, float x,float y,float z){
    return cc_prefab_spawn_ex(world, p, x,y,z, 0.0f, 1.0f);
}
