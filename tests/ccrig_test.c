/* ccrig_test — rig data survives the TEXT .ccmodel round-trip.
 *
 * Builds a model with geometry + skeleton + skin weights + an animation (tracks
 * + keyframes) + a blend shape, saves it to TEXT, loads it back, and asserts
 * every rig count and representative values match. Also loads a humanoid
 * (bones + facial blend shapes) through text. No rendering — this is a data
 * integrity test (rigs are the piece the binary format used to own exclusively).
 */
#include "cc/ccmodel.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int feq(float a,float b){ return fabsf(a-b)<1e-4f; }
static int fails=0;
#define CHECK(cond,msg) do{ if(!(cond)){ printf("FAIL: %s\n",msg); fails++; } }while(0)

int main(void){
    /* build a rigged model */
    CCModel* m=ccm_make_box("rigged",1,2,1);
    /* skeleton: root + child */
    float I[4]={0,0,0,1}, S[3]={1,1,1};
    uint16_t root=ccm_add_bone(m,"root",CCM_BONE_NO_PARENT,(float[]){0,0,0},I,S);
    uint16_t mid =ccm_add_bone(m,"mid", root,(float[]){0,1,0},I,S);
    uint16_t tip =ccm_add_bone(m,"tip", mid, (float[]){0,1,0},I,S);
    (void)tip;
    /* skin: weight every vertex to root/mid */
    uint32_t nv=m->geom.vertex_count;
    CCMSkinVertex* sw=calloc(nv,sizeof(CCMSkinVertex));
    for(uint32_t i=0;i<nv;i++){
        float y=m->geom.vertices[i].pos[1];
        float wt=(y+1.0f)/2.0f; if(wt<0)wt=0; if(wt>1)wt=1;
        sw[i].joint[0]=root; sw[i].joint[1]=mid; sw[i].joint[2]=0; sw[i].joint[3]=0;
        sw[i].weight[0]=1.0f-wt; sw[i].weight[1]=wt; sw[i].weight[2]=0; sw[i].weight[3]=0;
    }
    ccm_set_skin(m,sw,nv); free(sw);
    /* animation: one track on 'mid', a few keys */
    CCMAnimChunk* a=ccm_add_anim(m,"wave",1.5f,true); a->fps=24;
    CCMTrack* tr=ccm_anim_add_track(a,mid,CCM_TRACK_ROT_Z,CCM_INTERP_LINEAR);
    ccm_track_add_key(tr,0.0f, 0.0f,0,0);
    ccm_track_add_key(tr,0.75f,0.7f,0,0);
    ccm_track_add_key(tr,1.5f, 0.0f,0,0);
    /* blend shape: push vertex 0 outward */
    CCMBlendShape* bs=ccm_add_blend_shape(m,"bulge");
    ccm_bshp_add_delta(bs,0,(float[]){0.1f,0.2f,0.3f},(float[]){0,1,0});
    ccm_bshp_add_delta(bs,1,(float[]){-0.1f,0,0.1f},(float[]){0,0,1});

    /* record originals */
    uint32_t o_bones=m->skel.bone_count, o_skinv=m->skin.vertex_count;
    uint32_t o_anims=m->anim_count, o_tracks=a->track_count, o_keys=tr->key_count;
    uint32_t o_shapes=m->bshp.shape_count, o_deltas=bs->delta_count;
    float o_key1=tr->keys[1].value, o_w0=m->skin.weights[0].weight[1];

    /* TEXT round-trip */
    if(!ccm_save_text(m,"/tmp/rig.ccmodel")){ printf("save_text failed\n"); return 1; }
    CCModel* r=ccm_load_text("/tmp/rig.ccmodel");
    if(!r){ printf("load_text failed\n"); return 1; }

    CHECK(r->has_skel, "skeleton flag lost");
    CHECK(r->skel.bone_count==o_bones, "bone count mismatch");
    CHECK(r->has_skin && r->skin.vertex_count==o_skinv, "skin vertex count mismatch");
    CHECK(r->anim_count==o_anims, "anim count mismatch");
    CHECK(r->anim_count>0 && r->anims[0].track_count==o_tracks, "track count mismatch");
    CHECK(r->anim_count>0 && r->anims[0].tracks[0].key_count==o_keys, "key count mismatch");
    CHECK(r->has_bshp && r->bshp.shape_count==o_shapes, "blend shape count mismatch");
    CHECK(r->has_bshp && r->bshp.shapes[0].delta_count==o_deltas, "delta count mismatch");
    /* value integrity */
    CHECK(!strcmp(r->skel.bones[1].name,"mid"), "bone name lost");
    CHECK(feq(r->anims[0].tracks[0].keys[1].value,o_key1), "keyframe value drift");
    CHECK(feq(r->skin.weights[0].weight[1],o_w0), "skin weight drift");
    CHECK(r->anims[0].looping, "anim loop flag lost");
    CHECK(feq(r->anims[0].fps,24.0f), "anim fps lost");
    CHECK(r->skel.bones[1].parent==root, "bone parent lost");

    printf("rig round-trip: bones=%u skin=%u anims=%u tracks=%u keys=%u shapes=%u deltas=%u\n",
        r->skel.bone_count, r->skin.vertex_count, r->anim_count,
        r->anim_count?r->anims[0].track_count:0,
        r->anim_count?r->anims[0].tracks[0].key_count:0,
        r->bshp.shape_count, r->bshp.shape_count?r->bshp.shapes[0].delta_count:0);

    /* humanoid through text (bones + facial blend shapes) */
    CCModel* h=ccm_make_humanoid("hero");
    uint32_t hb=h->skel.bone_count, hs=h->bshp.shape_count;
    ccm_save_text(h,"/tmp/hero.ccmodel");
    CCModel* h2=ccm_load_text("/tmp/hero.ccmodel");
    CHECK(h2 && h2->skel.bone_count==hb, "humanoid bone count mismatch");
    CHECK(h2 && h2->bshp.shape_count==hs, "humanoid blendshape count mismatch");
    printf("humanoid round-trip: bones %u->%u  shapes %u->%u\n", hb,h2?h2->skel.bone_count:0, hs,h2?h2->bshp.shape_count:0);

    ccm_model_free(m); ccm_model_free(r); ccm_model_free(h); if(h2)ccm_model_free(h2);
    if(fails){ printf("RIG TEST: %d FAILURE(S)\n",fails); return 2; }
    printf("RIG TEST: all checks passed\n");
    return 0;
}
