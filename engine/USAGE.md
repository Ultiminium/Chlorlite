# Chlorlite USAGE — how to actually do everything

This is the **how-to**. It is written to be dummy-proof: follow the steps exactly, in
order, and things work. Each task is a recipe. Copy the commands as written.

Two things to know before you start:
1. Every command is run from the **top folder** of this skill (the folder that contains
   `engine/`, `tools/`, `tests/`, `deboog/`). That folder is called `SKILL_DIR` below —
   just replace `SKILL_DIR` with the actual path if needed, or `cd` into it first.
2. There is usually **no screen**. So you don't "watch" a game — you **run it, make it take
   a picture, and look at the picture.** This is normal and it is how everything is checked.

The build tool is `scripts/cc`. You run it like: `bash scripts/cc <command> ...`.

---

## TASK 0 — The golden loop (memorize this)

Almost everything is this five-step loop:

1. **Write** a game file (a `.c` file describing the game).
2. **Build** it for headless viewing: `bash scripts/cc dev mygame.c -o mygame`
3. **Run** it so it makes a picture: `./mygame /tmp/shot.png`
4. **Look** at the picture (open `/tmp/shot.png`).
5. **Measure** it if it's a model or placement (see Task 6), then fix and repeat.

That's it. Build → run → look → measure → fix. Everything below is a variation of this.

---

## TASK 1 — Build a game so you can see it (headless)

Use the **`dev`** build. This is the one that works without a screen and lets you take
pictures.

```bash
bash scripts/cc dev path/to/mygame.c -o mygame
```

This produces a program called `mygame`. Now run it and tell it where to save a picture:

```bash
./mygame /tmp/shot.png
```

Then open `/tmp/shot.png` and look. If you see your scene, it worked.

**If the build prints errors:** read the FIRST error only (later ones are usually caused by
the first). Fix that, rebuild. Don't chase all of them at once.

**If a change doesn't seem to take effect:** the build may be cached. Clear it and rebuild:

```bash
rm -rf engine/.build-*
bash scripts/cc dev path/to/mygame.c -o mygame
```

---

## TASK 2 — Run an existing test to see a feature

The `tests/` folder has 85 ready-made programs, one per feature (see `engine/README.md`
for the full list). To see any feature, build and run its test:

```bash
bash scripts/cc dev tests/shadow_test.c -o /tmp/shadow_test
./tmp/shadow_test /tmp/shadow.png     # then look at /tmp/shadow.png
```

Some tests are **logic tests** (they print PASS/FAIL instead of making a picture), like
`combat_test`, `timeline_test`, `loop_test`, `event_test`. For those, just run and read the
printed result:

```bash
bash scripts/cc dev tests/combat_test.c -o /tmp/combat_test
./tmp/combat_test          # prints "all checks passed" or the failures
```

**Tip:** if a test seems slow, it's doing full-quality rendering in software. That's normal.
Give it time or reduce the scene.

---

## TASK 3 — See MOTION, not just one frame (record gameplay)

One picture can't show movement. To see motion, capture a **series of frames** and stitch
them into a contact sheet (a grid of frames like a flip-book) and a video.

The engine can run a game headless while feeding it scripted inputs and saving every frame.
Then assemble them:

```bash
# after your game has written frames to /tmp/frames/ as frame_000000.png, ...
python3 tools/make_gameplay_video.py /tmp/frames /tmp/gameplay --fps 30 --sheet-cols 8 --sheet-step 6
```

This writes `/tmp/gameplay.mp4` (a video), `/tmp/gameplay.gif`, and
`/tmp/gameplay_contact_sheet.png` (the grid). **Look at the contact sheet** — it shows the
whole motion in one image, which is how you check that things actually move correctly.

(For how to make your game emit those frames, see the demo drivers in `tools/`:
`tetris_demo.c`, `demo_driver.c`.)

---

## TASK 4 — Add assets (models, textures, fonts, sounds)

Assets you want to use live in the **development asset library**: the `assets-dev/` folder,
sorted by type:
- `assets-dev/fonts/` — fonts (`.ttf`)
- `assets-dev/models/` — 3D models (`.gltf`, `.glb`, `.obj`, `.ccmodel`)
- `assets-dev/textures/` — images (`.png`, `.jpg`)
- `assets-dev/anims/` — animation clips

**To use an asset:** put the file in the right folder, then in your game refer to it by its
plain name (for example, ask for `hero.png`). The engine finds it in the library while you
build, and finds it in the shipped game's own folder after you ship.

**The nice part:** when you ship a game (Task 7), the engine automatically copies **only the
assets your game actually uses** into the finished game. The library can hold hundreds of
files; the shipped game only carries what it needs.

---

## TASK 5 — Fix the font / it looks bad

The default text font is already a clean, readable font (DejaVu Sans). If you want a
different one, drop a `.ttf` file into `assets-dev/fonts/` and use it by name, or replace
`engine/assets/fonts/cc_default.ttf` to change the default everywhere.

---

## TASK 6 — CHECK that something is actually correct (do NOT skip this)

**This is the most important task.** The number-one mistake is claiming something is fixed
or looks good when it isn't. Words and glances are not proof. Measure.

### 6a — Check a character/model's SHAPE (is it round, valid, correct?)

The shape truth comes from the model's actual structure, not a picture. Use `modelcheck`
(link your model into it via the `build_model()` hook), or use the standalone **Deboog**
tool which does the same math with no engine at all:

```bash
# Deboog is standalone — build it once:
cd deboog && make test        # proves it works; builds libdeboog.a
cd ..
```

Deboog answers, with exact numbers: is this mesh valid (no holes, no broken triangles)? is
this limb **round or a flat ribbon** (roundness 1.0 = round tube, 0.0 = flat cardboard)?
are there NaN/broken numbers hiding? To run it on a Chlorlite model, use the adapter
`tools/deboog_chlorlite.c` (it unpacks a model and calls Deboog).

**The rule:** if Deboog or modelcheck reports a `[FAIL]`, the model is NOT good — report the
number, do not describe it away. "Roundness 0.05" means flat, no matter what it looks like.

### 6b — Check WHERE something is / how it's turned (placement, rotation)

Use the **canonical frame** (`canonframe`, or the `tools/canongrid.c` demo). It gives any
object an exact coordinate on a fixed 580×720 grid whose center never moves, written as
`±XXX-±YYY:rotation`. So "is it centered?" becomes "it's at `+000-+000`" (yes) or
`+207-+000` (no, it's 207 to the right). It also compares the math-truth position against
the pixels and warns if they disagree (a rendering bug).

### 6c — Check a rendered picture (the addon check)

`tools/inspect.py` measures a screenshot of an isolated model (arm thickness, proportions,
symmetry, gaps) and draws the numbers on the image:

```bash
python3 tools/inspect.py /tmp/shot.png --figure
```

Use this as a *cross-check*, not the main proof. The main proof is the geometry (6a/6b).

**Diff two pictures to prove a change is real:**

```bash
python3 tools/inspect.py before.png after.png --diff
```

If it says "NEARLY IDENTICAL", your fix changed nothing — you didn't fix what you thought.

---

## TASK 7 — Ship the finished game (make a standalone program)

When the game is done, build a **shippable** version — a self-contained program you can give
to someone. Choose the target platform:

```bash
# for Linux:
bash scripts/cc bundle path/to/mygame.c --target linux -o mygame

# for Windows:
bash scripts/cc bundle path/to/mygame.c --target windows -o mygame
```

This produces a zip (like `mygame-linux.zip`) containing the program, its `assets/` folder
(only the assets the game uses — see Task 4), and a README. Unzip it anywhere and run.

**Multi-file games work fine.** You can pass a single `.c` file OR a directory of several
`.c` files — both build and bundle correctly. (If a directory build ever fails with
"undefined reference", it's a stale build cache, not a real problem — run
`rm -rf engine/.build-*` and rebuild.)

---

## TASK 8 — Common problems and what to do

- **The picture is all black.** Something wasn't drawn or wasn't lit. Check you added a
  light and a camera, that you drew something between "frame begin" and "frame end", and
  that you saved the screenshot after "frame end". (For 3D specifically, the lighting pass
  only runs if some 3D geometry was drawn.)
- **Text looks thin/ugly.** See Task 5.
- **A model looks flat / like cardboard.** Run the shape check (Task 6a). If roundness is
  low, the geometry is genuinely flat — it's not a lighting issue; fix the model.
- **"It works" but you're not sure.** You skipped Task 6. Measure it.
- **A change didn't take effect.** Clear the build cache: `rm -rf engine/.build-*`.
- **Input feels wrong.** Input is event-driven and read once per frame; make sure you read
  it at the right point in your loop (see `input_edge_test.c` for the correct pattern).
- **Point lights make black squares.** This was a bug (a light with no range set); it's
  fixed, but if you see it, make sure your point lights have a `range` set.
- **Slow renders.** Full effects in software are slow. Turn off anti-aliasing
  (`cc_aa_set_mode(e, CC_AA_OFF)`) and heavy post-effects for quick checks; capture fewer
  frames.

---

## TASK 9 — The absolute minimum, if you only remember one thing

```bash
# 1. build it so you can see it
bash scripts/cc dev mygame.c -o mygame
# 2. run it and make a picture
./mygame /tmp/shot.png
# 3. LOOK at /tmp/shot.png
# 4. before saying it's good, MEASURE it (Task 6). A FAIL means it's not fixed.
```

Build → run → look → measure. Never claim it's correct without the measurement.
