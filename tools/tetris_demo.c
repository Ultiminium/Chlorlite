/* tetris_demo — LIVE GAMEPLAY RECORD HARNESS.
 *
 * The static-screenshot viewer (one frame, one injected key) can't show motion, so
 * interactive bugs (flicker, soft-drop skipping, dropped inputs) were invisible to
 * headless testing. This runs the REAL game loop headless at a fixed timestep,
 * drives it with a scripted INPUT TIMELINE (key events at specific frames, exactly
 * as cc_run would deliver them), and captures EVERY frame to a numbered PNG. Those
 * frames assemble into a contact sheet + MP4 (see tools/make_gameplay_video.py) so
 * gameplay can actually be watched and inspected frame-accurately.
 *
 * This is the "live-gameplay capable" viewer: not real-time-interactive, but a
 * faithful recording of real play over a real input timeline — which is what's
 * needed to SEE motion, timing, and input response without a physical screen.
 *
 *   tetris_demo <out_dir>   → writes <out_dir>/frame_000000.png ...
 */
#define TETRIS_TEST                     /* pull in the game logic, suppress its main */
#include "../templates/game_tetris/main.c"
#include <stdio.h>
#include <string.h>

/* one scripted input event: at frame `f`, set key `k` to down/up */
typedef struct { int frame; QKey key; int down; } InputEvent;

/* A short scripted match that exercises the things static screenshots can't show:
   moving left/right (does the piece track?), rotating (Z/X), soft drop (does it
   skip rows?), hard drop (Space), and a pause toggle. Frame numbers assume 60fps. */
static const InputEvent SCRIPT[] = {
    {  10, QKEY_LEFT,  1 }, {  12, QKEY_LEFT,  0 },     /* tap left */
    {  20, QKEY_LEFT,  1 }, {  22, QKEY_LEFT,  0 },     /* tap left again */
    {  30, QKEY_X,     1 }, {  32, QKEY_X,     0 },     /* rotate CW */
    {  45, QKEY_RIGHT, 1 }, {  47, QKEY_RIGHT, 0 },     /* tap right */
    {  55, QKEY_RIGHT, 1 }, {  57, QKEY_RIGHT, 0 },
    {  70, QKEY_DOWN,  1 },                             /* hold soft drop... */
    {  95, QKEY_DOWN,  0 },                             /* ...release */
    { 110, QKEY_SPACE, 1 }, { 112, QKEY_SPACE, 0 },     /* hard drop */
    { 120, QKEY_Z,     1 }, { 122, QKEY_Z,     0 },     /* rotate CCW (new piece) */
    { 140, QKEY_LEFT,  1 }, { 142, QKEY_LEFT,  0 },
    { 150, QKEY_SPACE, 1 }, { 152, QKEY_SPACE, 0 },     /* hard drop */
    { 170, QKEY_P,     1 }, { 172, QKEY_P,     0 },     /* pause */
    { 185, QKEY_P,     1 }, { 187, QKEY_P,     0 },     /* unpause */
    { 200, QKEY_SPACE, 1 }, { 202, QKEY_SPACE, 0 },
};
static const int SCRIPT_N = (int)(sizeof(SCRIPT)/sizeof(SCRIPT[0]));
#define TOTAL_FRAMES 220

int main(int argc, char** argv) {
    const char* out_dir = argc > 1 ? argv[1] : "/tmp/tetris_demo";

    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = g_w; cfg.height = g_h; cfg.verbose = false;
    CCEngine* e = cc_init(&cfg);
    if (!e) { printf("demo: init failed\n"); return 1; }
    g_font = cc_font_builtin(e);
    tetris_reset(&g_game, 0x51E5D);

    const double dt = 1.0 / 60.0;
    char path[512];
    int applied = 0;

    for (int frame = 0; frame < TOTAL_FRAMES; frame++) {
        /* deliver this frame's scripted input BEFORE begin_frame, exactly like the
           real loop: poll → begin_frame → tick → on_frame → end_frame. */
        for (int i = 0; i < SCRIPT_N; i++)
            if (SCRIPT[i].frame == frame) {
                cc_input_inject_key(e, SCRIPT[i].key, SCRIPT[i].down != 0);
                applied++;
            }
        cc_input_begin_frame(e);
        cc_tick(e, dt);

        /* the game's own per-frame logic (mirrors tetris on_frame) */
        tetris_input(e, &g_game);
        int soft = cc_key_down(e, QKEY_DOWN);
        tetris_update(&g_game, (float)dt, soft);

        cc_frame_begin(e);
        tetris_draw(e, g_font, &g_game, g_w, g_h);
        cc_frame_end(e);

        snprintf(path, sizeof(path), "%s/frame_%06d.png", out_dir, frame);
        cc_screenshot(e, path);

        cc_input_end_frame(e);
    }

    printf("demo: wrote %d frames to %s (script events applied=%d, final score=%d lines=%d)\n",
           TOTAL_FRAMES, out_dir, applied, g_game.score, g_game.lines);
    cc_shutdown(e);
    return 0;
}
