# Chlorlite — Architecture Reference (how the pieces fit)

This explains how the whole skill is laid out and how the big parts connect. It is written
plainly. For *what each feature does*, see `engine/README.md`. For *how to do things*, see
`engine/USAGE.md`. For *what the engine is*, see the top-level `SKILL.md`.

## The folder layout (current, accurate)

```
cc-engine/                     ← the top folder (this is "SKILL_DIR")
├── SKILL.md                   ← what the engine IS (start here)
├── engine/
│   ├── README.md              ← catalog of every feature + every test
│   ├── USAGE.md               ← step-by-step how-to for everything
│   ├── CMakeLists.txt         ← the formal build description
│   ├── include/cc/            ← the 51 feature "headers" (what each feature offers)
│   │   ├── claudecore.h       ← the single include that pulls in the engine
│   │   ├── render.h           ← drawing (3D + 2D, textures, lights, camera)
│   │   ├── ecs.h              ← the object bookkeeping system
│   │   ├── physics.h          ← rigid-body physics
│   │   ├── anim.h             ← skeletal animation + IK
│   │   ├── combat.h           ← game-feel (hitstop, shake, knockback, hitstun)
│   │   ├── timeline.h         ← generic phase/window timelines (frame data)
│   │   ├── canonframe.h       ← fixed canonical coordinate frame (verification)
│   │   ├── assetpack.h        ← asset bundles + loading
│   │   └── ... (44 more, all listed in engine/README.md)
│   ├── src/                   ← the actual engine code (what makes the headers work)
│   │   ├── engine.c           ← lifecycle, the main loop, input, timing
│   │   ├── renderer.c         ← the OpenGL/OSMesa deferred PBR renderer
│   │   └── ... (one file per system)
│   └── assets/fonts/          ← the default font lives here
├── qwerty/                    ← STANDALONE input library (keyboard/mouse/gamepad)
├── netkit/                    ← STANDALONE networking library (transport/prediction)
├── deboog/                    ← STANDALONE math/geometry debugging library
├── assets-dev/                ← development asset library (fonts/models/textures/anims)
├── scripts/
│   └── cc                     ← THE build tool (build, dev, run, bundle)
├── tools/                     ← helper programs (verification, video, model checks)
│   ├── modelcheck.c           ← check a model's shape from its geometry
│   ├── canongrid.c            ← canonical-coordinate demo
│   ├── inspect.py             ← measure a rendered screenshot (addon check)
│   ├── make_gameplay_video.py ← turn frames into a video + contact sheet
│   └── deboog_chlorlite.c     ← bridge a Chlorlite model into Deboog
├── templates/                 ← starter/example games
├── tests/                     ← 85 tests, one per feature
└── references/                ← this file + troubleshooting
```

Two important structural facts:
1. **`qwerty/`, `netkit/`, and `deboog/` are top-level, standalone libraries** — not
   buried inside the engine. Each can be lifted out and used in another project on its own.
   The dependency points one way: the engine uses them; they don't use the engine.
2. **The build tool is `scripts/cc`.** Older docs mentioned `bootstrap.sh`,
   `build_game.sh`, `run_headless.sh` — those are superseded. Use `scripts/cc`.

## How a game is built (the pipeline)

```
your game.c  ──(scripts/cc dev)──►  headless program  ──(run)──►  screenshot.png
             ──(scripts/cc bundle)─►  shippable zip (program + only-used assets)
```

- `scripts/cc dev` makes a **headless** build (software rendering via OSMesa) so it runs
  with no screen and can take screenshots. This is how Claude sees output.
- `scripts/cc bundle` makes a **shippable** build for Linux or Windows, and auto-copies only
  the assets the game actually references (via `cc_asset("name")`) into its `assets/` folder.

## How the systems layer (bottom to top)

```
                 verification (debug, canonframe, Deboog)  ← keeps everyone honest
   ┌───────────────────────────────────────────────────────────────┐
   │  gui / worldui / nineslice        (interface on screen)        │
   │  combat / timeline                (action feel + timing)       │
   │  ai / director                    (enemies + pacing)           │
   │  anim   audio/soundfield          (bending characters, sound)  │
   │  render   physics/move/player   input   (draw, simulate, listen)│
   │  event                            (the shared announcer)       │
   │  ecs / actor / world / group      (object bookkeeping)         │
   │  ccmath                           (the math underneath all)    │
   └───────────────────────────────────────────────────────────────┘
```

Read it bottom-up: **ccmath** is the foundation. **ecs** organizes every object. **render**
draws, **physics/move/player** move things, **input** listens. **event** is the message bus
that lets any system react to any other (combat, physics, and interactions all post events).
Higher up, **anim/audio/ai/combat** add life and feel, and **gui/worldui** put the interface
on screen. Off to the side, the **verification** tools measure the truth so nobody has to
guess whether something is right.

## The runtime environment

- Runs in **Claude's Ubuntu environment** (sandbox / Cowork). Not a general consumer app.
- Renders with **OpenGL 3.3 via OSMesa** — software, headless, no GPU or display needed.
  This is the always-available path. A windowed GLFW path exists if a display is present.
- Because there's no screen by default, **screenshots are how output is seen and verified.**

## The verification philosophy (the most important architectural choice)

The engine is built around one hard rule: **the data is the truth; a render is a sanity
check; a description is neither.** Concretely:
- **Geometry/shape** truth comes from the model's actual vertices → `modelcheck` / **Deboog**.
- **Placement/rotation** truth comes from the object's transform → `canonframe`.
- **Rendered pixels** are only ever a cross-check (`inspect.py`), never the main proof.

This exists because the single most common failure is claiming something is correct because
it *looks* correct — and pixels can be misread. Measuring the data removes the guesswork.
