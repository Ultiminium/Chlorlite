/* move_test — collide-and-slide character movement. Verifies:
 *  1. moving straight into a wall stops (no pass-through),
 *  2. moving diagonally into a wall SLIDES (tangential motion preserved),
 *  3. standing/falling onto a floor plane reports grounded,
 *  4. an unobstructed move is unchanged.
 * Pure data test (no render). */
#include "cc/move.h"
#include <stdio.h>
#include <math.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)
static int feq(float a,float b){ return fabsf(a-b)<1e-3f; }

int main(void){
    CCMoveWorld* w=cc_move_world_create();
    /* a wall: AABB box centered at x=2, thin in x, tall/deep. Its -x face is at x=1.5 */
    cc_move_add_box(w, 2.0f,0.0f,0.0f,  0.5f, 3.0f, 5.0f);
    /* a floor plane: normal up, passes through y=0 (dot(up,p) >= 0 is above) */
    cc_move_add_plane(w, 0,1,0, 0.0f);
    CHECK(cc_move_collider_count(w)==2,"two colliders added");

    float radius=0.5f;

    /* 1. move straight into the wall from x=0 toward +x by 2 units.
       The sphere (r=0.5) should be stopped so its center x <= 1.5-0.5 = 1.0 */
    {
        float pos[3]={0.0f,1.0f,0.0f};
        float disp[3]={2.0f,0.0f,0.0f};
        float np[3];
        CCMoveResult r=cc_move_slide(w,pos,radius,disp,np);
        CHECK(r.hit,"straight-into-wall reports hit");
        CHECK(np[0] <= 1.0f+1e-3f,"straight move stopped at wall face");
        printf("straight: x %.2f -> %.2f (expected <= 1.0), hit=%d\n", pos[0], np[0], r.hit);
    }

    /* 2. move diagonally into the wall: disp = (+2 x, 0, +2 z). The x should be
       blocked (~1.0) but the z (tangential to the wall's -x face) should carry
       through to ~2.0 — that's the SLIDE. */
    {
        float pos[3]={0.0f,1.0f,0.0f};
        float disp[3]={2.0f,0.0f,2.0f};
        float np[3];
        CCMoveResult r=cc_move_slide(w,pos,radius,disp,np);
        CHECK(r.hit,"diagonal-into-wall reports hit");
        CHECK(np[0] <= 1.0f+1e-3f,"diagonal move blocked in x");
        CHECK(np[2] > 1.5f,"diagonal move slid along wall in z (tangential preserved)");
        printf("slide: target(2,_,2) -> (%.2f,_,%.2f)  [x blocked ~1.0, z slid ~2.0]\n", np[0], np[2]);
    }

    /* 3. fall onto the floor: start above, move down through y=0. Sphere center
       should rest at y = 0 + radius = 0.5, and grounded should be true. */
    {
        float pos[3]={-5.0f,1.0f,0.0f};   /* away from the wall */
        float disp[3]={0.0f,-2.0f,0.0f};
        float np[3];
        CCMoveResult r=cc_move_slide(w,pos,radius,disp,np);
        CHECK(r.grounded,"falling onto floor reports grounded");
        CHECK(np[1] >= radius-1e-3f,"sphere rests on top of floor (y >= radius)");
        printf("floor: y 1.0 + (-2) -> %.2f (expected ~%.2f), grounded=%d\n", np[1], radius, r.grounded);
    }

    /* 4. unobstructed move: far from everything, should be unchanged. */
    {
        float pos[3]={-20.0f,5.0f,-20.0f};
        float disp[3]={1.0f,0.0f,1.0f};
        float np[3];
        CCMoveResult r=cc_move_slide(w,pos,radius,disp,np);
        CHECK(!r.hit,"free move reports no hit");
        CHECK(feq(np[0],-19.0f)&&feq(np[2],-19.0f),"free move applied fully");
        printf("free: (-20,_,-20)+(1,_,1) -> (%.2f,_,%.2f), hit=%d\n", np[0], np[2], r.hit);
    }

    cc_move_world_destroy(w);
    if(fails){ printf("MOVE TEST: %d FAILURE(S)\n",fails); return 2; }
    printf("MOVE TEST: all checks passed (stop, slide, grounded, free)\n");
    return 0;
}
