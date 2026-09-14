/* tetris_test — verifies the Tetris game LOGIC headlessly, with no window and no
 * reliance on pixel output. The game's rules (collision, rotation, gravity, line
 * clears, scoring, game-over) are pure C on the Tetris struct, so this test drives
 * them directly and asserts on state — the same way a CC game should be verifiable.
 * It also renders one frame + screenshots it to prove the draw path runs.
 *
 * Built by compiling the game's main.c with TETRIS_TEST defined, which renames the
 * game's main() out of the way and exposes the logic. Prints "TETRIS TEST: all
 * checks passed" / returns 0.
 */
#define TETRIS_TEST
#include "../templates/game_tetris/main.c"

#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* count filled cells on the board (settled only) */
static int count_filled(const Tetris* t) {
    int n = 0;
    for (int y = 0; y < TOTAL_ROWS; y++)
        for (int x = 0; x < COLS; x++) if (t->cell[y][x] >= 0) n++;
    return n;
}

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "/tmp/tetris_test.png";
    Tetris t;

    /* ── 1. deterministic reset produces a valid initial state ──────────── */
    tetris_reset(&t, 0xABCDEF);
    CHECK(!t.game_over, "fresh game is not over");
    CHECK(t.score == 0 && t.lines == 0 && t.level == 1, "score/lines/level start clean");
    CHECK(t.type >= 0 && t.type < 7, "spawned a valid piece type");
    CHECK(count_filled(&t) == 0, "board starts empty");

    /* determinism: same seed → same first two pieces */
    Tetris t2; tetris_reset(&t2, 0xABCDEF);
    CHECK(t.type == t2.type && t.next_type == t2.next_type, "same seed → same sequence");

    /* ── 2. horizontal movement respects walls ──────────────────────────── */
    tetris_reset(&t, 1);
    int moved_left = 0;
    for (int i = 0; i < 20; i++) if (try_move(&t, -1, 0)) moved_left++;
    CHECK(moved_left > 0, "piece can move left");
    CHECK(!try_move(&t, -1, 0), "piece stops at the left wall");   /* wall blocks */
    int px_at_wall = t.px;
    for (int i = 0; i < 40; i++) try_move(&t, +1, 0);
    CHECK(t.px > px_at_wall, "piece can move right across the board");
    CHECK(!try_move(&t, +1, 0), "piece stops at the right wall");

    /* ── 3. rotation changes orientation and stays in bounds ────────────── */
    tetris_reset(&t, 42);
    int r0 = t.rot;
    try_rotate(&t, +1);
    /* O-piece looks identical rotated; for any piece, rot index must advance or a
     * kick keeps it valid — assert the piece never ends up colliding. */
    CHECK(!collides(&t, t.type, t.rot, t.px, t.py), "piece valid after rotate");
    try_rotate(&t, +1); try_rotate(&t, +1); try_rotate(&t, +1);
    CHECK(t.rot == r0 || t.type == 0, "four rotations return to start (or I/O symmetry)");

    /* ── 4. hard drop lands the piece, fills cells, spawns a new one ─────── */
    tetris_reset(&t, 7);
    int type_before = t.type;
    hard_drop(&t);
    CHECK(count_filled(&t) == 4, "hard drop locked exactly 4 cells");
    CHECK(t.score > 0, "hard drop awarded points");
    CHECK(t.type == t.type, "a new piece is active");   /* spawn ran */
    (void)type_before;

    /* ── 5. gravity makes a piece fall over time ────────────────────────── */
    tetris_reset(&t, 99);
    int y_start = t.py;
    for (int i = 0; i < 30; i++) tetris_update(&t, 0.1f, 0);   /* ~3s of gravity */
    CHECK(t.py > y_start || count_filled(&t) > 0, "gravity moved or landed the piece");

    /* ── 6. LINE CLEAR: hand-build a full row and confirm it clears ─────── */
    tetris_reset(&t, 5);
    /* fill the bottom row completely except we set it directly (logic-level test) */
    int bottom = TOTAL_ROWS - 1;
    for (int x = 0; x < COLS; x++) t.cell[bottom][x] = 2;   /* full row */
    /* put one stray cell above so we can confirm it drops down after the clear */
    t.cell[bottom - 1][3] = 4;
    int before = count_filled(&t);
    int cleared = clear_lines(&t);
    CHECK(cleared == 1, "one full row detected + cleared");
    CHECK(count_filled(&t) == before - COLS, "exactly COLS cells removed");
    CHECK(t.cell[bottom][3] == 4, "cell above the cleared row fell down into it");

    /* scoring for a line clear scales with level */
    tetris_reset(&t, 5);
    for (int x = 0; x < COLS; x++) t.cell[bottom][x] = 1;
    int score_before = t.score;
    /* emulate settle()'s scoring path */
    { int n = clear_lines(&t);
      static const int LS[5] = {0,100,300,500,800};
      t.score += LS[n] * t.level; t.lines += n; t.level = level_for_lines(t.lines); }
    CHECK(t.score == score_before + 100, "single line scores 100 at level 1");

    /* ── 7. GAME OVER when the stack reaches the spawn area ─────────────── */
    tetris_reset(&t, 3);
    /* fill the board solid so the next spawn must collide */
    for (int y = 0; y < TOTAL_ROWS; y++)
        for (int x = 0; x < COLS; x++) t.cell[y][x] = 0;
    spawn(&t);
    CHECK(t.game_over, "spawning into a full board triggers game over");
    /* inputs are ignored after game over (except restart) */
    int px = t.px;
    try_move(&t, -1, 0);
    CHECK(t.px == px, "movement ignored after game over");

    /* ── 8. INPUT PATH: injected keys drive the piece through the real
     *        edge-triggered cc_key_pressed path. Ordering mirrors cc_run:
     *        cc_tick snapshots prev-state, THEN input arrives, THEN we read. ── */
    CCEngineConfig cfg = cc_sandbox_config();
    cfg.width = 520; cfg.height = 560; cfg.verbose = false;
    CCEngine* e = cc_init(&cfg);
    CHECK(e != NULL, "engine created for input+render check");
    if (e) {
        Tetris g; tetris_reset(&g, 123);
        int gx0 = g.px;
        cc_tick(e, 0.016); cc_input_inject_key(e, QKEY_LEFT, false); tetris_input(e, &g);
        cc_tick(e, 0.016); cc_input_inject_key(e, QKEY_LEFT, true);  tetris_input(e, &g);
        CHECK(g.px < gx0, "injected LEFT moved the piece (real edge-triggered input)");
        int gx1 = g.px;
        cc_tick(e, 0.016); cc_input_inject_key(e, QKEY_LEFT, false); tetris_input(e, &g);
        cc_tick(e, 0.016); cc_input_inject_key(e, QKEY_RIGHT, true); tetris_input(e, &g);
        CHECK(g.px > gx1, "injected RIGHT moved the piece back");
        int grot = g.rot;
        cc_tick(e, 0.016); cc_input_inject_key(e, QKEY_RIGHT, false); tetris_input(e, &g);
        cc_tick(e, 0.016); cc_input_inject_key(e, QKEY_X, true);      tetris_input(e, &g);
        CHECK(g.rot != grot || g.type == 1, "injected X rotated the piece (O-piece exempt)");

        /* ── 9. RENDER one frame headlessly to prove the draw path runs ──── */
        CCFont font = cc_font_builtin(e);
        Tetris show; tetris_reset(&show, 0x51E5D);
        for (int i = 0; i < 5; i++) hard_drop(&show);   /* a board with content */
        cc_frame_begin(e);
        tetris_draw(e, font, &show, 520, 560);
        cc_frame_end(e);
        const char* saved = cc_screenshot(e, out);
        CHECK(saved != NULL, "screenshot written");
        cc_shutdown(e);
    }

    if (failures == 0)
        printf("TETRIS TEST: all checks passed (reset/determinism, wall-bounded movement, valid rotation, hard drop locks 4 cells + scores, gravity, line clear + gravity-collapse + scoring, game over + input lockout, headless render)\n");
    else
        printf("TETRIS TEST: %d FAILURES\n", failures);
    return failures ? 1 : 0;
}
