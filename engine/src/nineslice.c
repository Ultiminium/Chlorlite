#include "cc/nineslice.h"

/* draw one patch cell: dest rect (dx,dy,dw,dh) sampling source px rect
   (sx,sy,sw,sh) from a texture of size (tw,th). Skips zero-area cells. */
static void patch(CCEngine* e, CCTexture tex, float tw, float th,
                  float dx,float dy,float dw,float dh,
                  float sx,float sy,float sw,float sh, uint32_t tint){
    if(dw<=0 || dh<=0 || sw<=0 || sh<=0) return;
    float u0=sx/tw, v0=sy/th, u1=(sx+sw)/tw, v1=(sy+sh)/th;
    cc_draw_sprite_ex(e, tex, dx,dy,dw,dh, u0,v0,u1,v1, 0.0f, tint, 0);
}

void cc_nineslice_draw(CCEngine* e, const CCNineSlice* ns,
                       float x, float y, float w, float h, uint32_t tint){
    if(!e||!ns||ns->tex==0) return;
    float tw=ns->tex_w, th=ns->tex_h;
    float l=ns->left, r=ns->right, t=ns->top, b=ns->bottom;
    /* clamp borders so they fit the destination */
    if(l+r > w){ float k=w/(l+r); l*=k; r*=k; }
    if(t+b > h){ float k=h/(t+b); t*=k; b*=k; }
    /* source column x-edges: 0, l, tw-r, tw ; row y-edges: 0, t, th-b, th */
    float scx0=0, scx1=ns->left, scx2=tw-ns->right;
    float scy0=0, scy1=ns->top,  scy2=th-ns->bottom;
    float scw_mid = (scx2-scx1)>0 ? (scx2-scx1) : 1;
    float sch_mid = (scy2-scy1)>0 ? (scy2-scy1) : 1;
    /* dest column x-edges: x, x+l, x+w-r, x+w ; rows similarly */
    float dcx0=x, dcx1=x+l, dcx2=x+w-r;
    float dcy0=y, dcy1=y+t, dcy2=y+h-b;
    float dcw_mid=(dcx2-dcx1), dch_mid=(dcy2-dcy1);

    /* top row */
    patch(e,ns->tex,tw,th, dcx0,dcy0, l,t,            scx0,scy0, ns->left,ns->top, tint);
    patch(e,ns->tex,tw,th, dcx1,dcy0, dcw_mid,t,      scx1,scy0, scw_mid,ns->top, tint);
    patch(e,ns->tex,tw,th, dcx2,dcy0, r,t,            scx2,scy0, ns->right,ns->top, tint);
    /* middle row */
    patch(e,ns->tex,tw,th, dcx0,dcy1, l,dch_mid,      scx0,scy1, ns->left,sch_mid, tint);
    patch(e,ns->tex,tw,th, dcx1,dcy1, dcw_mid,dch_mid,scx1,scy1, scw_mid,sch_mid, tint);
    patch(e,ns->tex,tw,th, dcx2,dcy1, r,dch_mid,      scx2,scy1, ns->right,sch_mid, tint);
    /* bottom row */
    patch(e,ns->tex,tw,th, dcx0,dcy2, l,b,            scx0,scy2, ns->left,ns->bottom, tint);
    patch(e,ns->tex,tw,th, dcx1,dcy2, dcw_mid,b,      scx1,scy2, scw_mid,ns->bottom, tint);
    patch(e,ns->tex,tw,th, dcx2,dcy2, r,b,            scx2,scy2, ns->right,ns->bottom, tint);
}

void cc_panel_draw(CCEngine* e, float x, float y, float w, float h,
                   uint32_t fill, float bpx, uint32_t border_col){
    if(!e||w<=0||h<=0) return;
    if(bpx<0) bpx=0;
    /* fill the interior */
    cc_draw_rect(e, x, y, w, h, fill, 0, 0);
    if(bpx>0){
        /* four fixed-thickness border edges (corners never smear) */
        cc_draw_rect(e, x,          y,          w,   bpx, border_col, 0, 0); /* top */
        cc_draw_rect(e, x,          y+h-bpx,    w,   bpx, border_col, 0, 0); /* bottom */
        cc_draw_rect(e, x,          y,          bpx, h,   border_col, 0, 0); /* left */
        cc_draw_rect(e, x+w-bpx,    y,          bpx, h,   border_col, 0, 0); /* right */
    }
}
