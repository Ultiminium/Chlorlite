/* play_from_pixels — CLOSED-LOOP play: the program does NOT read game state. It
 * only gets the rendered FRAMEBUFFER PIXELS via cc_frame_pixels, analyzes them to
 * locate the falling piece and the stack height per column, decides a move, injects
 * it, and steps the next frame. This is the "actually USE the viewer" version:
 * perceive → decide → act, in-process, every frame — not record-then-watch.
 *
 * It proves the viewer is usable for real: if decisions based purely on pixels
 * visibly improve play (piece steered toward low columns, board fills evenly), the
 * perception loop works. It captures frames too so the result can be watched.
 *
 *   play_from_pixels <out_dir>
 */
#define TETRIS_TEST
#include "../templates/game_tetris/main.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* The play area on screen (from the game's layout constants), in DISPLAY px.
 * cc_frame_pixels returns the INTERNAL (supersampled) buffer, so we scale by ss. */
static int SS = 1;   /* supersample factor, derived at runtime from frame w vs g_w */

/* Is a pixel "a filled cell" (bright, saturated) vs board background (dark)? */
static int is_bright(const uint8_t* px) {
    int r=px[0], g=px[1], b=px[2];
    int mx = r>g?(r>b?r:b):(g>b?g:b);
    return mx > 110;   /* cells are vivid; C_EMPTY/C_GRID are ~0x12-0x1e */
}

/* Sample the board grid straight from the framebuffer: for each of COLS×ROWS cells,
 * read the pixel at the cell center and mark filled/empty. Returns via `grid`. */
static void perceive_board(CCEngine* e, int grid[ROWS][COLS]) {
    uint32_t fw=0, fh=0; uint8_t* px=NULL;
    cc_frame_pixels(e, &px, &fw, &fh);
    if (!px || !fw) { memset(grid,0,sizeof(int)*ROWS*COLS); return; }
    SS = (int)(fw / (uint32_t)g_w); if (SS < 1) SS = 1;
    for (int r=0;r<ROWS;r++)
        for (int c=0;c<COLS;c++) {
            int dx = BOARD_X + c*CELL_PX + CELL_PX/2;   /* display-space cell center */
            int dy = BOARD_Y + r*CELL_PX + CELL_PX/2;
            int sx = dx*SS, sy = dy*SS;                 /* → supersampled buffer */
            if (sx<0||sy<0||sx>=(int)fw||sy>=(int)fh){ grid[r][c]=0; continue; }
            const uint8_t* p = px + ((size_t)sy*fw + sx)*4;
            grid[r][c] = is_bright(p);
        }
}

/* From the perceived grid, find the falling piece's horizontal center (topmost
 * filled cells = the active piece, since the stack is at the bottom) and the
 * surface height of each column (# empty rows above the first filled cell). */
static int piece_center_col(int grid[ROWS][COLS]) {
    int minr=ROWS; for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++) if(grid[r][c]&&r<minr)minr=r;
    if (minr==ROWS) return -1;
    int lo=COLS, hi=-1;
    for(int r=minr;r<minr+2 && r<ROWS;r++)
        for(int c=0;c<COLS;c++) if(grid[r][c]){ if(c<lo)lo=c; if(c>hi)hi=c; }
    return hi<0 ? -1 : (lo+hi)/2;
}
/* lowest (deepest) column = column whose stack surface is furthest down = best
 * target to keep the board flat. Returns target column. */
static int lowest_column(int grid[ROWS][COLS]) {
    int best=0, best_surface=-1;
    for(int c=0;c<COLS;c++){
        int surf=ROWS; for(int r=0;r<ROWS;r++) if(grid[r][c]){surf=r;break;}
        /* ignore the top few rows where the active piece is */
        if (surf < 4) surf = ROWS;
        if (surf>best_surface){best_surface=surf;best=c;}
    }
    return best;
}

int main(int argc,char**argv){
    const char* dir = argc>1?argv[1]:"/tmp/pixel_play";
    CCEngineConfig cfg=cc_sandbox_config(); cfg.width=g_w;cfg.height=g_h;cfg.verbose=false;
    cfg.on_frame=NULL;   /* we drive the frame ourselves so we can read pixels mid-loop */
    CCEngine*e=cc_init(&cfg); g_font=cc_font_builtin(e); tetris_reset(&g_game,0x51E5D);

    const double dt=1.0/60.0;
    char path[512];
    int decisions=0, moves_left=0, moves_right=0;

    for (int frame=0; frame<420; frame++) {
        /* ---- perceive: read the board straight from rendered pixels ---- */
        int grid[ROWS][COLS];
        perceive_board(e, grid);

        /* ---- decide: steer the falling piece toward the lowest column ---- */
        int pc = piece_center_col(grid);
        int tgt = lowest_column(grid);
        QKey act = QKEY_NONE; int have_act=0;
        if (pc>=0) {
            if (pc>tgt){ act=QKEY_LEFT; have_act=1; moves_left++; }
            else if (pc<tgt){ act=QKEY_RIGHT; have_act=1; moves_right++; }
            else if ((frame%18)==0){ act=QKEY_SPACE; have_act=1; }  /* aligned → drop */
            if (have_act) decisions++;
        }

        /* ---- act: inject the decided key as a one-frame tap ---- */
        if (have_act) cc_input_inject_key(e, act, true);
        cc_input_begin_frame(e);
        cc_tick(e, dt);
        tetris_input(e, &g_game);
        tetris_update(&g_game, (float)dt, 0);
        cc_frame_begin(e);
        tetris_draw(e, g_font, &g_game, g_w, g_h);
        cc_frame_end(e);
        snprintf(path,sizeof(path),"%s/frame_%06d.png",dir,frame);
        cc_screenshot(e, path);
        cc_input_end_frame(e);
        if (have_act) cc_input_inject_key(e, act, false);
    }

    printf("play_from_pixels: %d decisions from pixels (L=%d R=%d), final score=%d lines=%d\n",
           decisions, moves_left, moves_right, g_game.score, g_game.lines);
    cc_shutdown(e);
    return 0;
}
