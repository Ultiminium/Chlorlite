# Chlorlite Engine — Feature & Test Catalog

This document is the **catalog**. It explains, in plain language, **every feature** the
engine has and how the features relate to one another, and then explains **every test** —
the small programs in `tests/` that each prove one feature works.

- To learn *what the engine is*, read the top-level `SKILL.md`.
- To learn *how to do things* step by step, read `engine/USAGE.md`.
- This file tells you *what exists and how it fits together*.

Every feature is a "header file" in `engine/include/cc/` (a header is the list of things a
feature can do). There are 51 of them. Every test is a file in `tests/` (there are 85).

---

# PART 1 — THE FEATURES

The features are grouped by what they're for. Within each group, each feature is named,
described in plain words, and connected to the features it works with.

## Group A — Drawing the picture (rendering)

This is the heart of the engine: turning a described world into an image.

- **render** — The main drawing system. It draws 3D shapes and 2D shapes, loads images
  (textures) and materials, places lights, sets the camera, and produces the final picture.
  Almost everything visual goes through here. It uses a "deferred PBR" pipeline, which is a
  professional way of drawing that handles many lights and realistic surfaces well.
- **camera** — The eye. Controls where you're looking from and how (first-person,
  follow-behind, orbit around a point, fixed path). Camera *shake* for impact lives here
  too. It is a *mechanism*: the game supplies the behavior, the camera system runs it.
- **aa** (anti-aliasing) — Smooths the jagged staircase edges on shapes so lines look
  clean. Several quality levels; can be turned off for speed.
- **material** (in render) & **pbrset** — How surfaces look: color, shininess,
  metal-vs-cloth, bumps. A "PBR set" is a folder of scanned real-world surface images
  (like real brick or metal) the engine can load.
- **ibl** (image-based lighting, in render/sky) — Lighting a scene using the sky/environment
  so metal and shiny things reflect their surroundings believably.
- **pixshape** — A precise model of what a pixel actually is (treating a pixel as a shape
  with real coverage, not a crude square), used to make edges and thin shapes accurate.

How they relate: **render** is the center. **camera** decides the viewpoint into render.
**aa** cleans render's output. **material/pbrset/ibl** feed render better surfaces and
light. **pixshape** sharpens render's edge math.

## Group B — Making characters and objects move and bend

- **anim** (animation) — Makes characters bend and move: a skeleton of bones inside a
  character, poses, blending between poses, and "IK" (making a hand reach a target, a foot
  plant on the ground). This is what separates a walking person from a sliding statue.
- **ccmodel** — The engine's own file format for a 3D model, including its bones and how
  its skin attaches to them. Models can be saved to and loaded from this format.
- **gltf** (in ccmodel) — Imports models made in standard 3D programs (the glTF format),
  so real artwork can be brought in.
- **editmesh** — A way to build and edit 3D shapes by their structure (half-edge topology),
  for making or modifying geometry directly.

How they relate: **ccmodel** stores a character's shape + skeleton; **anim** moves that
skeleton; **gltf** imports outside models into ccmodel; **editmesh** builds shapes from
scratch. All of them ultimately hand geometry to **render** to be drawn.

## Group C — How things behave (simulation & rules)

- **physics** — Real object behavior: gravity, falling, bouncing, colliding, being pushed.
  A rigid-body world.
- **move** — Collide-and-slide movement: sliding along walls instead of stopping dead or
  passing through. The everyday feel of moving in a 3D game.
- **player** — The character controller: ties the player's input to a moving character
  that respects the world.
- **ecs** — The engine's core bookkeeping ("entity component system"). Every object in the
  game is an "entity"; its parts (position, look, health) are "components". This is the
  organized, fast way the engine tracks thousands of objects.
- **actor** — A friendly handle over the ECS, so an object can be treated as one thing
  ("the player", "enemy 3") instead of scattered parts.
- **scene_graph / world / group** — Ways of organizing objects: parent/child relationships,
  spatial organization (so the engine only deals with what's nearby), and tagging objects
  into groups.
- **prefab** — A template for an object. Define "a tree" once, then stamp out many.

How they relate: **ecs** is the foundation; **actor** is the friendly front for it.
**physics** and **move** update where things are; **player** connects the human to a
character; **world/group/scene_graph** organize the objects; **prefab** mass-produces them.

## Group D — The player's controls (input)

- **input** — Reads game controllers (gamepads).
- **input_map** — Lets you bind actions ("jump") to buttons, so the game talks about
  "jump" rather than "the space bar".
- **inputrec** — Records the player's inputs and can replay them exactly. Great for testing
  and for reproducing a bug.
- (**qwerty**, in the top-level `qwerty/` folder) — The low-level keyboard/mouse/gamepad
  reader. It is a *standalone library* usable outside this engine.

How they relate: **qwerty** reads the raw buttons; **input** tracks gamepads; **input_map**
turns raw buttons into named actions; **inputrec** can record/replay the whole stream.

## Group E — Sound

- **audio** — Plays music and sound effects, positioned in the world.
- **audiofx** — Shapes sound by the environment: walls muffle it, spaces add echo.
- **soundfield** — Models how sound travels through a space, including so that AI enemies
  can "hear" the player around corners.
- **mic** — Captures and analyzes microphone input.

How they relate: **audio** plays sounds; **audiofx** colors them by environment;
**soundfield** spreads them through the world (and feeds hearing-based AI); **mic** listens.

## Group F — Enemies and pacing (AI)

- **ai** — Enemy intelligence: finding paths through a level (A* pathfinding) and following
  them, plus basic decision-making.
- **director** — A "pacing director" that adjusts difficulty and spawns challenges to keep
  the game tense but fair.

How they relate: **ai** drives individual enemies (partly using **soundfield** to hear);
**director** orchestrates the overall challenge across many enemies.

## Group G — Combat feel and timing (what makes action good)

- **combat** — The game-feel layer: on a hit, it can freeze time briefly (hitstop), shake
  the screen, knock the target back, and stun it — all as adjustable numbers. It is fully
  modular: every reaction can be turned on or off, so it fits a punchy action game or a
  realistic one. This is the direct answer to "AI can make combat that works but not combat
  that feels good."
- **timeline** — A generic timeline of phases and windows (startup → active → recovery,
  with a "hitbox is on now" window). The same mechanism describes a sword swing, a gun's
  reload, or an ability cast — it is genre-neutral. The game decides what each window means.

How they relate: **timeline** says *when* things happen in a move; **combat** says *how a
hit feels*; together with **physics** (for knockback) and **camera** (for shake) they make
a hit land with weight.

## Group H — Interactions, events, and story

- **event** — A message board. One part of the game announces "damage happened" or "door
  opened"; other parts listen and react. This keeps parts decoupled.
- **interact** — The mechanism for "things the player can act on": doors, switches,
  pickups. A general mechanism; the game supplies the meaning.
- **dialogue** — Branching conversations and scripted text events.
- **coro** (coroutines) — A way to script sequences that happen over time ("do this, wait,
  then do that") without freezing the game.
- **tween** — Smoothly changes a value over time (fade this in, slide that across) and
  provides timers/scheduling.

How they relate: **event** is the shared announcer many systems use (combat, physics, and
interact all post events). **interact** fires events when the player uses something.
**dialogue/coro/tween** script things that unfold over time.

## Group I — Building worlds automatically (procedural generation)

- **procgen** — Generates content by algorithm (random but controllable).
- **terrain** (test-demonstrated, built on procgen/render) — Landscapes from heightmaps.
- Trees, caves, and varied natural features are generated too (see the nature/tree tests).

How they relate: **procgen** provides the randomness and rules; **render** draws the
result; the world systems place it.

## Group J — On-screen interface (2D UI)

- **gui** — An immediate-mode toolkit for menus, HUDs, buttons, and debug panels.
- **nineslice** — Stretchable bordered panels (a box whose border stays crisp at any size).
- **worldui** — UI attached to world positions (a health bar floating over an enemy's head,
  a name tag).

How they relate: **gui** draws flat interface; **nineslice** makes its panels scale
cleanly; **worldui** pins interface elements to things in the 3D world.

## Group K — Saving, config, and tooling

- **save** / **savegame** — Saving and loading a game (the raw store, and the management
  layer with save slots and metadata).
- **ccscene** / **ccsave** (scene save) — Saving and loading whole levels/scenes from text
  files.
- **console** — Runtime variables (CVars) and a command console for tweaking and debugging
  while the game runs.
- **scripting** (+ python) — An embedded Python bridge, including hot-reload (change a
  script while the game runs and see it update).
- **jobs** — A thread pool for doing heavy work in parallel across CPU cores.
- **debug** — Debug drawing and inspection (draw a box around a thing, dump state to a
  file).
- **canonframe** — The fixed canonical render frame: one locked 580×720 coordinate space
  with a fixed center, so any object's position and rotation is an exact, comparable
  coordinate (`±XXX-±YYY:rotation`). This is a *verification* feature: it turns "is it
  centered?" into an exact number.

How they relate: **save/savegame/ccscene** persist the game; **console/scripting** tune it
live; **jobs** speeds it up; **debug/canonframe** verify it.

## Group L — Extras and platform

- **assetpack** — Bundles many assets into one shareable file (`.ccpak`), and loads assets
  by type. Works with the development asset library (`assets-dev/`) that auto-bundles only
  the assets a game actually uses.
- **net** (+ the standalone `netkit/`) — Networking for multiplayer: replicating game state
  from a server to clients, with prediction and reconciliation. `netkit` (transport,
  commands, prediction) is a separate library; **net** is the engine's replication piece.
- **steam** — A bridge to Steamworks (the Steam platform), for shipping on Steam.
- **particles** / **trail** / **decal** (test-shown) / **billboard** — Visual effects:
  particle systems (smoke, fire, sparks), motion trails/ribbons, projected decals (bullet
  holes, marks), and camera-facing sprites.
- **ccmath** — The math library everything is built on (vectors, matrices, quaternions).

How they relate: **ccmath** underlies everything. **assetpack** moves assets around.
**net/netkit** add multiplayer. **steam** ships it. The effects systems feed **render**.

---

# PART 2 — THE TESTS

Each file in `tests/` is a small program that proves one feature works. Most tests run
"headless" (no screen) and either **render a picture** you can look at, or **check exact
numbers** (pass/fail), or both. This is how the engine proves itself honestly: not by
claiming a feature works, but by demonstrating it.

Below, every test is listed with what it does and why it matters. Tests come in two kinds:
- **Logic tests** — check exact numbers/behavior, print pass/fail. No picture needed.
- **Render tests** — draw a scene to a PNG so the result can be seen and judged.

### Rendering & visual quality

- **aa_test** — Verifies anti-aliasing: that each smoothing mode is selected and changes
  the render. Proves edges get smoothed.
- **bloom_test** — Bright glowing shapes on a dark ground; proves the "glow" (bloom) around
  bright things works.
- **ssao_test** — Objects with tight crevices; proves ambient-occlusion (soft contact
  shadows in corners) darkens creases correctly.
- **ssgi_test** — Screen-space global illumination; proves colored light bounces off
  surfaces onto nearby ones (color bleed).
- **ssr_test** — A glossy floor reflecting objects above it; proves screen-space
  reflections.
- **shadow_test** — Objects casting shadows from a directional light onto the ground;
  proves shadow mapping.
- **spotshadow_test** — A spotlight casting a cone of light and a shadow; proves spot-light
  shadows specifically.
- **ibl_test / sky_test** — Spheres of varying metal/roughness lit only by the sky; proves
  image-based lighting and the sky model.
- **dof_test** — A row of spheres from near to far; proves depth-of-field (focus blur).
- **taa_test** — A scene of hard high-frequency detail; proves temporal anti-aliasing
  (smoothing across frames).
- **autoexposure_test** — The same scene at three brightness levels; proves the "eye
  adaptation" that adjusts exposure like a real eye.
- **outline_test** — A scene drawn with edge-detection outlines; proves the outline effect.
- **decal_test / decal_anglefade_test** — Projected marks on surfaces; the second proves a
  decal fades out on surfaces it hits at a steep angle (so it doesn't smear).
- **instancing_test** — A grid of cubes drawn in one efficient batch; proves instanced
  rendering (drawing many copies cheaply).
- **billboard_test** — A swarm of glowing sprites that always face the camera; proves
  billboards.
- **material_test** — Anisotropic filtering + detail maps; proves surface realism close up.
- **pbrset_test / gen_pbr_set** — Loading real scanned surface texture sets; the `gen_`
  file generates such a set, the `_test` loads it.
- **realism_test / realism_integration_test / nature_test / nature_photoreal** — Progressive
  proofs of realistic shading: one sphere in several shading models, then all realism
  features combined, then a small nature scene, then a photorealistic meadow at golden hour.
- **rt_test** — Render targets: render a scene, capture the finished frame; proves
  off-screen rendering/readback.
- **stress_test** — Deliberately varied, awkward geometry to surface bugs that simple
  spheres would hide. A bug-hunting scene.
- **geometry_test** — The primitive shapes (cylinder, cone, capsule, torus); proves the
  shape generators.

### Characters, animation, models

- **anim_test** — Builds a segmented body, applies GPU skinning + IK, and proves a posed
  limb actually bends. The core skeletal-animation proof. (A notable fixed bug: skinned
  meshes were rendering black because the draw didn't tell the engine 3D geometry was
  drawn, so the lighting pass was skipped — fixed by counting the skinned draw.)
- **ccmodel_test** — Proves the engine loads geometry from its own `.ccmodel` format.
- **ccrig_test** — Proves a character's rig (skeleton) survives being saved to text and
  loaded back.
- **gltf_test** — Proves importing standard glTF models works for both text and binary
  containers.
- **editmesh_test** — Proves the editable half-edge modeling system builds and renders
  shapes.

### Movement, physics, characters

- **physics_test** — The rigid-body world with the full set of collision shapes; proves
  gravity, collisions, and responses.
- **move_test** — Collide-and-slide movement; verifies sliding along walls, not clipping
  through.
- **player_test** — The kinematic character controller; proves player movement respects the
  world.
- **world_test** — Spatial culling: a big field of cubes, proving the engine only processes
  what's relevant (performance).

### Input

- **input_edge_test** — The edge-triggered input contract: a press fires once, a held key
  doesn't re-fire, a fast tap still registers. Proves input timing is correct. (Fixed bugs
  here included input being read at the wrong moment in the frame, and fast taps being
  dropped — solved by an event-driven input path with a per-frame press latch.)
- **gamepad_test** — Gamepad connect/disconnect and button/axis reading, via injection
  (headless).
- **inputrec_test** — Records an input stream and replays it deterministically; proves
  exact playback.

### Sound

- **audio_test** — Audio is invisible, so this *visualizes* the audio state to prove sounds
  are placed and playing.
- **audiofx_test** — Proves walls muffle sound (occlusion) and spaces change it.
- **soundfield_test** — Proves sound propagates through a space with wall occlusion.
- **hearing_enemy_test** — A rendered demo where an enemy moves based on *hearing* the
  player through the sound field. Proves AI hearing.
- **mic_test** — Exercises microphone capture/analysis via a synthetic (fake) audio feed so
  it runs headless.

### AI and pacing

- **ai_test** — A* grid pathfinding + path-following steering, plus a small formation.
  Proves enemies can navigate.
- **director_test** — The pacing director's state machine, spawn budget, and beat
  progression. Proves dynamic difficulty.

### Combat feel and timing

- **combat_test** — Verifies the game-feel layer with exact numbers: hitstop freezes time
  for the right duration then resumes; knockback points the right way and scales with
  strength; hitstun decays; the profile is tunable; a damage event fires. Also proves
  modularity (turn reactions off, supply your own).
- **timeline_test** — Proves the generic timeline mechanism, and that the *same* API drives
  both a fighting-game move and a looping weapon cycle (genre-neutral). Checks that phase/
  window/marker boundaries fire in the right order.

### Interactions, events, scripting

- **event_test** — The event bus contract (pure logic): subscribe, publish, receive.
- **event_integration_test** — Proves the engine's own subsystems talk over the event bus.
- **event_physics_test** — Proves physics contacts publish contact events with impact info.
- **interact_test** — A door, a switch, and a pickup; proves the interactable mechanism.
- **interact_custom_test** — Proves interact is a *mechanism*: you can define custom
  interactable behavior.
- **dialogue_test** — A branching conversation with gated choices and actions.
- **coro_test** — The coroutine scheduler stepping through a sequence (pure logic).
- **tween_test** — Tween completion and timers (data checks).
- **console_test** — Runtime variables (register/get/set/type-coerce/flags) and the command
  console.
- **python_test / python_reload_test** — The embedded Python bridge, and hot-reloading a
  script while running.
- **jobs_test** — The parallel job system: proves parallel work matches serial results
  (correctness) and runs across cores.
- **coro_test**, **loop_test** — `loop_test` verifies the fixed-timestep loop math: the
  simulation ticks at a steady rate regardless of frame rate, with catch-up and a
  safety clamp.

### World-building (procedural)

- **terrain_test** — Procedural heightmap terrain generation, rendered.
- **tree_test** — Close-up of procedural, intentionally imperfect (natural-looking) trees.

### 2D interface

- **gui_test** — Proves GUI widgets render and respond.
- **nineslice_test** — Proves a bordered panel keeps crisp borders when stretched.
- **worldui_test** — Proves UI elements (like health bars) anchor to world positions and
  track them on screen.

### Saving and scenes

- **save_test** — A full save/load round-trip with realistic save data.
- **savegame_test** — The save-management layer: slots and metadata.
- **ccsave_test** — Scene serialization: build a level, save it, reload it.
- **ccscene_test** — Load a whole scene from text files on disk.

### Effects

- **particles_test** — Several particle emitters (smoke, fire, sparks), rendered.
- **trail_test** — A flying sphere leaving a glowing motion trail.
- **pixshape_test** — Proves the "a pixel is a diagonal" coverage math is correct.

### Networking (multiplayer)

- **net_test** — Replication with no real network: a server and client in one program,
  proving state copies correctly.
- **netcmd_test** — The server-authority half (commands), no real network.
- **netpredict_test** — Client-side prediction + server reconciliation (the smoothness
  trick that hides lag).
- **nettransport_test** — The real UDP transport, moving actual network packets.
- **net_integration_test** — The whole networking stack working together in one test.

### Games (end-to-end)

- **tetris_test** — Verifies the Tetris game *logic* headlessly (no window): pieces move,
  rotate, drop, lines clear, scoring works.

### Capstones (many systems at once)

- **horror_vignette_test** — Ties together five systems (lighting, sound, AI, effects, and
  more) into one atmospheric demo. A proof that the systems combine.
- **realism_integration_test** — All realism features working together in one scene.

---

# How the whole thing fits together (one picture in words)

At the bottom is **ccmath** (the math) and the **ecs** (the object bookkeeping). On top of
those, **render** draws, **physics/move/player** simulate, and **input** listens. Around
those, **anim** bends characters, **audio/soundfield** make sound, **ai/director** run
enemies and pacing, and **combat/timeline** make action feel good. **event** is the shared
announcer that lets all of them react to each other without being tangled together.
**gui/worldui/nineslice** put the interface on screen. **save/scripting/console/jobs**
support and speed everything. **assetpack** moves assets in; **net/steam** take the game
online and to a store; the **verification tools** (debug, canonframe, and the standalone
Deboog) keep everyone honest by measuring the truth instead of trusting a description.
