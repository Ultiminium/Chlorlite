#include "cc/worldui.h"
#include "cc/render.h"
#include "cc/ccmath.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void cc_renderer_get_vp(CCRenderer* r, float out_vp[16]);
extern void cc_renderer_get_size(CCRenderer* r, uint32_t* w, uint32_t* h);
/* engine exposes its renderer via this (already used across the codebase) */
extern CCRenderer* cc_engine_renderer(CCEngine* e);

#define RGBA(r,g,b,a) (((uint32_t)(r)<<24)|((uint32_t)(g)<<16)|((uint32_t)(b)<<8)|(uint32_t)(a))

bool cc_world_to_screen(CCEngine* eng, float wx, float wy, float wz,
                        float* out_sx, float* out_sy, bool* out_visible){
    if(out_visible) *out_visible=false;
    CCRenderer* r=cc_engine_renderer(eng);
    if(!r) return false;
    float vpm[16]; cc_renderer_get_vp(r, vpm);
    uint32_t sw=0,sh=0; cc_renderer_get_size(r,&sw,&sh);
    if(sw==0||sh==0) return false;

    CCMat4 vp; memcpy(vp.m, vpm, 64);
    CCVec4 clip = mat4_mul_vec4(vp, (CCVec4){wx,wy,wz,1.0f});
    if(clip.w <= 1e-6f) return false;                 /* behind camera */
    float ndcx=clip.x/clip.w, ndcy=clip.y/clip.w;
    float sx=(ndcx*0.5f+0.5f)*(float)sw;
    float sy=(1.0f-(ndcy*0.5f+0.5f))*(float)sh;        /* flip Y: top-left origin */
    if(out_sx)*out_sx=sx; if(out_sy)*out_sy=sy;
    bool vis = (sx>=0 && sx<(float)sw && sy>=0 && sy<(float)sh);
    if(out_visible)*out_visible=vis;
    return true;
}

void cc_worldui_bar(CCEngine* eng, float wx,float wy,float wz,
                    float fill, float width, float height, float y_offset,
                    uint32_t fg, uint32_t bg){
    float sx,sy; bool vis;
    if(!cc_world_to_screen(eng,wx,wy,wz,&sx,&sy,&vis) || !vis) return;
    if(fill<0)fill=0; if(fill>1)fill=1;
    float x=sx-width*0.5f, y=sy-y_offset;
    /* background + border */
    cc_draw_rect(eng, x-1, y-1, width+2, height+2, bg, 1.0f, RGBA(0,0,0,200));
    /* fill */
    if(fill>0) cc_draw_rect(eng, x, y, width*fill, height, fg, 0, 0);
}

void cc_worldui_label(CCEngine* eng, float wx,float wy,float wz,
                      const char* text, float size, float y_offset, uint32_t color){
    if(!text||!text[0]) return;
    float sx,sy; bool vis;
    if(!cc_world_to_screen(eng,wx,wy,wz,&sx,&sy,&vis) || !vis) return;
    CCFont f=cc_font_builtin(eng);
    float tw=cc_text_width(eng,f,text,size);
    cc_draw_text(eng, f, text, sx-tw*0.5f, sy-y_offset, size, color);
}

/* ─── floating numbers ─── */
typedef struct {
    float x,y,z;          /* spawn world pos */
    char  text[24];
    float r,g,b;
    float age, life;
    bool  used;
} Floater;

struct CCFloaters {
    Floater* items;
    uint32_t cap;
};

CCFloaters* cc_floaters_create(void){ return (CCFloaters*)calloc(1,sizeof(CCFloaters)); }
void cc_floaters_destroy(CCFloaters* f){ if(f){ free(f->items); free(f); } }

void cc_floaters_spawn(CCFloaters* f, float wx,float wy,float wz,
                       const char* text, float r,float g,float b){
    if(!f||!text) return;
    /* find a free slot or grow */
    int idx=-1;
    for(uint32_t i=0;i<f->cap;i++) if(!f->items[i].used){ idx=(int)i; break; }
    if(idx<0){ uint32_t old=f->cap; f->cap=f->cap?f->cap*2:16;
        f->items=realloc(f->items,f->cap*sizeof(Floater));
        memset(&f->items[old],0,(f->cap-old)*sizeof(Floater)); idx=(int)old; }
    Floater* fl=&f->items[idx];
    fl->x=wx; fl->y=wy; fl->z=wz; fl->r=r; fl->g=g; fl->b=b;
    fl->age=0; fl->life=1.2f; fl->used=true;
    snprintf(fl->text,sizeof(fl->text),"%s",text);
}
void cc_floaters_update(CCFloaters* f, float dt){
    if(!f) return;
    for(uint32_t i=0;i<f->cap;i++){
        if(!f->items[i].used) continue;
        f->items[i].age += dt;
        if(f->items[i].age >= f->items[i].life) f->items[i].used=false;
    }
}
void cc_floaters_draw(CCEngine* eng, const CCFloaters* f){
    if(!eng||!f) return;
    CCFont font=cc_font_builtin(eng);
    for(uint32_t i=0;i<f->cap;i++){
        const Floater* fl=&f->items[i];
        if(!fl->used) continue;
        float t = fl->age/fl->life;             /* 0..1 */
        float rise = t*0.9f;                    /* world units up over life */
        float sx,sy; bool vis;
        if(!cc_world_to_screen(eng, fl->x, fl->y+rise, fl->z, &sx,&sy,&vis) || !vis) continue;
        float alpha = 1.0f-t;                   /* fade out */
        uint8_t a=(uint8_t)(alpha*255.0f);
        uint8_t rr=(uint8_t)(fl->r*255), gg=(uint8_t)(fl->g*255), bb=(uint8_t)(fl->b*255);
        float size=18.0f;
        float tw=cc_text_width(eng,font,fl->text,size);
        cc_draw_text(eng, font, fl->text, sx-tw*0.5f, sy, size, RGBA(rr,gg,bb,a));
    }
}
uint32_t cc_floaters_active(const CCFloaters* f){
    if(!f) return 0; uint32_t n=0;
    for(uint32_t i=0;i<f->cap;i++) if(f->items[i].used) n++;
    return n;
}
