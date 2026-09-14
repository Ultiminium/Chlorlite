/* nineslice_test — nine-slice panels. Generates a 32x32 bordered "frame"
 * texture, then draws it as a 9-patch at several very different sizes (wide,
 * tall, small, large) to show corners stay undistorted while edges/center
 * stretch. Also draws solid styled panels (cc_panel_draw). Renders to a PNG. */
#include "cc/claudecore.h"
#include "cc/nineslice.h"
#include <stdio.h>
#include <string.h>

#define RGBA(r,g,b,a) (((uint32_t)(r)<<24)|((uint32_t)(g)<<16)|((uint32_t)(b)<<8)|(uint32_t)(a))

/* build a 32x32 frame texture: 6px colored border, translucent dark interior,
   with distinct corner accents so distortion would be obvious. */
static CCTexture make_frame(CCEngine* e){
    const int S=32, B=6;
    uint8_t* px=(uint8_t*)malloc(S*S*4);
    for(int y=0;y<S;y++)for(int x=0;x<S;x++){
        int i=(y*S+x)*4;
        int edge = (x<B||x>=S-B||y<B||y>=S-B);
        int corner = (x<B||x>=S-B) && (y<B||y>=S-B);
        if(corner){ px[i]=250; px[i+1]=210; px[i+2]=90; px[i+3]=255; }   /* gold corners */
        else if(edge){ px[i]=90; px[i+1]=140; px[i+2]=220; px[i+3]=255; }/* blue edges */
        else { px[i]=25; px[i+1]=28; px[i+2]=36; px[i+3]=210; }          /* dark interior */
    }
    CCTextureDesc d={.width=S,.height=S,.format=CC_FMT_RGBA8,.mipmaps=false,.linear_filter=false,.wrap_repeat=false};
    CCTexture t=cc_texture_create(e,&d,px);
    free(px);
    return t;
}

int main(int argc,char**argv){
    const char* out=(argc>1)?argv[1]:"/tmp/nineslice_test.png";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=1000;cfg.height=560;cfg.verbose=false;
    CCEngine* e=cc_init(&cfg); if(!e)return 1;

    CCTexture frame=make_frame(e);
    CCNineSlice ns={ frame, 32,32, 6,6,6,6 };   /* 6px border insets all sides */

    CCPostFX fx=cc_postfx_default(); fx.tonemap_aces=true; cc_postfx_set(e,&fx);

    for(int f=0;f<4;f++){
        cc_frame_begin(e);
        CCCameraDesc cam={.pos={0,2,6},.target={0,0,0},.up={0,1,0},.fov_deg=50,.near_plane=0.1f,.far_plane=100,.exposure=1};
        cc_camera_set(e,&cam);
        /* nine-slice panels at very different sizes — corners must stay 6px */
        cc_nineslice_draw(e,&ns,  40, 40, 380, 90,  RGBA(255,255,255,255));  /* wide short */
        cc_nineslice_draw(e,&ns,  40,160, 120, 340, RGBA(255,255,255,255));  /* tall narrow */
        cc_nineslice_draw(e,&ns, 200,200, 300, 300, RGBA(255,255,255,255));  /* large square */
        cc_nineslice_draw(e,&ns, 560, 40,  70, 70,  RGBA(255,255,255,255));  /* tiny */
        /* tinted variant */
        cc_nineslice_draw(e,&ns, 560,140, 380,120, RGBA(255,180,180,255));   /* red tint */
        /* solid styled panels (no texture) */
        cc_panel_draw(e, 560, 300, 380, 90,  RGBA(40,44,54,235), 2.0f, RGBA(120,160,220,255));
        cc_panel_draw(e, 560, 410, 180, 110, RGBA(30,40,32,235), 3.0f, RGBA(110,200,130,255));
        cc_panel_draw(e, 760, 410, 180, 110, RGBA(44,32,40,235), 3.0f, RGBA(210,120,140,255));
        cc_frame_end(e);
    }
    const char* s=cc_screenshot(e,out);
    printf("screenshot: %s (nine-slice panels at varied sizes + solid panels)\n", s?s:"(null)");
    cc_shutdown(e);
    printf("NINESLICE TEST: rendered\n");
    return 0;
}
