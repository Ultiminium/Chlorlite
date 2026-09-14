# CC — MASTER FEATURE LIST (the original vision; POST-REALISM scope)

> This is the ORIGINAL master feature list CC was always meant to grow into.
> It is the real long-term scope. The realism stretch (auto-exposure, PCSS,
> materials, SSGI, TAA) and the queued asset-redesign + ENGINE.md are milestones
> ALONG the way to this — not the whole plan. Work this AFTER realism + assets.
>
> STATUS DISCIPLINE: do NOT mark an item done from memory. Verify against
> SKILL.md's capability table / the actual code before checking anything off.
> A companion status pass lives in the handoff; unmarked = unverified, treat as
> not-done until proven.

## Seeing & verifying
- A working image viewer on my end (blank all session).
- "Describe this screenshot" that names objects and positions.
- Object-ID / picking buffer — click a pixel, get the entity.
- Scene graph → JSON dump: every mesh, material, transform, light this frame.
- Draw-call list dump per frame.
- Headless orbit capture (auto 4–8 framed angles around a point).
- Auto-frame-subject camera (give it an entity, it frames it perfectly).
- Animation capture to sprite sheet / GIF / mp4.
- Side-by-side before/after screenshot diffing with visual highlight.
- Pixel-perfect regression snapshots (fail build if a shot changes).
- Debug gizmos: line, box, sphere, arrow, cross, label-at-worldpos.
- Wireframe mode, normal buffer, depth buffer, overdraw heatmap, lighting-only, albedo-only, UV-checker views.
- Draw-call / triangle / light / texture-memory counters in a debug overlay.
- Seed + build hash burned into every screenshot.
- Screenshot manifest JSON (what each shot targeted, camera params).
- Live in-engine debug console with commands.
- Time-scrubber: render frame N of a simulated timeline.
- Freecam / spectator mode in headless.
- "Explain why this pixel is this color" (which draw call wrote it).
- Heatmap of where the player/demon has walked.
- Visual light-range spheres and light-count overlay.

## Renderer / materials / post
- Emissive on dynamic meshes.
- Per-draw color tint / multiply uniform on cc_draw_mesh.
- Unlit / always-emissive material flag.
- Implement cc_mesh_quad, cc_texture_update.
- Make cc_mesh_plane render through cc_draw_mesh.
- Billboard mesh (always faces camera).
- Decal system (project texture onto surfaces): numbers, blood, signage, scorch.
- Working tonemapping / exposure (dead knob now) + bloom.
- Shadow mapping (point, spot, directional, cascaded).
- Contact / ambient occlusion (SSAO).
- Screen-space reflections.
- Fog: distance fog, height fog, volumetric light shafts / god rays.
- Predictable alpha blending, order-independent transparency.
- Vignette, film grain, chromatic aberration, motion blur, DOF, lens distortion, scanlines/CRT, color grading LUTs.
- Dithering / VHS / datamosh / glitch shaders (horror mood).
- Silent-failure warnings (bad handle, exhausted pool → log, don't no-op).
- Auto-growing mesh cache; GPU pool near-full warning.
- Documented + higher dynamic-light budget; clustered/forward+ lighting.
- Emissive bloom threshold controls.
- Custom shader hook / material graph.
- PBR texture maps: normal, roughness, metallic, AO, height/parallax.
- Triplanar mapping; texture atlas support.
- Skybox / cubemap / IBL environment lighting.
- Reflection probes.
- Vertex animation / shader-driven wind.
- Particle system (GPU): dust, smoke, sparks, blood mist, fog motes.
- Trail renderer; ribbon renderer.
- Line/debug-line rendering as a first-class primitive.
- Instanced rendering (N copies cheap).
- Mesh combine/bake to cut draw calls.
- LOD system; frustum + occlusion culling.
- Render-to-texture / portals / security-camera monitors.
- Mirrors.
- Stencil effects (portal cuts, X-ray).
- Outline / rim-light / silhouette-through-walls shader.
- Deferred + forward hybrid toggle.
- HDR pipeline, exposure adaptation (eye adjustment in dark rooms).
- Sprite / 2D layer for in-world posters and screens.

## Geometry / meshing
- Reliable cylinder, cone, sphere, capsule, torus, wedge, prism, arch, stairs.
- Extruded / beveled text-to-mesh; 3D world-space text.
- CSG booleans; wall-with-holes (doorways, windows) helper.
- Procedural building/room generator primitives (walls, floors, ceilings with openings).
- Bevel / chamfer / subdivide / smooth-normals utilities.
- Mesh simplification / decimation.
- Convex hull + auto-collider generation from a mesh.
- Spline / path meshes (pipes, cables, corridors).
- Marching cubes / metaballs.
- Heightmap terrain.
- Runtime mesh editing / vertex deform.
- UV unwrap helper.
- Loft / revolve / sweep operations.

## Player / input / controllers
- Built-in FPS character controller (mouselook, WASD, crouch, sprint, lean, jump, stairs, slopes).
- Head-bob, view-kick, camera shake, footstep-synced sway.
- Raycast, spherecast, boxcast; screen→world ray; world→screen project (expose real framebuffer size).
- Trigger volumes + interaction-prompt system.
- Configurable keybindings; input remapping UI.
- Gamepad support (rumble too).
- Touch controls.
- Input recording & playback (deterministic replays for tests).
- Mouse capture/lock helpers (partly there now).
- Context actions ("hold E", "tap F").
- Inventory / item-pickup system.

## Physics
- Character controller collide-and-slide (proper, reusable).
- Rigid bodies, joints, constraints.
- Ragdoll.
- Continuous collision (fast objects).
- Trigger/overlap callbacks.
- Raycast against physics world.
- Buoyancy, wind, force fields.
- Breakable / fracturable objects (guards, glass, furniture).
- Cloth (curtains, hanging wires, body bags).
- Soft body.
- Rope / chain / cable.
- Grappling / drag / throw.
- Destructible walls.
- Physics materials (friction, bounce, "sticky").

## AI / navigation
- Navmesh bake + pathfinding (A*, flow fields).
- Steering (seek, flee, wander, separation, pursuit with prediction).
- Line-of-sight, peripheral vision cones, hearing radius, last-known-position memory.
- Patrol routes, waypoints, schedules.
- Behavior trees / state machines / GOAP / utility AI.
- Blackboard / perception system.
- Squad coordination (guards flanking, calling for help).
- Demon "director" AI (dynamic-difficulty stalker like Alien: Isolation).
- Sound-propagation-aware AI (hears mic input, footsteps).
- Crowd simulation.
- Cover system.

## Audio
- Microphone input capture + level/RMS + basic speech/keyword detection.
- 3D positional audio (verified), Doppler, distance attenuation curves.
- Occlusion (muffle through walls), reverb zones per room type.
- Sound bank / event system; randomized variations.
- Music layers / adaptive stems that respond to tension.
- Procedural audio (drones, stingers, whispers).
- DSP: reverb, delay, EQ, distortion, pitch-shift, low-pass "underwater".
- Ambisonics / HRTF binaural for headphones.
- Audio ducking (dialogue over ambience).
- Beat/tempo sync hooks.
- Subtitle/caption system driven by audio events.
- Voice lines with lip-sync.

## Animation / characters
- Define & play custom clips on CharModel; clip library.
- Blend trees, crossfade/transition between states.
- Additive animations (breathing on top of walk).
- Animation events (footstep, hit, gib frames).
- Ragdoll ↔ animation blending (get-up, death).
- IK: foot placement, hand-reach, look-at (clerk eyes follow player), aim.
- Root motion.
- Procedural secondary motion (jiggle, cloth sway, hair).
- Facial animation / blendshapes / morph targets; expressions.
- Lip-sync from audio.
- Runtime CharStyle / tint override; CHAR_DEMON and other kinds.
- Body-type / height / proportion sliders.
- Skinned .ccmodel import/author pipeline + clip authoring.
- Model turntable previewer.
- Retargeting between rigs.
- Motion-capture / video-to-animation import.
- Text-to-animation ("make him lunge and tear").
- Crowd animation variation (phase offsets, gait noise).

## World / level / content
- Animated doors (sliding + swinging), collision-synced, auto-close, lockable, keycards.
- Openable drawers, cabinets, lockers, light switches, elevators, buttons, levers, valves.
- Breakables (glass, monitors, ceiling tiles, lights).
- Room & door numbering system.
- Signage / exit-sign / poster decal system.
- Elevator + multi-floor transition system.
- More floor archetypes (10 → curated ~50) + validation harness.
- Bigger prop library + prop scatter/auto-dress tool.
- Modular building kit (walls, corners, doorframes, trims).
- Procedural clutter (papers, cables, coffee cups).
- Flickering/failing lights, sparks, exposed wiring.
- Weather/HVAC ambience (vents, hum, flicker).
- Interactive computers/terminals (readable screens, minigames).
- Whiteboards / notes / readable lore documents.
- Security cameras + viewable monitor feeds.
- Vending machines, water coolers (functional).
- Day/night or "the building turns wrong" state-morphing system (walls move, rooms rearrange).
- Non-euclidean space / impossible geometry (endless hallway, loops).
- Liminal-space presets.

## Gameplay systems
- Save / load; checkpoints; autosave.
- Multiple + secret endings framework.
- Objective / quest / task tracker.
- Day-loop / shift system (the 7-day structure).
- Difficulty scaling / guard-stat buff system.
- Sanity / fear meter with effects.
- Stamina, health, status effects.
- Currency / files-delivered economy.
- Dialogue system (branching, timed, the password verify-or-die).
- Notification / subtitle / prompt UI.
- Journal / map / minimap.
- Achievements / stats tracking.
- Cutscene / scripted-sequence sequencer + camera director.
- Timeline/event scheduler ("at 3am the lights fail").
- Random-event director.
- Photo mode.
- New Game+.

## UI / HUD
- Immediate-mode UI toolkit (buttons, sliders, menus, layout).
- World-space UI / diegetic screens.
- Font loading (TTF), rich text, outlines, drop shadows, bold/italic (faux-bolded by overdraw now).
- Icon / sprite atlas for HUD.
- Menus: main, pause, settings, video/audio options.
- Damage vignette / directional damage indicator.
- Interaction reticle / context prompts.
- Subtitle + accessibility options (colorblind, text size).
- Loading screens / transitions / fades.
- Controller-navigable UI.
- Tweened/animated UI.

## Architecture / engine QoL
- Standard Actor/Entity abstraction (kill the parallel arrays).
- Full ECS ergonomics (queries, systems, prefabs).
- Prefab / blueprint instancing.
- Event bus / messaging.
- Scene serialization (save a whole level).
- Component hot-attach.
- Coroutine / async task system.
- Tween/easing library.
- Timer/scheduler helpers.
- Seeded RNG helpers exposed.
- Math helpers (lerp, damp, smoothstep, spring, remap, easing).
- State-machine helper.
- Object pooling.
- Job system / multithreading.
- Memory arena / leak tracking / profiler (CPU + GPU frame timings, flamegraph).
- Config/CVar system.
- Structured leveled logging.
- Assertion + crash-with-stacktrace.

## Build / tooling / workflow
- Hot-reload of game code (keep engine warm).
- cc watch (rebuild on save).
- cc run = build + headless + screenshot + describe in one shot.
- Faster incremental builds / ccache.
- Quiet builds (silence the __va_arg_pack header noise).
- API cheat-sheet of exact signatures (stop grepping headers).
- Conventions doc: unit scale, eye height, forward axis, quaternion order, handedness.
- In-repo searchable docs / examples gallery.
- Template games (FPS, horror, top-down).
- Asset importer (glTF/FBX/OBJ, PNG, WAV/OGG).
- .ccproj asset store used for the curated floor pool.
- Live-reload assets (edit texture, see it without rebuild).
- Unit-test + golden-image test harness.
- Benchmark suite.
- Editor GUI (place props, tweak lights, drag entities) — even a headless "level as JSON I can hand-edit".
- Visual node editor for materials/logic.
- CI hooks.
- Packaging/distribution (Steam build, itch).
- Crash reporter.
- Version/changelog surfaced in-engine.

## Networking / platform (dream tier)
- Multiplayer / co-op netcode, rollback, lobbies.
- Steam integration (achievements, cloud saves, workshop).
- Controller/console platform layers.
- Mod support / scripting sandbox (Lua/Wren/embedded).
- Web/WASM export.
- Mobile export.
- VR mode.
- Localization / i18n system.
- Analytics / telemetry.
- Replay sharing.

=============================================================================
VERIFIED STATUS MAP (cross-referenced against SKILL.md capability table + this
session's work — ONLY items confirmed by the table/code are marked DONE. Every-
thing else on the list above is UNVERIFIED → treat as NOT DONE until proven.)
=============================================================================
CONFIRMED DONE (evidence: SKILL.md "✅ Full" rows and/or shipped this session):
  Renderer/post: deferred PBR renderer, OSMesa headless + GLFW, bloom, FXAA
    (fixed this session), ACES tonemap, auto-exposure, vignette, chromatic
    aberration, 64-light deferred, custom GLSL/fullscreen passes, runtime resize
    + pixel readback, PCSS soft shadows (directional 4096²), SSAO, SSR, IBL/
    cubemap env lighting, decals, billboards, outline shader, distance+height fog,
    emissive on meshes, per-draw tint, unlit flag, cc_mesh_quad, cc_texture_update,
    SSGI (this session), TAA (this session).
  Materials: PBR albedo/roughness/metallic, normal maps, 16× anisotropic filtering,
    detail normal maps, per-material shading models (PBR/toon/flat/rim).
  Geometry: cube/sphere/plane/cylinder/cone/capsule/torus builders, geometry
    toolkit (normals/tangents/weld/bounds/flip), editable half-edge mesh (split/
    poke/extrude/bevel/Catmull-Clark/snap/bake).
  2D: sprite batch, TTF text atlas, 2D primitives (rect/circle/line/tri).
  ECS: archetype SOA, system scheduler, ECS→renderer draw path.
  Input: Qwerty (evdev/X11/headless), action binding (rebind/axes/contexts).
  Player/camera: kinematic character controller (jump/gravity/step), camera
    controllers (free/orbit/follow/path/shake/dolly).
  Physics: injectable rigid-body, full collision matrix, raycast.
  AI: A* grid pathfinding, steering (seek/flee/arrive/wander/separation/path-
    follow), behavior FSM.
  Scene/world: scene graph (parent-child transforms), world octree + frustum cull
    + distance LOD.
  Animation: skeleton, blend2D, mask, IK, state machine, GPU skinning, facial/
    morph (this-era), .ccmodel bin save/load, glTF 2.0 import + export.
  Audio: synthesis + OpenAL 3D, mixer (buses/fades/pause/music crossfade/rolloff).
  Scripting: C/C++ + Rust dlopen, plus a Python scripting bridge (opt-in).
    IMPORTANT DISTINCTION (clarified with user): the Python bridge is a FEATURE CC
    OFFERS ITS USERS, not CC using Python. Someone building ON TOP of CC (a Claude,
    or a developer) can write THEIR game/app logic in Python and call into the
    engine through the bridge. That is legitimate and the bridge should be KEPT and
    maintained as a product feature. The no-Python rule is about CC's OWN
    IMPLEMENTATION (engine/tooling/asset layer/converters) — those are never
    Python. The two never conflict: CC is not written in Python; CC lets its users
    script in Python.
  Debug/tooling: screenshot→PNG headless, seed+build-hash burned into shots,
    screenshot manifest, frame-by-frame visual debug, debug normal view.
  Assets/content (shipped post-realism): TEXT .ccmodel format — geometry,
    materials, AND rigs (skeleton/skin/anim/blendshape) load+save from human-
    readable text (cc_mesh_load_ccmodel / ccm_load_text / ccm_save_text);
    material-slot→CCMaterial bridge (sRGB-correct); .cclist scene manifests +
    SCENE SERIALIZATION (cc_sceneasset_new/add/save/load — build a level in
    memory, save it to hand-editable text, reload it). This is the "hand-editable
    level" unblocker. Procedural imperfect-geometry helpers (lumpy canopy /
    gnarled trunk) + per-vertex fbm displacement pattern.
  Gameplay (shipped this session): SAVE MANAGEMENT (cc/savegame.h — named slots
    + per-save metadata for a load menu, quicksave, autosave ring w/ throttle +
    latest-by-seq, and live actor capture/restore) on top of the existing
    cc/save.h KV store: the "save/load; checkpoints; autosave" master-list item.
  UI/HUD (shipped this session): IMMEDIATE-MODE GUI TOOLKIT now has a proper
    cc/gui.h header + PROGRESS/STAT BARS (health/sanity/stamina), spacer, and
    absolute-position buttons, plus the first interaction test (clicks driven
    headless via cc_input_inject_mouse_*): the "immediate-mode UI toolkit" item.
  Architecture (shipped this session): EVENT BUS (cc/event.h), COROUTINES
    (cc/coro.h), and a CVAR + COMMAND CONSOLE (cc/console.h — typed named
    runtime vars with READONLY/CHEAT flags, registerable string-dispatched
    commands, a log ring, and .cfg exec/save round-trip: the "Config/CVar system"
    AND "live in-engine debug console with commands" master-list items).
  Audio (shipped this session): MIC INPUT (cc/mic.h), SOUND-PROPAGATION hearing
    AI (cc/soundfield.h), and OCCLUSION + REVERB ZONES (cc/audiofx.h).
  AI (shipped this session): DIRECTOR AI (cc/director.h dynamic-tension pacing).
  Tooling (shipped this session): PYTHON scripting bridge + HOT-RELOAD (opt-in
    CC_PY=1), two TEMPLATE GAMES (game_stealth, game_horror), glTF/.glb import.
  Architecture: ACTOR/ENTITY abstraction (CCActor over the ECS — spawn/find/
    transform/material/visibility by handle; actors ARE entities so cc_scene_
    render draws them; "kill the parallel arrays" DONE). NOTE the ECS world type
    is CCScene (cc_scene_create); the loaded .cclist asset is CCSceneAsset.

RECONCILED against code on 2026-08-22 (this session). The list below previously
marked many SHIPPED, TESTED systems as open — corrected here by cross-referencing
every engine header against tests/. Method: a capability is DONE only if it has a
header AND a passing test (or is credited in SKILL.md's ✅ table). Items moved OUT
of "not done" this pass because they verifiably exist+pass: mic input, save/load +
savegame slots, dialogue, interact(ables core), director AI, immediate-mode UI
(gui), event bus, coroutines, CVars/console, occlusion/reverb zones (audiofx),
sound-propagation AI (soundfield), particles, trails/ribbons, prefab instancing,
tween lib, world-space UI (worldui), gamepad, input record/playback (inputrec),
DOF, terrain, procgen. (Networking replication + the netkit library also shipped
but are DREAM-TIER — see note below.) DO NOT rebuild these; verify before extending.

NOT YET DONE / UNVERIFIED (genuinely open, after reconciliation — the real backlog):
  - Seeing/verifying: object-ID/picking buffer, scene-graph JSON dump, draw-call
    dump, orbit capture, GIF/mp4 capture, screenshot diffing, wireframe/depth/
    overdraw/UV-checker debug views, "explain this pixel". [in-engine console: DONE
    (cc/console.h)]
  - Renderer: volumetric god rays, OIT/predictable alpha, motion blur/lens
    distortion/CRT/LUTs, glitch/VHS shaders, material graph, parallax/height maps,
    triplanar, reflection probes, mesh combine/bake, occlusion culling, render-to-
    texture/portals/mirrors, stencil FX. [particles, trails/ribbons, DOF: DONE]
  - Geometry: CSG booleans, text-to-mesh, room/building generator, decimation,
    convex-hull auto-collider, spline meshes, marching cubes, UV unwrap, loft/
    revolve/sweep. [heightmap terrain: DONE (terrain_test)]
  - Player/input: full FPS controller extras (lean/head-bob/view-kick), touch,
    trigger volumes+prompts, inventory. [gamepad: DONE (gamepad_test); input
    record/playback: DONE (inputrec)]
  - Physics: collide-and-slide reusable, joints/constraints, ragdoll, CCD, cloth,
    softbody, rope, destructibles, physics materials, force fields.
  - AI: navmesh bake, LOS/vision cones/last-known-pos, patrols/schedules, behavior
    trees/GOAP/utility, blackboard, squad coord, crowds, cover. [director AI: DONE;
    sound-propagation/hearing AI: DONE (soundfield, hearing_enemy_test)]
  - Audio: sound banks/events, adaptive music stems, procedural audio, DSP suite,
    HRTF, ducking, subtitles-from-audio, lip-sync voice. [MIC INPUT: DONE (mic);
    occlusion/reverb zones: DONE (audiofx)]
  - Animation: clip library/authoring, blend trees, additive, anim events, ragdoll
    blend, richer IK, root motion, secondary motion, lip-sync, body sliders,
    turntable, retargeting, mocap/video import, text-to-animation, crowd variation.
  - World/content: animated doors, interactables CONTENT (drawers/lockers/switches/
    elevators — the interact CORE is done; these are game-content instances on top),
    breakables, room numbering, signage, floor archetypes+validation, prop library+
    scatter, modular kit, clutter, flicker lights, HVAC ambience, terminals,
    readable notes, security cams+monitors, vending, state-morphing "building turns
    wrong", non-euclidean/liminal presets. [NOTE: much of this is GAME CONTENT built
    on interact.h/worldui.h/particles.h, not new engine systems.]
  - Gameplay: endings framework, quest tracker, day-loop/shift, difficulty scaling,
    sanity meter, stamina/health/status bars (gui has stat bars — the METER LOGIC is
    open), economy, notifications, journal/map, achievements, cutscene sequencer,
    event scheduler, random-event director, photo mode, NG+. [save/load+checkpoints:
    DONE (savegame); dialogue system: DONE (dialogue)]
  - UI/HUD: HUD atlas, menus, damage vignette (horror_vignette exists), reticle/
    prompts, accessibility, loading/transitions, controller-nav UI. [immediate-mode
    UI toolkit: DONE (gui); world-space UI: DONE (worldui); tweened UI: tween DONE]
  - Architecture: full ECS query ergonomics, component hot-attach, timers, exposed
    seeded RNG, math helpers (ccmath exists — audit coverage), state-machine helper,
    object pooling, memory/profiler, structured logging, assert+stacktrace. [event
    bus, coroutines, tween, prefab instancing, job system, CVars, scene
    serialization, Actor/Entity: ALL DONE]
  - Build/tooling: hot-reload game code (python hot-reload DONE; C/native open),
    cc watch, cc run one-shot, ccache/faster builds, API cheat-sheet, conventions
    doc, examples gallery, ASSET IMPORTER (glTF DONE; FBX/OBJ/WAV/OGG open), .ccproj
    store, live-reload assets, golden-image harness, benchmark suite, editor GUI /
    hand-editable level JSON (text .ccmodel/.cclist DONE; GUI editor open), node
    editor, CI, PACKAGING (DONE — cc build/bundle produce standalone Linux ELF +
    Windows PE32+ .exe folders; both verified this session), crash reporter.
    ** FIXED (was: Windows cross-compile blocker): ccmodel.c referenced
       ZSTD_compressBound with no fallback shim (CC_HAS_ZSTD is never defined, so
       raw-store fallbacks are always used, but this one shim was missing). Added
       it — one line. cc build --target windows now links clean. Not the "delete
       the binary path" fix guessed earlier — a one-line omission. **
  - Networking/platform (DREAM TIER — lowest priority; per SEQUENCING, only after
    the horror critical path): netcode replication (cc/net.h) + netkit library
    (netcmd/nettransport/netpredict) SHIPPED this session, but this whole cluster is
    aspirational — the open scope question (should CC ship netcode at all?) is
    unresolved; netkit is cleanly severable if not. Still open here: Steam (steam.h
    exists but UNTESTED — verify or treat as stub), console layers, mod/scripting
    sandbox, WASM, mobile, VR, i18n, analytics, replay sharing.


SEQUENCING (updated 2026-08-22 post-reconciliation): the horror-game critical path
the earlier note pointed to — mic input, save/load, dialogue, interact core,
director AI, UI toolkit — is now BUILT AND TESTED (see the reconciliation note in
the status map). So the ENGINE critical path is largely complete. What's actually
next, in defensible priority order:
  1. [DONE this session] PACKAGING + Windows cross-compile (ZSTD shim). Next up:
     This is the highest-impact open item: NO game ships to Windows today, and
     Windows is half the target. It gates the entire "ship a game folder" goal and
     is likely a small fix (delete/#ifdef the dead binary .ccmodel path). Unlike
     more features, this unblocks EVERY project.
  2. Turning the built systems into a PLAYABLE VERTICAL SLICE — a small complete
     game exercising the critical-path systems together (they're unit-tested in
     isolation but no shipped game proves they compose). This is the real test of
     whether CC meets its "an AI builds a complete game" goal, and it will surface
     integration gaps that isolated tests can't.
  3. The GAME-CONTENT layer on top of interact.h/worldui.h (doors, switches,
     drawers, notes, cams) — but this is game content, arguably built PER-GAME, not
     more engine. Decide whether it belongs in the engine at all (same scoping
     question as netkit).
  4. Genuinely-open ENGINE gaps if desired: physics depth (joints/ragdoll),
     animation authoring, renderer extras (god rays, reflection probes, material
     graph). Lower priority than shipping what exists.
Networking is DREAM TIER — do not extend it further (visibility culling, anti-cheat
hole) until the above are addressed and the "should CC ship netcode at all" scope
question is answered. Verify each item against code before claiming done — do not
trust this map blindly; re-reconcile from SKILL.md/tests at the start of that work
(this map was itself badly stale before the 2026-08-22 pass).
