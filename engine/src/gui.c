/* gui.c — immediate-mode GUI toolkit for CC.
 *
 * Implements the cc_gui_* API declared in render.h: begin_frame → window →
 * label/button/slider/checkbox/separator → end_window → end_frame. Widgets are
 * laid out top-to-bottom inside the current window and drawn via cc_draw_rect /
 * cc_draw_text (2D overlay, captured in screenshots). Interaction uses
 * cc_mouse_pos / cc_mouse_down; in headless mode there's no live mouse so
 * widgets render but don't fire (which is the intended "visible in screenshots"
 * behavior). Immediate-mode: no retained widget tree, state lives in the caller.
 */
#include "cc/claudecore.h"
#include "cc/render.h"
#include "cc/input.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* packed RGBA helpers */
#define RGBA(r,g,b,a) (((uint32_t)(r)<<24)|((uint32_t)(g)<<16)|((uint32_t)(b)<<8)|(uint32_t)(a))

typedef struct {
    int    active;         /* inside a window? */
    float  wx, wy, ww, wh; /* current window rect */
    float  cursor_y;       /* next widget top (inside window) */
    float  pad;            /* inner padding */
    float  row_h;          /* default widget height */
    CCFont font;
    float  font_px;
    int    mx, my;         /* mouse pos this frame */
    int    mdown;          /* mouse button down */
} GuiState;

static GuiState g_gui;   /* single-context immediate GUI (fine for tools/HUD) */

static int pt_in(float x,float y,float w,float h,int px,int py){
    return px>=x && px<=x+w && py>=y && py<=y+h;
}

void cc_gui_begin_frame(CCEngine* eng){
    memset(&g_gui,0,sizeof(g_gui));
    g_gui.pad=8; g_gui.row_h=22; g_gui.font_px=15;
    g_gui.font=cc_font_builtin(eng);
    int mx=0,my=0; cc_mouse_pos(eng,&mx,&my); g_gui.mx=mx; g_gui.my=my;
    g_gui.mdown = cc_mouse_down(eng, 0 /*left*/);
}

bool cc_gui_window(CCEngine* eng, const char* title, float x,float y,float w,float h){
    g_gui.active=1;
    g_gui.wx=x; g_gui.wy=y; g_gui.ww=w; g_gui.wh=h;
    /* panel background + border */
    cc_draw_rect(eng, x, y, w, h, RGBA(28,30,36,235), 1.0f, RGBA(90,96,110,255));
    /* title bar */
    cc_draw_rect(eng, x, y, w, 26, RGBA(46,50,60,255), 0, 0);
    if(title) cc_draw_text(eng, g_gui.font, title, x+g_gui.pad, y+6, g_gui.font_px, RGBA(230,232,238,255));
    g_gui.cursor_y = y + 26 + g_gui.pad;
    return true;
}

void cc_gui_label(CCEngine* eng, const char* text){
    if(!g_gui.active||!text) return;
    cc_draw_text(eng, g_gui.font, text, g_gui.wx+g_gui.pad, g_gui.cursor_y, g_gui.font_px, RGBA(210,214,222,255));
    g_gui.cursor_y += g_gui.row_h;
}

bool cc_gui_button(CCEngine* eng, const char* label){
    if(!g_gui.active) return false;
    float x=g_gui.wx+g_gui.pad, y=g_gui.cursor_y, w=g_gui.ww-2*g_gui.pad, h=g_gui.row_h;
    int hover=pt_in(x,y,w,h,g_gui.mx,g_gui.my);
    uint32_t bg = hover ? RGBA(70,110,180,255) : RGBA(54,60,72,255);
    cc_draw_rect(eng, x, y, w, h, bg, 1.0f, RGBA(96,104,120,255));
    if(label){
        float tw=cc_text_width(eng,g_gui.font,label,g_gui.font_px);
        cc_draw_text(eng, g_gui.font, label, x+(w-tw)*0.5f, y+3, g_gui.font_px, RGBA(235,238,245,255));
    }
    g_gui.cursor_y += h + 4;
    return hover && g_gui.mdown;   /* clicked this frame (down while hovering) */
}

float cc_gui_slider(CCEngine* eng, const char* label, float val, float mn, float mx){
    if(!g_gui.active) return val;
    float x=g_gui.wx+g_gui.pad, y=g_gui.cursor_y, w=g_gui.ww-2*g_gui.pad, h=g_gui.row_h;
    /* track */
    float ty=y+h*0.5f-3;
    cc_draw_rect(eng, x, ty, w, 6, RGBA(40,44,52,255), 1.0f, RGBA(80,86,100,255));
    float t = (mx>mn)? (val-mn)/(mx-mn) : 0; if(t<0)t=0; if(t>1)t=1;
    /* drag: if mouse down over the row, set value from x */
    if(pt_in(x,y,w,h,g_gui.mx,g_gui.my) && g_gui.mdown){
        t=(g_gui.mx-x)/w; if(t<0)t=0; if(t>1)t=1; val=mn+t*(mx-mn);
    }
    /* knob */
    float kx=x+t*w;
    cc_draw_rect(eng, kx-5, y+1, 10, h-2, RGBA(120,160,220,255), 1.0f, RGBA(200,214,235,255));
    if(label){
        char buf[128]; snprintf(buf,sizeof(buf),"%s: %.2f",label,val);
        cc_draw_text(eng, g_gui.font, buf, x, y-2, g_gui.font_px*0.9f, RGBA(200,204,212,255));
    }
    g_gui.cursor_y += h + 6;
    return val;
}

bool cc_gui_checkbox(CCEngine* eng, const char* label, bool* val){
    if(!g_gui.active||!val) return false;
    float x=g_gui.wx+g_gui.pad, y=g_gui.cursor_y, bs=g_gui.row_h-4;
    int hover=pt_in(x,y,bs,bs,g_gui.mx,g_gui.my);
    cc_draw_rect(eng, x, y, bs, bs, RGBA(44,48,58,255), 1.0f, RGBA(100,108,124,255));
    if(*val) cc_draw_rect(eng, x+4, y+4, bs-8, bs-8, RGBA(110,180,120,255), 0, 0);
    if(label) cc_draw_text(eng, g_gui.font, label, x+bs+8, y+2, g_gui.font_px, RGBA(210,214,222,255));
    g_gui.cursor_y += g_gui.row_h + 2;
    if(hover && g_gui.mdown){ *val = !*val; return true; }
    return false;
}

void cc_gui_separator(CCEngine* eng){
    if(!g_gui.active) return;
    float x=g_gui.wx+g_gui.pad, y=g_gui.cursor_y+2, w=g_gui.ww-2*g_gui.pad;
    cc_draw_rect(eng, x, y, w, 1, RGBA(80,86,100,255), 0, 0);
    g_gui.cursor_y += 8;
}

/* Progress/stat bar: a filled track showing `frac` (0..1), tinted by `rgba`
 * (0 = default blue). Great for health/sanity/stamina HUDs and load bars. The
 * label (optional) is drawn above; the numeric percent is centered in the bar. */
void cc_gui_progress(CCEngine* eng, const char* label, float frac, uint32_t rgba){
    if(!g_gui.active) return;
    if(frac<0)frac=0; if(frac>1)frac=1;
    float x=g_gui.wx+g_gui.pad, y=g_gui.cursor_y, w=g_gui.ww-2*g_gui.pad, h=g_gui.row_h;
    if(label){
        cc_draw_text(eng, g_gui.font, label, x, y-2, g_gui.font_px*0.9f, RGBA(200,204,212,255));
        y += g_gui.font_px; g_gui.cursor_y += g_gui.font_px;
    }
    /* track background */
    cc_draw_rect(eng, x, y, w, h, RGBA(34,38,46,255), 1.0f, RGBA(80,86,100,255));
    /* fill */
    uint32_t fill = rgba ? rgba : RGBA(90,150,220,255);
    if(frac>0.001f) cc_draw_rect(eng, x+1, y+1, (w-2)*frac, h-2, fill, 0, 0);
    /* percent text */
    char buf[16]; snprintf(buf,sizeof(buf),"%d%%",(int)(frac*100.0f+0.5f));
    float tw=cc_text_width(eng,g_gui.font,buf,g_gui.font_px*0.85f);
    cc_draw_text(eng, g_gui.font, buf, x+(w-tw)*0.5f, y+3, g_gui.font_px*0.85f, RGBA(235,238,245,255));
    g_gui.cursor_y += h + 4;
}

/* Add vertical space between widgets (menu breathing room). */
void cc_gui_spacer(CCEngine* eng, float pixels){
    if(!g_gui.active) return;
    g_gui.cursor_y += pixels;
}

/* A button at an EXPLICIT screen rect (for free-form menu layouts that don't use
 * the top-to-bottom flow). Returns true when clicked this frame. Works inside or
 * outside a window. */
bool cc_gui_button_at(CCEngine* eng, const char* label, float x, float y, float w, float h){
    int hover=pt_in(x,y,w,h,g_gui.mx,g_gui.my);
    uint32_t bg = hover ? RGBA(70,110,180,255) : RGBA(54,60,72,255);
    cc_draw_rect(eng, x, y, w, h, bg, 1.0f, RGBA(96,104,120,255));
    if(label){
        float tw=cc_text_width(eng,g_gui.font,label,g_gui.font_px);
        cc_draw_text(eng, g_gui.font, label, x+(w-tw)*0.5f, y+(h-g_gui.font_px)*0.5f, g_gui.font_px, RGBA(235,238,245,255));
    }
    return hover && g_gui.mdown;
}

/* Query the current layout cursor (for callers mixing flow + absolute widgets). */
void cc_gui_get_cursor(CCEngine* eng, float* x, float* y){
    (void)eng;
    if(x)*x=g_gui.wx+g_gui.pad;
    if(y)*y=g_gui.cursor_y;
}

void cc_gui_end_window(CCEngine* eng){ (void)eng; g_gui.active=0; }
void cc_gui_end_frame(CCEngine* eng){ (void)eng; }
