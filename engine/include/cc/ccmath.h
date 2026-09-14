#pragma once
/*
 * ccmath.h — Chlorlite math library
 * Column-major mat4 (OpenGL convention), right-hand coordinate system.
 * All functions inline — no separate .c needed.
 */
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_PI      3.14159265358979323846f
#define CC_TAU     6.28318530717958647692f
#define CC_DEG2RAD (CC_PI / 180.0f)
#define CC_RAD2DEG (180.0f / CC_PI)
#define CC_EPSILON 1e-6f

/* ─── Vec2 ───────────────────────────────────────────────────────────── */
typedef struct { float x, y; }       CCVec2;
typedef struct { float x, y, z; }    CCVec3;
typedef struct { float x, y, z, w; } CCVec4;
typedef struct { float x, y, z, w; } CCQuat;  /* xyzw */
typedef struct { float m[16]; }       CCMat4;  /* column-major */

/* ─── Vec3 ops ───────────────────────────────────────────────────────── */
static inline CCVec3 vec3(float x,float y,float z){return(CCVec3){x,y,z};}
static inline CCVec3 vec3_add(CCVec3 a,CCVec3 b){return(CCVec3){a.x+b.x,a.y+b.y,a.z+b.z};}
static inline CCVec3 vec3_sub(CCVec3 a,CCVec3 b){return(CCVec3){a.x-b.x,a.y-b.y,a.z-b.z};}
static inline CCVec3 vec3_scale(CCVec3 v,float s){return(CCVec3){v.x*s,v.y*s,v.z*s};}
static inline CCVec3 vec3_neg(CCVec3 v){return(CCVec3){-v.x,-v.y,-v.z};}
static inline float  vec3_dot(CCVec3 a,CCVec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static inline float  vec3_len2(CCVec3 v){return vec3_dot(v,v);}
static inline float  vec3_len(CCVec3 v){return sqrtf(vec3_len2(v));}
static inline float  vec3_dist(CCVec3 a,CCVec3 b){return vec3_len(vec3_sub(b,a));}
static inline CCVec3 vec3_norm(CCVec3 v){
    float l=vec3_len(v); return l>CC_EPSILON?vec3_scale(v,1.0f/l):v;
}
static inline CCVec3 vec3_cross(CCVec3 a,CCVec3 b){
    return(CCVec3){a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
static inline CCVec3 vec3_lerp(CCVec3 a,CCVec3 b,float t){
    return(CCVec3){a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),a.z+t*(b.z-a.z)};
}
static inline CCVec3 vec3_reflect(CCVec3 v,CCVec3 n){
    return vec3_sub(v,vec3_scale(n,2.0f*vec3_dot(v,n)));
}
static inline CCVec3 vec3_min(CCVec3 a,CCVec3 b){
    return(CCVec3){fminf(a.x,b.x),fminf(a.y,b.y),fminf(a.z,b.z)};
}
static inline CCVec3 vec3_max(CCVec3 a,CCVec3 b){
    return(CCVec3){fmaxf(a.x,b.x),fmaxf(a.y,b.y),fmaxf(a.z,b.z)};
}
static inline CCVec3 vec3_clamp(CCVec3 v,CCVec3 mn,CCVec3 mx){
    return vec3_min(vec3_max(v,mn),mx);
}

/* ─── Vec4 ops ───────────────────────────────────────────────────────── */
static inline CCVec4 vec4(float x,float y,float z,float w){return(CCVec4){x,y,z,w};}
static inline CCVec4 vec4_from3(CCVec3 v,float w){return(CCVec4){v.x,v.y,v.z,w};}
static inline CCVec3 vec4_xyz(CCVec4 v){return(CCVec3){v.x,v.y,v.z};}
static inline float  vec4_dot(CCVec4 a,CCVec4 b){return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;}
static inline CCVec4 vec4_lerp(CCVec4 a,CCVec4 b,float t){
    return(CCVec4){a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),a.z+t*(b.z-a.z),a.w+t*(b.w-a.w)};
}

/* ─── Quaternion ─────────────────────────────────────────────────────── */
static inline CCQuat quat(float x,float y,float z,float w){return(CCQuat){x,y,z,w};}
static inline CCQuat quat_identity(void){return(CCQuat){0,0,0,1};}
static inline float  quat_len(CCQuat q){return sqrtf(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);}
static inline CCQuat quat_norm(CCQuat q){
    float l=quat_len(q); return l>CC_EPSILON?(CCQuat){q.x/l,q.y/l,q.z/l,q.w/l}:q;
}
static inline CCQuat quat_mul(CCQuat a,CCQuat b){
    return(CCQuat){
        a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z
    };
}
static inline CCQuat quat_conj(CCQuat q){return(CCQuat){-q.x,-q.y,-q.z,q.w};}
static inline CCVec3 quat_rotate(CCQuat q,CCVec3 v){
    CCVec3 u={q.x,q.y,q.z};
    float s=q.w;
    return vec3_add(vec3_add(
        vec3_scale(u,2.0f*vec3_dot(u,v)),
        vec3_scale(v,s*s-vec3_dot(u,u))),
        vec3_scale(vec3_cross(u,v),2.0f*s));
}
static inline CCQuat quat_from_axis_angle(CCVec3 axis,float angle_rad){
    float h=angle_rad*0.5f,s=sinf(h);
    return(CCQuat){axis.x*s,axis.y*s,axis.z*s,cosf(h)};
}
static inline CCQuat quat_from_euler(float pitch,float yaw,float roll){ /* radians */
    float cy=cosf(yaw*0.5f),sy=sinf(yaw*0.5f);
    float cp=cosf(pitch*0.5f),sp=sinf(pitch*0.5f);
    float cr=cosf(roll*0.5f),sr=sinf(roll*0.5f);
    return(CCQuat){
        sr*cp*cy-cr*sp*sy,
        cr*sp*cy+sr*cp*sy,
        cr*cp*sy-sr*sp*cy,
        cr*cp*cy+sr*sp*sy
    };
}
static inline CCQuat quat_slerp(CCQuat a,CCQuat b,float t){
    float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0){b=(CCQuat){-b.x,-b.y,-b.z,-b.w};dot=-dot;}
    if(dot>0.9995f){
        CCQuat r={(a.x+t*(b.x-a.x)),(a.y+t*(b.y-a.y)),(a.z+t*(b.z-a.z)),(a.w+t*(b.w-a.w))};
        return quat_norm(r);
    }
    float theta0=acosf(dot),theta=theta0*t;
    float s0=cosf(theta)-dot*sinf(theta)/sinf(theta0);
    float s1=sinf(theta)/sinf(theta0);
    return(CCQuat){s0*a.x+s1*b.x,s0*a.y+s1*b.y,s0*a.z+s1*b.z,s0*a.w+s1*b.w};
}
static inline CCQuat quat_look_at(CCVec3 from,CCVec3 to,CCVec3 up){
    CCVec3 fwd=vec3_norm(vec3_sub(to,from));
    CCVec3 right=vec3_norm(vec3_cross(fwd,up));
    CCVec3 u=vec3_cross(right,fwd);
    /* Camera looks down local -Z: basis columns are (right, up, -fwd) */
    CCVec3 z=vec3_neg(fwd);
    float m00=right.x,m01=u.x,m02=z.x;
    float m10=right.y,m11=u.y,m12=z.y;
    float m20=right.z,m21=u.z,m22=z.z;
    float trace=m00+m11+m22;
    CCQuat q;
    if(trace>0){float s=0.5f/sqrtf(trace+1);q.w=0.25f/s;q.x=(m21-m12)*s;q.y=(m02-m20)*s;q.z=(m10-m01)*s;}
    else if(m00>m11&&m00>m22){float s=2.0f*sqrtf(1.0f+m00-m11-m22);q.w=(m21-m12)/s;q.x=0.25f*s;q.y=(m01+m10)/s;q.z=(m02+m20)/s;}
    else if(m11>m22){float s=2.0f*sqrtf(1.0f+m11-m00-m22);q.w=(m02-m20)/s;q.x=(m01+m10)/s;q.y=0.25f*s;q.z=(m12+m21)/s;}
    else{float s=2.0f*sqrtf(1.0f+m22-m00-m11);q.w=(m10-m01)/s;q.x=(m02+m20)/s;q.y=(m12+m21)/s;q.z=0.25f*s;}
    return quat_norm(q);
}

/* ─── Mat4 ───────────────────────────────────────────────────────────── */
/* Column-major: m[col*4+row]. Access: M(r,c) */
#define M(r,c) m->m[(c)*4+(r)]

static inline CCMat4 mat4_identity(void){
    CCMat4 m; memset(&m,0,sizeof(m));
    m.m[0]=m.m[5]=m.m[10]=m.m[15]=1; return m;
}
static inline CCMat4 mat4_zero(void){CCMat4 m;memset(&m,0,sizeof(m));return m;}

static inline CCMat4 mat4_mul(CCMat4 a,CCMat4 b){
    CCMat4 r=mat4_zero();
    for(int col=0;col<4;col++) for(int row=0;row<4;row++)
        for(int k=0;k<4;k++) r.m[col*4+row]+=a.m[k*4+row]*b.m[col*4+k];
    return r;
}
static inline CCVec4 mat4_mul_vec4(CCMat4 m,CCVec4 v){
    return(CCVec4){
        m.m[0]*v.x+m.m[4]*v.y+m.m[8]*v.z +m.m[12]*v.w,
        m.m[1]*v.x+m.m[5]*v.y+m.m[9]*v.z +m.m[13]*v.w,
        m.m[2]*v.x+m.m[6]*v.y+m.m[10]*v.z+m.m[14]*v.w,
        m.m[3]*v.x+m.m[7]*v.y+m.m[11]*v.z+m.m[15]*v.w
    };
}
static inline CCVec3 mat4_mul_point(CCMat4 m,CCVec3 p){
    CCVec4 r=mat4_mul_vec4(m,(CCVec4){p.x,p.y,p.z,1});
    return(CCVec3){r.x/r.w,r.y/r.w,r.z/r.w};
}
static inline CCVec3 mat4_mul_dir(CCMat4 m,CCVec3 d){
    return(CCVec3){
        m.m[0]*d.x+m.m[4]*d.y+m.m[8]*d.z,
        m.m[1]*d.x+m.m[5]*d.y+m.m[9]*d.z,
        m.m[2]*d.x+m.m[6]*d.y+m.m[10]*d.z
    };
}
static inline CCMat4 mat4_transpose(CCMat4 src){
    CCMat4 r;
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) r.m[i*4+j]=src.m[j*4+i];
    return r;
}
static inline CCMat4 mat4_inverse(CCMat4 m){
    /* Gauss-Jordan with partial pivoting */
    float a[4][8];
    for(int i=0;i<4;i++){for(int j=0;j<4;j++)a[i][j]=m.m[j*4+i];for(int j=0;j<4;j++)a[i][4+j]=(i==j?1.0f:0.0f);}
    for(int col=0;col<4;col++){
        int pivot=col;
        for(int row=col+1;row<4;row++) if(fabsf(a[row][col])>fabsf(a[pivot][col])) pivot=row;
        if(pivot!=col){float tmp[8];memcpy(tmp,a[col],32);memcpy(a[col],a[pivot],32);memcpy(a[pivot],tmp,32);}
        float s=a[col][col]; if(fabsf(s)<CC_EPSILON){/*singular*/return mat4_identity();}
        for(int j=0;j<8;j++) a[col][j]/=s;
        for(int row=0;row<4;row++) if(row!=col){float f=a[row][col];for(int j=0;j<8;j++)a[row][j]-=f*a[col][j];}
    }
    CCMat4 r;
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) r.m[j*4+i]=a[i][4+j];
    return r;
}
static inline CCMat4 mat4_upper3x3_inverse_transpose(CCMat4 m){
    /* For normal matrix: inverse transpose of upper-left 3x3 */
    CCMat4 inv=mat4_inverse(m);
    return mat4_transpose(inv);
}

/* ─── Transform matrices ─────────────────────────────────────────────── */
static inline CCMat4 mat4_translate(CCVec3 t){
    CCMat4 m=mat4_identity();
    m.m[12]=t.x; m.m[13]=t.y; m.m[14]=t.z; return m;
}
static inline CCMat4 mat4_scale(CCVec3 s){
    CCMat4 m=mat4_identity();
    m.m[0]=s.x; m.m[5]=s.y; m.m[10]=s.z; return m;
}
static inline CCMat4 mat4_scale_f(float s){return mat4_scale((CCVec3){s,s,s});}
static inline CCMat4 mat4_rotate_x(float a){
    CCMat4 m=mat4_identity(); float c=cosf(a),s=sinf(a);
    m.m[5]=c;m.m[6]=s;m.m[9]=-s;m.m[10]=c; return m;
}
static inline CCMat4 mat4_rotate_y(float a){
    CCMat4 m=mat4_identity(); float c=cosf(a),s=sinf(a);
    m.m[0]=c;m.m[2]=-s;m.m[8]=s;m.m[10]=c; return m;
}
static inline CCMat4 mat4_rotate_z(float a){
    CCMat4 m=mat4_identity(); float c=cosf(a),s=sinf(a);
    m.m[0]=c;m.m[1]=s;m.m[4]=-s;m.m[5]=c; return m;
}
static inline CCMat4 mat4_from_quat(CCQuat q){
    float x2=2*q.x*q.x,y2=2*q.y*q.y,z2=2*q.z*q.z;
    float xy=2*q.x*q.y,xz=2*q.x*q.z,yz=2*q.y*q.z;
    float wx=2*q.w*q.x,wy=2*q.w*q.y,wz=2*q.w*q.z;
    CCMat4 m=mat4_identity();
    m.m[0]=1-y2-z2; m.m[1]=xy+wz;    m.m[2]=xz-wy;
    m.m[4]=xy-wz;   m.m[5]=1-x2-z2;  m.m[6]=yz+wx;
    m.m[8]=xz+wy;   m.m[9]=yz-wx;    m.m[10]=1-x2-y2;
    return m;
}
static inline CCMat4 mat4_trs(CCVec3 t,CCQuat r,CCVec3 s){
    CCMat4 T=mat4_translate(t);
    CCMat4 R=mat4_from_quat(r);
    CCMat4 S=mat4_scale(s);
    return mat4_mul(T,mat4_mul(R,S));
}

/* ─── Camera matrices ────────────────────────────────────────────────── */
static inline CCMat4 mat4_look_at(CCVec3 eye,CCVec3 center,CCVec3 up){
    CCVec3 f=vec3_norm(vec3_sub(center,eye));
    CCVec3 r=vec3_norm(vec3_cross(f,up));
    CCVec3 u=vec3_cross(r,f);
    CCMat4 m=mat4_identity();
    m.m[0]=r.x; m.m[4]=r.y; m.m[8] =r.z; m.m[12]=-vec3_dot(r,eye);
    m.m[1]=u.x; m.m[5]=u.y; m.m[9] =u.z; m.m[13]=-vec3_dot(u,eye);
    m.m[2]=-f.x;m.m[6]=-f.y;m.m[10]=-f.z;m.m[14]= vec3_dot(f,eye);
    m.m[3]=0;   m.m[7]=0;   m.m[11]=0;   m.m[15]=1;
    return m;
}
static inline CCMat4 mat4_perspective(float fov_rad,float aspect,float near,float far){
    float t=tanf(fov_rad*0.5f);
    CCMat4 m=mat4_zero();
    m.m[0]=1.0f/(aspect*t);
    m.m[5]=1.0f/t;
    m.m[10]=-(far+near)/(far-near);
    m.m[11]=-1.0f;
    m.m[14]=-2.0f*far*near/(far-near);
    return m;
}
static inline CCMat4 mat4_ortho(float l,float r2,float b,float t,float near,float far){
    CCMat4 m=mat4_zero();
    m.m[0]=2.0f/(r2-l);
    m.m[5]=2.0f/(t-b);
    m.m[10]=-2.0f/(far-near);
    m.m[12]=-(r2+l)/(r2-l);
    m.m[13]=-(t+b)/(t-b);
    m.m[14]=-(far+near)/(far-near);
    m.m[15]=1;
    return m;
}
static inline CCMat4 mat4_ortho_2d(float w,float h){
    return mat4_ortho(0,w,h,0,-1,1); /* top-left origin */
}

/* ─── Frustum ────────────────────────────────────────────────────────── */
typedef struct {
    CCVec4 planes[6]; /* normal.xyz + distance w — left,right,bot,top,near,far */
} CCFrustum;

static inline CCFrustum frustum_from_vp(CCMat4 vp){
    CCFrustum f;
    float* m=vp.m;
    /* Gribb-Hartmann extraction */
    f.planes[0]=(CCVec4){m[3]+m[0],m[7]+m[4],m[11]+m[8],m[15]+m[12]};  /* left */
    f.planes[1]=(CCVec4){m[3]-m[0],m[7]-m[4],m[11]-m[8],m[15]-m[12]};  /* right */
    f.planes[2]=(CCVec4){m[3]+m[1],m[7]+m[5],m[11]+m[9],m[15]+m[13]};  /* bottom */
    f.planes[3]=(CCVec4){m[3]-m[1],m[7]-m[5],m[11]-m[9],m[15]-m[13]};  /* top */
    f.planes[4]=(CCVec4){m[3]+m[2],m[7]+m[6],m[11]+m[10],m[15]+m[14]}; /* near */
    f.planes[5]=(CCVec4){m[3]-m[2],m[7]-m[6],m[11]-m[10],m[15]-m[14]};/* far */
    for(int i=0;i<6;i++){
        float l=sqrtf(f.planes[i].x*f.planes[i].x+f.planes[i].y*f.planes[i].y+f.planes[i].z*f.planes[i].z);
        if(l>CC_EPSILON){f.planes[i].x/=l;f.planes[i].y/=l;f.planes[i].z/=l;f.planes[i].w/=l;}
    }
    return f;
}
static inline int frustum_test_sphere(CCFrustum* f,CCVec3 center,float radius){
    for(int i=0;i<6;i++)
        if(f->planes[i].x*center.x+f->planes[i].y*center.y+f->planes[i].z*center.z+f->planes[i].w<-radius)
            return 0;
    return 1;
}
static inline int frustum_test_aabb(CCFrustum* f,CCVec3 mn,CCVec3 mx){
    for(int i=0;i<6;i++){
        CCVec4* p=&f->planes[i];
        CCVec3 positive={(p->x>0?mx.x:mn.x),(p->y>0?mx.y:mn.y),(p->z>0?mx.z:mn.z)};
        if(p->x*positive.x+p->y*positive.y+p->z*positive.z+p->w<0) return 0;
    }
    return 1;
}

/* ─── Ray ────────────────────────────────────────────────────────────── */
typedef struct { CCVec3 origin; CCVec3 dir; } CCRay;

static inline CCRay ray_from_screen(float sx,float sy,float sw,float sh,
                                     CCMat4 inv_vp,float near,float far){
    float nx=(sx/sw)*2-1, ny=1-(sy/sh)*2;
    CCVec4 near_clip={nx,ny,-1,1}, far_clip={nx,ny,1,1};
    CCVec4 near_w=mat4_mul_vec4(inv_vp,near_clip);
    CCVec4 far_w =mat4_mul_vec4(inv_vp,far_clip);
    CCVec3 np={near_w.x/near_w.w,near_w.y/near_w.w,near_w.z/near_w.w};
    CCVec3 fp={far_w.x/far_w.w,  far_w.y/far_w.w,  far_w.z/far_w.w};
    return(CCRay){np,vec3_norm(vec3_sub(fp,np))};
    (void)near;(void)far;
}
static inline int ray_sphere(CCRay r,CCVec3 center,float radius,float* t){
    CCVec3 oc=vec3_sub(r.origin,center);
    float a=vec3_dot(r.dir,r.dir),b=2*vec3_dot(oc,r.dir),c=vec3_dot(oc,oc)-radius*radius;
    float disc=b*b-4*a*c; if(disc<0)return 0;
    *t=(-b-sqrtf(disc))/(2*a); return 1;
}
static inline int ray_aabb(CCRay r,CCVec3 mn,CCVec3 mx,float* t){
    float tmin=0,tmax=1e30f;
    float* o=(float*)&r.origin,*d=(float*)&r.dir,*lo=(float*)&mn,*hi=(float*)&mx;
    for(int i=0;i<3;i++){
        if(fabsf(d[i])<CC_EPSILON){if(o[i]<lo[i]||o[i]>hi[i])return 0;}
        else{float t1=(lo[i]-o[i])/d[i],t2=(hi[i]-o[i])/d[i];if(t1>t2){float tmp=t1;t1=t2;t2=tmp;}tmin=fmaxf(tmin,t1);tmax=fminf(tmax,t2);if(tmin>tmax)return 0;}
    }
    *t=tmin; return 1;
}

/* ─── Misc ───────────────────────────────────────────────────────────── */
static inline float cc_lerp(float a,float b,float t){return a+t*(b-a);}
static inline float cc_smoothstep(float e0,float e1,float x){
    float t=(x-e0)/(e1-e0); t=t<0?0:t>1?1:t;
    return t*t*(3-2*t);
}
static inline float cc_clamp(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static inline float cc_ease_in(float t){return t*t;}
static inline float cc_ease_out(float t){return t*(2-t);}
static inline float cc_ease_in_out(float t){return t<0.5f?2*t*t:(-1+(4-2*t)*t);}

#undef M
#ifdef __cplusplus
}
#endif
