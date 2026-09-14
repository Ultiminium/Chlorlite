/* Generic demo driver: records ANY game's on_frame via cc_demo_run. Here it drives
   the Tetris game through the engine's built-in record mode (no bespoke loop). */
#define TETRIS_TEST
#include "../templates/game_tetris/main.c"
int main(int argc,char**argv){
    const char* dir = argc>1?argv[1]:"/tmp/demo_frames2";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=g_w;cfg.height=g_h;cfg.verbose=false;
    cfg.on_frame=on_frame;                 /* the SAME callback cc_run would use */
    CCEngine*e=cc_init(&cfg); g_font=cc_font_builtin(e); tetris_reset(&g_game,0x51E5D);
    CCInputEvent tl[]={
        {10,QKEY_LEFT,1},{12,QKEY_LEFT,0},{30,QKEY_X,1},{32,QKEY_X,0},
        {70,QKEY_DOWN,1},{95,QKEY_DOWN,0},{110,QKEY_SPACE,1},{112,QKEY_SPACE,0},
    };
    uint32_t n=cc_demo_run(e,dir,140,1.0/60.0,tl,sizeof(tl)/sizeof(tl[0]));
    printf("demo_driver: cc_demo_run captured %u frames, score=%d\n",n,g_game.score);
    cc_shutdown(e); return 0;
}
