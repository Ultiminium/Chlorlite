/* Flicker diagnostic: NO game, NO 2D content — just clear the window to a solid,
 * unchanging color every frame and swap. Run the WINDOWED build and watch:
 *   - if this SOLID screen flickers → the flicker is in the swap/present/context
 *     layer (driver, vsync, GLFW window, double-buffer), NOT in CC's rendering.
 *   - if it's rock-steady → the flicker comes from the content/2D path, and this
 *     isolates it out.
 * Build: cc build templates/game_tetris/flicker_diag.c --target <os> -o flicker_diag
 */
#include "cc/claudecore.h"
#include "cc/render.h"

static void on_frame(CCEngine* e, double dt, void* ud){
    (void)dt;(void)ud;
    cc_frame_begin(e);
    cc_draw_rect(e, 0,0, 4000,4000, 0x2266ccff, 0,0);  /* solid blue, covers any window */
    cc_frame_end(e);
}
int main(int argc, char** argv){
    (void)argc;(void)argv;
    CCEngineConfig cfg = cc_default_config();
    cfg.width=640; cfg.height=480; cfg.title="CC flicker diagnostic (solid blue)";
    cfg.on_frame=on_frame; cfg.target_fps=60;
    CCEngine* e = cc_init(&cfg);
    if(!e) return 1;
    cc_run(e);
    cc_shutdown(e);
    return 0;
}
