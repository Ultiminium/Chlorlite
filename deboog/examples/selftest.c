/* selftest — proves Deboog works standalone (plain gcc + libm, zero engine). */
#include "deboog/deboog.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int fails=0;
#define EXPECT(cond,msg) do{ if(!(cond)){ printf("  SELFTEST FAIL: %s\n",msg); fails++; } }while(0)

int main(void){
    printf("Deboog %s — standalone self-test\n", deboog_version());

    /* 1. NUMERIC: catch a NaN */
    float buf[6]={1,2,3, 4,5,6};
    DbNumeric n=deboog_scan_floats(buf,6);
    EXPECT(n.ok,"clean floats pass");
    buf[4]=NAN; buf[5]=INFINITY;
    n=deboog_scan_floats(buf,6);
    EXPECT(!n.ok && n.nan_count==1 && n.inf_count==1,"NaN+Inf detected");
    deboog_report_numeric(stdout,"buf",n);

    /* 2. MATRIX: identity is orthonormal & ok; a collapsed one fails */
    float I[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    DbMatrix m=deboog_check_matrix(I);
    EXPECT(m.ok && m.orthonormal && fabs(m.determinant-1)<1e-6,"identity is orthonormal, det 1");
    float collapsed[16]={0}; collapsed[15]=1;   /* zero upper 3x3 → det 0 */
    DbMatrix m2=deboog_check_matrix(collapsed);
    EXPECT(!m2.ok && !m2.invertible,"collapsed matrix flagged not-invertible");
    float mirror[16]={-1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; /* det -1 */
    DbMatrix m3=deboog_check_matrix(mirror);
    EXPECT(m3.left_handed,"mirrored matrix flagged left-handed");
    deboog_report_matrix(stdout,"identity",m);
    deboog_report_matrix(stdout,"collapsed",m2);

    /* 3. QUAT: unit passes, non-unit fails */
    float uq[4]={0,0,0,1}; DbQuat q=deboog_check_quat(uq);
    EXPECT(q.ok && q.normalized,"unit quat ok");
    float bq[4]={1,1,1,1}; DbQuat q2=deboog_check_quat(bq);
    EXPECT(!q2.normalized && fabs(q2.length-2.0)<1e-6,"non-normalized quat flagged");

    /* 4. MESH: a closed tetrahedron is manifold+closed; an open triangle has boundary */
    float tet[12]={0,0,0, 1,0,0, 0,1,0, 0,0,1};
    uint32_t tetidx[12]={0,2,1, 0,1,3, 0,3,2, 1,2,3};   /* 4 faces, closed */
    DbMesh mesh=deboog_check_mesh(tet,4,3,tetidx,12);
    EXPECT(mesh.manifold && mesh.closed,"tetrahedron is manifold + closed");
    uint32_t oneidx[3]={0,1,2};
    DbMesh open=deboog_check_mesh(tet,4,3,oneidx,3);
    EXPECT(!open.closed && open.boundary_edges==3,"single triangle has 3 boundary edges");
    deboog_report_mesh(stdout,"tetrahedron",mesh);

    /* 5. CROSS-SECTION: a round ring vs a flat ribbon along +Y axis */
    float ring[8*3]; for(int i=0;i<8;i++){ float th=i/8.0f*6.2831853f;
        ring[i*3+0]=cosf(th); ring[i*3+1]=0; ring[i*3+2]=sinf(th); }
    float ap[3]={0,0,0}, ad[3]={0,1,0};
    DbCrossSection cs=deboog_cross_section(ring,8,3,ap,ad,0.45);
    EXPECT(cs.ok && cs.roundness>0.9,"round ring reads roundness ~1");
    float ribbon[8*3]; for(int i=0;i<8;i++){ float th=i/8.0f*6.2831853f;
        ribbon[i*3+0]=cosf(th); ribbon[i*3+1]=0; ribbon[i*3+2]=sinf(th)*0.05f; } /* squashed */
    DbCrossSection cr=deboog_cross_section(ribbon,8,3,ap,ad,0.45);
    EXPECT(cr.is_ribbon && cr.roundness<0.2,"flat ribbon detected (roundness < 0.2)");
    deboog_report_cross_section(stdout,"round",cs);
    deboog_report_cross_section(stdout,"ribbon",cr);

    /* 6. SKIN: valid vs bad weights */
    uint16_t J[8]={0,0,0,0, 1,0,0,0}; float Wt[8]={1,0,0,0, 1,0,0,0};
    DbSkin sk=deboog_check_skin(J,Wt,2,2,0.02);
    EXPECT(sk.ok,"valid rigid weights pass");
    float Wbad[8]={0.5f,0,0,0, 1,0,0,0}; /* first sums to 0.5 */
    DbSkin sk2=deboog_check_skin(J,Wbad,2,2,0.02);
    EXPECT(!sk2.ok && sk2.bad_sum==1,"bad weight sum flagged");

    /* 7. CANON PROJECT: identity VP puts origin at frame center (offset 0,0) */
    float vp[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float origin[3]={0,0,0};
    DbCanonPoint cp=deboog_canon_project(vp,origin,580,720,190);
    EXPECT(cp.visible && cp.x_off==0 && cp.y_off==0 && cp.rot_deg==190,"origin projects to C=0 with rot");
    char cbuf[32]; deboog_canon_format(&cp,cbuf,sizeof cbuf);
    printf("  canon origin id = %s\n", cbuf);

    printf(fails? "\nSELFTEST: %d FAILURES\n" : "\nSELFTEST: all checks passed\n", fails);
    return fails?1:0;
}
