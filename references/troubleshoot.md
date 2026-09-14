# Chlorlite — Troubleshooting Reference

Plain-language fixes for the problems that actually come up. Find your symptom, do the fix.
For the normal how-to, see `engine/USAGE.md` (this file is only for when something's wrong).

## The picture is completely black

Most common problem, several causes:
- **You drew nothing.** Make sure you draw something between "frame begin" and "frame end".
- **No light or no camera.** A 3D scene needs at least one light and a camera set each frame.
- **You saved the screenshot too early.** Save the screenshot *after* "frame end".
- **3D specifically:** the lighting pass only runs if some 3D geometry was actually drawn in
  the frame. If you only drew 2D, that's expected; if you drew a 3D model and it's black,
  the draw may not be registering as 3D geometry (this was a real bug with skinned
  characters, now fixed — if you hit it, update the engine).

## A model looks flat, like cardboard / thin ribbon arms

- This is almost always **real** — the geometry is genuinely flat, not a lighting trick.
- **Measure it** (do NOT trust your eyes or a description): run the shape check with Deboog
  or `modelcheck`. If **roundness** is low (near 0), it's flat. Near 1 is a round tube.
- The usual cause: building the shape's rings flat in one plane instead of perpendicular to
  the limb. Fix the geometry so each cross-section ring faces along the limb.

## "It works!" but you're not actually sure

- You skipped the measurement step. **Measure it.** A description or a glance is not proof.
- For shape: Deboog / modelcheck. For placement: canonframe. For a picture: `inspect.py`.
- If any of them prints `[FAIL]`, it is not fixed — report the number, don't explain it away.

## My change didn't take effect

- The build is cached. Clear it and rebuild:
  ```bash
  rm -rf engine/.build-*
  bash scripts/cc dev mygame.c -o mygame
  ```

## Text looks thin / hard to read

- The default font is fine (DejaVu Sans). If it looks thin, an old override font may be in
  `engine/assets/fonts/cc_default.ttf` — replace it with the font you want, or drop a `.ttf`
  in `assets-dev/fonts/` and use it by name.

## Point lights paint black squares on surfaces

- This was a bug: a point light with **no range set** caused a divide-by-zero → NaN → black.
- Fixed in the engine (range now guarded). If you still see it, make sure every point light
  has a `range` value set, and update to the latest engine.

## Input feels wrong / presses missed / fast taps dropped

- Input is **event-driven** and read **once per frame**. Read it at the right point in your
  loop. The correct pattern is shown in `tests/input_edge_test.c`.
- A "press fires once, held doesn't re-fire, fast tap still counts" contract is what you
  want; that test proves it.

## The build fails with errors

- Read the **FIRST** error only. Later errors are usually caused by the first one. Fix that,
  rebuild.
- If it complains it can't find something like `qwerty`, note that `qwerty/` is now a
  top-level folder (a sibling of `engine/`), not inside `engine/`. The `scripts/cc` build
  already knows this; only the raw CMake path needed the fix (already applied).

## Bundling a game fails to link ("undefined reference to cc_init")

- This is almost always a **stale build cache**, not a real problem — the engine library was
  built before a recent engine change. Clear it and rebuild:
  ```bash
  rm -rf engine/.build-*
  bash scripts/cc bundle mygame --target linux -o mygame
  ```
- **Multi-file (directory) games work fine** — you can bundle a single `.c` file or a folder
  of several `.c` files. (An earlier note claimed directories were broken; that was a
  misdiagnosed stale cache, now corrected.)

## Renders are very slow

- Full effects in software (OSMesa) are slow by nature. For quick checks:
  - Turn off anti-aliasing: `cc_aa_set_mode(e, CC_AA_OFF)`.
  - Turn off heavy post-effects (bloom, SSGI, SSR, depth-of-field) while iterating.
  - Capture fewer frames when recording motion.

## Headless picking or debug gizmos don't seem to work

- Debug *views* and JSON state dumps work headless. If `cc_debug_pick` returns nothing:
  it reads the list of things drawn this frame, which resets at frame start — so call it
  *after* you draw, within the same frame. Gizmos need `cc_debug_overlay` called to actually
  draw them before the frame ends.

## I want to check where something is on screen exactly

- Use the **canonical frame** (`canonframe` / `tools/canongrid.c`). It gives an exact
  coordinate `±XXX-±YYY:rotation` from a fixed center that never moves — so "is it centered"
  becomes a number. It also warns if the rendered position disagrees with the math (a bug).

## Golden rule when stuck

Build → run → **look at the picture** → **measure it** → fix → repeat. Don't reason in the
abstract about how it "should" look. Render it and measure it. The data is the truth.
