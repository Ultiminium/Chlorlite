/* debug.c — Seeing & verifying implementation. */
#include "cc/debug.h"
#include "cc/claudecore.h"
#include "cc/render.h"
#include "renderer_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

extern CCRenderer* cc_engine_renderer(CCEngine*);

/* gizmo storage lives here (engine-side, world-space, projected at overlay time) */
#define GZ_MAX 4096
typedef struct { int type; CCVec3 a,b,col; float size; char label[32]; } Gizmo;
typedef struct { Gizmo g[GZ_MAX]; uint32_t n; uint64_t seed; char hash[16]; } DbgState;
static DbgState* dbg(CCEngine* e){
    static DbgState s; static int init=0;
    if(!init){ memset(&s,0,sizeof(s)); snprintf(s.hash,sizeof(s.hash),"%08x",(unsigned)0xCC0002); init=1; }
    (void)e; return &s;
}

CCFrameStats cc_debug_stats(CCEngine* e){
    CCFrameStats st; memset(&st,0,sizeof(st));
    CCRenderer* r=cc_engine_renderer(e); if(!r)return st;
    cc_renderer_get_stats(r,&st.draw_calls,&st.triangles,&st.meshes_drawn,&st.lights,&st.textures,&st.tex_bytes);
    st.verts=st.triangles*3;
    return st;
}

char* cc_debug_drawcalls_json(CCEngine* e){
    CCRenderer* r=cc_engine_renderer(e); if(!r)return NULL;
    uint32_t n=cc_renderer_drawlog_count(r);
    size_t cap=256+n*256; char* buf=malloc(cap); size_t o=0;
    o+=snprintf(buf+o,cap-o,"{\"draw_calls\":%u,\"calls\":[",n);
    for(uint32_t i=0;i<n;i++){
        uint32_t mesh,mat,tris; float m[16];
        cc_renderer_drawlog_get(r,i,&mesh,&mat,&tris,m);
        o+=snprintf(buf+o,cap-o,"%s{\"i\":%u,\"mesh\":%u,\"material\":%u,\"tris\":%u,\"pos\":[%.3f,%.3f,%.3f]}",
            i?",":"",i,mesh,mat,tris,m[12],m[13],m[14]);
    }
    o+=snprintf(buf+o,cap-o,"]}");
    return buf;
}

char* cc_debug_scene_json(CCEngine* e){
    CCRenderer* r=cc_engine_renderer(e); if(!r)return NULL;
    CCFrameStats st=cc_debug_stats(e);
    uint32_t n=cc_renderer_drawlog_count(r);
    size_t cap=512+n*320+st.lights*160; char* buf=malloc(cap); size_t o=0;
    o+=snprintf(buf+o,cap-o,"{\"stats\":{\"draw_calls\":%u,\"triangles\":%u,\"meshes\":%u,\"lights\":%u,\"textures\":%u,\"tex_bytes\":%zu},",
        st.draw_calls,st.triangles,st.meshes_drawn,st.lights,st.textures,st.tex_bytes);
    o+=snprintf(buf+o,cap-o,"\"meshes\":[");
    for(uint32_t i=0;i<n;i++){
        uint32_t mesh,mat,tris; float m[16];
        cc_renderer_drawlog_get(r,i,&mesh,&mat,&tris,m);
        o+=snprintf(buf+o,cap-o,"%s{\"id\":%u,\"mesh\":%u,\"material\":%u,\"tris\":%u,"
            "\"transform\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]}",
            i?",":"",i,mesh,mat,tris,m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11],m[12],m[13],m[14],m[15]);
    }
    o+=snprintf(buf+o,cap-o,"],\"lights\":[");
    for(uint32_t i=0;i<st.lights;i++){
        int type; float dir[3],col[3],inten;
        cc_renderer_light_get(r,i,&type,dir,col,&inten);
        o+=snprintf(buf+o,cap-o,"%s{\"type\":%d,\"dir\":[%.3f,%.3f,%.3f],\"color\":[%.3f,%.3f,%.3f],\"intensity\":%.3f}",
            i?",":"",type,dir[0],dir[1],dir[2],col[0],col[1],col[2],inten);
    }
    o+=snprintf(buf+o,cap-o,"]}");
    return buf;
}

void cc_debug_dump_json(CCEngine* e, const char* path){
    char* s=cc_debug_scene_json(e); if(!s)return;
    FILE* f=fopen(path,"w"); if(f){ fputs(s,f); fclose(f); }
    free(s);
}

void cc_debug_set_view(CCEngine* e, CCDebugView v){
    CCRenderer* r=cc_engine_renderer(e); if(r) cc_renderer_set_debug_view(r,(int)v);
}

/* ── gizmos ── */
static void gz_add(CCEngine* e,int t,CCVec3 a,CCVec3 b,CCVec3 c,float sz,const char* lbl){
    DbgState* d=dbg(e); if(d->n>=GZ_MAX)return;
    Gizmo* g=&d->g[d->n++]; g->type=t; g->a=a; g->b=b; g->col=c; g->size=sz;
    g->label[0]=0; if(lbl)snprintf(g->label,sizeof(g->label),"%s",lbl);
}
void cc_gizmo_line(CCEngine* e,CCVec3 a,CCVec3 b,CCVec3 c){ gz_add(e,0,a,b,c,0,0); }
void cc_gizmo_box(CCEngine* e,CCVec3 ctr,CCVec3 h,CCVec3 c){ gz_add(e,1,ctr,h,c,0,0); }
void cc_gizmo_sphere(CCEngine* e,CCVec3 ctr,float rad,CCVec3 c){ gz_add(e,2,ctr,ctr,c,rad,0); }
void cc_gizmo_arrow(CCEngine* e,CCVec3 f,CCVec3 t,CCVec3 c){ gz_add(e,3,f,t,c,0,0); }
void cc_gizmo_cross(CCEngine* e,CCVec3 at,float s,CCVec3 c){ gz_add(e,4,at,at,c,s,0); }
void cc_gizmo_label(CCEngine* e,CCVec3 at,const char* txt,CCVec3 c){ gz_add(e,5,at,at,c,0,txt); }

/* project world → screen using current camera matrices */
extern void cc_renderer_camera_vp(CCRenderer*, float*);
static int project(CCRenderer* r,CCVec3 w,float W,float H,float* sx,float* sy){
    float vp[16]; cc_renderer_camera_vp(r,vp);
    CCMat4 VP; memcpy(VP.m,vp,64);
    float x=VP.m[0]*w.x+VP.m[4]*w.y+VP.m[8]*w.z+VP.m[12];
    float y=VP.m[1]*w.x+VP.m[5]*w.y+VP.m[9]*w.z+VP.m[13];
    float zc=VP.m[2]*w.x+VP.m[6]*w.y+VP.m[10]*w.z+VP.m[14];
    float wc=VP.m[3]*w.x+VP.m[7]*w.y+VP.m[11]*w.z+VP.m[15];
    if(wc<=0.0001f)return 0;
    *sx=( x/wc*0.5f+0.5f)*W; *sy=(1.0f-(y/wc*0.5f+0.5f))*H; (void)zc; return 1;
}
static uint32_t col2rgba(CCVec3 c){
    uint32_t rr=(uint32_t)(c.x*255)&255, gg=(uint32_t)(c.y*255)&255, bb=(uint32_t)(c.z*255)&255;
    return (rr<<24)|(gg<<16)|(bb<<8)|0xFF;
}
static void wline(CCEngine* e,CCRenderer* r,CCVec3 a,CCVec3 b,float W,float H,uint32_t col){
    float ax,ay,bx,by;
    if(project(r,a,W,H,&ax,&ay)&&project(r,b,W,H,&bx,&by))
        cc_renderer_draw_line(r,ax,ay,bx,by,1.5f,col);
    (void)e;
}

void cc_debug_overlay(CCEngine* e){
    CCRenderer* r=cc_engine_renderer(e); if(!r)return;
    DbgState* d=dbg(e);
    uint32_t W,H; const uint8_t* px=cc_renderer_frame_pixels(r,&W,&H); (void)px;
    if(!W){ W=1280; H=720; }
    /* draw gizmos */
    for(uint32_t i=0;i<d->n;i++){
        Gizmo* g=&d->g[i]; uint32_t col=col2rgba(g->col);
        if(g->type==0){ wline(e,r,g->a,g->b,W,H,col); }
        else if(g->type==1){ CCVec3 c=g->a,h=g->b; CCVec3 crn[8]={
            {c.x-h.x,c.y-h.y,c.z-h.z},{c.x+h.x,c.y-h.y,c.z-h.z},{c.x+h.x,c.y-h.y,c.z+h.z},{c.x-h.x,c.y-h.y,c.z+h.z},
            {c.x-h.x,c.y+h.y,c.z-h.z},{c.x+h.x,c.y+h.y,c.z-h.z},{c.x+h.x,c.y+h.y,c.z+h.z},{c.x-h.x,c.y+h.y,c.z+h.z}};
            int ed[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for(int k=0;k<12;k++) wline(e,r,crn[ed[k][0]],crn[ed[k][1]],W,H,col); }
        else if(g->type==2){ CCVec3 c=g->a; float rad=g->size; int seg=16;
            for(int a=0;a<seg;a++){ float t0=a*6.2832f/seg,t1=(a+1)*6.2832f/seg;
                wline(e,r,(CCVec3){c.x+cosf(t0)*rad,c.y+sinf(t0)*rad,c.z},(CCVec3){c.x+cosf(t1)*rad,c.y+sinf(t1)*rad,c.z},W,H,col);
                wline(e,r,(CCVec3){c.x+cosf(t0)*rad,c.y,c.z+sinf(t0)*rad},(CCVec3){c.x+cosf(t1)*rad,c.y,c.z+sinf(t1)*rad},W,H,col);
                wline(e,r,(CCVec3){c.x,c.y+cosf(t0)*rad,c.z+sinf(t0)*rad},(CCVec3){c.x,c.y+cosf(t1)*rad,c.z+sinf(t1)*rad},W,H,col);} }
        else if(g->type==3){ wline(e,r,g->a,g->b,W,H,col);
            CCVec3 dir={g->b.x-g->a.x,g->b.y-g->a.y,g->b.z-g->a.z};
            float L=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z); if(L>0.0001f){ dir.x/=L;dir.y/=L;dir.z/=L;
                CCVec3 tip=g->b; CCVec3 back={tip.x-dir.x*0.2f,tip.y-dir.y*0.2f,tip.z-dir.z*0.2f};
                wline(e,r,tip,(CCVec3){back.x+0.1f,back.y,back.z},W,H,col);
                wline(e,r,tip,(CCVec3){back.x-0.1f,back.y,back.z},W,H,col); } }
        else if(g->type==4){ float s=g->size; CCVec3 c=g->a;
            wline(e,r,(CCVec3){c.x-s,c.y,c.z},(CCVec3){c.x+s,c.y,c.z},W,H,col);
            wline(e,r,(CCVec3){c.x,c.y-s,c.z},(CCVec3){c.x,c.y+s,c.z},W,H,col);
            wline(e,r,(CCVec3){c.x,c.y,c.z-s},(CCVec3){c.x,c.y,c.z+s},W,H,col); }
        else if(g->type==5){ float sx,sy; if(project(r,g->a,W,H,&sx,&sy))
            cc_renderer_draw_text(r,cc_renderer_font_builtin(r),g->label,sx,sy,18,col); }
    }
    d->n=0;
    /* counters overlay */
    CCFrameStats st=cc_debug_stats(e);
    char line[160];
    snprintf(line,sizeof(line),"draws:%u  tris:%u  meshes:%u  lights:%u  tex:%u (%zuKB)",
        st.draw_calls,st.triangles,st.meshes_drawn,st.lights,st.textures,st.tex_bytes/1024);
    CCFont f=cc_renderer_font_builtin(r);
    cc_renderer_draw_rect(r,6,6,560,26,0x000000AAu);
    cc_renderer_draw_text(r,f,line,12,12,16,0x00FF88FFu);
}

void cc_debug_set_seed(CCEngine* e, uint64_t seed){ dbg(e)->seed=seed; }
void cc_debug_stamp(CCEngine* e){
    CCRenderer* r=cc_engine_renderer(e); if(!r)return; DbgState* d=dbg(e);
    uint32_t W,H; cc_renderer_frame_pixels(r,&W,&H); if(!W){W=1280;H=720;}
    char s[96]; snprintf(s,sizeof(s),"seed:%llu build:%s",(unsigned long long)d->seed,d->hash);
    cc_renderer_draw_text(r,cc_renderer_font_builtin(r),s,10,(float)H-22,14,0xFFFF00FFu);
}
const char* cc_screenshot_manifest(CCEngine* e, const char* png, const char* cam){
    static char path[600]; DbgState* d=dbg(e);
    snprintf(path,sizeof(path),"%s.json",png?png:"shot");
    CCFrameStats st=cc_debug_stats(e);
    FILE* f=fopen(path,"w"); if(f){
        fprintf(f,"{\"png\":\"%s\",\"seed\":%llu,\"build\":\"%s\",\"camera\":\"%s\",\"draw_calls\":%u,\"triangles\":%u,\"lights\":%u}\n",
            png?png:"",(unsigned long long)d->seed,d->hash,cam?cam:"",st.draw_calls,st.triangles,st.lights);
        fclose(f);
    }
    return path;
}

uint32_t cc_debug_pick(CCEngine* e, int x, int y){
    /* CPU pick: project each drawlog origin, nearest within 40px wins. */
    CCRenderer* r=cc_engine_renderer(e); if(!r)return 0;
    uint32_t W,H; cc_renderer_frame_pixels(r,&W,&H); if(!W){W=1280;H=720;}
    uint32_t n=cc_renderer_drawlog_count(r), best=0; float bestd=1e9f;
    for(uint32_t i=0;i<n;i++){ uint32_t mesh,mat,tris; float m[16];
        cc_renderer_drawlog_get(r,i,&mesh,&mat,&tris,m);
        float sx,sy; if(project(r,(CCVec3){m[12],m[13],m[14]},W,H,&sx,&sy)){
            float dx=sx-x,dy=sy-y,dd=dx*dx+dy*dy;
            if(dd<bestd&&dd<1600.0f){ bestd=dd; best=i+1; } } }
    return best;
}

void cc_debug_frame_point(CCEngine* e, CCVec3 c, float radius, float yaw, float pitch, float* outV, float* outP){
    (void)e; float d=radius/tanf(0.5f*0.9599f); /* ~55deg fov fit */
    float yr=yaw*0.01745f, pr=pitch*0.01745f;
    CCVec3 eye={ c.x+d*cosf(pr)*sinf(yr), c.y+d*sinf(pr), c.z+d*cosf(pr)*cosf(yr) };
    CCMat4 V=mat4_look_at(eye,c,(CCVec3){0,1,0});
    CCMat4 P=mat4_perspective(0.9599f,16.0f/9.0f,0.05f,d+radius*4+50);
    if(outV)memcpy(outV,V.m,64); if(outP)memcpy(outP,P.m,64);
}

int cc_debug_orbit_capture(CCEngine* e, CCVec3 c, float radius, int shots, const char* dir){
    if(shots<1)shots=8; int done=0; char path[512];
    for(int i=0;i<shots;i++){
        float yaw=(float)i/shots*360.0f; float V[16],P[16];
        cc_debug_frame_point(e,c,radius,yaw,20.0f,V,P);
        CCMat4 VM; memcpy(VM.m,V,64); CCMat4 PM; memcpy(PM.m,P,64); CCMat4 VP=mat4_mul(PM,VM);
        float eye[3]={0,0,0};
        cc_frame_begin(e);
        cc_upload_camera_matrices(e,V,P,VP.m,mat4_inverse(VP).m,eye);
        /* caller is expected to re-draw scene between? For simplicity we snapshot current. */
        cc_frame_end(e);
        snprintf(path,sizeof(path),"%s/orbit_%02d.png",dir?dir:".",i);
        cc_screenshot(e,path); done++;
    }
    return done;
}

/* ── light range gizmos ── */
extern void cc_renderer_light_pos_range(CCRenderer*, uint32_t, float*, float*);
void cc_debug_draw_lights(CCEngine* e){
    CCRenderer* r=cc_engine_renderer(e); if(!r)return;
    CCFrameStats st=cc_debug_stats(e);
    for(uint32_t i=0;i<st.lights;i++){
        int type; float dir[3],col[3],inten,pos[3],range;
        cc_renderer_light_get(r,i,&type,dir,col,&inten);
        cc_renderer_light_pos_range(r,i,pos,&range);
        CCVec3 c={pos[0],pos[1],pos[2]}, color={col[0],col[1],col[2]};
        if(type==0){ /* directional: arrow from above origin */
            cc_gizmo_arrow(e,(CCVec3){0,5,0},(CCVec3){dir[0]*3,5+dir[1]*3,dir[2]*3},color);
        } else { /* point/spot: range sphere + center cross */
            if(range<=0)range=5.0f;
            cc_gizmo_sphere(e,c,range,color);
            cc_gizmo_cross(e,c,0.3f,color);
        }
    }
}

/* ── freecam ── */
void cc_freecam_update(CCEngine* e, CCFreecam* cam, float dt, float move, float look){
    if(!cam)return;
    float mdx,mdy; { int a,b; cc_mouse_delta(e,&a,&b); mdx=(float)a; mdy=(float)b; }
    cam->yaw   += mdx*look*dt;
    cam->pitch -= mdy*look*dt;
    if(cam->pitch> 1.55f)cam->pitch= 1.55f;
    if(cam->pitch<-1.55f)cam->pitch=-1.55f;
    CCVec3 fwd={cosf(cam->pitch)*sinf(cam->yaw),sinf(cam->pitch),cosf(cam->pitch)*cosf(cam->yaw)};
    CCVec3 right={cosf(cam->yaw),0,-sinf(cam->yaw)};
    float sp=move*dt;
    if(cc_key_down(e,QKEY_W)){cam->pos.x+=fwd.x*sp;cam->pos.y+=fwd.y*sp;cam->pos.z+=fwd.z*sp;}
    if(cc_key_down(e,QKEY_S)){cam->pos.x-=fwd.x*sp;cam->pos.y-=fwd.y*sp;cam->pos.z-=fwd.z*sp;}
    if(cc_key_down(e,QKEY_D)){cam->pos.x+=right.x*sp;cam->pos.z+=right.z*sp;}
    if(cc_key_down(e,QKEY_A)){cam->pos.x-=right.x*sp;cam->pos.z-=right.z*sp;}
    if(cc_key_down(e,QKEY_SPACE))cam->pos.y+=sp;
    if(cc_key_down(e,QKEY_LSHIFT))cam->pos.y-=sp;
}
void cc_freecam_view(const CCFreecam* cam, float* outV, CCVec3* outTarget){
    if(!cam)return;
    CCVec3 fwd={cosf(cam->pitch)*sinf(cam->yaw),sinf(cam->pitch),cosf(cam->pitch)*cosf(cam->yaw)};
    CCVec3 tgt={cam->pos.x+fwd.x,cam->pos.y+fwd.y,cam->pos.z+fwd.z};
    CCMat4 V=mat4_look_at(cam->pos,tgt,(CCVec3){0,1,0});
    if(outV)memcpy(outV,V.m,64); if(outTarget)*outTarget=tgt;
}

/* ── walked heatmap ── */
#define HM_MAX 4096
static struct { CCVec3 p; int hits; } g_hm[HM_MAX]; static int g_hmn=0;
void cc_heatmap_mark(CCEngine* e, CCVec3 pos){ (void)e;
    for(int i=0;i<g_hmn;i++){ CCVec3 d={g_hm[i].p.x-pos.x,g_hm[i].p.y-pos.y,g_hm[i].p.z-pos.z};
        if(d.x*d.x+d.y*d.y+d.z*d.z<0.25f){ g_hm[i].hits++; return; } }
    if(g_hmn<HM_MAX){ g_hm[g_hmn].p=pos; g_hm[g_hmn].hits=1; g_hmn++; }
}
void cc_heatmap_draw(CCEngine* e){
    int mx=1; for(int i=0;i<g_hmn;i++) if(g_hm[i].hits>mx)mx=g_hm[i].hits;
    for(int i=0;i<g_hmn;i++){ float t=(float)g_hm[i].hits/mx;
        cc_gizmo_cross(e,g_hm[i].p,0.2f,(CCVec3){t,1.0f-t,0.0f}); } /* green→red by visits */
}
