/* Animation test — GPU skinning proof + IK. Builds a tall segmented box mesh
 * bound to a 2-bone vertical chain; the upper bone rotates so the mesh BENDS
 * (linear-blend skinning through the engine's bone-UBO skinning shader). Three
 * copies are drawn at increasing bend angles so the deformation is visible in a
 * single frame. Also runs the two-bone IK solver on a CPU pose and prints the
 * resulting end-effector error to validate cc_ik_solve_limb. */
#include "cc/claudecore.h"
#include "cc/anim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build a tall box (Y from 0..H) subdivided into `seg` rings, skinned to two
 * bones: bone0 (root at y=0) controls the lower half, bone1 the upper half,
 * with a smooth weight blend across the middle. */
static CCMesh make_skinned_bar(CCEngine* eng, float H, int seg,
                               uint16_t** out_j, float** out_w, uint32_t* out_nv) {
    int rings = seg+1;
    int cols  = 4;                     /* square cross-section */
    uint32_t nv = rings*cols;
    CCVertex* V = calloc(nv,sizeof(CCVertex));
    uint16_t* J = calloc(nv*4,sizeof(uint16_t));
    float*    W = calloc(nv*4,sizeof(float));
    float half=0.35f;
    float cx[4]={-half,half,half,-half}, cz[4]={-half,-half,half,half};
    for (int r=0;r<rings;r++){
        float t=(float)r/seg;           /* 0..1 up the bar */
        float y=t*H;
        for (int c=0;c<cols;c++){
            int i=r*cols+c;
            V[i].pos[0]=cx[c]; V[i].pos[1]=y; V[i].pos[2]=cz[c];
            /* outward normal in XZ */
            float nl=sqrtf(cx[c]*cx[c]+cz[c]*cz[c]);
            V[i].normal[0]=cx[c]/nl; V[i].normal[1]=0; V[i].normal[2]=cz[c]/nl;
            V[i].uv[0]=(float)c/cols; V[i].uv[1]=t;
            V[i].tangent[0]=1;V[i].tangent[3]=1;
            V[i].color[0]=V[i].color[1]=V[i].color[2]=V[i].color[3]=255;
            /* weights: lower half → bone0, upper half → bone1, blend at middle */
            float wb1 = t<0.4f?0.0f : t>0.6f?1.0f : (t-0.4f)/0.2f;
            J[i*4+0]=0; J[i*4+1]=1; J[i*4+2]=0; J[i*4+3]=0;
            W[i*4+0]=1.0f-wb1; W[i*4+1]=wb1; W[i*4+2]=0; W[i*4+3]=0;
        }
    }
    /* index the box sides (quad strip around, per ring) */
    uint32_t nq = seg*cols;
    uint32_t* I = malloc(nq*6*sizeof(uint32_t)); uint32_t ii=0;
    for (int r=0;r<seg;r++) for (int c=0;c<cols;c++){
        int c2=(c+1)%cols;
        uint32_t a=r*cols+c, b=r*cols+c2, cc2=(r+1)*cols+c, d=(r+1)*cols+c2;
        I[ii++]=a;I[ii++]=cc2;I[ii++]=b; I[ii++]=b;I[ii++]=cc2;I[ii++]=d;
    }
    CCMesh m=cc_mesh_create(eng,V,nv,I,ii,CC_MESH_STATIC);
    cc_mesh_attach_skin(eng,m,J,W,nv);
    free(V);free(I);
    *out_j=J;*out_w=W;*out_nv=nv;
    return m;
}

/* column-major mat4 helpers (match anim.h Mat4) */
static void mat_identity(float* m){ memset(m,0,64); m[0]=m[5]=m[10]=m[15]=1; }
/* rotation about Z by angle, with the pivot translated to y=pivot then back */
static void mat_bend(float* m, float angle, float pivot){
    float c=cosf(angle), s=sinf(angle);
    /* T(0,pivot,0) * Rz * T(0,-pivot,0), column-major */
    mat_identity(m);
    m[0]=c;  m[1]=s;
    m[4]=-s; m[5]=c;
    /* translation column */
    m[12]= s*pivot;
    m[13]= pivot - c*pivot;
    m[14]=0;
}

int main(int argc,char** argv){
    const char* out=(argc>1)?argv[1]:"/tmp/anim_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1024;cfg.height=576;cfg.verbose=false;
    CCEngine* eng=cc_init(&cfg); if(!eng)return 1;

    float H=4.0f;
    uint16_t* J; float* W; uint32_t nv;
    CCMesh bar=make_skinned_bar(eng,H,16,&J,&W,&nv);
    CCMesh ground=cc_mesh_plane(eng,40,40,4);

    CCMaterialDesc gd={.base_color={0.5f,0.52f,0.55f,1},.roughness=0.95f};
    CCMaterial mg=cc_material_create(eng,&gd);
    CCMaterialDesc bd={.base_color={0.85f,0.45f,0.3f,1},.roughness=0.4f,.metallic=0.2f};
    CCMaterial mb=cc_material_create(eng,&bd);

    cc_light_set_ambient(eng,0.16f,0.17f,0.2f,1.0f);
    CCLight sun={.type=CC_LIGHT_DIRECTIONAL,.dir={-0.4f,-0.85f,-0.4f},
                 .color={1,0.96f,0.9f},.intensity=3.0f,.cast_shadows=true};
    cc_light_add(eng,&sun);
    CCPostFX fx=cc_postfx_default(); fx.bloom=true; fx.bloom_threshold=1.3f; fx.bloom_intensity=0.08f;
    fx.vignette=true; fx.vignette_strength=0.28f;
    cc_postfx_set(eng,&fx);

    /* ─── IK validation (CPU) ──────────────────────────────────────────── */
    {
        CCIKLimb limb={0};
        limb.root_bone=0; limb.mid_bone=1; limb.end_bone=2;
        limb.chain_len_a=1.0f; limb.chain_len_b=1.0f;
        /* target within reach (dist 1.5 < 2.0) */
        Vec3 target={1.2f,0.9f,0.0f};
        /* build global bind: root at origin, mid at (0,1,0), end at (0,2,0) */
        Mat4 gin[3]; for(int i=0;i<3;i++){mat_identity(gin[i].m); gin[i].m[13]=(float)i;}
        Mat4 gout[3];
        CCPose* pose=cc_pose_new(3);
        cc_ik_solve_limb(&limb,target,gin,gout,pose,NULL);
        /* end-effector position after solve = gout[2] translation */
        float ex=gout[2].m[12], ey=gout[2].m[13], ez=gout[2].m[14];
        float err=sqrtf((ex-target.x)*(ex-target.x)+(ey-target.y)*(ey-target.y)+(ez-target.z)*(ez-target.z));
        printf("IK two-bone: end=(%.2f,%.2f,%.2f) target=(%.2f,%.2f,%.2f) err=%.3f\n",
            ex,ey,ez,target.x,target.y,target.z,err);
        cc_pose_free(pose);
    }

    /* ─── render three bars at increasing bend ─────────────────────────── */
    float bends[3]={0.0f, 0.6f, 1.2f};
    for(int frame=0;frame<3;frame++){
        cc_frame_begin(eng);
        CCCameraDesc cam={.pos={0,3.5f,11},.target={0,2.0f,0},.up={0,1,0},
                          .fov_deg=50,.near_plane=0.1f,.far_plane=200,.exposure=1};
        cc_camera_set(eng,&cam);
        CCTransform3D gxf={.pos={0,0,0},.rot={0,0,0,1},.scale={1,1,1}};
        cc_draw_mesh(eng,ground,mg,&gxf);

        for(int k=0;k<3;k++){
            /* bone palette: bone0 identity, bone1 bends about the mid pivot (y=H*0.5) */
            float palette[2*16];
            mat_identity(&palette[0]);
            mat_bend(&palette[16], bends[k], H*0.5f);
            cc_set_bones(eng,palette,2);
            CCTransform3D xf={.pos={(k-1)*3.0f,0,0},.rot={0,0,0,1},.scale={1,1,1}};
            cc_draw_skinned(eng,bar,mb,&xf);
        }
        cc_frame_end(eng);
    }
    const char* s=cc_screenshot(eng,out);
    printf("screenshot: %s  skinned_verts=%u\n", s?s:"(null)", nv);
    free(J);free(W);
    cc_shutdown(eng); return 0;
}
