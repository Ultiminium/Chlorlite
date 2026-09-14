# Tetris — a complete Chlorlite game

A full falling-block game built on CC's 2D API (`cc_draw_rect` + `cc_draw_text`):
seven tetrominoes in distinct colors, rotation with wall kicks, gravity with lock
delay, line clears, level-scaling score, next-piece preview, ghost drop, and game
over with restart.

## Controls
- **←/→** move
- **↓** soft drop (faster fall, +score)
- **Space** hard drop (+bonus, locks instantly)
- **Z / X** (or **↑**) rotate
- **R** restart

## Build & run
```sh
bash SKILL_DIR/scripts/cc dev templates/game_tetris/main.c -o /tmp/tetris
/tmp/tetris                       # interactive (opens a window)
unset DISPLAY && /tmp/tetris out.png   # headless: render one frame to a PNG
```

## Design
The game **logic** (board, collision, rotation, gravity, line clears, scoring,
game over) is pure C on a `Tetris` struct with **no engine dependency** — so it's
deterministic and unit-testable. The engine is used only for input, drawing, and
the frame loop. `tests/tetris_test.c` exercises the logic directly (injected
keystrokes through the real edge-triggered input path + state assertions) and
renders a frame; it's a model for how to verify a CC game headlessly.

A 7-bag randomizer with a seeded xorshift RNG makes runs reproducible (same seed →
same piece sequence), which is what lets the test assert on outcomes.

## Rendering note
Text and shapes render crisply because the engine draws at a supersampled
internal resolution (the default PDAA1 = 4×) and box-downsamples to the display
size. 2D drawing is display-space (0..width, 0..height); the engine maps that onto
the supersampled buffer correctly, so the game fills the frame at full quality.

(An earlier bug crammed all 2D output into the top-left 1/4 of the frame whenever
supersampling AA was active — the 2D pass authored coordinates against the
supersampled internal size instead of the display size. Fixed in the renderer's 2D
flush: `uResolution` is now the display resolution. This game surfaced it.)
