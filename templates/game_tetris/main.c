/*
 * Chlorlite — TETRIS
 *
 * A complete, playable falling-block game built entirely on CC's 2D API
 * (cc_draw_rect + cc_draw_text). Seven tetrominoes in distinct colors, rotation,
 * gravity with lock delay, line clears, scoring, levels that speed up, a
 * next-piece preview, and game over with restart.
 *
 * The game LOGIC (board, piece, collision, rotation, line-clear, scoring) is pure
 * C on a Tetris struct with no engine dependency — so it's deterministic and unit-
 * testable. The engine is used only for input, drawing, and the frame loop. That
 * separation is what lets the headless test (tetris_test.c) drive real games by
 * injecting keystrokes and asserting on state, then screenshot the result.
 *
 * Controls:  ←/→ move · ↓ soft drop · Space hard drop · Z/X or ↑ rotate · R restart
 *
 * Build:  bash SKILL_DIR/scripts/cc dev templates/game_tetris/main.c -o /tmp/tetris
 *         interactive (windowed):  /tmp/tetris
 *         headless screenshot:     unset DISPLAY && /tmp/tetris frame.png
 */
#include "cc/claudecore.h"
#include "cc/render.h"
#include "cc/input.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── board dimensions ─────────────────────────────────────────────────── */
#define COLS 10
#define ROWS 20
#define HIDDEN 2                 /* spawn rows above the visible field */
#define TOTAL_ROWS (ROWS + HIDDEN)

/* ── colors (RGBA, 0xRRGGBBAA) — one per tetromino, classic-ish palette ── */
#define C_EMPTY   0x12141cff
#define C_GRID    0x1e2230ff
#define C_BG      0x0b0d13ff
#define C_PANEL   0x151824ff
#define C_TEXT    0xe8ecf4ff
#define C_DIM     0x8a93a8ff
#define C_GHOST   0x2a3550ff

/* piece colors, indexed by piece type 0..6 = I,O,T,S,Z,J,L */
static const uint32_t PIECE_COLORS[7] = {
    0x2ee6e6ff, /* I — cyan   */
    0xf2d24bff, /* O — yellow */
    0xb45cf0ff, /* T — purple */
    0x4be66aff, /* S — green  */
    0xf0524bff, /* Z — red    */
    0x4b7cf0ff, /* J — blue   */
    0xf09a4bff, /* L — orange */
};

/* ── tetromino shapes: 4 rotations × 4 cells, as (x,y) offsets in a 4×4 box ──
 * Encoded as 16-bit masks over a 4×4 grid (bit = row*4 + col). One mask per
 * rotation state. This is compact and rotation is just picking the next mask. */
static const uint16_t PIECE_MASKS[7][4] = {
    /* I */ { 0x0F00, 0x2222, 0x00F0, 0x4444 },
    /* O */ { 0x0660, 0x0660, 0x0660, 0x0660 },
    /* T */ { 0x0E40, 0x4C40, 0x4E00, 0x4640 },
    /* S */ { 0x06C0, 0x8C40, 0x6C00, 0x4620 },
    /* Z */ { 0x0C60, 0x4C80, 0xC600, 0x2640 },
    /* J */ { 0x08E0, 0x6440, 0x0E20, 0x44C0 },
    /* L */ { 0x02E0, 0x4460, 0x0E80, 0xC440 },
};

/* ── game state ───────────────────────────────────────────────────────── */
typedef struct {
    int      cell[TOTAL_ROWS][COLS];  /* -1 empty, else piece color index */
    int      type, rot;               /* current piece */
    int      px, py;                  /* current piece top-left in the 4×4 box */
    int      next_type;
    int      hold_ready;

    int      score, lines, level;
    int      game_over;
    int      paused;

    float    fall_timer;              /* seconds since last gravity step */
    float    lock_timer;              /* lock-delay accumulator when grounded */
    int      grounded;

    uint32_t rng;                     /* deterministic bag RNG */
    int      bag[7], bag_i;           /* 7-bag randomizer */
} Tetris;

/* ── deterministic RNG (xorshift) + 7-bag ─────────────────────────────── */
static uint32_t rng_next(Tetris* t) {
    uint32_t x = t->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (t->rng = x);
}
static void bag_refill(Tetris* t) {
    for (int i = 0; i < 7; i++) t->bag[i] = i;
    for (int i = 6; i > 0; i--) {          /* Fisher–Yates */
        int j = rng_next(t) % (i + 1);
        int tmp = t->bag[i]; t->bag[i] = t->bag[j]; t->bag[j] = tmp;
    }
    t->bag_i = 0;
}
static int bag_pop(Tetris* t) {
    if (t->bag_i >= 7) bag_refill(t);
    return t->bag[t->bag_i++];
}

/* ── piece geometry helpers ───────────────────────────────────────────── */
/* call `fn` (via a small inline loop) for each of the 4 filled cells of a mask */
static int mask_bit(uint16_t m, int r, int c) { return (m >> (r * 4 + c)) & 1; }

/* does the piece (type,rot) placed at (px,py) collide with walls/floor/stack? */
static int collides(const Tetris* t, int type, int rot, int px, int py) {
    uint16_t m = PIECE_MASKS[type][rot & 3];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (!mask_bit(m, r, c)) continue;
            int x = px + c, y = py + r;
            if (x < 0 || x >= COLS || y >= TOTAL_ROWS) return 1;   /* wall/floor */
            if (y < 0) continue;                                   /* above top ok */
            if (t->cell[y][x] >= 0) return 1;                      /* stack */
        }
    return 0;
}

/* lock the current piece into the board */
static void lock_piece(Tetris* t) {
    uint16_t m = PIECE_MASKS[t->type][t->rot & 3];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (mask_bit(m, r, c)) {
                int x = t->px + c, y = t->py + r;
                if (y >= 0 && y < TOTAL_ROWS && x >= 0 && x < COLS)
                    t->cell[y][x] = t->type;
            }
}

/* clear full lines, return how many were cleared */
static int clear_lines(Tetris* t) {
    int cleared = 0;
    for (int y = TOTAL_ROWS - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < COLS; x++) if (t->cell[y][x] < 0) { full = 0; break; }
        if (full) {
            cleared++;
            for (int yy = y; yy > 0; yy--)                 /* shift everything down */
                memcpy(t->cell[yy], t->cell[yy - 1], sizeof(t->cell[0]));
            for (int x = 0; x < COLS; x++) t->cell[0][x] = -1;
            y++;                                           /* recheck this row */
        }
    }
    return cleared;
}

static int level_for_lines(int lines) { return 1 + lines / 10; }
static float gravity_interval(int level) {
    float g = 0.80f - (level - 1) * 0.07f;                 /* speeds up per level */
    return g < 0.05f ? 0.05f : g;
}

/* spawn the next piece; sets game_over if it immediately collides */
static void spawn(Tetris* t) {
    t->type = t->next_type;
    t->next_type = bag_pop(t);
    t->rot = 0;
    t->px = 3;                     /* centered-ish in the 4×4 box */
    t->py = 0;                     /* top (includes hidden rows) */
    t->grounded = 0;
    t->lock_timer = 0;
    if (collides(t, t->type, t->rot, t->px, t->py)) t->game_over = 1;
}

static void tetris_reset(Tetris* t, uint32_t seed) {
    memset(t, 0, sizeof(*t));
    for (int y = 0; y < TOTAL_ROWS; y++)
        for (int x = 0; x < COLS; x++) t->cell[y][x] = -1;
    t->rng = seed ? seed : 0x1234567u;
    t->score = 0; t->lines = 0; t->level = 1; t->game_over = 0;
    bag_refill(t);
    t->next_type = bag_pop(t);
    spawn(t);
}

/* ── player actions (return 1 if the action happened) ─────────────────── */
static int try_move(Tetris* t, int dx, int dy) {
    if (t->game_over) return 0;
    if (!collides(t, t->type, t->rot, t->px + dx, t->py + dy)) {
        t->px += dx; t->py += dy; return 1;
    }
    return 0;
}
/* rotate with simple wall-kick attempts (0, ±1, ±2 columns) */
static int try_rotate(Tetris* t, int dir) {
    if (t->game_over) return 0;
    int nr = (t->rot + (dir > 0 ? 1 : 3)) & 3;
    const int kicks[5] = { 0, -1, 1, -2, 2 };
    for (int k = 0; k < 5; k++) {
        if (!collides(t, t->type, nr, t->px + kicks[k], t->py)) {
            t->px += kicks[k]; t->rot = nr; return 1;
        }
    }
    return 0;
}
/* land the piece: lock, score lines, spawn next. shared by soft-lock + hard drop */
static void settle(Tetris* t) {
    lock_piece(t);
    int n = clear_lines(t);
    if (n > 0) {
        static const int LINE_SCORE[5] = { 0, 100, 300, 500, 800 };
        t->score += LINE_SCORE[n] * t->level;
        t->lines += n;
        t->level = level_for_lines(t->lines);
    }
    spawn(t);
}
static void hard_drop(Tetris* t) {
    if (t->game_over) return;
    int dist = 0;
    while (!collides(t, t->type, t->rot, t->px, t->py + 1)) { t->py++; dist++; }
    t->score += dist * 2;                  /* hard-drop bonus */
    settle(t);
}

/* advance gravity + lock timing by dt seconds */
static void tetris_update(Tetris* t, float dt, int soft_drop) {
    if (t->game_over || t->paused) return;
    float interval = gravity_interval(t->level);
    if (soft_drop) interval *= 0.20f;      /* soft drop = 5× gravity (was 10× — too fast) */

    t->fall_timer += dt;
    /* Step gravity AT MOST ONE row per update. The old `while (fall_timer >=
       interval)` could advance several rows in a single frame when soft-dropping
       (10× speed × frame dt), which looked like the piece "skipping" blocks. One
       step per update keeps motion smooth and readable; excess time is capped so a
       hitch can't bank multiple instant drops. */
    if (t->fall_timer >= interval) {
        t->fall_timer = 0;                 /* don't bank leftover time → no multi-step */
        if (!collides(t, t->type, t->rot, t->px, t->py + 1)) {
            t->py++;
            if (soft_drop) t->score += 1;  /* standard: 1 pt per soft-dropped cell */
            t->grounded = 0; t->lock_timer = 0;
        } else {
            t->grounded = 1;               /* resting on the stack/floor */
        }
    }
    if (t->grounded) {
        t->lock_timer += dt;
        if (t->lock_timer >= 0.5f) {        /* lock delay */
            if (collides(t, t->type, t->rot, t->px, t->py + 1)) settle(t);
            else { t->grounded = 0; t->lock_timer = 0; }
        }
    }
}

/* ghost drop row for the current piece (preview of where it lands) */
static int ghost_py(const Tetris* t) {
    int gy = t->py;
    while (!collides(t, t->type, t->rot, t->px, gy + 1)) gy++;
    return gy;
}

/* ═══ RENDER ═══════════════════════════════════════════════════════════ */
#define CELL_PX   24
#define BOARD_X   40
#define BOARD_Y   40
#define PANEL_X   (BOARD_X + COLS * CELL_PX + 24)

static void draw_cell(CCEngine* e, int cx, int cy, uint32_t color) {
    float x = BOARD_X + cx * CELL_PX, y = BOARD_Y + cy * CELL_PX;
    cc_draw_rect(e, x + 1, y + 1, CELL_PX - 2, CELL_PX - 2, color, 1.5f, 0x00000066);
    /* subtle top highlight for a beveled look */
    cc_draw_rect(e, x + 3, y + 3, CELL_PX - 6, 4, 0xffffff22, 0, 0);
}

static void draw_piece_at(CCEngine* e, int type, int rot, int ox, int oy,
                          int px, int py, uint32_t color) {
    uint16_t m = PIECE_MASKS[type][rot & 3];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (mask_bit(m, r, c)) {
                float x = ox + (px + c) * CELL_PX, y = oy + (py + r) * CELL_PX;
                cc_draw_rect(e, x + 1, y + 1, CELL_PX - 2, CELL_PX - 2, color, 1.5f, 0x00000066);
                cc_draw_rect(e, x + 3, y + 3, CELL_PX - 6, 4, 0xffffff22, 0, 0);
            }
}

static void tetris_draw(CCEngine* e, CCFont font, const Tetris* t, int W, int H) {
    cc_draw_rect(e, 0, 0, (float)W, (float)H, C_BG, 0, 0);

    /* playfield background + grid (only the visible ROWS, skipping HIDDEN) */
    cc_draw_rect(e, BOARD_X - 4, BOARD_Y - 4, COLS * CELL_PX + 8, ROWS * CELL_PX + 8,
                 C_PANEL, 2, 0x000000aa);
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            cc_draw_rect(e, BOARD_X + x * CELL_PX, BOARD_Y + y * CELL_PX,
                         CELL_PX, CELL_PX, C_EMPTY, 1, C_GRID);

    /* settled cells (map board row → visible row by subtracting HIDDEN) */
    for (int y = HIDDEN; y < TOTAL_ROWS; y++)
        for (int x = 0; x < COLS; x++)
            if (t->cell[y][x] >= 0)
                draw_cell(e, x, y - HIDDEN, PIECE_COLORS[t->cell[y][x]]);

    if (!t->game_over) {
        /* ghost */
        int gy = ghost_py(t);
        uint16_t m = PIECE_MASKS[t->type][t->rot & 3];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (mask_bit(m, r, c)) {
                    int vy = (gy + r) - HIDDEN;
                    if (vy >= 0)
                        cc_draw_rect(e, BOARD_X + (t->px + c) * CELL_PX + 2,
                                     BOARD_Y + vy * CELL_PX + 2,
                                     CELL_PX - 4, CELL_PX - 4, C_GHOST, 0, 0);
                }
        /* active piece */
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (mask_bit(m, r, c)) {
                    int vy = (t->py + r) - HIDDEN;
                    if (vy >= 0) draw_cell(e, t->px + c, vy, PIECE_COLORS[t->type]);
                }
    }

    /* ── side panel ─────────────────────────────────────────────────── */
    char buf[64];
    cc_draw_text(e, font, "TETRIS", PANEL_X, BOARD_Y, 34, C_TEXT);

    cc_draw_text(e, font, "SCORE", PANEL_X, BOARD_Y + 60, 18, C_DIM);
    snprintf(buf, sizeof(buf), "%d", t->score);
    cc_draw_text(e, font, buf, PANEL_X, BOARD_Y + 82, 28, C_TEXT);

    cc_draw_text(e, font, "LINES", PANEL_X, BOARD_Y + 130, 18, C_DIM);
    snprintf(buf, sizeof(buf), "%d", t->lines);
    cc_draw_text(e, font, buf, PANEL_X, BOARD_Y + 152, 28, C_TEXT);

    cc_draw_text(e, font, "LEVEL", PANEL_X, BOARD_Y + 200, 18, C_DIM);
    snprintf(buf, sizeof(buf), "%d", t->level);
    cc_draw_text(e, font, buf, PANEL_X, BOARD_Y + 222, 28, C_TEXT);

    /* next-piece preview box */
    cc_draw_text(e, font, "NEXT", PANEL_X, BOARD_Y + 274, 18, C_DIM);
    float nx = PANEL_X, ny = BOARD_Y + 298;
    cc_draw_rect(e, nx, ny, 5 * CELL_PX, 4 * CELL_PX, C_PANEL, 2, 0x000000aa);
    draw_piece_at(e, t->next_type, 0, (int)nx + CELL_PX / 2, (int)ny + CELL_PX / 2,
                  0, 0, PIECE_COLORS[t->next_type]);

    cc_draw_text(e, font, "Arrows / A D: move", PANEL_X, BOARD_Y + 420, 15, C_DIM);
    cc_draw_text(e, font, "Z / X / W: rotate", PANEL_X, BOARD_Y + 440, 15, C_DIM);
    cc_draw_text(e, font, "Space: hard drop", PANEL_X, BOARD_Y + 460, 15, C_DIM);
    cc_draw_text(e, font, "P: pause   R: restart", PANEL_X, BOARD_Y + 480, 15, C_DIM);

    if (t->game_over) {
        cc_draw_rect(e, BOARD_X, BOARD_Y + ROWS * CELL_PX / 2 - 40,
                     COLS * CELL_PX, 80, 0x000000cc, 2, 0xf0524bff);
        cc_draw_text(e, font, "GAME OVER", BOARD_X + 24,
                     BOARD_Y + ROWS * CELL_PX / 2 - 24, 30, 0xf0524bff);
        cc_draw_text(e, font, "press R", BOARD_X + 74,
                     BOARD_Y + ROWS * CELL_PX / 2 + 10, 20, C_TEXT);
    } else if (t->paused) {
        cc_draw_rect(e, BOARD_X, BOARD_Y + ROWS * CELL_PX / 2 - 36,
                     COLS * CELL_PX, 72, 0x000000cc, 2, 0x2ee6e6ff);
        cc_draw_text(e, font, "PAUSED", BOARD_X + 52,
                     BOARD_Y + ROWS * CELL_PX / 2 - 20, 30, 0x2ee6e6ff);
        cc_draw_text(e, font, "press P", BOARD_X + 74,
                     BOARD_Y + ROWS * CELL_PX / 2 + 12, 18, C_DIM);
    }
}

/* ═══ INPUT ════════════════════════════════════════════════════════════ */
static void tetris_input(CCEngine* e, Tetris* t) {
    if (cc_key_pressed(e, QKEY_R)) { tetris_reset(t, (uint32_t)(cc_time(e) * 1000) + 1); return; }
    if (cc_key_pressed(e, QKEY_P)) { t->paused = !t->paused; return; }   /* toggle pause */
    if (t->game_over || t->paused) return;
    if (cc_key_pressed(e, QKEY_LEFT)  || cc_key_pressed(e, QKEY_A)) try_move(t, -1, 0);
    if (cc_key_pressed(e, QKEY_RIGHT) || cc_key_pressed(e, QKEY_D)) try_move(t, +1, 0);
    if (cc_key_pressed(e, QKEY_X) || cc_key_pressed(e, QKEY_UP) || cc_key_pressed(e, QKEY_W))
        try_rotate(t, +1);
    if (cc_key_pressed(e, QKEY_Z))     try_rotate(t, -1);
    if (cc_key_pressed(e, QKEY_SPACE)) hard_drop(t);
}

/* ═══ ENGINE GLUE ══════════════════════════════════════════════════════ */
static Tetris  g_game;
static CCFont  g_font;
static int     g_w = 520, g_h = 560;

static void on_frame(CCEngine* e, double dt, void* ud) {
    (void)ud;
    tetris_input(e, &g_game);
    int soft = cc_key_down(e, QKEY_DOWN);
    tetris_update(&g_game, (float)dt, soft);
    cc_frame_begin(e);
    tetris_draw(e, g_font, &g_game, g_w, g_h);
    cc_frame_end(e);
}

#ifndef TETRIS_TEST   /* the test includes this file for its logic; it owns main() */
int main(int argc, char** argv) {
    int headless = (argc > 1);                 /* a path arg → screenshot mode */
    const char* out = headless ? argv[1] : NULL;

    CCEngineConfig cfg = headless ? cc_sandbox_config() : cc_default_config();
    cfg.width = g_w; cfg.height = g_h; cfg.verbose = false;
    cfg.target_fps = 60;
    cfg.on_frame = on_frame;
    CCEngine* e = cc_init(&cfg);
    if (!e) { printf("tetris: engine init failed\n"); return 1; }

    g_font = cc_font_builtin(e);
    tetris_reset(&g_game, 0x51E5D);

    if (headless) {
        /* deterministic demo: drop a handful of pieces so the screenshot shows a
         * real in-progress board, then write it. */
        for (int i = 0; i < 6; i++) {
            for (int f = 0; f < 8; f++) { tetris_update(&g_game, 0.10f, 0); }
            hard_drop(&g_game);
        }
        cc_frame_begin(e);
        tetris_draw(e, g_font, &g_game, g_w, g_h);
        cc_frame_end(e);
        const char* saved = cc_screenshot(e, out);
        printf("tetris: score=%d lines=%d level=%d wrote=%s\n",
               g_game.score, g_game.lines, g_game.level, saved ? saved : "(null)");
        cc_shutdown(e);
        return 0;
    }

    cc_run(e);            /* interactive: blocking loop drives on_frame */
    cc_shutdown(e);
    return 0;
}
#endif /* TETRIS_TEST */
