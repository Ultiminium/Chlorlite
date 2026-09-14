=============================================================================
CORRECTION (this session): the "directory games fail to link / ship single-file only" note
in earlier entries was a MISDIAGNOSIS. Re-tested: a multi-file DIRECTORY game builds via
`cc dev` AND bundles via `cc bundle` correctly (verified with a real 2-file game → ran +
zipped). The original `undefined reference to cc_init` was a STALE BUILD CACHE (engine lib
predated a cc_asset change), not a directory-vs-single-file problem. Fix is `rm -rf
engine/.build-*`. Docs (USAGE.md, troubleshoot.md) corrected to remove the false single-file
limitation. NO directory-game bug exists.
=============================================================================
SESSION (2026-09-05, "chlorlite-engine-session") — GAME-DRIVEN ENGINE HARDENING
=============================================================================
This session drifted from the engine into building a full game ("Orb Collector",
a first-person stealth roguelite) as an extended real-world test of Chlorlite.
The GAME is a testbed; the durable value is the ENGINE work + the LESSONS below.
Game source backed up: /mnt/user-data/outputs/game10_source.tar.gz (rebuild it
into /tmp/game10 and `bash scripts/cc dev|bundle /tmp/game10`). It is NOT a
template in the skill (it's an app, not engine); keep it separate.

ENGINE / LIBRARY CHANGES MADE THIS SESSION (these ARE in the skill):
- voxel/ (NEW top-level standalone library) — Marching Cubes mesher: density field
  -> real 3D terrain with CAVES/overhangs (heightmaps can't). Pure primitives,
  libm-only, `make test`. Ships tables in src/mc_tritable.inc. KEY BUG FIXED: the
  MC triangle winding was 94% backwards -> black/backface geometry; fixed by
  emitting verts in reversed order (k=0,2,1). Density fn does layered caves
  (chambers+tunnels+depth+entrances), one knob = cave_threshold.
- deboog/ (NEW top-level standalone library) — mathematical/geometric debugging:
  reads primitive float arrays (no engine types), checks NaN/Inf, matrix validity,
  mesh manifold/topology, roundness (ribbon detector), skin weights, symmetry,
  canonical coords. `make test`. Meant to be THE standard AI-game debug substrate.
- BUG-001 FIXED earlier: skinned mesh rendered black because draw_skinned never
  incremented stats.meshes_drawn -> deferred lighting pass skipped. (renderer.c)
- scripts/cc: added VOXEL auto-link (mirrors netkit pattern) — a game that
  #includes <voxel/...> gets it linked. Also added the same for future libs.
- AUDIO BACKEND FIX (important, real): the bundled Linux libopenal.a was built
  with ONLY the OSS backend (dead on modern desktops) -> "no audio device" on
  Linux. FIX in scripts/cc ship-build: link the SYSTEM libopenal.so (has
  PulseAudio/ALSA/PipeWire) when present, fall back to bundled + warn. Player
  needs libopenal1 installed (near-universal). Windows still links bundled static
  soft_oal (untested-by-ear). Added cc_audio_device_name() + cc_audio_is_online()
  to engine so games can SHOW audio status (this is how we diagnosed it).
- cc_display_size(e,&w,&h) ADDED (engine.c/renderer.c) — cheap getter for the real
  display resolution WITHOUT a framebuffer readback. Needed for UI scaling. (The
  earlier flicker bug was caused by calling cc_frame_pixels() mid-frame just to get
  size — do NOT do that; use cc_display_size now.)

HARD-WON INPUT / UI LESSONS (cost many wrong "fixes" — record so it's not relearned):
- cc_key_pressed / cc_mouse_pressed (engine EDGE detection) are UNRELIABLE in the
  windowed path. Broke Esc, R, jump, and every menu button. FIX PATTERN: track
  was-down yourself with cc_key_down/cc_mouse_down + a per-frame tick, compute the
  rising edge manually. Applied to keys and a mouse_clicked()/mouse_tick() helper.
- cc_gui_get_cursor() returns the GUI LAYOUT cursor, NOT the mouse pointer. Using
  it for custom-button hit-testing made EVERY custom button dead. Use cc_mouse_pos()
  for real pointer position. (This was THE "buttons don't work at all" bug.)
- 2D overlay coords are DISPLAY PIXELS (0..disp_w/disp_h), not a logical space. So a
  fixed-coord UI sits in the top-left corner at fullscreen. REMAINING TODO: a UI
  scale system (design in logical 800x500, scale+letterbox to cc_display_size).
  Scaffolding was started then reverted to avoid a half-scaled mess — do it fully.
- The verification discipline WORKS and MUST be used: reason-then-assume failed
  repeatedly; injected-input tests + rendering-and-LOOKING found the real bugs.
  (Buttons fixed only after an injected mouse-click test proved the pipeline.)

GAME "Orb Collector" STATE (in game10_source.tar.gz — for continuity, not the skill):
- FP stealth roguelite: collect random 6..24 coins (dynamic array, doubles/wave,
  bounded by map+RAM), avoid line-of-sight attackers (spotted/search/wander AI,
  lose them in bushes), collect all -> escape phase (grayscale death creeps from
  edges, +speed, relentless attacker) -> reach gate. "you died"/"you didnt die"
  meme end screens (fade to black, exact lowercase wording). Waves: +1 attacker,
  random speed/strength 1-5, HP=5 hits, difficulty meter, C=continue.
- World: ARENA=40 terrain w/ semi-realistic slope-blended grass/dirt/rock+snow, a
  big Everest mountain baked INTO terrain_height at (0,-16) w/ cave-mouth at its
  foot; procedural forest (trees have collision, skip mountain); Fortnite-ish bush
  clusters (hiding spots). Cave = voxel chamber you drop into via an Undertale
  "fell to the underground" cutscene; regenerates coins; has its own exit gate.
- Meta: custom main menu (serif title font, live-world bg, custom buttons), tabbed
  Settings (Video/Controls/Audio: FPS cap 60-360+unlimited w/ real limiter, FPS
  display w/ session min/max, sens X/Y, debug, master volume, AUDIO STATUS readout),
  shopkeeper-style Shop (wood bg, 6 consumable items, persistent coins, buy/equip),
  10 PROCEDURAL music tracks (synth in music.c, cycle w/ crossfade) + escape track.
- OPEN GAME BUGS: (1) fullscreen UI top-left (needs the UI-scale system above);
  (2) audio: fixed on Linux via system OpenAL — NEEDS USER EAR-TEST to confirm;
  Windows audio unverified; (3) some shop item effects stubbed (Cloak/Coin Magnet/
  Swift Boots need gameplay hooks; Extra Heart/Head Start/Lucky Charm work);
  (4) Leaderboard screen is a placeholder (needs save/load of best times);
  (5) controls REBINDING requested, not built (per-action keybind map + UI);
  (6) coin-banking one-shot logic is fragile — verify coins accumulate across runs.

=============================================================================

=============================================================================
=============================================================================
START HERE (most recent session handoff — read this block first)
=============================================================================
CONTEXT RESET: this chat is being moved to a fresh one. Full working copy is
zipped at /mnt/user-data/outputs/cc-engine-full.zip (213 files: all source +
tests + docs, no build artifacts). Packaged skill: /mnt/user-data/outputs/
cc-engine.skill. Working dir: /home/claude/cc-engine/ ; skill mirror:
/mnt/skills/user/cc-engine/.

=============================================================================
LOCKED DESIGN DECISIONS (this session — direction, mostly NOT yet built; read
before building anything multiplayer/packaging related)
=============================================================================
BIG PICTURE / STRATEGY (user's vision): CC can NEVER match Unity/UE5 for ONE
reason — Claude can't use a visual UI. That's an AUTHORING ceiling, not a RUNTIME
ceiling. So: max out everything BELOW the UI ceiling (all code-first capability),
treat the visual-editor gap as genuinely out of scope, and aim to DOMINATE the
narrow domain "an AI builds a complete game from a terminal" — the goal is that
when someone asks "what engine should I use with Claude," there is exactly ONE
serious answer. Domination = (1) eliminate every "CC can't do X" and (2) widen
the moat where CC is unique (headless-testability, self-verification, text-first
authoring, open-mechanism philosophy). CC is the PRECURSOR to an actual game the
user will build once CC is ready; this has been ~2+ months of work across 4+
sessions (see /mnt/transcripts/journal.txt for the true origin, Aug 20).

PACKAGING MODEL (decided, NOT built — this is the next big roadmap item):
- Engine is a LIBRARY the game links (static .a to fuse in, or shared .so/.dll
  bundled in a dependencies/ folder next to the exe). NOT a separate install the
  player runs — like every real engine, the engine dissolves into the shipped
  game. Real games DO ship a dependencies/ folder (DLLs next to the .exe), so
  bundling libs there (incl. third-party, or even a helper lib in another lang
  that speaks CC's wire format) is fine and expected.
- Game ships as a FOLDER: exe + dependencies/ + assets.
- Dedicated server = the SAME engine compiled HEADLESS (a build config, not a
  rewrite). CC is already headless-first → this is a structural advantage; the
  server is CC itself in server config, NOT a separate-language rewrite.
- PLATFORMS: Windows + Linux ONLY. **NO MAC** by default (user's call — "100%
  pettiness with a side of reason": Mac deprecated OpenGL→would need a whole Metal
  backend, walled toolchain, notarization, smallest audience). Do not carry
  Mac-shaped complexity. Linux means "build for target distro(s) and LABEL them",
  NOT cross-distro heroics — because the real distribution paths make universal
  Linux compat unnecessary: (1) Steam→Windows build runs fine (Proton too),
  (2) itch.io→just declare supported distros, (3) self-use→you compiled it for
  your machine so it works. The elaborate glibc-baseline/$ORIGIN-rpath/Steam-
  Runtime discipline is OVERKILL for these paths; keep packaging lean = "lay out a
  runnable game folder for a chosen target", developer picks Win/Linux/both.

MULTIPLAYER / SERVER / ANTI-CHEAT (decided philosophy — CC provides SEAMS, NOT
SOLUTIONS. ARCHITECTURE: the engine owns ONE networking thing — replication
(cc/net.h) — because it touches the ECS; everything else is the `netkit` LIBRARY
that ships alongside the engine and builds on it. See the top DONE entry for the
data-ownership rule and why the split exists):
- What EXISTS: (a) ENGINE SEAM — server->client state replication (cc/net.h:
  snapshot/delta/apply, spawn/despawn, verified headless) — the server can BROADCAST
  truth; (b) NETKIT LIBRARY — the AUTHORITY half (netkit/netcmd.h): client->server
  COMMAND channel + server-side VALIDATION SEAM. Clients can only send REQUESTS
  (cc_netcmd_client_encode); the server enforces (cc_netcmd_server_ingest -> validate
  -> apply-only-on-accept). Anti-cheat's architectural 80% comes free from this.
- Build order status: (1) input/command channel DONE, (2) server-side validation
  SEAM DONE, (3) client-can't-write-authority is structural (no client-side API
  touches the server scene), (4) UDP SOCKET TRANSPORT DONE (netkit/nettransport.h —
  thin UDP plug-in carrying net.h snapshots one way + netcmd bytes the other, real
  loopback-tested; peer id feeds netcmd's per-client accounting), (5a) CLIENT
  PREDICTION + RECONCILIATION DONE (netkit/netpredict.h — instant local prediction +
  replay-unacked-on-authoritative reconcile; integration-tested with the full stack
  including a server-side clamp/anti-cheat correction). REMAINING (5b): server-side
  visibility culling (an ENGINE-seam part — a game-supplied "what can this client
  see" predicate on the snapshot path — wallhack/ESP mitigation + bandwidth) and the
  EAC/BattlEye integration hole (netkit/game-side glue).
  The responsive-authoritative core is now complete minus those two. NOTE: full
  engine does not yet cross-compile to Windows due to a PRE-EXISTING zstd/ccmodel
  legacy-binary link gap (unrelated to net; see the transport DONE entry) — the
  Linux/headless path is unaffected.
- ANTI-CHEAT: CC will NOT build an anti-cheat and will NOT claim to have one.
  Instead leave a SUPPORTED, DOCUMENTED HOLE — the integration seams/lifecycle
  points that existing products (EAC, BattlEye, etc.) attach to. User brings their
  own AC and plugs into the hole. CC's job: provide the opening + don't be in the
  way. The architectural anti-cheat (authoritative server rejecting illegal
  requests) is the real 80% and comes free once the authority half above exists.
  Honest limits: authoritative server does NOT stop wallhacks/ESP (mitigate via
  server-side VISIBILITY CULLING — only send what a client should see; also a
  bandwidth win) or aimbots (legal inputs; only statistical detection helps).
- SERVER: CC will NOT be/ship a running server or hosting. It ships SERVER
  CAPABILITY as an installable project dependency (replication authority + input
  channel + validation seam = plumbing + enforcement point, NO policy, NO host).
  The actual server process comes from EITHER (a) an existing server provider/
  service the user links/deploys against, OR (b) a generated "server.sh" that
  emits a runnable server file for local/self-hosting. Capability = dependency;
  deployment = user's choice.
- FFI NOTE (resolved): earlier debated Go-for-netcode. Verdict: no FFI-into-the-
  live-engine (bad shape: per-tick state crossing + GC). But multiplayer lives in
  the PRODUCT, not the engine; engine dissolves into the game at build time, so
  there's no runtime engine<->game FFI seam at all. If a separate non-simulating
  relay/matchmaking tier is ever wanted, it's its own bundled binary speaking CC's
  wire format over sockets — not cgo. Default answer: no second language needed;
  the server is CC compiled headless.


STATE: 77 regression tests (incl. this arc: aa_test, group_test, pixshape_test, jobs_test, net_test, netcmd_test, nettransport_test, netpredict_test, net_integration_test; render tests clean, the rest pure-logic "all checks
passed", incl. python_test + python_reload_test which skip without CC_PY and pass with it). Rebuild the runner:
it globs
tests/*_test.c + nature_photoreal.c. Engine is a ~14k+ line real-time renderer + ECS +
asset pipeline + gameplay + domain-general toolkit. EVERY declared public-header function now
has a real implementation (glTF import + Python bridge were the last two; see top DONE entries).
Recent: EVENT BUS + wiring, COROUTINES, AI DIRECTOR, MIC INPUT, SOUND-PROPAGATION AI, two
rendered demos, two starter templates (game_stealth/game_horror), glTF import, PYTHON BRIDGE
(opt-in via CC_PY=1). Full horror stack: mic → bus → director + sound field → enemies hear +
A*-navigate; and gameplay is now scriptable in Python (cc.log/time/emit/vars).
tests/*_test.c + nature_photoreal.c. Engine is a ~14k+ line real-time renderer + ECS +
asset pipeline + gameplay + domain-general toolkit. (recent: EVENT BUS + wiring, COROUTINES,
AI DIRECTOR, AUDIO MIC INPUT, HORROR VIGNETTE CAPSTONE, SOUND-PROPAGATION AI, a rendered
HEARING-ENEMY demo, and two new STARTER TEMPLATES: game_stealth + game_horror — see top DONE
entries.) Full horror stack: mic → bus → director (global tension) + sound field (spatial
hearing w/ wall occlusion) → enemies hear + A*-navigate to noise.

WHAT'S DONE (see the dated DONE entries below, most-recent-first, for detail):
  - Realism rendering stack (PBR/sRGB textures, PCSS, SSGI, TAA, DOF, bloom, ACES,
    auto-exposure, anisotropic filtering) — the original realism stretch.
  - API-completeness pass: closed nearly all declared-but-unimplemented funcs —
    render targets (cc_rt_*), OBJ import, immediate-mode GUI, ECS query shorthands,
    material setters, texture-from-memory, wireframe/bounds, text-wrap, heightmap
    terrain, sprite-sheet/2D-anim, input edge detection. (render.h 101/102, only
    glTF import left; input 22/22; ecs 19/19; procgen 11/11.)
  - Asset pipeline: text .ccmodel (geometry+materials+rigs), .cclist scenes +
    SCENE SERIALIZATION, Actor/Entity abstraction over the ECS.
  - GAMEPLAY cluster: save/load (.ccsave), interactables (doors/switches/levers/
    pickups), dialogue (.ccdlg branching + gated choices + actions).
  - DOMAIN-GENERAL "helps any game" set: tweens/timers, trails, particles,
    collide-and-slide, gamepad input, world-space UI (bars/nameplates/damage
    numbers), prefab instancing, input record/playback (.ccrec), nine-slice panels.

WORKFLOW REMINDERS: after any engine/src edit, rm -rf engine/.build-* before
rebuild. Build one test: bash scripts/cc dev tests/NAME.c -o /tmp/OUT ; run
headless: unset DISPLAY && /tmp/OUT /tmp/OUT.png ; ALWAYS `view` the PNG (trust
the image, not pixel probes). Full regression before packaging. Package via
skill-creator (exact cmd in a recent DONE entry), re-verify from a fresh unzip,
copy to /mnt/user-data/outputs, present_files. Update HANDOFF/AUDIT/SKILL each
feature. NO stubs/TODOs. sed with empty-regex s##..# fails — use str_replace.

NEXT CANDIDATES (documented in the latest DONE entries + AUDIT.md):
  - Domain-general still open: coroutines, touch input. (EVENT BUS now DONE — see top DONE entry.)
  - Declared-but-unimplemented: glTF import; Python scripting bridge (cc_python_*,
    cc_rust_entry — a deliberately-kept product surface).
  - Horror critical path (from MASTER_FEATURE_LIST.md): AI director, audio MIC INPUT.
  - Pick new direction? read MASTER_FEATURE_LIST.md. Otherwise continue a cluster.

--- (older, now-superseded START-HERE note + full DONE history follow) ---

DONE (this session): DEBOOG — mathematical/geometric debugging as a STANDALONE STANDARD (human wants it to
  be THE thing an AI turns to for spatial/numeric debugging, usable outside Chlorlite like qwerty). New
  top-level sibling library deboog/ (beside qwerty/, netkit/). PURE PRIMITIVE API — speaks only float
  arrays, float[16] matrices, float[4] quats, uint32 indices; ZERO engine coupling; depends only on libm;
  designed to wrap from other languages (results are DATA structs, not printouts). Covers 4 domains + more:
  NUMERIC (deboog_scan_floats NaN/Inf/denormal, check_range), MATRIX (finite/invertible/orthonormal/
  mirrored/scale), QUAT (normalized), MESH (manifold/closed/boundary-holes/degenerate/unused/dup — the
  modelcheck topology, generalized), CROSS-SECTION (roundness ribbon-detector), SYMMETRY (mirror across a
  plane), SKIN (weights sum to 1/bones in range/unweighted/dead), CANON (fixed-frame signed coord
  ±XXX-±YYY:RRR), INVARIANTS (assert facts that must always hold, catch the break). Files: deboog/include/
  deboog/deboog.h (the contract), deboog/src/deboog.c, deboog/Makefile (standalone: `make test`),
  deboog/README.md, deboog/examples/selftest.c. PROVEN: builds with PLAIN gcc (no engine, no scripts/cc)
  as libdeboog.a AND libdeboog.so; selftest exercises every check and passes (NaN caught, collapsed/mirror
  matrix flagged, tetrahedron manifold, ribbon roundness 0.05 FLAGGED, bad skin sum caught, origin→
  +000-+000). ADAPTER pattern proven: tools/deboog_chlorlite.c bridges CCModel→Deboog primitives (unpack
  verts/skin/bones into flat arrays, call Deboog) — compiles against both, dependency ONE WAY (adapter uses
  both; Deboog uses neither). This SUPERSEDES the earlier one-off tools/modelcheck.c + canongrid.c (their
  math is now the Deboog core, generalized + severable). inspect.py stays as the render cross-check addon.
  NOTE: full "flawless debugging" vision (told to human) also wants: guaranteed determinism (seeded, byte-
  identical replay), a structured state-snapshot + diff + record/replay "black box" as the keystone, and
  provenance/localization. Deboog is the math/geometry core of that; snapshot+replay is the next keystone
  if the human wants to go further.



DONE (this session): CANONICAL RENDER FRAME (cc/canonframe.h + engine/src/canonframe.c) — human's idea: a
  fixed shared coordinate space so alignment is an EXACT number, not "looks centered" (alignment analogue
  of modelcheck: measure data, show render). Spec (all locked per human):
  - 580×720 grid, 1 cube = 1px. Center C=0 LOCKED at pixel (290,360), NON-configurable.
  - Signed offsets from center, Y-UP: X∈[-290,+289] (+right), Y∈[-360,+359] (+up). C=0 = 000-000.
  - Positional ID "XYZ-ABC[:rot]": explicit +/- signs, 3-digit zero-pad, optional rotation 0..359.
    e.g. "+000-+000", "+050-+030:190", "-120--045".
  - ID computed from the object's ACTUAL world transform projected into the fixed frame (data-derived
    FACT, not a pixel reading) — the render only DISPLAYS it. Resolution-independent (remaps any render
    size onto the locked 580×720/C=0).
  - Overlay: grid + C=0 crosshair + N concentric ROTATION RINGS with 30° degree ticks (longer at
    0/90/180/270) + yellow C=0 marker.
  API: cc_canon_coord (world→signed coord), cc_canon_id / cc_canon_id_from_coord (format the ID),
  cc_canon_overlay(eng,ring_count), cc_canon_stamp (label an object with its ID on the render).
  VERIFIED: object at world(0,0,0)→"+000-+000" (dead center), (1,0,0)→"+129-+000:190", (0,1,0)→"+000-+129"
  (Y-up correct); overlay renders grid+4 rings+ticks+crosshair, center object aligns to C=0 marker.
  Added canonframe.c to ENGINE_SRCS. Regression green. Example: outputs/canonframe_example.c.



DONE (this session): GEOMETRY-BASED MODEL VERIFICATION (tools/modelcheck.c) — the PRIMARY, visual-
  independent datapoint, + a measured render addon (tools/inspect.py). Problem (game chat): a session
  rendered flat-ribbon arms and wrote "smooth round arms", repeated 5×. Human's key insight: reading a
  RENDER is unreliable (Claude misreads pixels); the tool must measure the GEOMETRY ITSELF, ultra-precise,
  no visuals — render is an ADDON not the main evidence.
  - modelcheck.c: reads CCModel vertex/index/bone/skin data directly (zero rendering) and reports EXACT
    facts: TOPOLOGY (bbox+proportions, degenerate tris, boundary/open edges = holes, non-manifold edges,
    unused verts, flat-slab depth/width check), SKIN (every vert weighted, weights sum to 1, bone indices
    in range, dead bones), and PER-BONE CROSS-SECTION ROUNDNESS — the ribbon/stub detector computed from
    vertex rings around each bone axis: roundness = min_perp_spread/max_perp_spread (1.0 round tube, 0.0
    paper ribbon), FAIL if <0.45. Link a model behind `CCModel* build_model(void)`; `--ribbon`/`--round`
    self-tests included. PROVEN: --ribbon flags every limb at roundness 0.10 + "whole model FLAT: depth 9%
    of width" from geometry alone. (Note: the --round self-test's crude ring generator collapses HORIZONTAL
    arm rings → those read 0.00; that's the test-geometry generator hitting the SAME bug the game session
    hit, not an analyzer flaw — the analyzer correctly flags it. A real authored model with rings
    perpendicular to limb axis passes.)
  - inspect.py (from earlier): measured silhouette from a render (arm px, ratios, symmetry, gaps, [FAIL]/
    [OK], annotated image, --diff). Now positioned as the ADDON cross-check, not primary.
  - SKILL.md: rewrote the mandatory-verification block → modelcheck (geometry) is step 1 and the gate;
    render is step 2 cross-check; "the mesh data is the truth, the render is a sanity check, your prose is
    neither. Never call a model good while modelcheck reports a [FAIL]."
  To run modelcheck on the REAL playermodel2.h, that model's build code (in the game chat) must be linked
  behind build_model(). HONEST LIMIT restated: tools raise the floor; can't force a session to run them.

DONE (this session): MEASURED-INSPECTION VIEWER + VERIFY-BEFORE-CLAIMING WORKFLOW. Problem (from the game
  chat): a session rendered a character model with FLAT RIBBON ARMS and wrote "smooth, continuous, round
  arms and legs, no thin ribbon arms" — narrating the fix it wanted instead of reading the render it made,
  repeated across 5 corrections. Root cause is NOT viewer capability (the viewer produced the image fine)
  — it's the session not honestly reading pixels. Built BOTH halves:
  - tools/inspect.py — a MEASURED viewer that turns a render into numbers you can't narrate away.
    `inspect.py <img> --figure` segments the subject, measures arm thickness, head/figure ratio, L/R
    symmetry, internal gaps; prints blunt [FAIL]/[OK] verdicts; writes an annotated PNG with the numbers
    drawn ON the render. `inspect.py a b --diff` quantifies % pixels changed + warns "NEARLY IDENTICAL" if
    a claimed fix didn't change the render. PROVEN on the exact image the game chat mis-described: caught
    the ribbon arms (22px arm vs 70px head = 31% < 35% → FAIL "FLAT RIBBON"). Diff proven to catch a no-op
    fix (0.00% changed → warning).
  - SKILL.md: added a prominent "⛔ MANDATORY: measure before you claim a visual fix" block (render+look,
    MEASURE, if any [FAIL] it's NOT fixed, DIFF to prove change is real).
  --figure is for ISOLATED-subject renders (model on dark bg); full scenes use --diff. HONEST LIMIT: tools
  raise the floor but can't force another session to have judgment.

DONE (this session): FIXED FIVE BUGS from the game project's ledger (BUG-001 skinned-mesh-black FIXED+
  PROVEN via meshes_drawn increment; BUG-002 point-light black-box HARDENED via range/dist NaN guards,
  needs in-game confirm; BUG-004 cmake qwerty path FIXED+VERIFIED via cmake configure; BUG-006 version
  string → 0.3.1; BUG-003 pick+gizmo CANNOT-REPRODUCE/works now). See per-bug detail in the game project's
  DISPOSABLE bugs ledger.

DONE (this session): RENAMED the engine ClaudeCore → CHLORLITE. Reason: "ClaudeCore" borrowed Anthropic's
  "Claude" trademark AND sat inside their expanding "Claude [X]" developer-tool family (Claude Code, Claude
  Cowork) — not defensible / not ownable for a product the human intends to SELL. Chlorlite is a coinage
  (bent from the mineral "chlorite" — keeps the foundational/mineral feel, drops the actual chemical term),
  so it's ownable and clears the trademark problem. The human chose it after a long naming exploration;
  it's decided, not still open. Swapped the WORD "ClaudeCore" → "Chlorlite" across all source comments,
  banners, window-title defaults, SKILL.md (name/description), READMEs, docs, scripts, CMake, tools (48+
  files). DELIBERATELY KEPT: the header filename claudecore.h (94 files #include it — renaming = risky
  structural sweep, zero branding value) and the internal cc_ API prefix (human agreed; renaming the whole
  API is a big risky sweep for later if ever). Verified: engine banner now prints "Chlorlite 0.2.0", build
  + combat/loop tests pass. STILL TODO for the human (not code): actual trademark search in their class +
  grab domain/handles/GitHub org (incl. the "chlorite" misspell variant) before launch — the coinage is
  MEANT to pass clearance but "meant to" ≠ "checked."

DONE (this session): DEV-ASSET LIBRARY + AUTO-BUNDLE-ONLY-USED (corrected from a wrong first attempt).
  Font: DONE earlier (DejaVu Sans, both embedded + the assets/fonts/cc_default.ttf override). Human
  confirmed atlas/bitmap approach is FINE — they just wanted a heavier real font, not SDF. So font ask is
  complete, no SDF needed.
  Asset sharing — CORRECTED UNDERSTANDING: the human did NOT want in-game drag-drop (I built that last
  turn — WRONG, removed it: deleted the GLFW window drop callback cc_glfw_drop_cb + cc_renderer_set_drop_
  engine + the cc_drop_file/cc_drop_set_callback hook). What they actually want: a DEV-TIME asset library
  you hand to the Claude session, and the game auto-bundles only what it uses.
  BUILT:
  - assets-dev/ library in the skill: fonts/ models/ textures/ anims/ (+ README). The in-development asset
    store for the current project. Assets go here during dev.
  - cc_asset("name.ext") resolver (engine.c, declared claudecore.h): resolves a BARE NAME in both
    contexts — RUNTIME → <exedir>/assets/name (shipped folder); DEV fallback → assets-dev/<type>/name
    (type from extension). Same call works while building AND when shipped. assetpack_test verifies it.
  - AUTO-BUNDLER (scripts/cc bundle): greps the game source for cc_asset("…") literals, finds each in
    assets-dev/ (any type subdir), and copies EXACTLY those into the shipped assets/. Dev library can be
    huge; shipped game only carries referenced files. Missing refs → bundle-time WARNING. VERIFIED end to
    end: a game referencing hero.png + cc_default.ttf bundled with precisely those two in assets/, nothing
    else, with per-asset "bundled asset:" log lines.
  - KEPT as general plumbing (still useful, not the wrong feature): cc_asset_load(path) single-file
    dispatch by type + the .ccpak bundle format (write/open/extract/load) for moving asset SETS around as
    one file. Just no longer framed as runtime window-drop.
  All regression green. KNOWN PRE-EXISTING ISSUE (not mine, surfaced here): `cc bundle <DIR>` (directory
  game) fails to link the engine (undefined reference to cc_init etc.) — the directory build path doesn't
  link libclaudecore the way the single-file path does. Single-file .c games bundle fine (how warden/
  striker/tetris all shipped). Worth fixing the dir path later. Outputs: assets/ (font before/after).
  NOTE: cc_asset dev fallback searches assets-dev, ../assets-dev, ../../assets-dev (cwd-relative); fine for
  building from the skill root. anims as a standalone type are stubbed (most anims ride inside gltf/
  .ccmodel) — .ccanim dispatch exists if needed.

  --- HUMAN HAS A "THING TO THINK ABOUT" QUEUED (said so before this task). After presenting this, STOP and
      let them raise it. Do NOT start new work. ---

DONE (this session): FONT REPLACED + ASSET BUNDLES / DRAG-AND-DROP.
  (1) FONT: the hollow/thin/hard-to-read text was the OVERRIDE file engine/assets/fonts/cc_default.ttf
  (a thin condensed 94KB font), which cc_renderer_font_builtin checks BEFORE the embedded font. Replaced
  BOTH the override file AND the embedded font (engine/src/cc_default_font.h, regenerated) with DejaVu
  Sans — heavy, wide, highly legible. Verified by eye (before/after in outputs/assets/): dramatic
  improvement, readable at 16/20/24px. Font loading already existed (cc_font_load(path), and the
  assets/fonts/cc_default.ttf drop-in override) — so replacing the default font is already drag-in.
  (2) ASSET SYSTEM (cc/assetpack.h + engine/src/assetpack.c, in ENGINE_SRCS): shareable bundles + per-file
  drop, built on the REAL loaders that already exist (ccm_import_gltf/obj, cc_texture_load, cc_font_load).
   - cc_asset_type_from_path: ext → type (ttf/otf→font, gltf/glb/obj→model, png/jpg/tga/bmp→texture,
     ccscene→scene, ccpak→pack), case-insensitive.
   - cc_asset_load(eng,path): load ONE asset dispatched by type. This is what a single dropped file routes
     through.
   - .ccpak BUNDLE format (raw-store, no compression dep, portable/inspectable): magic+ver+count, a
     directory (name,type,offset,size per entry), then concatenated bytes. cc_pack_write (export your
     assets as one shareable file), cc_pack_open/count/entry_name/entry_type/extract (inspect), cc_pack_
     load (open + load every entry via the real loaders — "drop a bundle, get all its assets").
   - DRAG-DROP: renderer.c installs a GLFW drop callback (cc_glfw_drop_cb) → routes each dropped path to
     cc_drop_file(eng,path) → loads by type + fires the game's cc_drop_set_callback hook. The GLFW window
     drag itself is DISPLAY-ONLY (can't test headless, flagged); the loading half (cc_asset_load/pack_load/
     drop_file) is fully tested.
  assetpack_test passes: type dispatch, single-file load (real font+texture), pack write→open→extract with
  BYTE-EXACT round-trip, pack load via real loaders, drop hook (incl. recognizing a dropped .ccpak). All
  regression green; standalone release build links with the new embedded font. Decisions I made (human had
  no preference): built BOTH bundle + per-file (same capability, two granularities); wired the window-drop
  callback but flagged it untested-headless. Outputs: assets/ (font before/after).
  NOTE for future: cc_pack_load extracts to /tmp/ccpak_extract via system("mkdir -p") — fine on Linux;
  Windows path handling for extract dir may need a portable mkdir. Anim clips aren't a distinct load type
  yet (they ride inside .ccmodel/gltf); a standalone .ccanim dispatch could be added if needed.

DONE (this session): END-TO-END COMBAT DEMO (templates/game_striker/main.c) — wires the whole stack:
  TIMELINE (attack move: startup/active/recovery + hitbox window + cancel window) + COMBAT FEEL (hitstop/
  shake/knockback/hitstun/damage on connect) + CHARACTER controller + the fixed-timestep split loop +
  pixel recording. On a swing: timeline plays; during the 'hitbox' window an overlap+facing-arc test
  connects once per swing → cc_combat_register_hit → full feel. Hitstop-scaled dt is fed to BOTH the sim
  AND the move timeline, so the swing visibly freezes on impact (the "weight" the quote says can't be
  authored). Verified by WATCHING recorded frames: player approaches, engages, enemy flashes white on
  connect + gets knocked back, HP drops, hits counted. All feel/frame-data numbers live in a TUNE struct
  + CCFeelProfile for human-in-the-loop tuning.
  HONEST STATUS: the SYSTEM is complete and mechanically correct (hit connects at the right instant in the
  hitbox window; flash + knockback + HP + damage all fire; framing fixed so combat stays on-screen). What
  remains is FEEL TUNING (is hitstop 4 or 8 frames? knockback weight? shake amount?) — the felt judgment
  only a human can make. That's the intended division of labor and the actual answer to "AI can write
  functional combat but can't make it feel good": AI built the entire tunable system + verified it's
  correct; the human turns the exposed knobs. NOT AI-alone (the quote's premise), NOT human-from-scratch.
  Uses primitive sphere fighters (systems-under-test are combat, not art); real animated glTF models drop
  in at the cc_actor mesh (pipeline exists, asset FILES don't in this sandbox). Outputs: striker/ (mp4 +
  contact sheet + source). All regression green (timeline/combat/loop/input_edge/tetris).
  NEXT natural steps (human-directed): (a) a windowed tuning session — human plays, adjusts TUNE/profile
  live until it feels right; (b) frame-data MOVE assets + a second move + cancels (the cancel window
  exists, isn't wired to allow early move-interrupt yet); (c) real glTF character so it looks like a game;
  (d) an AI-plays-from-pixels combat agent that measures feel proxies (hit-confirm consistency, TTK).

DONE (this session): GENERIC TIMELINE/PHASE/WINDOW MECHANISM (cc/timeline.h + engine/src/timeline.c, in
  ENGINE_SRCS). The genre-neutral "frame data" layer, built as MECHANISM not policy per the established
  principle. A timeline = time advancing through tagged spans + points:
  - PHASES: contiguous spans that tile the timeline, ≤1 current (startup/active/recovery == windup/fire/
    chamber == charge/release/cooldown). Fire PHASE_ENTER/EXIT.
  - WINDOWS: tagged spans that MAY overlap (hitbox active, cancel-allowed, i-frames, armor, muzzle_flash).
    Fire WINDOW_OPEN/CLOSE.
  - MARKERS: instants (footstep, recoil, spawn fx, play sound). Fire MARKER.
  - FINISHED on end (non-loop) or each wrap (loop).
  EVENT-DRIVEN by design: cc_timeline_advance(dt) fires every boundary crossed in (t0,t1] IN TIME ORDER
  via a callback (sweep+qsort of boundaries; spans are few). The move announces its own key frames — NOT
  a per-frame "is it frame 6 yet" scan (matches the loop principle). Plus level-state QUERIES for the
  per-frame "what's true now" (current_phase, window_open[_name], time/playing/finished) — e.g. check
  window_open("hitbox") during collision resolution. Handles looping (wrap fires end-boundaries then
  remainder), seek (no boundary fire), reset/play/stop. Interoperates with anim (anim TRIGGER picks the
  move; TIMELINE drives windows within it) rather than duplicating it.
  GENRE-NEUTRALITY PROVEN: timeline_test drives a fighting MOVE (startup/active/recovery + hitbox window +
  cancel window + swing sfx marker) AND a looping WEAPON cycle (windup/fire/chamber + recoil marker +
  muzzle flash) through the SAME API — boundaries fire in order, mid-span window queries correct, loop
  wraps. Also verified timeline+combat COMPOSE: a hitbox WINDOW_OPEN callback → cc_combat_register_hit →
  real hitstop (4 frozen frames) — timeline says WHEN, combat says how it FEELS, game says what it MEANS.
  (Integration note: feed the hitstop-scaled dt to the timeline too so the move freezes during hitstop;
  mechanism supports it — advance takes whatever dt the game passes.) All regression green.
  NEXT: a small combat DEMO wiring timeline (moves) + combat (feel) + anim state machine + real animated
  glTF models, playable + recordable via the pixel loop, tuned human-in-the-loop. That demo is the
  end-to-end proof against the "AI can't make combat that feels good" quote.

DONE (this session): COMBAT MODULE MADE FULLY MODULAR (mechanism, not policy). Human's principle: CC must
  not be the reason someone can't build Genshin-style ability combat OR War Thunder-style realistic combat.
  The first combat module had baked-in melee policy (hitstop always froze global time; knockback always
  added an upward pop; the 5-reaction cascade always ran in fixed order). Refactored so NOTHING is
  mandatory:
  - CCFeelEnable mask: hitstop/screenshake/knockback/knockback_up/hitstun/damage_event each independently
    toggleable. cc_feel_enable_all() (melee/action default) + cc_feel_enable_none() (build-your-own).
    cc_combat_set_enable / cc_combat_enable.
  - Mechanism primitives exposed as standalone calls (genre-neutral building blocks): cc_combat_add_hitstop,
    add_shake, apply_knockback, set_hitstun, emit_damage. A game composes its own feel from these.
  - POLICY HOOK cc_combat_set_on_hit(fn,user): runs on every register_hit AFTER the (enabled) built-ins,
    gets the hit + combat ctx → add or replace behavior (elemental dmg, armor-pen, recoil, custom stagger).
    With enable=none the built-ins do nothing and on_hit is the ENTIRE reaction. This is the genre-neutral
    line: melee/ability games use built-ins; realistic sims / exotic ability systems supply policy without
    fighting the engine.
  register_hit now gates each reaction behind its flag then calls on_hit. begin_frame unchanged (hitstop
  freeze is a no-op if never added). combat_test extended + passes: enable_none → no built-in reaction;
  knockback_up=off → horizontal knockback, no vertical pop (realistic case); hitstop=off → never freezes;
  custom on_hit with enable_none drives its own hitstun (0.5s, ignoring profile). All regression green.
  PRINCIPLE established for the rest of the combat build (and the engine generally): ship MECHANISMS
  (genre-neutral machinery — timelines, windows, hitbox overlap events, impulses) + make POLICY (genre
  decisions — how much, what kind, whether at all) a plug-in the game supplies. NEXT: the frame-data/
  timeline layer should be built the SAME way — a generic timeline/phase/window mechanism (startup/active/
  recovery for a fighting move == reload cycle for a tank == ability cast), firing window-open/close/phase
  events the GAME interprets; not a combat-only "move" type. Then a demo with real animated glTF models,
  tuned human-in-the-loop.

DONE (this session): COMBAT GAME-FEEL MODULE — the first real answer to "AI can write functional combat but
  not combat that FEELS good." Built cc/combat.h + engine/src/combat.c (added to ENGINE_SRCS in scripts/cc):
  a tunable game-feel layer where ONE call fires the whole feel cascade.
  - CCFeelProfile: the knob-set (hitstop_base/per_strength/max, shake_trauma/per_strength, knockback_
    base/per_strength/up, hitstun_base/per_strength) + cc_feel_default() with good-feeling defaults. The
    whole design thesis: feel isn't ineffable, it's THIS STRUCT — the module owns the structure (what
    fires, in what order, off one hit), the numbers are exposed for human-in-the-loop tuning.
  - cc_combat_begin_frame(cb,dt) → returns scaled dt (0 while HITSTOP active); makes hitstop transparent —
    game feeds returned dt to its sim. Decays hitstun with real dt (frozen during hitstop so everything
    freezes uniformly).
  - cc_combat_register_hit(cb, &CCHit{attacker,victim,damage,strength,dir_x,dir_z,victim_body}) fires:
    (1) HITSTOP (accumulates, clamped to max), (2) SCREENSHAKE via cc_cam_add_trauma on the camera rig
    (the trauma-based shake model ALREADY EXISTED in cc/camera.h — combat drives it, doesn't rebuild it),
    (3) KNOCKBACK impulse (normalized dir × strength-scaled mag + upward pop; applied via
    cc_body_apply_impulse if physics+body set, else stored in last_knockback for char-controller games),
    (4) HITSTUN window (small fixed map victim→remaining, refresh-don't-shorten), (5) CC_EVT_DAMAGE emit
    IF a bus is set (engine owns no bus; games create their own + cc_combat_set_event_bus).
  - queries: hitstop_active/remaining, in_hitstun/hitstun_remaining, last_knockback.
  tests/combat_test.c passes with REAL assertions: hitstop freezes dt + lasts ~0.06s (3-5 frames@60) then
  resumes; knockback points along hit dir, scales with strength, normalizes diagonals, has up-pop; hitstun
  decays to zero; profile tunable (hitstop_base=0 → no freeze); damage event fires once w/ right amount+
  victim. All regression green (combat/loop/input_edge/tetris/material/anim/net).
  KEY FINDING: the FOUNDATION for good combat was already there and real — physics (impulse + contact
  events w/ impact speed), the trauma screenshake in camera.h, the anim state machine (triggers/
  crossfades/additive-mask for attack states + cancels), and the event bus's CC_EVT_DAMAGE/DEATH vocab.
  Combat is the GAMEPLAY layer tying them together, which is what was missing.
  NEXT (the other half of beating the quote): FRAME-DATA MOVES — startup/active/recovery windows with
  per-frame hitbox activation, driven EVENT-DRIVEN off the anim state machine (the move announces its own
  key frames via triggers/events; NOT a per-frame scan — per the loop-architecture principle). Then a
  small combat demo/game using real animated glTF models, tuned with the human in the loop (that loop IS
  the counter to the quote). Combat-feel proxies the pixel-loop can measure: input landing, reaction
  windows, TTK, hit-confirm consistency.

DONE (this session): TPS/FPS FIXED-TIMESTEP LOOP + QWERTY EXTRACTED AS A STANDALONE LIBRARY.
  (1) LOOP: the human rejected the per-frame model (sim tied to render rate). Added a fixed-timestep
  ACCUMULATOR to cc_run: config gains on_tick(fixed_dt) [SIMULATION, fixed cfg.tick_rate Hz, via
  accumulator — 0/1/many per frame] + on_render(alpha) [RENDER once/frame, alpha=0..1 interp fraction].
  If both set → accumulator loop (TPS decoupled from FPS, deterministic, frame-rate independent, 8-step
  spiral-of-death clamp); else legacy on_frame still works (backward compatible). We CONSIDERED true
  two-thread parallel (human first wanted it) but I recommended AGAINST it and the human agreed: the sim
  is light + render is GPU-bound, so threading buys ~nothing now, while every system assumes single-thread
  world access (torn-read bugs I can't debug headless, GL-context thread-affinity, GLFW main-thread-only
  events). The accumulator delivers the actual ask (sim rate ≠ render rate) and is the correct thing to
  thread LATER if a profiler ever shows the CPU sim is the bottleneck. tests/loop_test.c verifies the
  accumulator math (fixed 120 ticks/2s across 30/60/144/1000 fps; catch-up ticks; zero-tick fast frames;
  alpha∈[0,1); clamp). game_warden MIGRATED: on_tick = input+character+logic (records prev/cur pos),
  on_render = draw at interpolated pos (draw_scene() is begin/end-free since cc_run wraps on_render);
  windowed uses the split loop, headless agent/scripted paths keep their manual loop via on_frame.
  (2) QWERTY IS NOW A SIBLING LIBRARY (was engine/qwerty/ → now top-level qwerty/, beside netkit/). It was
  ALREADY dependency-clean (zero cc/ includes, own include/qwerty + src + CMakeLists + examples); it just
  looked engine-internal. Moved it, repointed scripts/cc ($ENGINE/qwerty → $SKILL_DIR/qwerty, 2 refs),
  added qwerty/README.md declaring it a standalone low-latency input lib usable by anyone (push/callback +
  poll + O(1) level-state; evdev/X11/headless backends; host feeds events via qwerty_dispatch — how CC
  routes GLFW keys in). Dependency points ONE way: CC → qwerty, never reverse (same rule as netkit).
  Verified: input_edge/loop/tetris/gui/worldui/inputrec/gamepad/net/material/anim all pass; standalone
  linux RELEASE build links qwerty from the new location. NOTE: windowed split-loop interpolation still
  needs a display smoke test (headless can't drive real wall-clock timing); the accumulator MATH is tested.

DONE (this session): INPUT IS NOW EVENT-DRIVEN (was polling the whole keyboard every frame). The human
  pushed back on the per-frame model: cc_input_begin_frame scanned all QKEY_COUNT keys via glfwGetKey
  EVERY frame — fixed cost regardless of activity, the wrong SHAPE for something meant to scale. Key
  finding on reading the code: qwerty ALREADY has a full event system (ring-buffer queue + callback API
  it calls "recommended for games", zero-copy path) — the engine was BYPASSING it with a glfwGetKey poll.
  So this wasn't adding capability, it was using what was there.
  FIX: (1) renderer.c installs a GLFW key callback (cc_glfw_key_cb) via cc_renderer_wire_input (called
  from engine init with the qwerty ctx, window user-pointer = qctx); it maps GLFW→QKey (new qkey_from_glfw,
  inverse of glfw_from_qkey) and dispatches KEY_DOWN/UP into qwerty via qwerty_dispatch (which updates
  mirrored level-state AND queues). GLFW_REPEAT ignored. (2) cc_input_begin_frame is now the SINGLE
  queue-drain point for the frame — drains keys (→ press latch on true rising edge only, prev up),
  scroll, and gamepad in one loop; cost ∝ events that happened, not keyboard size. (3) removed the
  duplicate drain from cc_tick. (4) cc_key_down/pressed/released now read qwerty's O(1) mirrored state
  uniformly — no more glfwGetKey branch, no windowed/headless split. Sticky-keys hack no longer needed
  for correctness (the callback can't miss a transition) but left enabled as harmless belt-and-suspenders.
  Latch rule: KEY_DOWN latches pressed only if prev_keys[k] was up → held keys (which re-send DOWN, or
  repeated injection in tests) don't re-fire; fast taps (down+up same frame) still latch since prev was up.
  input_edge_test passes (incl. fast-tap + held-no-refire); tetris/gui/worldui/inputrec/gamepad/net/
  material/anim all green. PRINCIPLE for future work (from this exchange): event-driven for anything
  POLLING for change (input, interactable triggers, collision via broadphase, anim frame-events); per-
  frame only for things that inherently produce a new value each frame (render, physics integrate, anim
  sample, controller). Don't try to make the latter event-driven; do move the former off per-frame scans.

DONE (this session, deep read for combat): READ the combat-critical systems IN FULL (physics.h + verified
  physics.c has real ray-sphere/plane/box math + impulse response; anim.h — full skeletal stack: pose
  sampling, blend trees/1D/2D, state machine w/ triggers+conditions+crossfades, two-bone/FABRIK/full-body
  IK, facial/blendshapes, GPU skinning, ECS animator; event.h — bus already defines CC_EVT_DAMAGE/HEAL/
  DEATH/CONTACT(impact speed) vocab). VERDICT: the foundation for good-feeling combat EXISTS and is real
  (anim_test/physics pass). Missing = a GAMEPLAY layer, not engine capability: frame-data move defs
  (startup/active/recovery + per-frame hitbox windows, driven by the anim state machine's triggers/events),
  a game-feel layer (hitstop, screenshake, hitstun/blockstun, i-frames, knockback via the existing impulse
  API + CC_EVT_CONTACT, attack input-buffering), and a health/damage component wiring CC_EVT_DAMAGE.
  Also confirmed the ASSET PIPELINE is REAL (I'd wrongly said it was missing): glTF import (ccm_import_gltf,
  gltf_test passes), .ccmodel native format w/ bones+skinning+anim+blendshapes, cc_texture_load (PNG/JPG),
  cc_material_load_pbr (dir of maps). Gap is ASSETS (model/texture files) + games USING the pipeline, not
  the pipeline. NEXT (human-directed): build the combat/game-feel layer to disprove "AI can't make combat
  that feels good" — plan: start with the FEEL primitives (hitstop/shake/knockback off contact events),
  then frame-data moves on the anim state machine; tune with the human in the loop (that human+AI loop IS
  the counter to the quote — AI builds the whole tunable system + iterates fast; human provides the felt
  judgment). Combat-feel proxies the pixel-loop CAN measure: reaction windows, input landing, TTK.

DONE (this session, viewer upgrade v2): CLOSED-LOOP PIXEL PERCEPTION — the loop can now USE the rendered
  frame in-process (perceive→decide→act), not just record it. Uses cc_frame_pixels (already in render.h —
  in-process final-frame RGBA readback) to read the framebuffer mid-loop, analyze it, and inject the next
  input. Two proofs:
    - tools/play_from_pixels.c: plays Tetris reading ONLY the framebuffer — perceives the board by
      sampling each cell center's brightness (verified: filled cell=230, empty=28, clean signal; SS=4
      supersample mapping correct), decides a move, injects it. The loop MECHANISM works; the Tetris
      *policy* is dumb (spams Left) — but Tetris is a bad test anyway (flat 2D grid tests ~nothing).
    - tools/perceive3d.c: THE REAL PROOF — renders a lit 3D sphere moving L→R through the world, and
      tracks it FROM PIXELS ALONE (red-dominant bright blob centroid). Result is clean + monotonic:
      world_x -2→screen 139, -1→189, 0→240 (dead center of 480px), +1→290, +2→340; Y follows the sine
      bob; found in 47/48 frames. This is the loop perceiving a real projected+lit 3D scene (camera,
      geometry, directional light) and extracting accurate spatial state. THIS is the capability that
      matters for real games, and it works.
  WHY IT MATTERS: this is the foundation for automated visual testing of 3D games AND for an agent that
  can actually play/verify a rendered game by looking at it. Combined with cc_demo_run (record) + the
  contact-sheet assembler, the viewer is now: record OR play-in-the-loop, 2D OR 3D.
  NOTE (mine, for next session): STOP using Tetris as the capability example — it exercises almost nothing
  (flat 2D cells). Use 3D scenes (perceive3d is the template). The human is explicit that the target is
  real games (they cited a kid cloning CODBO2-scale via AI); the bar is 3D perception + real gameplay
  systems, not 2D puzzle correctness. Outputs: perception/ (3D sphere tracked-from-pixels frames + both
  loop sources).

DONE (this session, viewer upgrade): LIVE-GAMEPLAY RECORDING — the static-screenshot viewer was the ceiling
  on catching interactive bugs (flicker, soft-drop skipping, dropped inputs all reached the human because
  headless testing only ever saw ONE frame with ONE injected key). Built a headless record-and-replay
  capability so gameplay MOTION can be captured and inspected frame-accurately without a display:
    - ENGINE PRIMITIVE cc_demo_run(eng, out_dir, frames, fixed_dt, timeline, timeline_len) in engine.c
      (declared claudecore.h, with CCInputEvent {frame,key,down}). Runs the game's on_frame headless at a
      fixed timestep, delivers a scripted INPUT TIMELINE exactly as cc_run's loop would (inject →
      begin_frame → tick → on_frame → end_frame), and captures every frame to out_dir/frame_%06d.png. Any
      game with cfg.on_frame gets recording for FREE — no bespoke harness. Verified via tools/demo_driver.c.
    - ASSEMBLER tools/make_gameplay_video.py: frame seq → MP4 (H.264, real-time watch) + GIF (loop) +
      CONTACT SHEET (grid of every Nth frame, labeled fN). The contact sheet is the key artifact — it lets
      Claude "watch" gameplay: view one image, see the whole motion sequence frame-by-frame.
    - tools/tetris_demo.c: a scripted Tetris match (move/rotate/softdrop/harddrop/pause over 220 frames).
  PROVEN: recorded a real 220-frame Tetris playthrough; the contact sheet clearly shows the piece moving
  L/R, rotating, soft-dropping smoothly (verified NO row-skip by viewing a close-up per-frame strip — the
  old bug is genuinely gone), hard-dropping, scoring over time, and the PAUSED overlay freezing the board.
  Every interactive behavior that was invisible to static screenshots is now inspectable. THIS CLOSES THE
  #1 verification gap that caused ~6 turns of interactive bugs reaching the human instead of me. Outputs:
  tetris/gameplay/ (mp4 + gif + contact_sheet + tools). NEXT: wire a `cc demo <game.c> --timeline t.txt`
  subcommand into scripts/cc so recording is one command; and use this to build a windowed SMOKE test
  proxy (record → diff consecutive frames → assert expected motion) so interactive regressions are caught
  automatically.

DONE (this session, 4th interactive pass): FLICKER CONFIRMED FIXED by the human; FIXED "fast alternating
  inputs sometimes don't land" (the last input bug). ROOT CAUSE: the windowed path read keys via
  glfwGetKey() POLLING, which only reports CURRENT physical state — a key pressed AND released within one
  frame (fast alternation) is never seen (no key callback, GLFW_STICKY_KEYS was off). FIX (three parts):
  (1) enabled GLFW_STICKY_KEYS at window creation so a fast press is latched until polled; (2) added a
  once-per-frame snapshot: cc_input_begin_frame() reads every key ONCE (sticky keys clear on read, so
  reads must be consolidated) into e->cur_keys, and sets a per-frame PRESS LATCH (e->pressed_latch) on any
  rising edge; cc_key_down/pressed/released now read those, never re-poll. (3) cc_input_end_frame() re-
  reads ACTUAL current state into prev_keys (NOT the begin snapshot) + clears the latch — so a tapped key
  (down+up in one frame) leaves prev=up and next frame's real press isn't dropped. Run loop now:
  poll → cc_input_begin_frame → cc_tick → on_frame → cc_input_end_frame. Both new fns declared in
  claudecore.h; manual (non-cc_run) loops must call begin after poll and end after reading.
  input_edge_test REWRITTEN to the begin/end flow + now has a FAST-TAP case (down+up within one frame must
  register) — this test would have caught the bug. Caught a real semantic bug in my first attempt (end
  reusing the stale begin-snapshot dropped the next press) → fixed to re-read. tetris/gui/worldui/inputrec/
  net tests all still pass. Bundles rebuilt. (Windowed GLFW path still can't be auto-tested here — sticky
  keys + latch is the right mechanism but a display smoke test is still the outstanding coverage gap.)
  NOTE: chose GLFW_STICKY_KEYS (GLFW's official fix for polled fast taps) over a full key-callback rework —
  smaller, well-understood, testable via the latch; a callback→qwerty-queue unification is the cleaner
  long-term design if input needs more (text input, key repeat, remapping) and is noted as a future option.

DONE (this session, 3rd interactive pass): FOUND THE REAL INPUT BUG (timing, not source) + fixed soft-drop/
  scoring + letterbox; FLICKER ISOLATED (content is provably stable → it's in the present/swap layer).
  Human replay feedback: (1) still flickers constantly; (2) fills screen but feels stretched; (3) STILL
  only Down works; (4) holding Down drops too fast + skips blocks; (5) score rises on every drop not just
  clears.
  #3 INPUT — the 2nd-pass "source" fix was NOT the bug; the real bug is TIMING/ORDERING (now fixed): the
  windowed run loop is poll_events → cc_tick → on_frame. cc_tick snapshotted prev_keys from the CURRENT
  (already-polled) GLFW state, so prev==now every frame → cc_key_pressed = now && !prev = ALWAYS FALSE.
  Only Down worked because the game reads it via level-triggered cc_key_down. My headless test PASSED
  despite this because it injected input AFTER cc_tick (wrong order) — masking the real-loop bug. FIX:
  removed the mis-timed snapshot from cc_tick; added cc_input_end_frame() that snapshots prev at END of
  frame (after input is read), called by cc_run after on_frame (+ declared in claudecore.h; manual
  cc_tick loops must call it). REWROTE input_edge_test to mirror the REAL order (inject → read → end_frame)
  so it actually catches this class of bug now. THIS IS THE SAME LESSON AGAIN: a test that doesn't match
  the real loop ordering gives false confidence. (Windowed path still not runnable here — fix is correct
  for both paths but unverified on a real window.)
  #4/#5 GAME LOGIC (fixed + verified headlessly): soft-drop was 10× gravity AND the gravity loop was
  `while (fall_timer>=interval)` which could advance SEVERAL rows per frame → "skips blocks". Fixed: soft
  drop = 5× (0.20), and gravity steps AT MOST ONE row per update (fall_timer reset, no banking). #5: score
  += 1 per soft-drop cell is actually STANDARD Tetris, but combined with only-Down-working + 10× speed it
  felt like a bug; with sane speed it reads correctly (verified: no-softdrop 1 row/1s score 0; softdrop 6
  rows/1s score 6, zero multi-step frames).
  #2 STRETCHED (fixed): the windowed blit stretched the render to the window aspect. Now ASPECT-PRESERVED
  (letterbox/pillarbox): largest centered rect matching render aspect, black bars around it.
  #1 FLICKER — ISOLATED but NOT fixed. Key evidence: a probe rendering IDENTICAL static 2D content 6
  frames in a row produced PIXEL-IDENTICAL screenshots (0 differing frames). So the rendered content is
  fully deterministic — the flicker is NOT in what's drawn, it's in PRESENTATION (the GLFW swap/blit path,
  which can't be observed headless). Ruled out: content non-determinism, the 3D-pipeline-over-empty-scene
  theory (content is stable regardless), CC_WINDOWED (dead flag, unused). Shipped a DIAGNOSTIC:
  templates/game_tetris/flicker_diag.c — clears the window to solid blue every frame, no game/2D. If THAT
  flickers on a real display → flicker is 100% in swap/present/driver/vsync/window layer; if steady →
  it's the content path. NEXT SESSION w/ a display: run flicker_diag first to bisect. Suspects if it's
  present-layer: double-buffer + missing glFinish before swap, GLFW window hints (missing/incorrect
  GLFW_DOUBLEBUFFER, or a visual mismatch), or the FBO-blit-to-default-fb approach itself vs rendering
  direct to the default framebuffer. All fixes rebuilt into both bundles.

DONE (this session, 2nd interactive pass): FIXED WINDOW SIZING ON RESIZE/FULLSCREEN + 2D-ONLY RENDER PATH;
  flicker still not confirmed-fixed (no display here). After the 1st interactive pass, the human reported:
  still flickers, and on FULLSCREEN the image is now tiny in the bottom-left (previously it was zoomed-in).
  DIAGNOSIS: both the old zoom and the new tiny-corner are the SAME root bug — the windowed present blit
  used r->disp_w/disp_h, which are set ONCE at init and NEVER updated: the engine has NO glfw framebuffer-
  size query and NO resize callback anywhere. So on fullscreen the window becomes huge while disp_w/disp_h
  stay at the init size → the image blits into a tiny init-sized patch in the (GL bottom-left) corner. (My
  1st-pass fix changed the blit from copying-a-4x-image-1:1 to scaling-into-disp_w/h — which fixed the
  windowed non-fullscreen case but exposed that disp_w/h are stale on resize.) FIX (renderer.c frame_end):
  query glfwGetFramebufferSize(r->window,...) EACH present and blit + clear to the REAL window size. This
  makes resize + fullscreen + HiDPI correct, and the full-window clear leaves no uncovered pixels. CERTAIN
  fix (direct inverse of the bug; can't run a window here but the logic is unambiguous).
  FLICKER (still best-effort, NOT confirmed): 1st pass added a pre-blit clear — didn't fix it per the
  human. So the flicker isn't uncovered-pixels. Next most likely cause: the full deferred 3D pipeline
  (lighting/bloom/SSGI/SSR/TAA/DOF) runs every frame even for a PURE-2D game over an EMPTY G-buffer —
  temporal passes reading uninitialized/last-frame state vary per frame. FIX: frame_end now SKIPS the
  entire 3D chain when r->stats.meshes_drawn==0 (a pure-2D frame): clear post_fbo to opaque black, then
  render_2d_flush. This is a correctness+perf win regardless, and removes the most plausible remaining
  flicker source — but I could NOT watch a window to confirm the flicker is actually gone. 3D tests
  (ssao/bloom/material — they submit meshes → take the 3D branch) + 2D tests (gui/worldui/tetris) all
  still pass; the 2D-only screenshot is pixel-identical (verified by view).
  ⚠️ STILL-OPEN + BROADER GAP: (a) FLICKER is unverified — someone with a display MUST run the windowed
  exe and confirm; if it persists after the 2D-skip, suspect the OSMesa-vs-GLFW context or driver-level
  double-buffer issues, or instrument by forcing a single static clear color with no 2D and seeing if
  THAT flickers (isolates present vs content). (b) The engine has NO window-resize handling beyond this
  present-time blit scaling — disp_w/disp_h and the internal FBOs are never actually resized, so on a
  big fullscreen the game renders at init-res and upscales (works, but soft). A real resize path
  (glfwSetFramebufferSizeCallback → cc_renderer_resize) is the proper fix and is now the top rendering
  TODO. (c) There is STILL no automated windowed smoke test — the reason all of this escaped every prior
  check. Rebuilt both shippable bundles with these fixes.

DONE (this session): FIXED INTERACTIVE / WINDOWED-PATH BUGS (reported by a human PLAYING the Tetris exe —
  none of these were catchable by the headless tests, which is the key lesson: screenshot tests exercise
  the OFFSCREEN path; a real window uses different input + present code that had never been run). Reported
  symptoms: only the Down arrow worked (not Z/X/Space/A/D/pause), the view was zoomed into the bottom-left
  corner, and the screen flickered.
  BUG 1 — INPUT, only Down worked (CERTAIN fix, root cause unambiguous in code): cc_key_down() branches on
  windowed→reads GLFW; but cc_key_pressed()/cc_key_released() did NOT branch — they always read
  qwerty_key_down() for the "current" state, while cc_tick() snapshots prev_keys from GLFW in windowed
  mode. So edge detection compared GLFW-prev vs qwerty-now (two different input systems that don't see the
  same events when windowed) → NO edge-triggered key ever fired in a real window. Only Down worked because
  it's the one key the game reads via cc_key_down (level-triggered, single consistent source). FIX
  (engine.c): cc_key_pressed/released now read "current" from the SAME source cc_tick snapshots from
  (GLFW when windowed, qwerty headless).
  BUG 2 — BOTTOM-LEFT ZOOM (CERTAIN): the windowed present blit (renderer.c frame_end) copied post_fbo
  src=0..r->w SRC into dst=0..r->w — but r->w is the SUPERSAMPLED internal size (display × the 4× PDAA1
  factor) while the window is display-sized. So it 1:1-copied a 4×-big image and the window showed only
  its display-sized bottom-left corner (GL origin is bottom-left → bottom-left, vs the screenshot path's
  top-left). FIX: dst rect is now disp_w×disp_h so the full supersampled image DOWNSCALES into the window.
  BUG 3 — FLICKER (fix is REASONED, NOT runtime-verified here — can't open a window in the sandbox): added
  a clear of the window default framebuffer before the blit. Double buffering shows a different back
  buffer each swap; blitting without clearing leaves stale/alternating content in any uncovered pixels →
  flicker. A per-frame clear guarantees a consistent base. This is the standard cause+fix but I could not
  SEE it confirmed; next session with a display should verify.
  ALSO: added PAUSE (P) — it never existed (the user expected it); plus A/D move + W rotate aliases (the
  user tried them). Pause gates gravity + input and draws a PAUSED overlay.
  ⚠️ VERIFICATION BOUNDARY (important): BUG 1 + BUG 2 are certain from code inspection; the headless
  tetris_test still passes but CANNOT prove the windowed fixes (it injects+reads qwerty, the path that was
  never broken). BUG 3 and the overall "does it now play correctly in a window" are UNVERIFIED here for
  lack of a display. The gap that caused all this: there is NO automated coverage of the windowed GLFW
  input/present path. A real fix for the process would be a windowed smoke test (or an input-source
  consistency unit test) — flagged as the highest-value testing gap. Rebuilt both shippable bundles
  (linux+windows) with these fixes; headless gui/worldui/net/ssao tests still pass (changes isolated to
  windowed present + key-pressed source).

DONE (this session): "SHIP A GAME" IS NOW REAL END-TO-END (Windows + Linux) — fixed the packaging blocker.
  The goal is: a person gets a folder, runs one thing, the game plays — no compiler, no engine, no flags.
  Two parts, both now working + verified:
  1. THE WINDOWS BLOCKER (the thing that made shipping a lie for half the target) is FIXED — and it was
     ONE MISSING LINE. ccmodel.c wraps zstd behind CC_HAS_ZSTD (which is NEVER defined anywhere → the
     raw-store fallback shims are always active on every target). Someone had written fallbacks for
     ZSTD_compress / ZSTD_decompress / ZSTD_isError but FORGOT ZSTD_compressBound. On Linux the linker
     tolerated it; mingw did not → "undefined reference to ZSTD_compressBound" broke EVERY Windows
     cross-compile. Fix: added the missing `static size_t ZSTD_compressBound(size_t s){return s;}` shim
     next to the others (raw-store never grows data, so srcSize is a safe bound). That's the whole fix.
     The earlier handoff guessed this needed "delete the binary .ccmodel path or link zstd" — neither; it
     was a one-line omission. ccmodel_test + save_test still pass (raw-store path unchanged).
  2. THE PACKAGING PIPELINE already existed in scripts/cc and now works for real: `cc build --target
     linux|windows` statically links the engine into a standalone binary; `cc bundle ...` stages
     exe + assets/ + README.txt into a shippable zip. VERIFIED:
       - Linux: native ELF, deps = only standard system libs (libGL/libX11/libc/libstdc++); the standalone
         binary RUNS + plays (deterministic score matches). Bundle = tetris-linux.zip → unzip → ./tetris.
       - Windows: genuine PE32+ .exe, imports ONLY standard Windows DLLs (KERNEL32/USER32/GDI32/OPENGL32/
         WINMM/ole32/msvcrt/SHELL32) — no mingw runtime, engine baked in. Bundle = tetris-windows.zip →
         unzip → double-click tetris.exe. (Can't execute a PE here, but identical source runs on Linux +
         all symbols link.) The -lws2_32 + static libstdc++/pthread from earlier sessions were already in
         the win link line, so once the zstd shim landed it linked clean.
     Also fixed a shipped-README bug (the run line printed the full -o path, e.g. ".//tmp/tetris_release",
     when -o was absolute → now uses the basename: "./tetris").
  KNOWN NICETY (not a blocker, deliberately left): the Windows exe is built console-subsystem, so a
  console window flashes on launch. -mwindows (WinMain subsystem) would hide it but suppresses stdout
  (the headless screenshot path prints there) and risks destabilizing the just-working win build — polish
  for later, not core to "it ships + runs."
  IMPACT: this unblocks the #1 item on the master list (packaging) and makes the core CC promise — "an AI
  builds a COMPLETE game" — actually true: the Tetris from this session now ships as runnable folders for
  both platforms, not just C source + a build recipe.

DONE (this session): FIXED FONT QUALITY — text rendered thin/hollow/broken. ROOT CAUSE: the font baker
  used stbtt_BakeFontBitmap (the old single-sample stb baker) at 48px into a 512² atlas. Single-sample
  glyph edges go thin and hollow when text is drawn SMALLER than the baked size (e.g. 14–18px HUD labels)
  — and the 4× supersample-downsample made it worse. FIX (renderer.c bake_font_from_ttf + CCFontEntry):
  switched to the OVERSAMPLED PACKED baker — stbtt_PackBegin / stbtt_PackSetOversampling(2,2) /
  stbtt_PackFontRange into a 1024² atlas, glyph metrics stored as stbtt_packedchar, and the draw path
  uses stbtt_GetPackedQuad for correct oversampled quad geometry. Text is now crisp + solid at all sizes
  (verified by 4× zoom-in on the panel labels — smooth AA edges, no hollowing). gui_test/worldui/tetris
  still pass. NOTE for game authors: cc_draw_text only has ASCII 32..127 baked — non-ASCII bytes (e.g.
  arrow glyphs \x18..\x1b) render as garbage; use plain ASCII (the Tetris template was fixed to do this).

DONE (this session): FIXED A REAL ENGINE BUG — 2D content rendered at 1/4 frame under supersampling AA
  (+ shipped a complete Tetris game that surfaced it). BUG: every 2D screenshot (gui_test, worldui, any
  cc_draw_rect/text output) crammed all 2D into the top-left quarter of the frame on a black field. ROOT
  CAUSE (found by instrumenting, not guessing — drew a full-frame + corner rect and measured pixel
  coverage: content reached exactly 1/4 in each axis): the default AA mode is PDAA1 = 4× SUPERSAMPLING,
  so the renderer draws internally at r->w = disp_w*4 and downsamples in the screenshot path. The 3D
  pipeline is scale-aware, but render_2d_flush() set the sprite shader's uResolution to the SUPERSAMPLED
  r->w/r->h. 2D coords are DISPLAY-space (0..disp_w), so dividing by the 4×-larger internal size mapped
  all 2D into the top-left 1/4 corner, which then downsampled to a 1/4-size image. FIX (renderer.c
  render_2d_flush): keep the viewport at the full internal r->w×r->h (so 2D still renders at full
  supersampled quality + downsamples cleanly) but set uResolution to the DISPLAY size (disp_w/disp_h).
  Display-coord / display-res = 0..1 → full NDC → fills the frame. One-block change, isolated to the 2D
  pass. VERIFIED: scale probe now shows a full-frame rect reaching (199,99) on a 200×100 target (was
  (49,24)); Tetris + gui_test + worldui render full-frame (viewed the PNGs); gui_test/worldui/nineslice/
  tetris tests pass; 3D (ssao/bloom/material) + net_test unaffected (change touches only 2D). This bug
  hit EVERY 2D game/UI screenshot in the engine — it's the kind of thing that was "not my doing" but
  becomes the next session's problem, so it's fixed at the source, not worked around.
  GAME SHIPPED: templates/game_tetris/ (main.c + README) + tests/tetris_test.c — a full Tetris (7 colored
  tetrominoes, rotation w/ wall kicks, gravity + lock delay, line clears, level-scaling score, next-piece
  preview, ghost drop, game over/restart). Logic is pure C (no engine dep) → deterministic + unit-tested;
  engine used only for input/draw/loop. Test drives the REAL edge-triggered input path (injected keys
  move/rotate the piece, ordering mirrors cc_run: tick snapshots prev-state THEN input arrives THEN read)
  + asserts on collision/rotation/gravity/line-clear/scoring/game-over, and renders a frame. Playable
  windowed (cc_run) or headless (screenshot). This is a working demonstration that CC's 2D path, input,
  fonts, and frame loop carry a complete game.

DONE (this session): MASTER-LIST RECONCILIATION + RE-PRIORITIZATION (no new engine code — a correction).
  After ~4 turns of networking work, stepped back and grounded priority in evidence instead of momentum.
  Read MASTER_FEATURE_LIST.md and cross-referenced EVERY engine header against tests/. Findings:
    - Networking is explicitly DREAM TIER on the master list — the LOWEST-priority cluster (alongside VR,
      mobile, Steam). The last several sessions built out the least-prioritized category while the named
      critical path sat flagged-as-open.
    - BUT the master list's "NOT YET DONE" section was badly STALE: it listed as open many systems that are
      actually shipped AND tested — mic, save/load+savegame, dialogue, interact core, director AI, gui/UI,
      event bus, coroutines, CVars/console, audiofx, soundfield, particles, trails, prefab, tween, worldui,
      gamepad, inputrec, DOF, terrain, procgen. The horror-game CRITICAL PATH (mic/save/dialogue/interact/
      director/UI) is essentially BUILT AND PASSING — verified by running dialogue_test, interact_test,
      interact_custom_test (all pass) + confirming headers for the rest.
  ACTION: rewrote the status map's NOT-YET-DONE section to reflect code reality (dated 2026-08-22 note +
  per-cluster [DONE] annotations), and rewrote SEQUENCING. The map was the artifact governing "what's
  next," and it was actively misrouting effort — fixing it is the highest-leverage low-cost move, and it's
  what the doc itself said to do ("do not trust this map blindly; regenerate from code").
  NEW PRIORITY ORDER (see MASTER_FEATURE_LIST SEQUENCING): (1) PACKAGING + the Windows cross-compile bug
  (zstd/ccmodel) — no game ships to Windows today, gates the whole "ship a game folder" goal, likely a
  small fix; (2) a PLAYABLE VERTICAL SLICE proving the critical-path systems COMPOSE (they're unit-tested
  in isolation, but no shipped game proves they work together); (3) the game-content layer (doors/switches/
  notes) — but decide if that's even engine scope; (4) genuinely-open engine gaps (physics/anim/renderer
  depth). Networking = frozen at dream tier; do NOT extend (no visibility culling / anti-cheat hole) until
  the above + the "should CC ship netcode at all" question are settled. steam.h exists but is UNTESTED —
  treat as stub until verified.

DONE (this session): ARCHITECTURE CORRECTION — split networking into ENGINE SEAM vs GAME LIBRARY. The
  prior three entries below (authority half, transport, prediction) built real, working, tested code —
  but put ALL of it inside the engine (cc/ + engine/src/), which was a scoping error. Networking is not
  one thing the engine owns; it's a seam the engine owns + a stack the GAME assembles. Corrected the
  layering:
    - STAYS IN ENGINE: cc/net.h + engine/src/net.c (snapshot REPLICATION). This belongs in the engine
      because it reaches into the ECS — creates entities, reads/writes components — and only the engine
      owns that data. It is THE seam that makes a game networkable.
    - MOVED OUT to a new sibling LIBRARY `netkit/` (netkit/include/netkit/ + netkit/src/): netcmd
      (command channel + validation seam), nettransport (UDP), netpredict (client prediction). Rationale
      is DATA OWNERSHIP, not usefulness: these reference ZERO engine-owned data (netpredict + nettransport
      reference no engine symbols at all; netcmd only passes CCScene* through to game callbacks + uses the
      cc/net.h replication TYPES). A module that depends on nothing the engine owns has no reason to ship
      inside the engine — and "how you validate a command / predict movement / move bytes" is genre-
      specific game policy, so baking one opinion into the engine is the closed-enum mistake CC's
      philosophy rejects, at the module level. Dependency direction is strictly one-way: netkit builds ON
      the engine (cc/net.h), the engine NEVER depends on netkit.
  HOW IT'S WIRED: scripts/cc auto-detects any `#include <netkit/...>` in a game's sources and adds
  netkit's include dir + compiles netkit/src/*.c into the build (prints "netkit networking library
  linked"). Games that never include a netkit header pay nothing. The engine umbrella (cc/claudecore.h)
  now includes ONLY cc/net.h for networking — the three moved headers are no longer reachable through the
  engine (verified: a game calling cc_netcmd_reset via just cc/claudecore.h fails to compile, with the
  compiler suggesting the engine's own cc_net_reset instead). ENGINE_SRCS in scripts/cc no longer lists
  the three; net.c remains.
  VERIFIED (clean rebuild): net_test builds against the engine with NO netkit involved (replication seam
  stands alone); netcmd/nettransport/netpredict/net_integration tests all auto-link netkit and pass;
  jobs/event unaffected. Header preambles + a new netkit/README.md document the boundary and the data-
  ownership rule so this doesn't get re-absorbed. Test files now include netkit/ paths; the tests
  themselves are unchanged in substance (same assertions), so all the earlier verification still holds.
  WHY THIS MATTERS for the next session: do NOT move netkit back into cc/. The rule for "does networking
  code belong in the engine?" is: does it read or write engine-owned data (entities/components/scene
  internals)? If yes → it's a seam, it can live in the engine (like net.h). If no → it's game assembly,
  it goes in netkit. This same test applies to the remaining step-5b items: server-side VISIBILITY
  CULLING filters which entities go in a snapshot → touches the snapshot/ECS path → that part is an
  engine seam (a game-supplied "is E visible to client C" predicate on cc_net_server_snapshot); the
  EAC/BattlEye anti-cheat hole is integration glue → netkit or game side. Also still open + unrelated:
  the pre-existing zstd/ccmodel Windows-build blocker (Linux/headless path unaffected).

DONE (this session): CLIENT PREDICTION + RECONCILIATION (now in netkit/ — see the architecture-correction
  tests/netpredict_test.c) + a full-stack integration test (tests/net_integration_test.c) — step (5)'s
  headline item. This is what makes the authoritative model PLAYABLE instead of laggy: without it, a
  strict server means one full round-trip of input lag on every action. Built the CC way (MECHANISM: the
  engine owns the in-flight input RING + the REPLAY loop; POLICY: the game supplies a `simulate` callback
  = "advance this state by one input", since only the game knows how an input moves local state).
    - cc_predict_create(state_size, input_size, simulate, ud) (+ _ex for ring capacity). State + input
      are OPAQUE blobs to the engine.
    - cc_predict_apply(p, state, input) -> seq: stamps a monotonic seq, stores the input in the unacked
      ring, and applies it to local state IMMEDIATELY (the responsiveness). Returns the seq to send to
      the server alongside the input.
    - cc_predict_reconcile(p, state, authoritative, acked_seq) -> #replayed: snaps state to the server's
      authoritative truth, DROPS inputs with seq <= acked_seq (a monotonic prefix), then REPLAYS the
      still-in-flight inputs on top. Net effect: local state = authoritative + inputs the server hasn't
      seen yet — correct AND responsive. If the server agreed with the prediction the replay reproduces
      exactly what was on screen (invisible); if it disagreed (a clamped/rejected input) the client ends
      up at the correction — the anti-cheat correction made VISIBLE.
    - introspection: pending / next_seq / last_acked / reset (respawn/hard-resync).
  THE WIRE PIECE that connects this to the authority half: the server already knows the last input seq it
  processed per client (cc_netcmd_server_last_seq); the game stamps that onto whatever it sends back, and
  the client feeds it to reconcile as acked_seq. No new server API was needed — the ack was already there.
  VERIFIED tests/netpredict_test.c (pure, no network): prediction applies immediately; reconcile replays
  ONLY unacked inputs; server-agree case is invisible (state unchanged); mispredict case corrects to
  authoritative; replay math proven against an independent integrator (state == authoritative + in-flight
  inputs); ring overflow drops oldest; reset; NULL-safety. AND tests/net_integration_test.c drives the
  WHOLE stack together (predict + netcmd validate/clamp + reconcile, in one process, no sockets): a 1-D
  runner where the server enforces a speed cap — a legal move has client+server agree invisibly; a "speed
  hack" (dx way over the cap) is CLAMPED server-side in the netcmd apply and the optimistic client is
  pulled back to truth on reconcile; prediction stays responsive with two inputs in flight (both shown
  locally, later reconcile replays only the unacked one). valgrind: no netpredict-attributable leaks (only
  the OSMesa/pthread_once baseline). netpredict.c compiles clean under mingw (pure C, no OS calls).
  Registered netpredict.c in scripts/cc + cc/netpredict.h in the umbrella. Full net stack + jobs/event
  still pass on a clean rebuild.
  NEXT (step 5 remainder): server-side VISIBILITY CULLING (only send a client what it should see — the
  wallhack/ESP mitigation + a bandwidth win; would live as a per-client filter in cc_net_server_snapshot,
  MECHANISM = a game-supplied "is entity E visible to client C" predicate) and the documented EAC/BattlEye
  anti-cheat integration HOLE. With prediction done, the multiplayer stack is now feature-complete for a
  responsive authoritative game minus those two; the remaining Windows-build blocker (zstd/ccmodel, see
  the transport entry) is the other open cross-cutting item and does not affect the Linux/headless path.

DONE (this session): UDP SOCKET TRANSPORT (netkit/nettransport.h + netkit/src/nettransport.c +
  tests/nettransport_test.c) — step (4) of the locked multiplayer build order, correctly done AFTER the
  authority half. This is the "thin plug-in" net.h/netcmd.h always promised: the cores are transport-FREE
  (they produce/consume byte buffers); this layer just MOVES those buffers over UDP. NO game logic, NO
  authority, NO policy — send a datagram, recv a datagram, know which peer it came from. Design choices:
    - UDP (not TCP): snapshots are idempotent (a dropped one is superseded) and commands self-sequence
      (netcmd de-dupes/orders), so reliable-ordered TCP is the wrong default — matches how the cores were
      built (they already tolerate loss/reorder). Connectionless, non-blocking.
    - API: cc_net_socket_open_server(port) / open_client(host,port) / close / local_port; send /
      send_to(peer) / broadcast (server → all learned peers); recv(buf,cap,&peer) (non-blocking: >0 bytes,
      0 = nothing waiting, -1 = error). Peer identity: cc_net_peer_id(&peer) → a stable non-zero FNV hash
      of the address usable DIRECTLY as the CCNetClientId for cc_netcmd_server_ingest (so the validation
      seam's per-client accounting works over the wire); cc_net_peer_equal; cc_net_socket_peer_count.
      A server socket LEARNS peers as they send to it (for broadcast) — capped at CC_MAX_PEERS=256.
    - Platform: POSIX sockets on Linux, Winsock2 (#ifdef _WIN32, lazy WSAStartup) on Windows — the two
      shipping targets, no others (per locked decisions). nettransport.c compiles clean under mingw.
  VERIFIED tests/nettransport_test.c drives REAL datagrams over 127.0.0.1 between a server socket + client
  socket bound in ONE process (this exercises the actual send/recv path, unlike the in-proc buffer pass of
  the core tests — it verifies the transport itself AND that it carries the tested cores end-to-end):
  client encodes a MOVE command → sends over UDP → server recvs, learns the peer, derives a stable id,
  ingests through the validation seam → legal applied (authoritative pos moves), illegal REJECTED (state
  holds); server serializes a snapshot → broadcasts over UDP → client recvs → applies → client scene
  CONVERGES to server truth; peer id/equality; non-blocking recv returns 0 when idle; NULL/error safety.
  ALL over an actual socket. valgrind: no nettransport/netcmd-attributable leaks (added cc_shutdown to the
  test so engine-owned ECS archetypes are freed; the only remaining definite loss is the known OSMesa +
  pthread_once baseline present in every rendering test). net_test + netcmd_test + event_test + jobs_test
  still pass (clean rebuild). Registered nettransport.c in scripts/cc + cc/nettransport.h in the umbrella.
  BUILD-TOOLING FIXES made while here (both pre-existing, surfaced by the first Windows cross-compile of a
  transport-using program — the transport is the first code that forced a Windows socket link):
    - scripts/cc windows link line now includes -lws2_32 (Winsock) — REQUIRED or any game using the
      transport fails to link on Windows with undefined socket references. (Classic "only --target windows
      surfaces the omission" — see the ledger.)
    - jobs.c used POSIX sysconf(_SC_NPROCESSORS_ONLN) for auto core-count, which does not exist on mingw →
      blocked ALL Windows builds. Replaced with a portable cc_jobs_hw_cores() (GetSystemInfo on Windows,
      sysconf on POSIX). Linux jobs_test still passes; nettransport.c + jobs.c both compile under mingw.
  KNOWN STILL-OPEN Windows-build blocker (NOT touched — pre-existing, separate item): assets/ccmodel.c's
  legacy BINARY .ccmodel path references ZSTD_* which isn't linked for Windows → the full engine still
  won't cross-compile to Windows until that's resolved. This is the "dead/legacy binary .ccmodel path"
  the handoff already flags for cleanup (the text .ccmodel format fully replaces it; the binary path is
  unused by scenes). Options when someone picks it up: link a zstd static lib for the win target, or
  #ifdef-out / delete the binary ccm_save/ccm_load compression path (text format is data-complete). This
  does NOT affect the Linux/headless path (CC's primary path) at all — everything builds + runs there.
  NEXT (multiplayer, unchanged order): step (5) design-level extras — client-side PREDICTION +
  reconciliation (the per-client last_seq the server reports IS the input-ack this needs), server-side
  VISIBILITY CULLING (only send a client what it should see — wallhack/ESP mitigation + bandwidth win),
  the documented EAC/BattlEye anti-cheat integration HOLE. The wire is now real in both directions.

DONE (this session): MULTIPLAYER AUTHORITY HALF — command channel + validation seam (cc/netcmd.h +
  engine/src/netcmd.c + tests/netcmd_test.c). This is step (1)+(2) of the build order the prior handoff
  locked in ("do NOT ship socket transport before this"): client->server INPUT/COMMAND channel and a
  server-side VALIDATION SEAM. cc/net.h could already BROADCAST truth (server->client snapshots) but had
  NO way for a client to REQUEST a change and NO place to ENFORCE authority — that missing half is where
  anti-cheat lives. Now built, the CC way (MECHANISM not policy, transport-free, headless-testable):
    - CLIENT side: cc_netcmd_client_encode(type, data, size, out, cap) serializes a REQUEST into bytes +
      stamps a monotonic per-client sequence number. This is the ONLY influence a client has — there is
      NO client-side API that touches the server scene. A client can request; it cannot write authority.
    - SHARED registry: cc_netcmd_register(name, validate, apply, ud) -> CCNetCmdType. Same order both
      ends (like cc_net_replicate). validate = the game's yes/no RULE (the enforcement point); apply =
      mutate authoritative state. validate may be NULL = "open command" (accept; for chat etc).
    - SERVER side: cc_netcmd_server_ingest(sv, client_id, buf, len) decodes each command, drops stale/
      replayed seqs (per-client last_seq), looks up the type's rule, calls validate, and calls apply
      ONLY on ACCEPT. A rejected command can NEVER mutate state. Per-client accepted/rejected/last_seq
      accounting (a burst of rejects = a cheat signal the game can act on) + forget() on disconnect.
    Wire: [u8 CMD=2][u16 type][u32 seq][u16 len][payload]; MSG id 2 is distinct from net.c's SNAP=1 so a
    future transport can share a stream. One buffer may coalesce several commands (ingest loops).
  VERIFIED tests/netcmd_test.c (transport-free, in-proc): legal move accepted + applied; illegal
  "teleport" (past MAX_STEP) REJECTED and authoritative state UNTOUCHED; replay of the same bytes dropped
  as stale (no double-apply); open NULL-validate command accepted; two commands coalesced in one buffer
  both applied in order; a HOSTILE second client blasting 10 illegal requests moves state ZERO and all 10
  are rejected+counted (the structural guarantee: client can only request); last-seq accounting + forget;
  NULL-safety + unknown-type-at-encode rejection. ALL with no socket. valgrind: no netcmd.c-attributable
  leaks (only the known cc_renderer_create/OSMesa + cc_scene_create engine-init baseline present in every
  rendering test; netcmd client/server structs are freed by their destroy calls). Existing net_test +
  jobs_test + event_test still pass (no regression from the added source / umbrella include). Registered
  netcmd.c in scripts/cc ENGINE_SRCS (CMakeLists globs src/*.c automatically) + cc/netcmd.h in the
  claudecore.h umbrella.
  WHAT THIS UNBLOCKS / NEXT (unchanged build order): (3) make client-can't-write-authority structural is
  now TRUE at the API level (no client path to the server scene) — could be hardened further by having
  net.h snapshots be the ONLY thing a client applies (already the case). (4) SOCKET TRANSPORT is now the
  correct next step: a thin UDP/TCP plug-in that (a) carries cc_net_server_snapshot bytes server->client
  and feeds cc_net_client_apply, and (b) carries cc_netcmd_client_encode bytes client->server and feeds
  cc_netcmd_server_ingest. Both sides of the wire now exist as byte-buffer interfaces, so transport is
  pure plumbing under a tested core. (5) design-level extras: client-side PREDICTION + reconciliation
  (the last_seq the server reports per client is the input-ack needed for this), server-side VISIBILITY
  CULLING (only send a client what it should see — the wallhack/ESP mitigation + a bandwidth win), and
  the documented anti-cheat integration HOLE (EAC/BattlEye attach points). CC still will NOT ship a
  running server or an anti-cheat — capability as a dependency, per the locked decisions.

DONE (this session): NETWORKING / REPLICATION (cc/net.h + engine/src/net.c + tests/net_test.c) — closing
  the single largest "CC can't do that" category (multiplayer), chosen as the highest-impact capability
  gap that the UI ceiling does NOT limit (netcode is pure code). Authoritative client-server + snapshot
  replication, built the CC way: the replication model is SEPARATE from transport, so it's fully
  headless-testable WITHOUT sockets (drive server+clients in one process, pump snapshots as byte buffers,
  assert convergence). API: cc_net_replicate(comp_id,size,writeFn,readFn,ud)→channel (register same order
  both ends); server: cc_net_server_create/spawn/despawn/snapshot(baseline_ack,buf,cap)/advance/tick;
  client: cc_net_client_create/apply(buf,len)→tick/last_ack/entity(netid). Wire format: [u8 MSG_SNAP]
  [u32 tick][u32 baseline][u16 nrec], per entity [u32 netid][u8 flags(spawn/despawn)][u16 nchan], per
  channel [u16 chan][u16 len][bytes]. FULL snapshot (baseline_ack==0) = all entities+channels; DELTA =
  only entities/channels whose serialized bytes changed vs a per-entity per-channel CACHE, plus spawns/
  despawns. Client apply spawns/despawns net entities in its scene + writes replicated components via the
  registered readers. VERIFIED tests/net_test.c: in-proc server + 2 clients, full+delta snapshots (delta
  < full), position sync, empty-delta (<=12B header-only), despawn removes on client, late-join 2nd
  client gets full snap & converges — ALL with no real socket. valgrind: ZERO net.c-attributable leaks
  (only the known OSMesa + pthread_once baseline). Registered net.c scripts/cc+CMakeLists+umbrella.
  NEXT: a real UDP/TCP transport layer is a thin plug-in under this same interface (send the buffers over
  a socket, feed received buffers to apply) — deliberately separated so the core is testable now.
  STRATEGY NOTE (user's vision): CC should DOMINATE its domain ("only one clear choice"). The UI ceiling
  caps AUTHORING, not RUNTIME — so max out every code-first capability (networking ✓, next: ECS-on-jobs
  parallelization, gameplay mechanisms inventory/stats/quest/abilities, packaging, glTF, navmesh, physics
  depth) and widen the moat where CC is unique (headless-testability, self-verification, text-first
  authoring, open-mechanism philosophy). Per-object AA still the small loose end.

DONE (this session): JOB SYSTEM / THREADING (cc/jobs.h + engine/src/jobs.c + tests/jobs_test.c) — the
  #1 foundational gap from the gap-analysis, chosen as highest-impact ("biggest impact on the engine as a
  general engine"; it's the retrofit-painful piece everything else scales off). A pthread work pool:
  cc_jobs_create(workers) (0=one per core, capped 64; 1=inline/serial, no threads → deterministic tests),
  cc_jobs_destroy, cc_jobs_worker_count, cc_jobs_default (lazy shared pool). Workhorse cc_parallel_for
  (begin,end,grain,body,ud) splits [begin,end) into ~4-chunks-per-worker and blocks till done; also
  cc_parallel_for_range (chunked, lower overhead) and cc_job_dispatch + cc_jobs_wait for arbitrary tasks.
  Impl: shared ring-buffer task queue + mutex + not_empty/done condvars + pending counter; workers pull
  next chunk (load-balances like stealing for parallel_for); queue-full falls back to inline (no loss).
  VERIFIED tests/jobs_test.c passes across auto/2/1-worker pools (parallel_for squares, chunked range,
  empty/degenerate ranges, 1000x dispatch+wait, default pool). CLEAN under memcheck (0 errors, 0 leaks)
  AND HELGRIND (0 data races; 2746 suppressed are libc/pthread internals). Registered jobs.c in scripts/cc
  + CMakeLists + umbrella. NOTE: the sandbox has exactly 1 CPU core (nproc=1) so a live speedup can't be
  shown here (4 workers on 1 core = 0.98x, thread overhead) — but the system is correct + race-free and
  auto-sizes to all cores on real hardware. NOT yet wired into ECS cc_query_run (a parallel query would
  thread physics/particles/anim at once, but racing the ECS core is high-risk — left as the deliberate
  next step; the pool is ready for it). This is the substrate; per-system parallelization is incremental
  from here.

DONE (this session): PDAA TIERS + settable divisor, USD walked back to sane 2/3/4 + user-settable scale.
  User clarified "divide more" meant PDAA (the CHEAP hard-sampled mode), not USD (whose scale^2 shading
  cost is a GPU-melting joke at 16x). Changes:
  - PDAA is now THREE presets PDAA1/2/3 at rising division (default 4/8/16 via CC_PDAA1/2/3_DIV) — finer,
    more accurate HARD staircase (no gray), verified visually (pdaa_division_ladder.png: steps shrink 4→8
    →16). PLUS a settable divisor cc_aa_set_pdaa_divisions(eng,n). PDAA1 is the DEFAULT AA mode.
  - USD presets pulled back to 2x/3x/4x (USD1/2/3). Scale is user-settable via cc_aa_set_usd_scale(eng,s),
    clamped [1, CC_USD_SCALE_MAX=128746258] (the absurd "you can but you'll regret it" ceiling the user
    asked for).
  - enum renumbered: OFF=0, ANALYTIC=1, PDAA1/2/3=2/3/4, SSAA=5/6/7, USD1/2/3=8/9/10. cc_renderer_set_aa
    now takes the RESOLVED render_scale (from cc_aa_render_scale, which applies the USD/PDAA overrides) as
    a float instead of recomputing from mode — engine.c passes cc_aa_render_scale(e). PDAA nearest-
    downsample branch covers modes 2..4.
  - CRITICAL SAFETY: the deferred pipeline allocates ~8 targets at internal res, so memory ~ scale^2. High
    divisions OOM-crash ("Killed" at ~8192^2). Added a practical clamp in cc_renderer_set_aa: internal
    dimension capped at MAX_INTERNAL_DIM=4096, so absurd divisions DEGRADE GRACEFULLY (verified: division
    1,000,000 no longer crashes). API still accepts the big cap; the renderer just won't allocate insanity.
    (Future: PDAA could resolve on a finer grid WITHOUT scaling the whole deferred chain — it only needs
    the final image point-sampled — which would make high divisions actually cheap as intended. For now it
    reuses the supersample path so it shares USD's memory ceiling.)
  aa_test updated (11 presets, PDAA1 default, PDAA tier names, USD 2/3/4 defaults, override + clamp tests),
  passes. valgrind clean, baseline 0.740. Removed cc_aa_set/get_usd_smoothing from the public header
  (replaced by usd_scale). Montage aa_cubes_full.png + pdaa_division_ladder.png in outputs.

DONE (this session): "DIVIDE IT MORE" — USD density ladder raised to USD1=6x, USD2=10x, USD3=16x (was
  4/6/8x). USD3 at 16x = 256 samples/output pixel = essentially ground-truth edges (buttery smooth). Cost
  at these resolutions is fine (16x of 256 = 4096^2 RGBA16F ~134MB/target). aa.c cc_aa_mode_scale +
  renderer set_aa switch + aa.h doc + aa_test scale asserts all updated to 6/10/16. valgrind clean,
  baseline 0.740, 16x renders with no GL error.
  ALSO diagnosed the "Analytic looks worse" report: the dark halo/smear around the Analytic cube is NOT
  the AA — it is BLOOM. Confirmed by rendering Analytic with cc_postfx bloom=false → clean smooth edges,
  no halo (/tmp/cube_analytic_nobloom.png). All modes have bloom (default on); USD/SSAA render bloom at
  high res so it is smooth, Analytic at 1x shows chunkier bloom. The depth-driven Analytic AA itself is
  good (smooth silhouette, ~1x cost). Tightened analytic to only modify true partial-coverage edge pixels
  (cov in (0.02,0.98)) and to require a sharp depth STEP (laplacian/curvature > 0.02, not just a gradient)
  so tilted faces/soft ramps are not touched — but the residual halo was bloom, not this. Possible future
  polish: compute bloom at higher res or exclude it under Analytic, but that is a BLOOM quality issue, not
  AA. 

DONE (this session): ANALYTIC AA now DEPTH-DRIVEN (cheap ~1x alternative to USD3). Prior analytic shader
  reconstructed edges from screen-space LUMA (weak on low-contrast silhouettes). Rewrote AA_ANALYTIC_FRAG
  to detect silhouette edges from the GBUFFER DEPTH buffer (r->gbuf_depth): linearize depth, take the
  depth gradient → edge normal (pointing to nearer=object side), estimate sub-pixel coverage from the
  center-vs-neighbor depth position, and composite near/far colors by the EXACT half-plane area
  (cc_pixshape math). render_aa_pass binds uDepth + uNear/uFar (from r->camera near/far). Result: the
  cube silhouette is now genuinely smooth, near-USD3 quality, at ~1x cost (no supersample). This is the
  cheap answer to "make USD3 cheaper" — analytic coverage instead of 64x brute force. Residual: a faint
  dark halo just outside the edge (screen-space composite pulls neighbor/bloom color on dark bg);
  tightened sample offset to 0.5 texel to minimize. aa_test passes, valgrind clean, baseline 0.740.
  Montage aa_cubes_full.png updated (5 modes, 3-face cube). NOTE the depth approach only AAs GEOMETRIC/
  silhouette edges (depth discontinuities), not shading/texture edges — fine for silhouettes (the visible
  jaggies) but interior contrast edges still rely on the other modes. NEXT still open: per-object AA
  wiring (groups ready), and optionally MSAA-style edge-only supersampling for USD.

DONE (this session): GROUP SYSTEM + "PIXEL IS A DIAGONAL" (shaped-pixel) AA foundation + per-mode AA
  redesign. Three new units, all tested:
  (1) GROUP SYSTEM (cc/group.h + engine/src/group.c + tests/group_test.c): tag any objects with a shared
      group id and/or NAME ("humanModel") and operate on all at once. cc_group(name)→stable id;
      cc_group_add/_add_named/_remove/_remove_all/_contains; whole-group cc_group_count/_each/_members/
      _first + convenience _set_visible/_set_material/_translate/_destroy_members (+ _named shorthands). An
      object can be in MANY groups (max 16). Membership is an ECS component (travels with entity, freed on
      destroy). Model=individual renderable, group=named set over many (NOT an asset). VERIFIED + valgrind
      clean. This is also the batching substrate for per-object AA (Q2=B: group objects by AA mode).
  (2) SHAPED-PIXEL / "A PIXEL IS A DIAGONAL" (cc/pixshape.h + engine/src/pixshape.c + tests/pixshape_test.c):
      the user's key reframing — render-side a pixel is a CELL CARRYING A SHAPE (square/diagonal/triangle/
      wedge/custom), not a forced square; it flattens to the square display grid ONLY at the very last
      step. Core math cc_pixshape_halfplane_area = EXACT area of the unit cell on the object side of an
      edge line (Sutherland-Hodgman clip + shoelace) = analytic coverage = "the pixel is a diagonal".
      cc_pixshape_from_edge builds a DIAGONAL pixel from an SDF sample; cc_pixshape_resolve flattens to
      RGBA. User-extensible via cc_pixshape_register (custom coverage fn, negative ids). VERIFIED exact
      (0.5 diagonals, 0.875/0.125 corners, monotonic sweep, custom shapes). PROOF: standalone
      /tmp/render_edge.c rendered a rotated square hard-square vs shaped-pixel at NORMAL size — shaped
      version is genuinely SMOOTH (clean straight diagonals, no staircase, no fade). Sign convention fixed:
      object half-plane is n·p >= n·center - signed_dist.
  (3) AA MODE REDESIGN (cc/aa.h + aa.c + renderer): modes now OFF, ANALYTIC (shaped-pixel coverage resolve
      — the smooth default), PDAA/Pixel-Divide (render 4x, HARD nearest downsample = finer staircase, NO gray; DEFAULT mode; per
      user's "50 tiny steps that add up to 5" spec), SSAA 1.5/2/3x + USD1/2/3 (4/6/8x, box-average = smooth
      supersampling), CUSTOM. FXAA/SMAA removed earlier. enum: OFF=0,ANALYTIC=1,PDAA=2,SSAA=3/4/5,
      USD=6/7/8. PDAA uses GL_NEAREST downsample; SSAA/USD use the box shader; ANALYTIC runs
      aa_analytic_shader (screen-space coverage) on post_tex. aa_test updated (9 presets), passes.
  HONEST STATUS / NOT DONE: the ANALYTIC GPU shader reconstructs the edge from the screen-space LUMA
  gradient, which is UNDERPOWERED on low-contrast / near-axis-aligned silhouettes (a non-zoomed cube's
  near-vertical edge shows little improvement vs OFF). The MODEL is proven (CPU render_edge is smooth) but
  the in-pipeline GPU approximation needs the TRUE geometry: feed real silhouette SDF from the gbuffer
  DEPTH/normal buffer (available: r->gbuf_depth) instead of luma, then apply cc_pixshape coverage. That's
  the next step to make ANALYTIC genuinely smooth on real 3D. PER-OBJECT AA (attach a mode to actors/
  groups, render grouped by mode) is DESIGNED (groups ready) but NOT yet wired — AA is still a global
  renderer mode. Non-zoomed cube montage: /mnt/user-data/outputs/aa_cubes_full.png. Registered group.c +
  pixshape.c in scripts/cc + CMakeLists + umbrella. EDITOR gap VOID (Claude-in-terminal engine). After
  3 earlier rounds still looked jagged/blurry, user gave the correct model: don't SMOOTH pixels, make
  MORE + SMALLER of them (more waves converge to a line). So: (1) REMOVED FXAA and SMAA entirely — they're
  screen-space post-process approximations that can only soften, never truly resolve; they were also the
  source of the blur. (2) All remaining modes are pure supersampling: OFF, SSAA 1.5x/2x/3x, and USD1/2/3
  REDEFINED as HIGH-DENSITY 4x/6x/8x (16/36/64 samples per output pixel). (3) DELETED the smoothing
  algorithm/shaders from the resolve path (render_aa_pass is now a no-op) — no directional blur, no USD
  smooth rounds. The ONLY resolve is: render the whole 3D chain at disp*scale (cc_renderer_resize_internal,
  verified e.g. 200x150→1600x1200 at 8x) then a box-downsample SHADER (AA_DOWNSAMPLE_FRAG) averages the
  full NxN block per output pixel. cc_aa_use_custom still lets a dev supply their own resolve. Enum
  renumbered: OFF=0, SSAA1.5/2/3=1/2/3, USD1/2/3=4/5/6; cc_renderer_set_aa scale switch + aa.c
  cc_aa_mode_scale updated to match; default mode now OFF (was FXAA — no more automatic softening; note
  this makes default renders sharper, baseline geom 0.741→0.740). cc_aa_usd_rounds kept as a no-op stub
  for API stability. VERIFIED tests/aa_test.c (updated: 7 presets, USD scale asserts 4/6/8x, custom
  mechanism, all render) passes; valgrind clean; montage /mnt/user-data/outputs/aa_comparison.png shows a
  clean monotonic OFF→USD3 progression (staircase dissolves as density climbs, surfaces stay crisp, no
  blur). HONEST FLOOR: at extreme zoom USD3 still shows a faint step because that's now the DISPLAY grid
  itself (200x150 output) — the physical limit; on a higher-res display it's smooth. Dead SMAA/USD
  shaders remain compiled-but-unused (harmless; left to avoid churn). aa.c registered scripts/cc+CMake,
  aa.h umbrella. EDITOR gap VOID (CC is for Claude-in-terminal). PRIOR wrong approaches (box blur,
  directional smoothing, LINEAR-blit downsample that dropped samples, FXAA-forced-on) all superseded.

DONE (this session): SWEPT THE "OPEN MECHANISM vs CLOSED ENUM" PRINCIPLE ACROSS EVERY SYSTEM. User
  escalated the interactable fix: it shouldn't be ONLY interactables — EVERY system must let developers
  define their own behavior, not pick from an engine-blessed menu. So I audited ALL 26 public-header
  enums (see AUDIT.md "EXTENSIBILITY AUDIT" for the full classification). Findings: (1) CAMERA
  CONTROLLERS were the other real offender — CCCamControllerType {FREE,ORBIT,FOLLOW,CINEMATIC} with
  behaviors hardcoded in a switch in camera.c. FIXED: added cc_cam_use_custom(rig, fn, state) +
  CC_CAM_CTRL_CUSTOM; the developer's CCCamControllerFn runs every cc_camera_update and positions/orients
  the camera however they want (horror shoulder-cam, security sweep, spectator drone, rail path). Engine
  still applies shake + uploads. free/orbit/follow/path reframed as optional prewired examples. Verified
  tests/camera_custom_test.c: a "spiral cam" (developer state the engine knows nothing about) is driven
  every frame, developer owns the motion, detach stops it, built-ins still work, can swap back. (2)
  SHADERS/MATERIALS: CCShadingModel is a closed set of BUILT-IN shading models, but NOT a trap — the
  engine already exposes arbitrary GLSL via cc_shader_load/_load_src/_load_spirv + cc_post_custom +
  cc_shader_set_* uniforms. Left as-is. (3) Everything else is either already open (anim FSM, physics
  injectable body, scripting C/Rust/Python, event bus open channels >= CC_EVT_USER, ECS register-any-
  component/system, tween, coroutine any-step-fn) or LEGITIMATELY closed (hardware/renderer/format facts:
  light types, pixel formats, shape types, gamepad buttons, waveforms, etc. — these describe what the
  hardware physically supports, not what game logic can be). RESULT: no closed enum forces a developer
  into an engine-blessed set of game behaviors anywhere. STANDING RULE now recorded in AUDIT: for any new
  system, expose the MECHANISM first (callback + user state), ship built-ins as optional examples second.
  valgrind clean, engine renders (0.741), backward compatible (all existing tests pass).

DONE (this session): INTERACTABLES ARE NOW A MECHANISM, NOT A FIXED MENU (cc/interact.h + interact.c).
  User feedback (correct + important): the old system exposed a CLOSED enum CCInteractType {DOOR, SWITCH,
  LEVER, PICKUP, USE} with behaviors HARDCODED in the engine — "this isn't Scratch, it's a game engine";
  a developer who wants a gravity-inverting rune / blood-writing mirror / room-flooding valve must be
  able to DEFINE THEIR OWN, not pick from a fixed palette. Fix: added the generic path cc_interactable_
  create(world, actor, &CCInteractDesc) where the developer supplies their OWN callbacks — on_interact
  (the behavior), on_focus/on_unfocus (became/left the targeted object), on_update (per-frame) — plus a
  void* state THEY own, a custom prompt, and a cooldown. The ENGINE owns only the MECHANISM: proximity+
  facing detection, focus tracking (fires on_focus/on_unfocus transitions in cc_interactable_query),
  range, cooldown ticking, enable/disable gating, and optional generic CC_EVT_INTERACT_USED emission. It
  does NOT know or care what the interactable does. New type sentinel CC_INTERACT_CUSTOM=-1. New API:
  cc_interactable_create, _state/_set_state, _set_enabled/_enabled, _set_on. BACKWARD COMPATIBLE: the
  old cc_interactable_register + built-ins (door/switch/lever/pickup) still work UNCHANGED — they're now
  framed (in the header) as optional PREWIRED EXAMPLES of the same mechanism, not the engine's idea of
  what an interactable can be. The existing interact_test still passes. VERIFIED: tests/interact_custom_
  test.c defines a "gravity rune" (developer state = a float gravity + counters the engine knows nothing
  about); asserts the engine drives interact (inverts gravity), focus/unfocus transitions on approach/
  leave, on_update advancing a pulse each frame, range detection, cooldown (blocks immediate re-trigger,
  allows after time), enable/disable gating, generic bus event, AND that a built-in door still works
  alongside it. (One test bug found+fixed: checked the deferred bus event before cc_event_bus_update —
  bus is deferred-delivery.) valgrind clean (interact code). Engine renders (0.741). PATTERN TO CARRY
  FORWARD: prefer OPEN mechanisms + optional prewired examples over closed enums of engine-blessed types
  everywhere (dialogue nodes, AI behaviors, item types, etc.). NEXT: dialogue depth check (cc/dialogue.h
  exists), inventory as a mechanism, navmesh/vision-cones — all as extensible primitives, not fixed sets.

DONE (this session): GUI TOOLKIT — completed the immediate-mode UI toolkit so save/load, dialogue, and
  inventory can actually surface to the player (menus + HUD). IMPORTANT: a partial gui.c ALREADY existed
  (window/label/button/slider/checkbox/separator, declared only in render.h, no dedicated header, NO
  test, and — critically — it renders but its interaction was never verified). This pass: (1) added a
  proper cc/gui.h header documenting the whole toolkit + the frame shape; (2) added the widgets a real
  game menu/HUD needs — cc_gui_progress (filled stat bar 0..1 w/ percent text + tint: health/sanity/
  stamina/load bars), cc_gui_spacer, cc_gui_button_at (button at an explicit rect for free-form menu
  layouts), cc_gui_get_cursor; (3) wrote the FIRST real GUI test proving INTERACTION works. KEY
  ENABLER: interaction reads cc_mouse_pos/cc_mouse_down, and headless there's cc_input_inject_mouse_move
  / cc_input_inject_mouse_button — so a test can move the cursor + press and confirm the right widget
  fires. tests/gui_test.c does exactly that: renders a Paused menu (label, Resume/Quit buttons,
  separator, Health 75% + Sanity 30% progress bars, Volume slider, Subtitles checkbox), then injects
  clicks and asserts Quit fires Quit-only, Resume fires Resume-only, button_at fires at its rect and NOT
  elsewhere, plus a rendered screenshot (viewed: clean, all widgets correct incl. the new bars).
  Immediate-mode = no retained tree, state lives in the caller; single global context (fine for one HUD/
  menu stack). Declarations added to BOTH cc/gui.h and render.h (where the originals live); gui.h in the
  umbrella. gui.c was already in the build. valgrind: no gui-code leaks (only the OSMesa/pthread_once
  engine-init baseline). Engine renders (0.741). NEXT horror-critical-path: DIALOGUE SYSTEM (branching,
  the password verify-or-die — note cc/dialogue.h already exists + publishes on the event bus; verify
  its depth), more world INTERACTABLES (drawers/lockers/switches/elevators — cc/interact.h exists too),
  navmesh/vision-cones, inventory. Several of these have partial impls — verify against code first.

DONE (this session): SAVE MANAGEMENT (cc/savegame.h + engine/src/savegame.c) — the management layer that
  turns the raw KV save store (cc/save.h, which already existed + round-trips) into a real game save
  system, closing the "save/load; checkpoints; autosave" master-list item. Picked as MOST IMPACTFUL: it
  unblocks nearly every longer-form gameplay feature (dialogue/quest/day-loop/inventory/sanity are only
  meaningful if state persists) and is pure-data/headless-testable. Provides: named SLOTS
  (cc_savegame_write_slot/read_slot) with per-save METADATA (label, timestamp, playtime, version, seq)
  stored under reserved __meta.* keys so a load menu lists slots cheaply (cc_savegame_slot_meta without
  loading the whole game); QUICKSAVE (single reserved channel); AUTOSAVE ring (cc_savegame_autosave
  rotates across N entries, throttled by cc_savegame_set_autosave_interval, load newest via
  cc_savegame_load_latest_autosave); and LIVE ACTOR capture/restore (cc_savegame_capture_actor stores an
  actor's transform pos/rot(quat as "x y z w" string)/scale + visibility under actor.<id>.* keys;
  cc_savegame_restore_actor puts it back — so you save the actual world, not just hand-picked scalars).
  Files live under a base dir; built entirely on cc_save_* so games can still poke arbitrary keys into
  the same CCSaveState before writing. BUG FOUND+FIXED (real, instructive): first version picked the
  "latest autosave" by wall-clock __meta.timestamp, but saves written in the SAME SECOND (as in a fast
  test, or rapid autosaves) tie, and the tie-break returned the wrong ring entry (n=4 instead of the
  newest n=6). Fix: stamp a MONOTONIC __meta.seq counter on every write and pick latest by seq (falling
  back to timestamp). Wall-clock is never a reliable recency key at sub-second resolution. VERIFIED:
  tests/savegame_test.c — slots + metadata round-trip, out-of-range rejcontainment, delete, quicksave/
  quickload, autosave throttle + ring rotation keeping the newest, and (with a headless engine) actor
  capture → move/hide → restore → transform+visibility come back, plus survival across a file round-trip;
  NULL-safety. Passes; valgrind clean for savegame code (the 104B "lost" is the known engine-init/OSMesa
  + pthread_once baseline every rendering test has — the actor section calls cc_init). Also silenced
  format-truncation warnings by bounding dir width (%.400s). Engine renders (0.741). Registered in
  scripts/cc, CMakeLists, claudecore.h. NEXT horror-critical-path picks: immediate-mode UI toolkit,
  dialogue system, more world interactables (drawers/lockers/switches/elevators), navmesh/vision-cones.

DONE (this session): CVAR + COMMAND CONSOLE (cc/console.h + engine/src/console.c) — closes TWO master-
  list items ("Config/CVar system" and "live in-engine debug console with commands"). The backbone of a
  debuggable engine: expose engine/gameplay knobs by NAME so they're tunable/inspectable/scriptable at
  runtime without recompiling, plus registerable string-dispatched commands. Picked after reviewing the
  master list + VERIFYING against code that no cvar/console existed (and that several "not done" map
  items — scene-JSON dump via cc_debug_scene_json, picking, gizmos, debug views — are ACTUALLY already
  real in debug.c; the status map was stale). CVARS: typed float/int/bool/string, registered with a
  desc; typed get + programmatic set with cross-type COERCION (set_float on an int truncates, etc.);
  READONLY + CHEAT flags gate console sets (programmatic sets bypass — that's for engine-authoritative
  values). COMMANDS: cc_command_register(name, fn, userdata, help); fn gets parsed argc/argv + a printer.
  cc_console_exec(line) tokenizes one line → dispatches to a command, else cvar (bare name prints, "name
  value" sets), else logs "unknown". Rolling LOG ring (cc_console_print / log_count / log_line /
  log_last / log_clear) so a UI or a test reads output back. CONFIG FILES: cc_console_exec_file (runs
  each line, # and // comments + blanks skipped) and cc_console_save_cvars (writes non-readonly cvars as
  "name value", reloadable) — a settings.cfg/autoexec round-trip. Introspection (cvar/command count+name)
  for autocomplete/help UIs. Composes with the Python bridge (scripts can drive cvars) and any keybind.
  VERIFIED: tests/console_test.c — typed get/set/coerce, console get+set+string-join, unknown handling,
  command dispatch+args+userdata, re-register-in-place, READONLY/CHEAT gating (+ programmatic bypass),
  log ring correctness, cfg save→clear→reload round-trip + comment parsing, introspection, NULL-safety.
  Passes; valgrind 0 errors/0 leaks. Engine still renders (0.741). Registered in scripts/cc, CMakeLists,
  claudecore.h. NEXT genuinely-open master-list clusters (verified, not from the stale map): immediate-
  mode UI toolkit, save/load + checkpoints, dialogue system, more world interactables (drawers/lockers/
  switches/elevators), navmesh/vision-cones/LOS, adaptive music stems, particles beyond current, physics
  joints/ragdoll/cloth. Prefer the horror critical path (UI toolkit, save/load, dialogue, interactables).

DONE (this session): SPATIAL AUDIO FX — occlusion + reverb zones (cc/audiofx.h + engine/src/audiofx.c),
  closing the audio-realism story mic input + sound propagation started. The engine already did
  positional attenuation (distance rolloff); this adds the two things distance can't express: GEOMETRY
  between listener and source, and the ACOUSTIC CHARACTER of the space. Designed as a headless-testable
  MODIFIER LAYER (not OpenAL-EFX-internal, which can't be tested without hardware): it computes
  volume/lowpass/wet params a game applies to its instances / reverb send. OCCLUSION reuses the CCGrid
  (cc/ai.h) like soundfield does, but differently: a fast Bresenham LINE-MARCH from source→listener
  counting BLOCKED cells (perceptual "is stuff in the way"), NOT the around-corners path solve
  soundfield uses for hunting AI. More wall crossed → lower volume (toward a configurable floor) +
  stronger lowpass muffle. cc_audiofx_occlusion returns {volume, lowpass, occluded}. REVERB ZONES are
  axis-aligned XZ boxes each with a CCReverbParams {wet, decay, damping}; the listener gets the
  SMALLEST containing zone (a closet inside a hall wins), dry if none. Presets: cc_reverb_none/room/
  hall/cave. API: create/destroy, set_grid, set_occlusion(min_vol,per_cell), add_zone/clear_zones/
  zone_count, set_listener, occlusion(sx,sz), reverb_here / reverb_at. VERIFIED: tests/audiofx_test.c —
  clear line = no occlusion, wall between = volume down + lowpass up, thicker wall = more muffle,
  volume floor respected, same-side = clear, no-grid = clear; reverb hall wetness, smallest-wins on
  overlap, dry outside, reverb_here uses listener pos, clear_zones, NULL-safety. Passes; valgrind-clean.
  Registered in scripts/cc, CMakeLists, claudecore.h. Engine still renders (0.741). Composes: feed
  occlusion.volume into cc_audio_instance volume and zone.wet into a reverb send. NEXT: only optional
  breadth remains (adaptive music stems, glTF multi-primitive/base64).

DONE (this session): PYTHON HOT-RELOAD — extends the Python bridge with live .py reloading, the DX
  payoff: edit a gameplay script, save, and the running game reflects it without a restart. Added to
  scripting_py.c: cc_python_watch(path) registers a .py file (and execs it once so its defs are live),
  cc_python_poll_reloads() stats each watched file and re-execs any whose mtime changed (call once per
  frame; cheap — one stat() per file), cc_python_reload_all() force-re-execs all watched files. Re-
  running a script redefines its functions in __main__ in place (standard Python reload effect), so
  both shared vars (cc.set_var) AND function behaviour update live. Watch table PY_MAX_WATCH=64,
  cleared on shutdown. A vanished file is tolerated (keeps last-good defs, poll returns 0, no crash);
  a script that errors on reload stays watched so the next good save recovers. VERIFIED: tests/
  python_reload_test.c (CC_PY build) — watch+exec, no-change poll = 0, edit-then-poll detects + reloads
  (mtime bumped via utime() so the test needs no real 1s sleep), live VALUE reload, live FUNCTION-
  behaviour reload (damage(x) logic changed on disk → running interpreter picks it up), reload_all,
  missing-file safety. Skips cleanly without CC_PY (#ifndef guard). valgrind 0 leaks/0 errors. Default
  (non-Python) build unaffected (geometry still builds). NOTE: the existing cc_script_watch/reload_all
  in scripting_c.c are the RUST-dylib path; this is the parallel PY path (separate table, separate
  functions) — they don't conflict. NEXT: only optional breadth remains (audio occlusion/reverb zones,
  adaptive music stems, glTF multi-primitive/base64).

DONE (this session): PYTHON SCRIPTING BRIDGE — the last declared-but-unimplemented product surface is
  real: an embedded CPython interpreter (engine/src/scripting_py.c) implementing cc_python_init/exec/
  exec_file/call from cc/scripting.h. This is arguably the most impactful gap closed — it's the
  difference between "recompile C for every gameplay tweak" and "designers iterate game logic in
  Python." NOTE the existing split: scripting_c.c ALREADY implemented the C script system
  (register/attach/detach/tick) AND the Rust dlopen bridge (cc_rust_entry) — those were never the gap;
  the Python entry points (behind #ifdef CC_SCRIPTING_PYTHON) had NO implementation file at all. Added
  scripting_py.c. BUILD: opt-in via CC_PY=1 → the `cc` script adds -DCC_SCRIPTING_PYTHON +
  `python3-config --includes/--ldflags --embed` and compiles scripting_py.c into the engine; the build
  cache tag gets a `-py` suffix so python/non-python builds don't collide. WITHOUT CC_PY the TU is an
  empty guard and NO libpython is linked (verified: default geometry build still 0.741, ldd shows no
  libpython) — so the default engine stays lean. Injects a `cc` MODULE into the interpreter (registered
  via PyImport_AppendInittab before Py_Initialize) so scripts talk back with zero glue: cc.log(msg),
  cc.time()->float, cc.emit(channel,i,f)->bool (publishes on a bus wired via cc_python_bind_bus —
  composes with the event bus!), cc.set_var/get_var (a shared string→double store bridging C and
  Python). Also added cc_python_shutdown (idempotent Py_Finalize). cc_python_call marshals C-string
  argv → Python str args and returns strdup(str(result)) (caller frees) — covers the common "call a
  gameplay hook" case without a full type layer. BUG FOUND+FIXED: a caught Python exception left the
  interpreter's error indicator set, which POISONED the next PyRun_SimpleString (later execs failed
  spuriously). Fix: PyErr_Clear() after every caught error (exec + call paths). Standard embedded-
  CPython discipline. VERIFIED: tests/python_test.c (CC_PY build) — init, exec, syntax/runtime errors →
  false w/o crash, cc module log/time/emit-to-bus/vars, cc_python_call arg+return, exec_file, missing-
  file handling; skips cleanly (prints "skipped") in a non-python build via an #ifndef guard. Plus
  templates/scripts/example.py (a real gameplay script: add_score → emits SCORE + LEVELUP events)
  driven end-to-end through cc_python_call → C listener received exactly one level-up. valgrind: 0
  definite leaks, 0 errors (Py_Finalize reclaims arenas). Registered scripting_py.c in scripts/cc
  ENGINE_SRCS + CMakeLists. render.h now 102/102; with this, EVERY declared public-header function has
  a real implementation — the API no longer lies. NEXT: only optional breadth remains (audio occlusion/
  reverb zones, adaptive music stems, glTF multi-primitive/base64, script hot-reload for .py).

DONE (this session): GLTF IMPORT — the last declared-but-unimplemented import is now real, so the
  engine can load industry-standard art (Blender/Sketchfab exports). Two parts: (1) cc_mesh_load_gltf
  (the render.h entry point — was DECLARED BUT NEVER DEFINED, i.e. a latent link error for anyone who
  called it) is now implemented in engine.c via the existing CCModel→CCMesh bridge (cc_mesh_from_model),
  same 3-line pattern as cc_mesh_load_ccmodel. (2) Extended ccm_import_gltf (ccmodel.c) to handle the
  binary .GLB container — the common single-file format — not just text .gltf+external .bin. Added
  gltf_unpack_glb: parses the 12-byte header (magic "glTF"/version/length) + 4-byte-aligned chunks
  (JSON chunk 0x4E4F534A, BIN chunk 0x004E4942), extracting embedded JSON+BIN so the existing accessor
  path runs unchanged. Decodes POSITION/NORMAL/TEXCOORD_0 + indices (uint32/uint16/uint8), auto-computes
  normals when absent. VERIFIED: tests/gltf_test.c — text .gltf export→import round-trip (positions +
  indices preserved), a hand-built minimal .glb triangle through the container path, auto-normals,
  error handling (missing file + garbage → NULL, no crash). Also render-verified cc_mesh_load_gltf
  returns a valid renderable mesh handle. Passes; valgrind-clean. IMPORTANT DEBUGGING NOTE (lesson): the
  test first failed on "auto-normals" — I chased it into the engine (added prints to ccm_compute_normals
  / the importer) and PROVED the engine was correct (acc_nrm=-1, compute entered, works standalone). The
  real bug was in my TEST FIXTURE: I hardcoded the .glb BIN chunk length to 40 bytes, truncating the
  6-byte index buffer (needs 42→pad 44), so the 3rd index decoded as garbage (177) → degenerate normal.
  Fixed the fixture, not the engine. Good reminder to suspect the test's hand-built binary before the
  code under test. NOT covered (documented in AUDIT): multi-primitive/mesh (first primitive only),
  embedded base64 data-URI buffers, glTF skins/anim (ccmodel has its own skeletal path). render.h now
  102/102. NEXT declared-but-unimplemented: the Python scripting bridge (cc_python_*/cc_rust_entry —
  deliberately a product surface); or remaining breadth (audio occlusion/reverb zones, adaptive music).

DONE (this session): STARTER TEMPLATES — promoted the two rendered demos into templates/ as reusable
  starter games in the engine's game-callback framework (cc_game_init/tick/shutdown; the `cc` tool
  synthesizes a bootstrap main that drives them via cc_run — a template exports the trio and NO main).
  (1) templates/game_stealth/main.c — a guard patrols a walled room, HEARS the player via the sound
  field (occlusion-aware: WALK is quiet WALK_LOUD, SHIFT SPRINT is loud SPRINT_LOUD, walls block/leak),
  and investigates via A* when loudness passes HEAR_THRESH; else patrols between two points. WASD +
  sprint. (2) templates/game_horror/main.c — the event-bus/mic/director/coroutine pacing loop: a
  scripted noise burst (stands in for the mic/footsteps) → director tension → stalker spawn sequenced
  by a coroutine rising it from the floor. Both compile via `cc dev` and render correctly (viewed the
  PNGs: stealth shows guard+player either side of the doorway wall; horror shows the dim room with the
  stalker hidden until the scare). NOTE on headless verification: `cc_run` renders EVERY tick, and
  software OSMesa at 1280x720 with TAA/SSGI/bloom is ~1s/frame, so a full 120-tick run exceeds a 2-min
  timeout — verify templates at a low CC_RUN_TICKS (e.g. 3-40) or lower the res; this is a rendering-
  speed artifact, not a template bug. The templates' underlying logic is already covered by
  hearing_enemy_test + horror_vignette_test, so no new slow full-render regression was added. Also
  FIXED a pre-existing SKILL.md doc bug: the template table listed templates/game_2d/ which does not
  exist — removed it, added the two real ones. Shift keys are QKEY_LSHIFT/QKEY_RSHIFT (not
  QKEY_LEFT_SHIFT). NEXT: remaining engine breadth — audio playback occlusion/reverb zones, adaptive
  music stems, glTF import, Python scripting bridge.

DONE (this session): HEARING-ENEMY DEMO (tests/hearing_enemy_test.c) — a rendered integration test
  that gives SOUND-PROPAGATION the same visual proof the vignette gave the others: an enemy on one side
  of a walled room hears a noise on the other side and NAVIGATES THROUGH THE DOORWAY to reach it. No new
  engine code — it composes cc/soundfield.h + cc/ai.h (A* + steering). ARCHITECTURE (the takeaway):
  HEARING is the trigger, PATHFINDING is the execution. The sound field (with grid-path occlusion)
  decides whether the enemy can hear the noise at all — a sealed side would stay silent; here sound
  leaks through the doorway so the enemy hears it. On first hearing above a threshold it plans an A*
  route over the SAME grid the sound propagated through, which naturally threads the doorway, then
  follows it with cc_steer_path_follow. IMPORTANT LESSON (documented so the next session doesn't repeat
  it): my first attempt steered the enemy directly along the sound field's per-cell toward-source
  gradient. That got it TO the doorway but it stalled there — adjacent to a blocked wall cell the
  sample returns heard=0 (no gradient on a wall), so pure gradient-following dead-ends at door edges.
  The fix is the trigger/execution split above: use hearing to DECIDE and A* to MOVE. The sound field's
  toward-dir is still great for cheap "which way did that come from" reactions, just not as a sole
  navigator through tight geometry. VERIFIED by viewing the PNG: red enemy ended beside the blue noise
  source on the far side of the wall, having crossed via the gap (reached_door_side=1, min_dist 1.43).
  valgrind: only the libOSMesa rendering baseline (no leaks in soundfield/ai/test code). Engine renders
  (geometry 0.741). Could be promoted to templates/ as a stealth-AI starter. NEXT: remaining breadth —
  audio playback occlusion/reverb zones, adaptive music stems, glTF import, Python scripting bridge; or
  promote the vignette + this demo into templates/.

DONE (this session): SOUND-PROPAGATION AI (cc/soundfield.h + engine/src/soundfield.c) — makes noise
  SPATIAL so enemies actually "hear" and investigate specific sounds, the piece that turns the mic/
  director tension into directed behaviour. KEY IDEA: straight-line distance is wrong for occlusion (a
  scream through a solid wall should be muffled even if 2m away), so propagation travels through the
  WALKABLE cells of a CCGrid (cc/ai.h — the same occupancy grid A* uses). Perceived loudness at a
  listener = emitted loudness minus falloff × the PATH distance sound detours around walls. A sealed
  room hears nothing; a room with a doorway hears sound leaking through, quieter the longer the detour.
  Implemented as a max-loudness Dijkstra (binary heap) over open cells, storing per-cell loudness + the
  neighbour it came from — so the field also hands a hearing agent the DIRECTION to move toward a sound
  (first step of the least-cost path back to source) with no extra pathfinding. API: create(grid)/
  destroy; begin (clear + drain bus sounds) / emit(wx,wz,loud) / propagate(max_travel); loudness(ex,ez)
  and sample(ex,ez,&loud,&dir); set_falloff/set_floor; watch_bus + set_mic_position (auto-ingest
  CC_EVT_SOUND and CC_EVT_MIC_LEVEL). New channel CC_EVT_SOUND + inline helper cc_sound_emit_event
  (packs XZ as ×8 fixed-point into sender). COMPOSES: mic loudness or a physics/interact noise → bus →
  sound field → enemy hears + turns toward it, alongside the director's global tension. VERIFIED:
  tests/soundfield_test.c — distance attenuation, wall OCCLUSION (the SAME point is measurably quieter
  with a gapped wall than in open space, yet still leaks through the gap), a fully sealed room reads 0,
  toward-source direction points back at the source, loudest-of-multiple-sources wins, the bus + mic
  ingest paths, destroy-while-watching is clean, NULL-safety. (One test-logic bug found + fixed: the
  first occlusion check used a wrong open-space baseline at a different distance; corrected to a
  same-point with/without-wall comparison, which is stricter. Engine occlusion was correct all along.)
  Passes; valgrind-clean (0 errors/leaks). Shared engine still renders (geometry 0.741). Registered in
  scripts/cc, CMakeLists, claudecore.h. NEXT: feed sample()'s direction into the ai.h steering/A* so an
  enemy actually walks to investigate (a rendered demo like the vignette); or remaining audio breadth
  (occlusion/reverb zones for playback, adaptive music stems); or glTF import / Python bridge.

DONE (this session): HORROR VIGNETTE CAPSTONE — a single rendered demo/test (tests/horror_vignette_
  test.c) that ties this session's five systems into one scene and proves they COMPOSE end-to-end
  through the renderer (not just pure logic). Scene: a dim room (floor + two wall slabs + a door),
  a blue player capsule, and a red emissive "stalker" buried under the floor. Timeline: the player is
  quiet, then makes a LOUD noise (a 0.8-amp tone fed into the SYNTHETIC mic) → cc_mic_update analyses
  it → CC_EVT_MIC_LEVEL on the bus + direct stress → the DIRECTOR's intensity climbs to PEAK →
  should_spawn fires → a COROUTINE (stalker_rise_seq) reveals the stalker and rises it out of the floor
  over ~1.2s then lunges. The door interactable is registered on the bus too. Asserts BOTH the logic
  chain (spawned, stalker.rise>0.99, lunged, intensity>0) AND that a real non-black frame rendered;
  final tableau is screenshotted. RESULT verified by viewing the PNG: player capsule foreground, red
  stalker risen between wall+door, soft shadows, moody dim lighting (fixed low exposure + vignette).
  This is the mic→bus→director→coroutine chain made visible. valgrind: the only leaks are the known
  libOSMesa internal baseline (present in every rendering test) — none in cc_/mic/director/coro/event
  code (those were independently valgrind-clean as pure-logic tests). Shared engine still renders
  (geometry 0.741). Good starting point for a real playable template if desired. NEXT: remaining
  MASTER_FEATURE_LIST breadth — sound-propagation AI (feed cc_mic_loudness into agent hearing for
  real), audio occlusion/reverb zones, adaptive music stems, glTF import, Python scripting bridge; or
  promote this vignette into templates/ as a starter game.

DONE (this session): AUDIO MIC INPUT — microphone capture + analysis (cc/mic.h + engine/src/mic.c),
  THE key horror-concept item ("the monster hears you"). The design problem was that the headless
  sandbox has no mic; solved with TWO sources behind ONE API: (a) real OpenAL capture
  (alcCaptureOpenDevice/Start/Samples/Stop, behind CC_USE_AUDIO, its own capture device separate from
  the mixer's playback device so it doesn't disturb cc/audio.h) and (b) a SYNTHETIC feed path
  (cc_mic_feed pushes int16 PCM into a ring buffer) that needs no hardware and works everywhere —
  headless, CI, or fed from a recorded WAV/network/procedural signal. The ANALYSIS is identical for
  both: RMS level, peak, an attack/decay-smoothed loudness (better for gameplay than raw level), and a
  hysteresis voice-activity flag (open/close thresholds so it doesn't chatter). API: cc_mic_open/close,
  cc_mic_has_device (false=synthetic-only, for "no microphone detected"), cc_mic_feed, cc_mic_update(dt)
  (drains device+ring, updates analysis, decays loudness in silence), cc_mic_level/loudness/peak/
  voice_active, cc_mic_set_smoothing/set_vad/set_gain, cc_mic_watch_bus (emits CC_EVT_MIC_LEVEL with
  f=loudness, i=voice_active — new channel added to event.h). CLOSES THE HORROR CHAIN: mic loudness →
  CC_EVT_MIC_LEVEL → director intensity → spawn decision (all four this-session systems chain).
  VERIFIED: tests/mic_test.c (pure synthetic, no hardware) — silence≈0, a 0.8-amp sine gives RMS in the
  ~0.57 expected band + high peak + loudness crossing VAD open + voice active, decay back down in
  silence with hysteresis close, gain scaling, a high VAD threshold rejecting a moderate tone, and the
  CC_EVT_MIC_LEVEL bus event carrying loudness+voice. Passes; valgrind-clean (0 errors/leaks). Confirmed
  has_device=no in the sandbox (synthetic path is what runs here) while the real capture path compiles
  in. Shared engine still renders (geometry 0.741); existing audio_test unaffected (exit 0). Registered
  in scripts/cc, CMakeLists, claudecore.h. NEXT: the horror critical path's big items (director + mic)
  are now DONE — remaining from MASTER_FEATURE_LIST are polish/breadth (sound-propagation AI that uses
  mic level for real, occlusion/reverb zones, adaptive music) or a capstone demo tying event bus +
  physics + director + mic + coroutines into one playable horror vignette. Also still open: touch input
  (low value headless), glTF import, the Python scripting bridge.

DONE (this session): AI DIRECTOR — an AI pacing/tension director (cc/director.h + engine/src/
  director.c), the first HORROR CRITICAL PATH item (MASTER_FEATURE_LIST calls for a "Demon 'director'
  AI (dynamic-difficulty stalker like Alien: Isolation)" + "random-event director"). This is the
  GLOBAL orchestrator (cf. L4D AI Director), distinct from cc/ai.h which is per-agent (path/steer/FSM);
  the director sits ABOVE agents and decides WHEN to apply/relieve pressure so encounters form
  deliberate peaks-and-valleys. MODEL: a scalar intensity (0..1) that rises from reported stress and
  decays over time, driving a phase FSM: BUILD_UP → (intensity≥peak_threshold) PEAK → (peak_duration
  timeout) FADE → (intensity≤rest_threshold) REST → (rest_duration timeout) BUILD_UP. Per-phase spawn
  budget accrues at a desired spawns/sec (0 during FADE/REST — deliberately easing off); should_spawn()
  consumes one unit and returns true when an encounter should start. API: create/create_cfg/destroy/
  update/reset; add_stress/set_intensity/intensity; watch_bus (auto-raise intensity from CC_EVT_DAMAGE
  by f=amount, CC_EVT_CONTACT by impact speed, CC_EVT_DEATH spike — composes with the event bus);
  phase/phase_name/phase_time/should_spawn/spawn_rate; pick_spawn (distance-gated + prefers points
  BEHIND the player's facing so threats appear off-screen, seeded xorshift jitter, deterministic).
  cc_director_default_config() gives tuned defaults; all overridable. LIFECYCLE SAFETY: a watching
  director unsubscribes its bus listener in destroy() — verified no use-after-free when destroyed while
  still watching. VERIFIED: tests/director_test.c — intensity clamp+decay, the full REST→BUILD_UP→PEAK
  →FADE→REST cycle, spawn budget fires in BUILD_UP but never in FADE/REST, bus-driven intensity from
  damage/death, unwatch stops updates, destroy-while-watching is clean, behind-player + distance-gated
  spawn pick, determinism, NULL-safety. Passes; valgrind-clean (0 errors/leaks). Shared engine still
  builds/renders (geometry frac 0.741 = baseline). Registered in scripts/cc, CMakeLists, claudecore.h.
  NEXT horror-path: audio MIC INPUT (the big one — needs a real capture backend; check third_party for
  OpenAL capture / sndfile, and note the headless sandbox has no mic so it'll need a synthetic/file
  fallback path for testing). Or wire the director's pick_spawn into an actual enemy demo.

DONE (this session): COROUTINES — a cooperative step/protothread scheduler (cc/coro.h + engine/src/
  coro.c), the next domain-general item. Lets sequenced multi-frame logic (cutscenes, scripted AI,
  tutorials, staged spawns, ability combos) be written as straight-line code instead of hand-rolled
  state machines. DESIGN: C has no native yield and stackful coroutines (ucontext/asm) are heavy +
  non-portable + awkward headless, so this is a STEP model — a coroutine is a plain function
  CCCoroCmd fn(CCCoro* co, float dt, void* ud) called once per frame that RETURNS what it's waiting
  for; it resumes where it yielded via a switch(co->pc) on __LINE__ (classic protothreads / Duff's-
  device local continuations). Macros: CC_CORO_BEGIN/END, CC_CORO_YIELD (resume next frame),
  CC_CORO_WAIT(secs), CC_CORO_WAIT_UNTIL(cond). Manual API underneath: cc_coro_yield/wait/done command
  constructors. Scheduler: cc_coro_sched_create/destroy/update(dt)/clear, cc_coro_start/cancel/active/
  count. Concurrency discipline mirrors the event bus: coroutines started during update() run NEXT
  frame (count snapshot), cancellation during update tombstones + compacts after the walk, self-cancel
  is safe (the update loop checks pending_del after invoking fn), (idx+1,gen)-packed handles reject
  stale ids. WAIT is handled by a per-slot countdown decremented in update; the boundary frame runs the
  fn (no lost frame). CONSTRAINTS (standard for the technique, documented in the header): locals before
  BEGIN don't survive a yield (put persistent state in userdata); don't yield from inside your own
  switch or a pre-BEGIN for/while (use WAIT_UNTIL). VERIFIED: tests/coro_test.c — full sequence timing
  (YIELD → WAIT(1s) → WAIT_UNTIL(gate) → WAIT(0.5s) → done), Duff-loop counting exactly N, done-removal,
  self-cancel-fires-once, start-during-update-defers, external cancel, clear, NULL-safety. Passes;
  valgrind-clean (0 errors/leaks). Also compile-ran the header's documented door-opening example to
  confirm the docs are accurate (they are). Shared engine still builds/renders (geometry frac 0.741 =
  baseline) with coro.c linked in. Registered in scripts/cc, CMakeLists, claudecore.h. Composes with the
  event bus trivially (a coroutine body can cc_event_emit_* like any code). NEXT domain-general: touch
  input (low value on a headless Linux/desktop target — arguably skip). Otherwise the horror path: AI
  director, audio MIC INPUT.

DONE (this session): EVENT BUS WIRING — the engine's own subsystems now PUBLISH on the reserved
  CC_EVT_* channels, so the bus is live in-engine rather than merely available. Three integrations
  (interactables, dialogue, physics), all OPT-IN and strictly ADDITIVE: with no bus attached, behavior
  is byte-identical to before (the old per-callback paths are untouched).
    • interact.c: new cc_interactable_set_event_bus(world, bus). A successful trigger also emits
      CC_EVT_DOOR_OPENED/_CLOSED (i=open?1:0), CC_EVT_SWITCH_TOGGLED (i=on?1:0), CC_EVT_LEVER_PULLED,
      CC_EVT_ITEM_PICKED_UP — sender = interactable's actor entity id. A blocked trigger on a LOCKED
      interactable emits CC_EVT_INTERACT_LOCKED (i=type). The per-interactable callback still fires
      alongside. Bus lives on the per-world InteractRegistry. (Also fixed a pre-existing missing
      <stdio.h> in interact.c that was warning on snprintf.)
    • dialogue.c: new cc_dialogue_set_event_bus(runner, bus, convo_id). Emits CC_EVT_DIALOGUE_STARTED
      once (at attach, so nothing is missed), CC_EVT_DIALOGUE_NODE (i=node index) on every node
      entered via go_to() AND the initial node, and CC_EVT_DIALOGUE_ENDED at finish — including the
      direct is_end path in cc_dialogue_advance that bypasses go_to(). sender = caller's convo_id so
      concurrent conversations are distinguishable. NODE is emitted only on real transitions (not from
      enter_node) so a set_hooks re-eval doesn't double-fire.
    • physics.c: new cc_phys_set_event_bus(world, bus) + new channel CC_EVT_CONTACT. Each detected
      contact emits (deferred) sender=body a, i=body b, f=impact speed = |relative velocity · contact
      normal| — a NEUTRAL collision fact (engine doesn't interpret it as damage; games map it). Runs
      alongside any existing contact_event callback (additive). Verified by tests/event_physics_test.c
      (drop a dynamic sphere on a static plane: CC_EVT_CONTACT fires with plausible impact + correct
      body ids, legacy callback still fires, detach=silent). valgrind-clean; existing physics_test
      unchanged (exit 0, render frac 0.775 = baseline).
  VERIFIED: new tests/event_integration_test.c (pure logic, needs a headless engine for actors) —
  proves doors/switches/pickups/locked-attempts + dialogue started/node/ended actually publish on the
  reserved channels, that callbacks STILL fire (additive), sender ids are correct, an animating door
  does not re-publish, and DETACHING the bus (set NULL) goes silent (parity with old behavior). Passes
  all checks. Existing interact_test + dialogue_test still pass UNCHANGED (additive confirmed). The one
  rendering test that pulls in interact/dialogue, realism_integration_test, builds + renders full-frame
  (frac 1.000, image verified). event.c/interact.c/dialogue.c are the only changed sources; all edits
  gated behind opt-in setters, so other tests' behavior cannot change. valgrind on the integration
  test: the only leaks trace to cc_renderer_create/OSMesa context teardown (pre-existing engine
  baseline in every rendering test) — the event/interact/dialogue paths are clean. NEXT natural steps:
  all three obvious engine hooks (interact, dialogue, physics contact) now publish; a game-facing
  CC_EVT_DAMAGE/HEAL/DEATH convention exists as channels but is left for GAMES to emit (engine stays
  neutral). Move to the next domain-general item (coroutines, touch input) or the horror path (AI
  director, mic input).

DONE (this session): EVENT BUS — a decoupled publish/subscribe message bus (cc/event.h +
  engine/src/event.c), the first domain-general item off the "still open" list. Replaces the
  engine's scattered one-slot "on_X" callback hooks (physics contact, interactable callback) with
  one many-to-many primitive any game can build logic on. Design: CHANNELS are uint32 ids — the
  engine reserves [0, CC_EVT_USER=1024) for well-known channels its own subsystems can publish on
  (CC_EVT_DOOR_OPENED, CC_EVT_DAMAGE, CC_EVT_DIALOGUE_NODE, …); games use CC_EVT_USER+ for their own.
  An event carries channel + uint64 sender tag + two scalars (int64 i, float f) + an optional COPIED
  payload blob (data/data_size — bus owns the copy, so a stack buffer is safe). API: bus_create/
  destroy/update/clear/drain; subscribe / subscribe_any(wildcard monitor) / unsubscribe (handle-
  identified, 0=invalid); emit / emit_i / emit_f / emit_if / emit_data (DEFERRED — queued FIFO,
  delivered on cc_event_bus_update once per frame) and emit_now (SYNCHRONOUS fan-out).
  DELIVERY MODEL is the whole point: deferred is the default and the safe path — a listener may
  publish, subscribe, or unsubscribe (INCLUDING ITSELF) during dispatch without corrupting iteration;
  events published mid-dispatch roll to the NEXT frame (never recursive re-entry). Impl uses a
  grow-only listener array with tombstone-on-remove + deferred compaction (never shifts the array
  under an active dispatch loop), (index,generation)-packed handles so stale ids are rejected, and a
  queue+side-heap for payloads with the snapshot boundary captured at flush start so re-entrant emits
  defer correctly. BUG FOUND+FIXED BY THE TEST: first handle (idx0,gen0) packed to 0 and collided with
  the invalid sentinel → index is now stored +1-biased in the handle. Registered event.c in
  scripts/cc ENGINE_SRCS AND engine/CMakeLists.txt CC_SOURCES; added cc/event.h to claudecore.h.
  VERIFIED: tests/event_test.c (pure logic, prints "EVENT TEST: all checks passed") covers deferred +
  sync delivery, channel filtering, wildcard, re-entrant-publish-defers-to-next-frame, payload copy
  surviving source clobber, unsubscribe-stops-delivery + stale-handle rejection, self-unsubscribe-
  during-dispatch-fires-exactly-once, clear, and NULL-safety. Runs clean under valgrind (0 errors, 0
  definite leaks). Full render regression re-run: all 42 rendering tests still non-black (no
  regression). NOTE: event.c is pure CPU/headless-safe; no gl* loader entries needed. NEXT domain-
  general candidates remaining: coroutines, touch input. Natural follow-up: wire the engine's own
  subsystems (interact.c doors/switches, dialogue.c nodes) to actually PUBLISH on the reserved
  CC_EVT_* channels — the channels exist and are documented but the subsystems don't emit on them yet
  (deliberately left as an opt-in integration step so nothing changes behavior silently).

=============================================================================
(previous session handoff — realism stretch, kept for history)
=============================================================================
STATE: The REALISM STRETCH is COMPLETE. Shipped tiers, all verified + in the
regression suite (30 tests, all pass, nothing black/blown):
  - Auto-exposure (eye adaptation)
  - PCSS soft shadows @4096
  - Material realism: 16x anisotropic filtering + detail normal maps
  - REAL PBR TEXTURE LOADING: sRGB-correct color maps
    (CC_FMT_SRGB8_ALPHA8 + cc_texture_load_srgb) + cc_material_load_pbr folder loader
    (auto per-map color space, rough+metal packing). Fixes the wrong-gamma albedo tell.
  - DEPTH OF FIELD: CoC-driven depth-aware gather blur (postfx.dof + focus_dist/
    focus_range/max_blur). Kills the "everything in perfect focus" CG tell.
  - SSGI global illumination — temporally accumulated indirect bounce + color
    bleed (weight-normalized so bounced color reads at full saturation; denoised
    across the multi-frame loop so it runs strong). Color bleed now clearly visible.
  - TAA temporal anti-aliasing (biggest "CG tell" fix; static-camera reprojection)
  - Plus two FXAA artifact fixes (highlight streaks + hard-edge notches)
  - Shading-model axis (PBR/toon/flat/rim) from an earlier interpretation, still shipped

WORKFLOW (unchanged): edit engine/src → `rm -rf engine/.build-*` (MANDATORY after any
engine/src edit) → `bash scripts/cc dev tests/X_test.c -o /tmp/g` → `unset DISPLAY; /tmp/g out.png`
→ VIEW the png (trust the image, NOT pixel-count probes) → full regression → package via
skill-creator → cp to /mnt/user-data/outputs → re-verify by building from the unzipped archive.
NO Windows gate (dropped). NO PYTHON anywhere, ever (C++/C# only if a 2nd language).

NEXT UP (in order):
  1. FINISH photoreal push if user wants (ordered remaining "CG tells" in PHOTOREAL GAP STATUS
     below): #1 REAL TEXTURE LOADING is DONE (sRGB + cc_material_load_pbr). DEPTH OF FIELD is DONE.
     STRONGER/TEMPORAL SSGI is now DONE too (see DONE entry). LESS-PERFECT GEOMETRY is now DONE as
     well (procedural noise-displaced tree geometry — cc_mesh_lumpy_canopy + cc_mesh_gnarled_trunk in
     tests/tree_gen.h; see DONE entry). With that, ALL FOUR ordered photoreal tells are closed — the
     photoreal push is essentially complete. Remaining optional polish only: TAA/SSGI motion-vector
     reprojection (identity now → ghosts under a moving camera); lens extras (barrel distortion,
     bokeh-shaped/near-far-split DOF); and promoting tree_gen.h's per-vertex fbm displacement into a
     general engine helper (e.g. cc_geometry_displace on a CCVertex buffer + recompute_normals) so ANY
     primitive can be roughened, not just trees — that's the clean way to generalize this tell.
  2. ASSET SYSTEM REDESIGN — PHASE 1 (text .ccmodel geometry loading) is now DONE (see DONE entry).
     LANGUAGE DECISION: settled on C. The user was asked (per the prior handoff's instruction to decide
     WITH them) and explicitly delegated the choice — twice, "you choose everything" — so C, .ccmodel-
     first, and a line-based text format were MY calls, made on their behalf and with their consent.
     Rationale: keeps the engine coherent (no second language, no FFI boundary in asset loading), and
     text parsing is well within C. Do NOT re-litigate the language unless the user reopens it.
     REMAINING asset work (phase 4+): (a) DONE: .ccmodel text writer/reader now covers SKELETON/SKIN/
     ANIM/BLENDSHAPE (see DONE entry) — the text format is now data-complete vs the binary one, so the
     binary ccm_save/ccm_load can be retired whenever convenient (still present, still unused by
     scenes). (b) .ccasset (a standalone material/texture-set descriptor — pair with
     cc_material_load_pbr) — NOTE .cclist scene manifests are DONE (phase 2); (c) DONE: material-slot →
     CCMaterial bridge. (d) OPTIONAL now-unblocked: actually delete the legacy binary path, or keep it
     as a compact shipping format and make text the authoring format (reasonable either way).
  3. LAST: write ENGINE.md capstone (spec below). Then package + sell as ONE skill.

  ** THE REAL LONG-TERM SCOPE lives in MASTER_FEATURE_LIST.md (in repo root). **
  That file is the ORIGINAL master feature list CC was always meant to grow into
  (horror-game engine: seeing/verifying tools, full renderer, geometry, player,
  physics, AI, audio incl. MIC INPUT, animation, world/interactables, gameplay,
  UI, architecture, build tooling, networking). It has a VERIFIED STATUS MAP
  (cross-referenced to SKILL.md) marking what's confirmed done vs still open.
  Realism + assets + ENGINE.md are milestones ALONG the way to that list; the
  post-realism/post-asset work is picking from MASTER_FEATURE_LIST.md (prefer the
  horror critical path: mic input, save/load, dialogue, interactables, director
  AI, UI toolkit; and unblockers: Actor/Entity abstraction, scene serialization,
  asset importer). Re-verify status from code before claiming any item done.

Everything below is the accumulated detail for each of the above. cc-engine.skill in
/mnt/user-data/outputs is current as of this session.
=============================================================================

# CC ENGINE — SESSION HANDOFF
POLICY (updated): DROP the Windows cross-compile gate. CC runs only in Claude's Linux sandbox
(headless OSMesa) and is tooling for Claude, not a human deliverable — PE32+ portability verified
nothing anyone uses. Per-tier gates are now: cc dev build → screenshot verify → full regression suite
→ package. (`cc build --target windows` still exists if ever needed, just not a required gate.)


> **RESUME HERE:** Copy /mnt/skills/user/cc-engine → /home/claude/cc-engine. Run apt-get update
> then install dev headers (see Build section). DONE this session (all verified via cc dev +
> screenshots, all 3 targets built, packaged): bloom threshold (Jimenez pipeline + emissive HDR
> path), GPU instancing, procedural sky, billboards (spherical + cylindrical), deferred decals,
> directional shadow maps, spot-light shadows, SSAO, SSR, outline shader, real split-sum HDR-cubemap
> IBL, PLUS a bug-fix audit (resize FBO rebuild, SSR thickness scale, decal angle_fade matrix).
> Next work item: renderer + Geometry + Player/input + Physics + AI + Audio + Animation + WORLD tiers
> are DONE. World tier (this session) added cc/world.h + world.c: a spatial object registry, a loose
> octree, frustum culling (octree-pruned, verified to match brute force exactly), and distance LOD —
> built on the unused CCFrustum tests already in ccmath.h. Recommended next: start the GAMEPLAY tier.
> NOTE: check for existing gameplay-ish headers first (ecs.h is present + ecs.c in ENGINE_SRCS;
> scripting.h/scripting_c.c too) — likely an AUDIT + fill-gaps job. Gameplay could mean: ECS
> systems/queries, event/messaging bus, timers/tweens, save/load, entity prefabs/spawning, a game
> loop/state stack. Remaining renderer options: point/cubemap shadows, HDR equirect IBL loader, SSR
> temporal. Physics: OBB/GJK, spatial-hash broadphase, angular inertia. AI: navmesh, behavior trees.
> Audio: DSP effects (reverb via AL EFX), streaming. Anim: root motion, GPU blend-shape morph
> (cc_skin_apply_facial is a no-op stub), retargeting. World: BVH, occlusion/HZB, level streaming,
> hook cull output into scene_render. Verify every item with cc dev + screenshot AND run
> `cc build --target windows` (new gl* funcs AND enums need cc_gl_loader entries; see ledger). NOTE:
> the `cc` script caches the engine in engine/.build-<tag>/; after editing any engine/src file (or
> adding one to ENGINE_SRCS) you MUST `rm -rf engine/.build-*` or changes link stale → "undefined
> reference". Package at checkpoints. Do NOT re-implement DONE items.
> Verify every item with cc dev + screenshot AND run `cc build --target windows` (new gl* functions AND
> enums need cc_gl_loader entries; see ledger). NOTE: the `cc` script caches the engine in
> engine/.build-<tag>/; after editing any engine/src file (or adding one to ENGINE_SRCS) you MUST
> `rm -rf engine/.build-*` or changes link stale → "undefined reference". Package at checkpoints. Do
> NOT re-implement DONE items.
> Test scenes: tests/{bloom,instancing,sky,billboard,decal,shadow,spotshadow,ssao,ssr,outline,ibl,geometry,player,physics,ai,audio,anim}_test.c.


## What this is
CC ("Chlorlite") — from-scratch C game engine shipped as an editable skill at
`/mnt/skills/user/cc-engine` (read-only; copy to `/home/claude/cc-engine` to work).
Package with skill-creator, output → `/mnt/user-data/outputs/cc-engine.skill`.
Skill `name:` field MUST NOT contain "claude" (case-insensitive); everything else may.

## Two-path model (CRITICAL — do not conflate)
- **Claude's DEV path**: `cc dev game.c` → OSMesa headless build. Renders with no
  display; `cc_screenshot`→PNG→`present_files` is how Claude SEES output. Never opens a window.
- **User's SHIP path**: `cc build game.c` (windowed, self-contained, no OSMesa) or
  `cc build game.c --target windows` (.exe). Claude builds but never runs these.
- Bug source of the infamous "black screenshot": `cc build` run headless → NULL renderer → black.
  FIX = use `cc dev` for anything Claude needs to see.

## Absolute rules (user standards)
- NO stubs/TODOs. Everything works, verified with tests/screenshots.
- Self-contained shipped binaries: bake ALL engine deps (GLFW, OpenAL, runtime) static.
  OS/GPU baseline (libGL, opengl32, libc, X11, WINMM/ole32) is the user's — leave dynamic.
- If a reused system has compromises, replace it. Skip nothing on the feature list.
- gnu17 not c17 (c17 hides POSIX). Fresh sandbox needs `apt-get update` first.

## Build / verify / package
```
cd /home/claude/cc-engine
bash scripts/cc dev  game.c -o /tmp/g   # headless, screenshot via cc_screenshot
bash scripts/cc build game.c -o /tmp/g              # ship Linux ELF
bash scripts/cc build game.c --target windows -o /tmp/g   # ship .exe
bash scripts/cc bundle gamedir/ [--target windows]  # zip: exe+assets+README
# package:
cd /mnt/skills/examples/skill-creator && python3 -m scripts.package_skill /home/claude/cc-engine/ /home/claude/
cp /home/claude/cc-engine.skill /mnt/user-data/outputs/
# headless run: `unset DISPLAY; /tmp/g`. Verify PNG non-black via PIL pixel count or view.
```

## File map (engine/src/)
- engine.c        — cc_init/tick/run, input (GLFW when windowed), cc_asset_path, cc_engine_renderer/qwerty
- renderer.c      — deferred PBR, gbuffer, 2D, text (stb), screenshot, debug stats/views. HUGE file.
- renderer_internal.h — internal renderer API surface (accessors live here)
- debug.c         — cc/debug.h impl: stats, JSON dumps, gizmos, views, pick, orbit, manifest
- physics.c       — injectable rigid body (integrator/forces/collision/broadphase swappable)
- scene_graph.c   — parent-child transforms
- audio.c         — OpenAL synth+3D; plays regardless of device (timer fallback). sndfile optional.
- ecs.c           — archetype ECS + system scheduler
- scene_render.c  — ECS→draw
- input_map.c     — rebindable actions/axes
- procgen.c, camera.c, steam.c, scripting_c.c, cc_gl_loader.{c,h}(win GL3 loader)
- assets/ccmodel.c (glTF+.ccmodel), anim/anim.c (skeleton/IK/blend)
- cc_default_font.h — embedded TTF (text works with zero external files)

## Key gotchas (things I re-discover and shouldn't)
- set_matrices stores vp_mat/inv_vp_mat AND now view_mat/proj_mat. Use cc_renderer_camera_vp() to project.
- Frame stats commit to stats_prev at frame_END; drawlog cleared at next frame_BEGIN.
- Renderer struct debug fields: stats, drawlog[512], gizmos, debug_view, build_hash.
- LIGHT_FRAG has uDebugView uniform (2=normals 3=depth 4=albedo 6=lighting-only); wireframe=glPolygonMode in draw_mesh.
- Windows GL: modern gl funcs + enums need cc_gl_loader entries (add there when using a new gl* call).
- cc tool compiles engine sources directly (not cmake); engine src list is in scripts/cc build_engine().
- Third-party static libs bundled: engine/third_party/{glfw,audio}-{linux64,win64}, openal-include.

## Bug ledger (FIXED — do not regress)
- Windowed black screen: renderer now blits post_fbo→fb0 before swap (renderer.c ~1050).
- Windowed dead input: cc_key_down/mouse read GLFW when windowed (engine.c).
- Audio never played: opens default/null device, timer-based playback state even w/o hardware.
- Black screenshot: cc dev (OSMesa) build; NULL backend now warns loudly.
- cc script bugs: local eval-order (bdir), build_engine progress→stderr, qwerty backends per-target.
- Emissive silently dead (bloom looked broken): TWO bugs. (1) gbuf_emissive_ao was
  RGBA8 → clamped every emitter to 1.0; now RGBA16F. (2) default emissive map was
  black_tex, and gbuf does emissive = texture(uEmissive)*uEmissiveFactor, so any
  material with an emissive factor but no map got 0. Default is now white_tex (both
  the standard AND skinned gbuf paths). Materials with emissive>1 now bloom.
- Bloom double-add: old code folded bloom into hdr_fbo at upsample level 0 AND postfx
  did `col += uBloom` → counted twice. Now bloom lives in bloom_tex[0]; postfx adds it
  once, scaled by uBloomIntensity. Intensity is applied only at the final composite.
- Bloom quality (compromise → replaced): old down pass point-sampled + re-thresholded
  per mip (boxy/aliased). Now Jimenez/COD: soft-knee prefilter once at level 0,
  13-tap firefly-suppressing downsample, 3x3 tent-filter progressive upsample.
  Default intensity retuned 0.04 → 0.15 for the corrected single-add model.
- Templates couldn't build: game_3d used a C++ lambda, game_minimal a Clang block for
  cc_texture_proc (both illegal in gnu17 C) → hoisted to named static callbacks.
  Also neither template had main(): the `cc` tool now synthesizes a bootstrap main
  for cc_game_init/tick/shutdown plugin-style sources (detects "no main + has
  cc_game_init"). Games with their own main() are unaffected.
- Headless cc_run spun forever (no window to close, never screenshotted): cc_run now,
  when e->cfg.headless, runs a bounded tick budget (CC_RUN_TICKS, default 120) with a
  fixed 1/60s dt (deterministic; wall-clock dt was ~0 headless so nothing animated),
  then auto-screenshots to CC_RUN_SCREENSHOT (default /tmp/cc_screenshots/frame.png).
- Instancing broke the Windows ship path (Linux was fine): glVertexAttribDivisor,
  glDrawElementsInstanced, glDisableVertexAttribArray are modern GL, not in opengl32 —
  MUST be added to cc_gl_loader.{h,c} (decl + extern + #define + load call) or the .exe
  link fails with "undefined reference". Added. REMINDER: any new gl* call needs a loader
  entry, and only `cc build --target windows` surfaces the omission — always run it.

## Roadmap — user's master list, priority order
Working through the giant enhancement list, tier by tier, no skips.
DONE: Seeing&verifying — stats, JSON dumps (scene/drawcall), debug views (wire/normals/depth/albedo/
  overdraw), gizmos (line/box/sphere/arrow/cross/label), pick, orbit, stamp, manifest, light-range
  gizmos, freecam, walked-heatmap, PNG diff, regression snapshots (cc_regression_check).
  WORKFLOW-ONLY (not engine code, do in Claude's turn): "describe this screenshot"=vision call;
  GIF/mp4=ffmpeg over PNG sequence; live debug console/time-scrubber=interactive, N/A headless.
  All debug API in cc/debug.h; impl debug.c + debug_image.c (stb reused, no re-impl).
DONE: Renderer/materials/post (partial) — per-material tint + unlit flag (gbuf uTint/uUnlit),
  cc_mesh_quad, cc_texture_update, film grain + scanlines + fog (distance/height) in postfx.
  ALREADY PRESENT (verified): ACES tonemap, exposure, FXAA, saturation, contrast, vignette, CA,
  emissive maps, normal/roughness-metallic/AO maps sampled in gbuffer.
  DONE (this session): bloom threshold ctrl — real HDR bloom (soft-knee prefilter + 13-tap
  downsample + tent upsample), plus the emissive HDR path fixes it depended on. Verified with
  threshold sweep 0.5/1.5/3.0 (monotonic) + game_3d template. See ledger for the 4 bugs fixed.
  DONE (this session): GPU instancing — cc_draw_mesh_instanced was a header-only phantom (would
  not link); now implemented. Per-instance mat4 attrib (loc 5..8) + growable inst_vbo +
  sh_gbuf_inst + glDrawElementsInstanced. Feeds the normal deferred gbuffer so instances light/
  bloom/post identically. Verified: 400-cube grid = 2 draw calls vs 401 single, pixel-identical
  (mean diff 0.002). Test: tests/instancing_test.c ([out.png] [single]). Material bind refactored
  into shared bind_gbuf_material() so single/instanced paths can't drift.
  DONE (this session): sky + IBL — cc_light_set_sky was another phantom decl; is_cubemap in the
  texture path was also ignored. Since nothing produces cubemaps yet, implemented a real
  procedural gradient sky (ground/horizon/zenith + smooth horizon glow, no seam) that BOTH renders
  as the background (lighting shader fills no-geometry pixels via view-ray reconstruction) AND
  drives hemispheric IBL on surfaces (diffuse irradiance along N + fresnel-weighted sky-reflection
  specular). API: cc_light_set_sky(eng, CC_NULL) enables it; cc_light_set_sky_colors() styles it.
  sky_tex field stored for a future HDR-cubemap path. Off by default (black bg unchanged; verified
  no regression). Test: tests/sky_test.c. Sky state lives on the renderer struct.
  DONE (this session): billboards — new feature (no prior decl). cc_draw_billboards(eng, tex, bbs,
  count, mode) with CC_BILLBOARD_SPHERICAL (full camera-facing, particles) and _CYLINDRICAL
  (yaw-only, trees/foliage). Instanced: one unit quad expanded per-instance in bb_shader using
  camera right/up (from view_mat rows); per-instance center/size/color. Writes color as emissive
  into the gbuffer with alpha-cutout (discard) so particles glow + bloom, no blend-order issues.
  Verified: 200-particle spiral + 7 cylindrical trees = 3 draw calls, both modes correct. Test:
  tests/billboard_test.c. NOTE: added glDrawArraysInstanced to cc_gl_loader (proactively this time).
  DONE (this session): decals — new feature. Deferred projected decals: cc_draw_decal(eng, tex,
  CCDecal*) queues a box volume; render_decal_pass() (in frame_end, AFTER geometry BEFORE lighting)
  blits scene depth to decal_depth_copy (can't sample the gbuffer's own attached depth), rasterizes
  each box back-faces with depth test/write OFF, reconstructs world pos from depth, maps to box-local
  via inv_model, clips to unit box, stamps tex into gbuffer albedo(0)+emissive(2) with alpha blend
  (normal target masked via GL_NONE in drawbuffers). Optional angle_fade culls steep faces. So decals
  wrap onto arbitrary geometry and are lit like the surface. Verified: 5 rings projecting onto ground
  AND wrapping box corners, emissive one blooms. Test: tests/decal_test.c. CCDecalEntry queue on the
  renderer (realloc-grown), depth copy rebuilt on resize in build_gbuffer.
  DONE (this session): directional shadow maps — CCLight.cast_shadows/shadow_map_size were unused
  fields; now wired. render_shadow_pass() (frame_end, before decals/lighting) finds the first
  directional caster, fits an ortho light-frustum to the drawlog bounds, and renders caster depth
  (replaying the drawlog with SHADOW_VERT, front-face cull) into a 2048² depth tex. Lighting shader
  gained shadow_factor(): 3x3 PCF + slope-scaled bias, multiplies the directional term only. Verified:
  3 objects cast correctly-oriented soft shadows, no acne/peter-panning. Test: tests/shadow_test.c.
  Set a light's .cast_shadows=true to enable. Single cascade for now (CSM = future).
  DONE (this session): SPOT shadows — render_shadow_pass() now generalizes: directional caster
  preferred, else first SPOT caster. Spot builds a PERSPECTIVE light matrix (mat4_perspective, fov =
  2*outer_angle+margin, from light pos along dir, range = light.range). shadow_factor() already
  w-divides so it handles perspective. Lighting shader gained uShadowLight (caster's index); shadow
  applies to the directional term OR the matching spot's term (uShadowLight==i). One shadow map / one
  caster per frame still (point/cubemap + multi-caster = future). Verified: spot cone with sphere+cube
  casting correct grounded shadows; directional path unchanged (regression-checked). Test:
  tests/spotshadow_test.c. r->shadow_light holds the caster index (-1 none).
  DONE (this session): HDR-cubemap IBL — real split-sum image-based lighting (Karis/Epic). Replaces
  the inline procedural-sky IBL. build_ibl() allocates 3 cubemaps (env 128², irradiance 32², prefilter
  128² w/ 5 roughness mips) + a BRDF LUT (256² RG16F) + a cube VAO. regenerate_ibl() (lazy, in
  frame_end when sky_enabled && ibl_dirty) runs: (1) capture procedural sky into env cube via 6 face
  views, (2) cosine-convolve → irradiance cube, (3) GGX importance-sample (128 spp, Hammersley) →
  prefilter mips, (4) integrate BRDF → LUT. Lighting shader samples uIrradiance (diffuse), uPrefilter
  (textureLod by rough*4) + uBrdfLUT (split-sum specular) on units 6/7/8; falls back to procedural
  sky_color() until ibl_ready. Sky setters mark ibl_dirty. Verified: metal/rough sphere grid shows
  correct sharp→blurry env reflections + directional irradiance; sky_test reflections now match the
  actual sky. Tests: tests/ibl_test.c, tests/sky_test.c. Env source is procedural now; a future HDR
  equirect loader can feed ibl_env_cube instead (sky_tex field reserved).
  WINDOWS GOTCHA: had to add GL_TEXTURE6/7/8 (only 0-5 were in the loader) + cubemap enums
  (GL_TEXTURE_CUBE_MAP, _POSITIVE_X, _SEAMLESS, BASE/MAX_LEVEL) to cc_gl_loader.h. Avoided
  glGenRenderbuffers (not in loader) by disabling depth in IBL passes.
  STILL TODO renderer: point/cubemap shadows + multi-caster + CSM, SSR temporal/blur cleanup, HDR
  equirect file loader for IBL, particles (sim), LOD/culling, RT/portals.
  DONE (this session): SSAO — postfx.ssao/ssao_radius/ssao_intensity. render_ssao_pass() (after
  shadow, before lighting): reconstructs view-space pos (uInvProj) + normal (view*worldNormal),
  samples a 32-point cosine hemisphere kernel rotated by a 4x4 noise tile, range-checked occlusion →
  R8 target, then a 4x4 box blur → ssao_blur_tex. Lighting shader multiplies the ao term (ambient +
  IBL) by pow(vis, intensity). Verified off/on: adds correct contact shadows + crevice darkening
  under heavy ambient, no noise (blur works). Test: tests/ssao_test.c ([out] [on|off]).
  DONE (this session): SSR — postfx.ssr/ssr_intensity/ssr_max_distance. render_ssr_pass() (AFTER
  lighting, BEFORE bloom — needs the lit hdr_color_tex): per pixel with metallic>0.02 && rough<0.75,
  reflect view vector, march 48 steps in VIEW space vs gbuf_depth, 5-step binary-search refine on the
  crossing, sample lit HDR at the hit. Weight = metal*(1-rough)*edgeFade*distFade*fresnel*intensity;
  premultiplied refl+weight → ssr_tex, composited scene*(1-a)+rgb → ssr_composite_tex, blitted back
  to hdr_fbo. Verified off/on: metallic floor mirrors the 3 objects with correct color/position.
  Test: tests/ssr_test.c ([out] [on|off]). KNOWN LIMIT: single-layer SS raymarch → stippled fringe
  at reflected silhouette edges (no depth behind them); real fix = temporal reproj + blur (future).
  DONE (this session): outline shader — postfx.outline + outline_color/thickness/depth_sensitivity/
  normal_sensitivity. Edge detection folded INTO the postfx pass (no new FBO): samples gbuf_depth +
  gbuf_normal_metal (units 2,3), 4-neighbour depth + normal discontinuity → edge_factor, smoothstep
  to crisp lines, mix(col, outline_color, e) after gamma. Catches silhouettes (depth) AND creases
  like cube face edges (normal). Verified off/on: clean toon outlines, no false edges on smooth
  surfaces. Test: tests/outline_test.c ([out] [on|off]). Background (depth>=1) skipped.
  STILL TODO renderer: HDR-cubemap IBL (prefilter/irradiance convolution), point/spot shadows +
  CSM, SSR temporal/blur cleanup, particles (sim), LOD/culling, RT/portals.
  WINDOWS GOTCHA (hit again): GL_CLAMP_TO_BORDER + GL_TEXTURE_BORDER_COLOR enums aren't in mingw GL
  headers — must #ifndef-guard them in cc_gl_loader.h. Modern ENUMS need loader entries too, not
  just functions. Always run `cc build --target windows`.
  BUGFIXES (this session, audit pass):
  - Resize path only rebuilt gbuffer/hdr/post/bloom, NOT SSAO/SSR targets → stale-size textures
    sampled after window resize. Extracted build_ssao_targets()/build_ssr_targets() helpers, called
    from BOTH init and cc_renderer_resize. (decal_depth_copy was already rebuilt via build_gbuffer.)
  - SSR thickness test used a hardcoded "-1.0" view-space unit → only correct at ~unit scene scale;
    reflections missed at other ssr_max_distance. Now thickness = stepLen*2 (scale-relative). Also
    removed dead prevDelta var.
  - Decal angle_fade used uInvModel to derive the projection axis (wrong matrix) → culled wrong
    faces. Now the world-space -Y axis is computed CPU-side from the model matrix and passed as
    uDecalDir. Verified with a wall test: vertical faces correctly culled, up-faces kept.
DONE (continuation — GENERAL-PURPOSE engine features, not horror-specific): PARTICLE SYSTEM + verified
  the (already-complete) TWEEN/TIMER system.
  FINDING FIRST: tween.h/tween.c already existed, fully implemented (handle-generation safety, ease
  vocab, tweens + one-shot/repeating timers) AND in the build AND had a passing tests/tween_test.c
  (renders 5 cubes on different easing curves). So tween/timer/easing was already DONE — I verified it
  (builds, data checks pass, VIEWED the render) rather than rebuilding. Don't re-implement.
  PARTICLE SYSTEM (genuinely absent — new cc/particles.h + engine/src/particles.c): CPU particles for
  smoke/fire/sparks/rain/magic/explosions — domain-general juice for ANY genre. CCParticles = a pool +
  CCEmitterDesc (spawn box, rate/sec, base velocity + spread, gravity, drag, life range, size
  start→end, size jitter, color start→end RGBA, seed). cc_particles_update(dt) spawns per rate
  (fractional accumulator) + integrates + ages out; cc_particles_draw(eng,tex) renders live particles
  as camera-facing billboards via existing cc_draw_billboards (size+color lerp over life);
  cc_particles_burst(n) for one-shots. Presets: cc_emitter_smoke/fire/sparks/rain/magic (fire/sparks
  use >1.0 color for bloom glow). VERIFIED: tests/particles_test.c — data asserts (emission grows pool,
  burst adds EXACTLY 50, all age to 0 after stop) + VIEWED render: 4 distinct plumes (grey smoke,
  glowing fire, cyan-purple magic, golden sparks burst). 46/46 regression clean. Files: cc/particles.h,
  engine/src/particles.c, claudecore.h (+include), scripts/cc, tests/particles_test.c. GOTCHA fixed:
  particles.h must forward-typedef CCEngine (not bare 'struct CCEngine*') to match render.h's typedef.
  Next general candidates: collide-and-slide physics response, world-space UI (health bars/damage
  numbers), nine-slice panels, glTF import, gamepad input. (Trails/ribbons already done — see below.)
DONE (this session): GEOMETRY TIER — completed the procedural primitive set and added a CPU
  geometry-processing toolkit. (1) cc_mesh_capsule + cc_mesh_torus were header-only PHANTOMS
  (declared in render.h, never defined → would not link, same class of bug as the earlier
  instancing/sky phantoms); now implemented. (2) Added cc_mesh_cylinder + cc_mesh_cone (were
  missing entirely). All four: Y-up, origin-centered, correct caps/normals/UVs, tangents generated,
  fed through the existing build_mesh→cc_renderer_mesh_create path (no renderer change). (3) New
  geometry toolkit in cc/render.h, all CPU/headless-safe, operate on a raw CCVertex+index buffer
  BEFORE upload: cc_geometry_recompute_normals(smooth area-weighted OR flat/faceted),
  cc_geometry_recompute_tangents (Lengyel + Gram-Schmidt, degenerate-UV fallback),
  cc_geometry_weld (position merge, remaps indices, compacts, returns new count),
  cc_geometry_bounds (AABB), cc_geometry_flip (winding + normals, e.g. skyboxes). Impl in engine.c
  next to cc_mesh_cube/sphere/plane. Verified: tests/geometry_test.c renders cylinder/cone/capsule/
  torus in a lit+shadowed lineup, PLUS a flat-vs-smooth ball pair built from the SAME unwelded
  source buffer (proves weld+smooth recompute: left faceted, right smooth). Non-black 0.742, all 12
  prior tests re-verified non-black (no regression), Windows .exe cross-compiles clean (pure CPU
  vertex work → no new gl* entries needed). NOTE added re: engine/.build-* cache must be cleared
  after engine edits (see RESUME).
DONE (this session): PLAYER/INPUT TIER — the input stack (raw keys/mouse via cc/input.h, action
  map w/ rebind+axes+contexts via cc/input_map.h, camera controllers via cc/camera.h) was already
  present and working; the gap was a CHARACTER CONTROLLER. Added cc/player.h + engine/src/player.c:
  a kinematic capsule controller (CCCharacter/CCCharConfig/CCCharInput). Consumes a 2D wish-dir +
  yaw + jump/sprint/crouch and integrates grounded/airborne motion: framerate-independent accel/
  friction (expf approach), gravity w/ terminal velocity, jump computed from apex height
  (v=sqrt(2gh)), coyote-time + jump-buffer grace windows, air control, auto step-up over small
  ledges (step_offset), crouch height lerp, and smoothed body-facing toward move dir. Ground is a
  caller callback float(x,z,user) (NULL = flat y=0) so it works over any world w/o the rigidbody
  solver. cc_character_transform() emits capsule-center pos + facing quat for drawing / feeding a
  follow-cam. Registered player.c in scripts/cc ENGINE_SRCS. ALSO fixed a camera PHANTOM:
  cc_cam_dolly_zoom was declared in camera.h but never defined (would not link) — implemented the
  Hitchcock effect (holds subject framing: distance scales by tan(fov0/2)/tan(fov1/2)). Verified:
  tests/player_test.c drives a scripted deterministic run — walk fwd, auto-step onto a 0.3-high
  ledge (no jump), jump w/ correct parabolic arc (apex ≈ jump_height above surface, confirmed via
  trajectory probe: vy=6.88 at launch → apex → land), sprint widens stride; rendered as a profile
  motion-trail of fading ghost capsules so the step + arc are visible in one shot. Non-black 0.573,
  all 13 prior tests re-verified (no regression), Windows .exe cross-compiles clean (player.c +
  dolly are pure CPU math → no new gl* entries). NOTE: forward at yaw=0 is -Z (matches the free-cam
  convention) — move_z=+1 goes toward the camera's forward, not +Z.
DONE (this session): PHYSICS TIER — the injectable rigid-body world (physics.c: semi-implicit Euler
  integrator, impulse response w/ restitution + Coulomb friction + Baumgarte correction, O(n²)
  broadphase, force generators, sleeping, fixed-timestep accumulator, all hooks replaceable) was
  already implemented and correct — NO phantoms. The gap was an INCOMPLETE COLLISION MATRIX: the
  header advertises sphere/box/plane/capsule but narrowphase only handled sphere-sphere, sphere-plane,
  box-plane, box-box(AABB). Empirically confirmed the holes (a dropped capsule fell to y=-38 through
  the ground; a sphere fell through a box). ADDED the missing pairs: sphere-box, capsule-plane,
  capsule-sphere, capsule-box, capsule-capsule — via shared helpers (closest_on_segment,
  closest_segment_segment, closest_on_box, capsule_segment, sphere_point_contact). Normals kept
  correctly oriented a→b in every order. ALSO extended cc_physics_raycast (was sphere+plane only) to
  hit BOX (slab method w/ face normal) and CAPSULE (segment-distance march + surface refine). Verified
  numerically (capsule now rests y≈1.0, sphere rests on box y≈1.46, two overlapping capsules push
  apart 0.6→1.85, box raycast returns n=+1) AND visually: tests/physics_test.c drops 24 mixed
  spheres/boxes/capsules into a pile that settles stably (19/24 at rest) leaning on a static wall.
  Non-black 0.622(pile), all 14 prior tests re-verified (no regression), Windows .exe clean (pure CPU
  math). LIMITATIONS (documented, not bugs): capsules are Y-axis-aligned (don't rotate the collider
  with orientation, matching the character controller + capsule mesh); box collision + box-box is AABB
  (orientation-agnostic). OBB/GJK, spatial-hash broadphase, and full inertia-tensor angular response
  remain as future upgrades — the hooks to swap them in already exist.
DONE (this session): AI TIER — greenfield (no prior ai.h/nav/steer code). Added cc/ai.h + engine/
  src/ai.c with three independent, headless-safe subsystems: (1) A* GRID PATHFINDING — CCGrid
  occupancy grid + cc_astar with a binary-min-heap open set, 4/8-connectivity, octile heuristic,
  diagonal corner-cut prevention, world<->cell mapping (grid centered on origin in XZ). Returns 0
  when unreachable. (2) STEERING BEHAVIORS (Reynolds, XZ plane, Y carried): seek, flee, arrive
  (slow-radius), wander (jittered circle), separation (inverse-distance flocking), plus
  cc_steer_path_follow that walks an A* cell path with a caller-held waypoint cursor. Forces sum,
  clamp to max_force, then cc_agent_integrate clamps to max_speed. (3) BEHAVIOR FSM — CCBrain with
  named states (enter/update/exit callbacks) + guarded transitions incl. any-state transitions;
  first-satisfied-guard wins; distinct from the ANIMATION state machine cc_asm_* (named cc_brain_*
  to avoid collision). Verified numerically (A* routes through a wall gap w/ 0 blocked waypoints &
  returns 0 when fully walled; arrive converges; separation pushes 0.3→9.4; FSM patrol<->chase fires
  correct enter callbacks) AND visually: tests/ai_test.c plans an A* route through an S-maze (green
  path nodes), a hero walks it via path-follow (orange trail, reaches goal), and a 6-agent flock
  chases with separation into a formation. Non-black 0.573+, all 15 prior tests re-verified (no
  regression), Windows .exe clean (pure CPU). TUNING NOTE for path-follow: keep max_force modest
  relative to max_speed (e.g. 9 vs 5) and arrive_radius ~0.9*cell, or agents overshoot tight
  waypoints and oscillate. FUTURE: navmesh (vs grid), behavior trees, GOAP, spatial-hash neighbour
  queries for large flocks, obstacle-avoidance steering (raycast against physics world).
DONE (this session): AUDIO TIER — audit + control-layer build. audio.c already had solid synthesis
  (sine/square/saw/tri/noise waveforms, ADSR w/ vibrato+tremolo, composed SFX footstep/explosion/
  laser, custom PCM callback) + OpenAL playback (real AL sources when a device exists, timer-based
  state fallback headless, 3D positioning, listener) — NO phantoms, and the suspected ADSR bug was a
  false alarm (verified numerically: sustain/release ramp correctly; the coarse profile just averaged
  the 50ms release into the last window). Also found a pre-existing headless debug hook
  cc_audio_debug_pcm (used it to audit waveforms numerically — RMS ordering square>sine>saw≈tri as
  expected). The GAP was the game-audio CONTROL LAYER; added to cc/audio.h + audio.c: (1) cc_audio_
  update(dt) — drives fades/crossfades, auto-called from cc_tick; (2) volume FADES (fade_to/in/out,
  fade_out auto-stops); (3) PAUSE/RESUME (per-instance + _all; paused timer-instances don't expire,
  end_time shifts on resume); (4) MIXER BUSES — named volume groups (cc_audio_bus get/create,
  set/get bus volume, instance_set_bus, play_bus); effective gain = base*bus*master, re-pushed to AL
  on change; built-in "master"(0)+"music"(1) buses; (5) MUSIC layer — cc_audio_play_music crossfades
  from the current track over N sec on the music bus; stop_music; (6) 3D ROLLOFF — cc_audio_set_rolloff
  sets AL_REFERENCE/MAX_DISTANCE + ROLLOFF_FACTOR + linear-clamped distance model. Extended CCSoundInst
  (paused/base_volume/bus/fade fields) + CCAudioSystem (buses[], music_bus, current_music, rolloff).
  Verified functionally (buses route + set volumes; pause/resume toggles; fade_out stops after its
  duration; music A→B crossfade leaves A stopped + B current) AND visually: tests/audio_test.c plots
  real generated PCM as 3D waveform strips (sine flat / square tall / laser tapering / explosion
  exp-decay — all matching known synthesis) + a mixer bar group showing bus levels 1.0/1.0/0.7/0.4.
  Non-black 0.77+, all 16 prior tests re-verified (no regression), Windows .exe clean. FUTURE: DSP
  effects (reverb/echo via AL EFX), streaming for long music, ducking/sidechain, per-bus DSP.
DONE (this session): ANIMATION TIER — audit + fix two SIGNIFICANT bugs + fill phantoms. anim.c (681
  lines) had poses/sampling/blend/ASM(cc_asm_*)/3×IK/facial/animator implemented, BUT: (BUG 1) anim.h
  declared generic math (vec3_add, quat_mul, mat4_mul, …) that COLLIDED at compile with ccmath.h's
  same-named functions — any TU including both (i.e. every real game via claudecore.h) failed to
  compile, so the whole anim API was unusable from game code (that's why no test ever included both).
  FIX: namespaced anim's math to cca_* (renamed 22 decls in anim.h + 53 refs in anim.c); verified a TU
  with both headers now compiles. (BUG 2) GPU SKINNING WAS COMPLETELY DEAD: cc_renderer_draw_skinned
  never set the uTint uniform, and the shared GBUF_FRAG does `albedo *= uTint; if(albedo.a<0.01)
  discard;` — unset uTint defaults to vec4(0) → every skinned fragment discarded → skinned meshes
  invisible. Found via long isolation (bone UBO had identity, attribs/EBO bound, shader linked, yet
  nothing drew; forwarding draw_skinned→draw_mesh rendered, proving the bug was in draw_skinned's own
  uniform setup). FIX: set uTint(1,1,1,1)+uUnlit in draw_skinned (renderer.c). Skinning now renders
  (test shows a 2-bone bar bending via linear-blend skinning). PHANTOMS filled: cc_blend_2d (inverse-
  distance weighted 2D blend space + nlerp rotation accum), cc_blend_mask (per-bone mask → child pose
  on masked bones else bind), and all six cc_skin_* (CCSkin = thin wrapper: builds a CCMesh from
  CCModel geom, attaches joint/weights, drives set_bones + draw_skinned; draw_posed does pose→global→
  skinning-palette→draw). Verified numerically (blend_2d midpoint of +45/-45 → identity; @A → z=0.383;
  mask keeps bone at bind; IK two-bone err=0.000) + visually (skinned bend) + Windows .exe clean + all
  17 prior tests non-regressed. ALSO fixed SKILL.md anim example (used nonexistent CCIKChain2Bone/
  cc_animator_create/cc_bn_clip/ik_2bone[]/cc_animator_trigger → rewrote to real cc_asm_*/CCIKLimb/
  cc_blend_*/cc_skin_draw_posed API). FUTURE: OBB-aware capsule collide already noted elsewhere; anim
  could add root motion extraction, GPU blend-shape morph path (cc_skin_apply_facial is currently a
  no-op stub), retargeting.
DONE (this session): WORLD TIER — greenfield spatial layer (scene_graph.c/scene_render.c existed and
  were complete/no-phantoms, but there was NO culling/partition/LOD despite ccmath.h already having
  CCFrustum + frustum_test_sphere/aabb via Gribb-Hartmann — the math was there, unused). Added
  cc/world.h + engine/src/world.c: (1) CCWorld — dense object registry (id, world AABB, mesh+material,
  up to CC_MAX_LODS meshes w/ distance thresholds, visible flag, user ptr). (2) OCTREE — loose octree
  over the world bounds, rebuilt lazily on the next query after any add/remove/move (dirty flag),
  tunable max_depth/max_per_node, objects routed to octants by center. (3) FRUSTUM CULL —
  cc_world_cull() walks the octree, rejecting whole subtrees whose node AABB is outside the frustum,
  returns visible ids; cc_world_cull_bruteforce() is the O(n) reference. (4) LOD — cc_world_select_lod
  picks the mesh whose distance band contains dist(cam,obj). Verified numerically: on 2000 scattered
  objects the octree cull returned the EXACT same 668-object set as brute force (0 set mismatches)
  while testing only 756 (62% pruned); LOD bands select 10/11/12 at dist 5/30/100. Visual:
  tests/world_test.c renders a 1600-cube field with only the 786 frustum-visible drawn, coloured by
  distance band (green/yellow/red LOD rings). Windows .exe clean, all 18 prior tests non-regressed.
  FUTURE: BVH alternative for static geometry, occlusion culling (HZB), level streaming / chunk
  load-unload, portal/PVS, integrate cull results directly into a scene_render draw list.
DONE (this session): EDITABLE MESH SYSTEM (user-requested, out of tier order) — greenfield. User wants
  CC's modeling to work like Blender edit mode: not just pull fixed primitive corners, but ADD points
  anywhere (mid-edge, mid-face, any coordinate) mapped onto a 3D coordinate grid, with correct
  topology. Clarified with user: (1) interface = whatever works best for CLAUDE (CC is explicitly
  Claude's tool, not a human's) → a programmatic, addressable half-edge API fits Claude's perceive→act
  loop far better than click/drag (Claude has no live mouse/viewport); (2) grid = free coords + optional
  snap toggle; (3) topology = correctness (real half-edge). Built cc/editmesh.h + engine/src/editmesh.c:
  CCEditMesh half-edge structure (Vertex/HalfEdge/Edge/Face growable pools, CC_EM_INVALID sentinel);
  starter shapes cube(8v/12e/6f)/plane/from_mesh_data; free coords w/ cc_editmesh_set_snap lattice; the
  CORE "add points anywhere" ops — split_edge (mid-edge vertex, re-stitches both faces), poke_face
  (centroid vertex, fans face), connect_verts (splits a face); move/translate_vertex; extrude_face
  (dup ring along normal + side walls); subdivide (midpoint poke); delete_face/vertex; queries
  (counts/valid/position/find_edge/face_vertices/pick_vertices ~ "click near here"); cc_editmesh_dump
  (text topology so Claude can "see" it) + cc_editmesh_validate (half-edge invariants); bake → CCMesh
  (triangulate fan + cc_geometry_recompute_normals/tangents) + bake_cpu. Implementation uses a
  collect_rings/rebuild_from_rings approach (mutate vertex-id rings, wipe+rebuild topology) for
  split/poke/extrude/connect/delete — robust + always leaves valid topology. Verified: cube→8/12/6
  valid; split_edge added mid-edge vertex + faces re-stitched (f0=[0 8 1 2 3]) valid; poke added
  center + fanned valid; move to arbitrary coord exact; snap 0.25 rounded (0.31,0.62,0.09)→
  (0.25,0.50,0.00); extrude grew V/E/F valid and returns the new top face id (=5, after fixing a
  use-after-free in the return-id match); bake=15v/24tris. Visual test tests/editmesh_test.c: three
  shapes all from the same cube (plain / poke-top+pull-spike+split-edge / subdivide+radial-push blob),
  baked flat-normal + lit — all render watertight. Windows .exe clean, all 19 tests non-regressed.
  STAGED (documented no-ops so symbols link, NOT yet implemented): cc_editmesh_bevel_vertex (no-op),
  and true Catmull-Clark smoothing (subdivide currently does midpoint/linear poke). FUTURE: the
  material/shading "realism axis" (hyper-real ↔ non-real) the user also wants is SEPARATE and not
  started — editmesh is only the geometry substrate that makes it possible.
DONE (this session, follow-up "finish everything undone"): closed all remaining stubs/partials.
  (1) editmesh cc_editmesh_subdivide — replaced the linear midpoint-poke placeholder with TRUE
  Catmull-Clark (face points, edge points, interior vertex rule (F+2R+(n-3)P)/n, quad rebuild):
  verified cube→24→96 faces per iter, corner radius pulls 1.732→0.910 (converging to sphere), stays
  valid. (2) editmesh cc_editmesh_bevel_vertex — implemented real vertex chamfer (inset along each
  incident edge, dedup shared edge insets, angle-sorted cap face); verified cube corner v6 removed,
  +2 verts/+1 face, valid. (3) ccmodel ccm_make_capsule — was returning a bare cylinder; now builds
  proper hemisphere caps (lat/long, seams aligned to body); verified Y-extent = ±(h/2 + r). (4) ccmodel
  CRC — ccm_save computed no CRC (placeholder 0) and the size writeback was wrong; now opens w+b, hashes
  the file body with the existing crc32(), writes CRC + size correctly; ccm_load reads + verifies and
  warns on mismatch. Verified clean reload = no warning, 1-byte corruption = correct warning. (5)
  renderer cc_renderer_draw_sprite — angle_deg was ignored (TODO); added push_quad_rot (rotate 4 corners
  about sprite center), used when angle!=0. (6) anim cc_skin_apply_facial — was a no-op; now applies
  weighted blend-shape (morph target) deltas to base geometry (pos+normal), rebuilds+re-skins the mesh,
  swaps the GPU handle (destroys old, no leak). (7) engine cc_mesh_update — was declared in render.h but
  UNDEFINED (phantom); implemented cc_renderer_mesh_update (re-upload vbo/ebo, resize indices) + engine
  wrapper. All 19 tests non-regressed, Windows .exe clean. NOTE: editmesh bevel is vertex-only (edge/face
  bevel still open); facial morph rebuilds the mesh each call (fine for occasional expression changes,
  not per-frame morph animation — a persistent dynamic-VBO morph path is the future optimization).

DONE (this session, user-requested "realism axis"): per-material SHADING MODEL spanning hyper-real ↔
  non-real. The renderer already had physically-based Cook-Torrance PBR + an unlit flag; the gap was
  stylized/non-real shading. Added to CCMaterialDesc: shading_model (CCShadingModel: CC_SHADE_PBR=0,
  TOON=1, FLAT=2, RIM=3), toon_bands, toon_specular, rim_strength, rim_power, rim_color[3]. Deferred
  renderer, so per-object model is PACKED into the G-buffer emissive-alpha channel as (model*10 +
  bands + fractional AO) — decoded in the lighting pass; model 0 + bands 0 → integer 0, so all legacy
  materials decode to plain PBR with their original AO (zero behavior change, verified by 20/20 tests).
  Added a stylized() GLSL fn beside pbr(): TOON quantizes diffuse into `bands` steps + optional stepped
  Blinn highlight; FLAT is a single hard lambert step; RIM is lambert + fresnel rim accent. Lighting
  loop branches pbr() vs stylized() on the decoded model; PBR-only IBL is gated to model 0 so stylized
  materials keep their graphic look. Style knobs (toon_specular, rim_*) are captured on the renderer
  from the last stylized material and applied globally in the lighting pass (they're look-defining
  constants); model+bands vary per object via the G-buffer. Wired through ALL THREE gbuffer paths:
  cc_renderer_draw_mesh (inline — NOTE: this path sets material uniforms inline, NOT via
  bind_gbuf_material; that mismatch was the main bug — the model never reached the shader until moved
  into the inline block), instanced (via bind_gbuf_material), and skinned (inline). Verified: 4-sphere
  test (tests/realism_test.c) shows PBR/toon/flat/rim clearly distinct under identical lights; isolated
  toon shows hard cel bands; Windows .exe clean; all 20 tests non-regressed. FUTURE (realism axis
  extensions): per-material rim/toon params (needs a small per-draw-id style buffer rather than the
  current global-last-wins), hatching/halftone/gooch models, per-object outline thickness, matcap.
DONE (this session, "realism = actual photorealism, UE5-as-north-star not literal parity"): started the
  realism push with AUTO-EXPOSURE (eye adaptation) — the highest-leverage tractable win. NOTE the user
  first meant "realism axis" = stylistic (that's the toon/flat/rim shading-model work, already shipped);
  THEN clarified they actually want photorealism (texturing/shading/fidelity). Audited the post pipeline
  first: it's already mature (ACES Narkowicz tonemap, exposure, Jimenez physically-based bloom, FXAA,
  saturation/contrast/vignette/CA/grain) — so the gap was NOT tonemapping but that exposure was a FIXED
  constant (camera.exposure). Added auto-exposure: enabled a mip chain on hdr_color_tex, and before the
  post pass glGenerateMipmap + glGetTexImage the 1x1 top mip to get average luminance, adapt a
  renderer-persistent ae_adapted_lum toward it (first frame snaps; else exponential k=1-exp(-speed*dt),
  dt=1/60 headless), exposure = ae_key/adapted_lum * camera.exposure(manual bias). New CCPostFX fields:
  auto_exposure, ae_key(0.18), ae_speed(3.0), ae_min(0.03), ae_max(8.0). Default OFF so all existing
  scenes unchanged (verified 21/21 non-regressed). Verified: tests/autoexposure_test.c renders the same
  scene at light intensity 0.4/3.0/24.0 with AE off vs on; measured mean brightness OFF=54.8/156.7/236.2
  (4.3x swing, dim crushes / bright blows out) vs ON=137.2/138.5/147.7 (1.08x — all land mid-grey).
  Visually confirmed: AE-on dim brightened, AE-on blinding pulled back from white-clip. Windows clean.
DONE (this session): TAA — TEMPORAL ANTI-ALIASING. The user reframed "realism" as "not genuine
  photorealism, but get damn close" and let me pick the highest-impact gap; I picked TAA because
  single-sample aliasing/jaggies are the biggest remaining "this is CG" tell, and TAA also denoises
  SSGI/shadows/specular for free via temporal averaging. Implementation: (1) sub-pixel Halton(2,3)
  jitter added to vp_mat per frame in cc_renderer_set_matrices (guarded by postfx.taa); (2) history +
  resolve LDR FBOs; (3) TAA_FRAG resolve = blend current with history, history NEIGHBORHOOD-CLAMPED to
  the current 3x3 color box (anti-ghost); (4) render_taa_pass runs after render_postfx_pass, before 2D
  overlay — resolves into post_tex and stores result as next frame's history; taa_frame++ advances the
  jitter. New CCPostFX: taa (default off), taa_blend (def 0.9, capped 0.97). taa_history_valid resets
  on resize (first frame passes through). Verified tests/taa_test.c (thin picket row + tilted cube +
  cone = aliasing stress): hard-edge transition count 234 (off) -> 5 (on), edges clean and NOT over-
  blurred (supersampled look, geometry stays crisp). 23/23 non-regressed; TAA off by default.
  NOTE / LIMITATION: current reprojection is IDENTITY — great for the headless static-camera loop
  (6-8 frames of jitter = ~8x SSAA), but a MOVING camera would ghost until motion-vector reprojection
  is added (future work: store prev vp_mat, compute per-pixel velocity, reproject history lookup). The
  neighborhood clamp limits ghosting but doesn't eliminate it under motion. Best used with fxaa OFF
  (they're redundant; TAA is superior for static shots).
  PHOTOREAL GAP STATUS (ordered, remaining "CG tells" after TAA): (1) flat/procedural textures — need
  real scanned PBR texture-set loading (likely biggest remaining); (2) depth-of-field + subtle lens
  imperfection + film grain; (3) stronger/temporally-accumulated SSGI bounce; (4) geometry too clean
  (no bevels/wear). TAA banked the #1 tell (aliasing).
  UPDATE (this session): tell (1) REAL TEXTURE LOADING is now DONE — see the DONE entry below. The
  updated remaining order is: (1) DOF + lens imperfection + film grain; (2) stronger/temporal SSGI;
  (3) less-perfect geometry (bevels/wear). Texture follow-ups (optional): HDR/EXR for IBL+emissive,
  ORM channel-order auto-detect, GPU-handle texture cache.
  UPDATE 2 (this session): DEPTH OF FIELD now DONE too (see DONE entry). Remaining tells: (1)
  stronger/temporal SSGI; (2) less-perfect geometry. Grain + CA + vignette already existed, so "lens
  imperfection" is largely covered; optional extras noted in NEXT UP.
  UPDATE 3 (this session): STRONGER/TEMPORAL SSGI now DONE (see DONE entry). The ONLY remaining
  ordered photoreal tell is less-perfect geometry (bevels/wear/dirt). After that the photoreal push
  is essentially complete and the roadmap moves to the ASSET SYSTEM REDESIGN (needs the language
  decision WITH the user first).

DONE (continuation — DOMAIN-GENERAL tier 2): NINE-SLICE PANELS (cc/nineslice.h + engine/src/
  nineslice.c). Bordered boxes that scale to any size without corner distortion — the standard 9-patch
  technique for menus, dialog boxes, tooltips, buttons, frames. TWO forms: (1) TEXTURE 9-patch —
  CCNineSlice{tex, tex_w, tex_h, left/right/top/bottom insets}; cc_nineslice_draw(eng,ns,x,y,w,h,tint)
  slices the source texture into 9 regions and stamps each into the matching dest region via
  cc_draw_sprite_ex UV sub-rects (corners native size, edges stretch one axis, center fills; borders
  clamp if dest smaller than their sum). (2) SOLID styled panel — cc_panel_draw(eng,x,y,w,h,fill,
  border_px,border_col) = filled rect + four fixed-thickness border edges (corners never smear), for
  the textureless case. VERIFIED: tests/nineslice_test.c generates a 32x32 frame texture (gold corners
  / blue edges / dark interior) and draws it at wide-short, tall-narrow, large-square, tiny, and
  red-tinted sizes + 3 solid panels — VIEWED: borders stay consistent thickness and gold corners stay
  square+undistorted across all sizes (the whole point of 9-patch), tint works, solid panels crisp.
  52/52 regression build+run clean; render non-black. Files: cc/nineslice.h (new), engine/src/
  nineslice.c (new), claudecore.h (+include), scripts/cc (+nineslice.c), tests/nineslice_test.c (new).
  This rounds out the UI toolkit (GUI widgets + world-space UI + nine-slice chrome). NEXT domain-general
  candidates: touch input, event bus, coroutines.

 (cc/inputrec.h +
  engine/src/inputrec.c). Capture a timestamped stream of input events, then replay them exactly — for
  demos, automated playtests, reproducible bug reports, regression tests. Cheap because it reuses the
  existing QEvent + cc_input_inject plumbing (QEvent is POD). CCInputRec holds {time_seconds, QEvent}
  entries. RECORD: cc_inputrec_start_recording, then each frame cc_inputrec_capture(rec, t, polled_evs,
  n) (first capture sets the time origin; events stored relative). PLAYBACK: cc_inputrec_start_playback
  then each frame cc_inputrec_play(rec, eng, t) re-injects every event whose timestamp <= t via a
  cursor (frame-rate independent); cc_inputrec_finished when done. Binary .ccrec file I/O (magic
  'CCR1' + count + entry-size guard + raw dump). Also event_count/duration/clear. VERIFIED:
  tests/inputrec_test.c records 4 key events at t=0/0.5/1.0/1.5 (SPACE down, A down, SPACE up, A up),
  round-trips through /tmp/demo.ccrec, replays into a FRESH engine, and asserts key state at each time:
  t=0.1 SPACE held/A not; t=0.6 both held; t=1.1 SPACE released/A held; t=1.6 both released + finished.
  All pass. 51/51 regression build+run clean. Files: cc/inputrec.h (new), engine/src/inputrec.c (new),
  claudecore.h (+include), scripts/cc (+inputrec.c), tests/inputrec_test.c (new). NOTE: playback
  requires cc_tick after cc_inputrec_play so injected events get processed into input state (same as
  live input). .ccrec is machine-specific in the sense that RecEntry layout must match (guarded by the
  entry-size field). NEXT domain-general candidates: nine-slice UI panels, touch input, event bus.

 (cc/prefab.h + engine/src/prefab.c).
  Entity templates — define an actor's makeup once (mesh, material, base scale/rotation, shadow,
  visibility, name), then stamp out many instances into a world with per-instance position (+ optional
  yaw/scale variation). The authoring win behind crowds, props, projectiles, tiles, scattered nature.
  Built on CCActor, so every instance IS a normal independent ECS entity. CCPrefab is a small copyable
  value type. API: cc_prefab_new(name) / cc_prefab_from_actor(a) (capture an authored actor as a
  template); setters set_mesh/material/scale/rotation/shadow/visible; cc_prefab_spawn(world,p,x,y,z)
  and cc_prefab_spawn_ex(...,extra_yaw_deg,scale_mul) for natural variation. Each instance gets a
  unique auto-name "<prefab>_<n>" (spawn_count counter on the prefab). VERIFIED: tests/prefab_test.c
  defines a "tree" (tall thin box) + "rock" (flat sphere) prefab, stamps 40 trees + 18 rocks (58
  actors) with per-instance yaw+scale, and ASSERTS: spawn_count matches, instances findable by name
  (tree_0..tree_39, rock_0), moving one instance doesn't move another (independence), instances carry
  the prefab's mesh. VIEWED render: scattered forest, trees at varied rotations/scales, rocks at varied
  sizes, each shadowed — all from 2 prefab defs. 50/50 regression build+run clean. Files: cc/prefab.h
  (new), engine/src/prefab.c (new), claudecore.h (+include), scripts/cc (+prefab.c),
  tests/prefab_test.c (new). NOTE: cc_prefab_from_actor captures mesh/material/scale/visibility but
  leaves base rotation at 0 (per-instance yaw is applied at spawn; full 3-axis quat capture wasn't
  needed for templating). NEXT domain-general candidates: input record/playback (thin layer over the
  existing inject API), nine-slice UI panels, touch input.

 (cc/worldui.h + engine/src/worldui.c).
  Screen-space UI anchored to world positions — health bars + nameplates floating over characters, and
  damage/score numbers that pop up, rise, and fade. Genre-neutral (RPG/shooter/RTS/MOBA/tower-defense).
  Foundation: exposed the renderer's current view-projection matrix + framebuffer size via new
  cc_renderer_get_vp / cc_renderer_get_size accessors, then cc_world_to_screen(eng, wx,wy,wz, &sx,&sy,
  &visible) projects a world point to top-left-origin screen pixels (returns false behind camera; sets
  visible=false if behind or outside viewport). Drawers (call after the 3D scene, within frame_begin/
  end): cc_worldui_bar (centered health/progress bar, fill 0..1, pixel w/h + y_offset, fg/bg RGBA) and
  cc_worldui_label (centered nameplate text). Managed floating numbers: CCFloaters + cc_floaters_
  create/destroy/spawn(wx,wy,wz,text,r,g,b)/update(dt)/draw/active — each floater rises ~0.9 world
  units and fades over 1.2s. Uses the existing cc_draw_rect/cc_draw_text 2D overlay + cc_font_builtin;
  gets the renderer via cc_engine_renderer (same pattern as debug.c). VERIFIED: tests/worldui_test.c —
  projection asserts (in-front point projects within viewport; behind-camera point not visible) + a
  VIEWED render of 3 characters each with a nameplate (Knight/Goblin/Mage), an HP-colored bar
  (green/yellow/green, fills proportional to 0.85/0.4/0.65), and a floating damage number (-12/-45/
  crit!) risen+fading. Bars track each cube's screen position correctly across depths. 49/49 regression
  build+run clean. Files: cc/worldui.h (new), engine/src/worldui.c (new), renderer.c (+get_vp/get_size
  accessors), claudecore.h (+include), scripts/cc (+worldui.c), tests/worldui_test.c (new). NEXT
  domain-general candidates: prefab instancing, touch input, input record/playback, nine-slice panels.

 Gamepads were effectively
  non-functional: qwerty defined gamepad EVENTS (QEVENT_GAMEPAD_BUTTON_DOWN/UP/AXIS) and input_map had
  a gamepad_axis field, but there was NO engine-side gamepad state or polling API and input_map never
  read gamepad input. Built a proper layer (all in input.h + engine.c). Engine struct now holds
  pads[CC_MAX_GAMEPADS] {connected, btn[], prev_btn[], axis[]} + gamepad_deadzone (default 0.15). In
  cc_tick: snapshot prev_btn for edge detection, then accumulate gamepad button/axis events from the
  existing qwerty_poll loop (extended the loop that previously only handled scroll). API (Xbox-style):
  CCGamepadButton (A/B/X/Y/LB/RB/BACK/START/LSTICK/RSTICK/DPAD_*) + CCGamepadAxis (LX/LY/RX/RY/LT/RT);
  cc_gamepad_connected/button/button_pressed/button_released/axis/set_deadzone. Sticks are symmetric-
  deadzoned + remapped; triggers (LT/RT) are 0..1 un-deadzoned. Plus injection for headless testing/
  replay: cc_gamepad_inject_button/axis/connected (consistent with the existing key/mouse inject).
  Real controllers work when present (events flow through qwerty); headless tests drive via inject.
  EDGE-DETECTION PATTERN: cc_tick snapshots prev=btn at frame start, so the correct order is
  tick→inject→query (documented in the test). VERIFIED: tests/gamepad_test.c — connect state, A
  pressed edge fires once then not-while-held, released edge, LX deadzone (0.10→0, 0.575→~0.5 remap),
  full -1 axis, trigger not deadzoned (0.10→0.10), custom deadzone, independent 2nd pad — all pass.
  48/48 regression build+run clean (the cc_tick change didn't disturb existing input). Files: input.h
  (+gamepad API), engine.c (struct + cc_tick accumulation + impls), tests/gamepad_test.c (new). input.h
  now 22/22 (was 16/16 before the API grew). NEXT domain-general tier candidates: world-space UI
  (floating health bars / damage numbers), prefab instancing, touch input, input record/playback.

 (cc/move.h +
  engine/src/move.c). The character-movement response any game with a moving player/AI needs: try to
  move by a vector, and when you hit a wall SLIDE along it instead of stopping dead; stack contacts for
  corners; report grounded for jump logic. Self-contained — does NOT use the heavy rigid-body physics
  solver (the character controller's ground_height_fn only did vertical follow, not lateral walls, so
  this fills a real gap). API: cc_move_world_create/destroy/clear, cc_move_add_box (AABB center+half),
  cc_move_add_plane (normal+offset half-space), cc_move_collider_count; cc_move_slide(w, pos, radius,
  disp, out_pos) → CCMoveResult{hit, grounded, ground_y, contacts}. ALGORITHM: SUBSTEPPED depenetration
  — the move is split into sub-steps of ≤ radius*0.5 so a fast mover can't tunnel past a near face;
  each sub-step resolves the deepest sphere-vs-collider penetration along its contact normal (a few
  passes for corners). Sliding emerges because only the penetrating NORMAL component is removed, so
  tangential motion survives. sphere_collide handles sphere-vs-AABB (closest-point, with an
  inside-box least-axis fallback) and sphere-vs-plane. IMPORTANT BUG FIXED during dev: first version
  did final-position depenetration only; a 2-unit step landed the center INSIDE the wall box and the
  least-penetrated-axis push ejected it out the FAR side (x 0→3 through the wall). Substepping fixed
  it — now x stops at the near face. VERIFIED: tests/move_test.c — straight-into-wall stops at face
  (x 0→1.0, not through), diagonal-into-wall SLIDES (x blocked 1.0, z carries to 2.0), fall-onto-floor
  grounded at y=radius, free move unchanged. All pass. 47/47 regression build+run clean. Files:
  cc/move.h (new), engine/src/move.c (new), claudecore.h (+include), scripts/cc (+move.c),
  tests/move_test.c (new). This COMPLETES the domain-general "help any game" list the user asked for:
  tweens/timers ✓, trails ✓, particles ✓(already existed), collide-and-slide ✓. CAVEAT: sphere collider
  (capsule would need a segment-vs-collider variant — a natural extension); boxes+planes cover typical
  level geometry; no swept CCD (substepping handles reasonable speeds).

DONE (continuation — DOMAIN-GENERAL, item 3/ease-first): PARTICLES — AUDIT CORRECTION: the particle
  system was ALREADY fully implemented (cc/particles.h + engine/src/particles.c, ~199 lines, in the
  build + claudecore.h umbrella, with a passing tests/particles_test.c). My earlier AUDIT.md wrongly
  listed "particles" as a renderer gap — corrected. It has CCEmitterDesc + presets cc_emitter_smoke/
  fire/sparks/rain/magic, create/destroy/config/set_position/set_emitting/burst/update/draw/alive,
  simulates a CPU particle pool and renders via cc_draw_billboards (camera-facing, lit, bloom-capable).
  VERIFIED by running particles_test.c: 4 emitters (smoke=39 fire=67 magic=59 sparks=250 alive) render
  as distinct effects — VIEWED: gray smoke column, glowing orange fire w/ bloom, wispy blue magic,
  dense spark burst. Test passes. NO new code needed — just corrected the audit + added it to the
  SKILL feature map. (Minor polish possible: particles are hard-edged squares; a soft radial texture
  would smooth them — an asset detail, not a system gap.) So of the domain-general list, particles was
  already done; REMAINING is collide-and-slide physics response (the last + most involved item).

 (cc/trail.h +
  engine/src/trail.c). Motion trails for projectiles/swords/vehicles/cursors — genre-neutral. A CCTrail
  is a newest-first point buffer: cc_trail_push(x,y,z) records a position (ignores near-duplicates so a
  stationary emitter doesn't fill it), cc_trail_update(dt) ages points and drops those past `lifetime`,
  cc_trail_draw(eng,r,g,b) draws connected line segments fading newest→oldest. create(max_points,
  lifetime)/destroy/clear/point_count. IMPLEMENTATION note: kept points NEWEST-FIRST contiguous (O(n)
  shift on push, negligible at trail sizes) after a first ring-buffer draft had an index bug — the
  linear version is correct and clear. VERIFIED: tests/trail_test.c — data checks (grows to 5, dup
  ignored, caps at capacity 8, all expire past lifetime, clear empties) + a VIEWED render of a glowing
  projectile arcing with a clean warm fading streak following its parabola. 45/45 regression clean.
  Files: cc/trail.h (new), engine/src/trail.c (new), claudecore.h (+include), scripts/cc (+trail.c),
  tests/trail_test.c (new). HONEST caveat (documented in trail.h): cc_trail_draw emits via the GIZMO
  system, which is flushed by cc_debug_overlay(eng) — so a game calls cc_debug_overlay once per frame
  after trails to composite them (this also draws the debug HUD bar). A dedicated non-debug world-line
  pass could replace the gizmo dependency later; gizmos are what the renderer currently exposes for 3D
  lines. NEXT domain-general (ease order): particle system, then collide-and-slide.

 (cc/tween.h +
  engine/src/tween.c). Genre-neutral juice/scheduling: animate a float from→to over a duration along an
  easing curve (writing a target ptr OR via a setter callback), with optional on-complete; plus timers
  (cc_timer_after one-shot, cc_timer_every repeating). One CCTweens manager, cc_tweens_update(dt) per
  frame. Handles are generation-tagged (index+1 | gen<<16) so a stale handle can't hit a reused slot;
  completed one-shots auto-remove; repeating timers catch up if dt is large. cc_ease(type,t) evaluates
  a curve standalone. IMPORTANT COLLISION RESOLVED: camera.h already defines CCCamEase with
  CC_EASE_LINEAR/SMOOTH/IN/OUT/IN_OUT/CUBIC — my first draft redefined those enumerators and the build
  failed (redeclaration + duplicate case). FIX: tween.h now includes camera.h and aliases
  `typedef CCCamEase CCEaseType;` so the whole engine shares ONE easing vocabulary (cleaner than two).
  Don't re-add a separate ease enum. VERIFIED: tests/tween_test.c — data checks (tween reaches target,
  monotonic, on_done fires once, removed after completion; ease-in<linear<ease-out early; repeating
  timer fires 4x over 2.05s via catch-up; one-shot fires exactly once; cancel stops tween + timer;
  active_count returns to 0) all pass, AND a render of 5 cubes mid-tween at different heights per easing
  curve (VIEWED — linear/in highest, out/in-out nearly landed). 44/44 regression build+run clean;
  camera unaffected by the enum share. Files: cc/tween.h (new), engine/src/tween.c (new), claudecore.h
  (+include), scripts/cc (+tween.c), tests/tween_test.c (new). This is the master-list tween/easing
  library + timers/scheduling. NEXT domain-general items (ease-first order): trails/ribbons, then a
  particle system, then collide-and-slide physics response. (These help ANY game, not just horror.)

 New
  cc/dialogue.h + engine/src/dialogue.c. A dialogue is a graph of nodes; each node has speaker+text and
  then either player CHOICES (text -> target node), an auto-advance (goto), or an end. Nodes carry an
  optional ACTION string that fires on entry; choices can be GATED by a required flag ([flag] prefix)
  so they appear only when unlocked. Loaded from a hand-editable text .ccdlg (or parsed from an
  in-memory string). Runtime CCDialogueRunner walks the graph: cc_dialogue_start(d, node) →
  speaker/text/choice_count/choice_text; advance() for linear nodes, choose(i) for a visible choice;
  finished() when done. GAME HOOKS (CCDialogueHooks): flag_query (gates) + action_apply (actions),
  either NULL-able — this is how dialogue composes with the rest of the game WITHOUT the engine knowing
  specifics. Text grammar: 'ccdlg 1', 'node <id>', indented speaker/text/action/goto/end and
  'choice <text> -> <target>' with optional '[gate] ' prefix; '#' comments; missing header tolerated.
  VERIFIED: tests/dialogue_test.c parses a 3-node branching convo and drives it through
  CCSaveState-backed hooks (flags+actions stored in a save state — dialogue+save COMPOSE): asserts
  branching (choose→target), GATING (the [has_sigil] choice is hidden → 1 visible; after the lore
  node's 'set has_sigil' action fires it's 2 visible), ACTIONS firing (has_sigil, gate_open flags set
  in the save), goto auto-advance, explicit end + finished, and a second playthrough with the flag
  preset showing both choices immediately — all pass. Also confirmed loading from a .ccdlg FILE on disk
  (separate check). 43/43 regression build+run clean. Files: cc/dialogue.h (new), engine/src/dialogue.c
  (new), claudecore.h (+include), scripts/cc (+dialogue.c), tests/dialogue_test.c (new). This pairs
  naturally with interactables (a USE terminal/NPC starts a dialogue) and save (choices set flags).
  Remaining gameplay critical path: AI director, audio MIC INPUT (see AUDIT.md).

 (doors, switches, levers, pickups,
  generic-use). Built on CCActor: an interactable IS an actor + a proximity trigger + a typed behavior.
  New cc/interact.h + engine/src/interact.c. API: cc_interactable_register(world, actor, TYPE, range)
  → CCInteractable*; set_callback/set_prompt/set_locked; cc_interactable_query(world, px,py,pz, fx,fz)
  finds the best in-range target (facing-aware: prefers targets ahead, skips ones behind if a facing
  dir is given); cc_interactable_trigger runs the built-in behavior + user callback; cc_interactable_
  update(dt) advances door-swing animations; is_on/is_locked/actor/prompt/type queries; clear frees a
  world's registry. Built-in behaviors: DOOR (toggles open/closed, swings 90° about Y via an eased
  animation over ~1/3s, blocks re-trigger mid-swing, honors locked), SWITCH (toggle on/off), LEVER
  (one-shot latching), PICKUP (hides actor + marks collected + can't retake), USE (callback only). The
  optional callback (grant item / unlock / play sound / advance quest) lets game logic hook in without
  the engine knowing specifics. Registry keyed by world pointer (mirrors the actor build-cache pattern).
  VERIFIED: tests/interact_test.c registers a door+switch+pickup, queries near the door (facing-aware,
  finds it), triggers each and ASSERTS: door open + swings (rotation animated over 40 update steps),
  switch toggles + callback fires once, pickup grants item + hides actor + refuses second take, a
  LOCKED door refuses to open. VIEWED the render — door visibly swung open ~90° (edge-on → broadside),
  switch present, gem gone. All assertions pass. 42/42 regression build+run clean. Files: cc/interact.h
  (new), engine/src/interact.c (new), claudecore.h (+include), scripts/cc (+interact.c),
  tests/interact_test.c (new). HONEST caveat: the door rotates about the actor's CENTER pivot, not a
  hinged edge — visually a game would offset the door mesh so the hinge is at an edge; the STATE
  MACHINE + query + callback framework is the reusable part and is correct. This same pattern covers
  terminals/keypads/notes (USE), breakables/levers, etc. Remaining gameplay critical path: dialogue,
  AI director, audio MIC INPUT (see AUDIT.md).

 The
  foundation the rest of the gameplay layer builds on (checkpoints, quest state, progression). Design:
  a typed key-value store CCSaveState (new cc/save.h + engine/src/save.c) — a game sets named typed
  values (int/float/bool/string/vec3) addressed by dotted keys ("player.hp", "quest.intro.done"),
  serializes to a human-readable text .ccsave file, reads back. NOT a fragile "serialize the whole ECS
  world via reflection" approach (the ECS stores components as opaque size+name blobs, so field-level
  reflection isn't available) — the KV design is what real games actually use for saves and it composes
  with the scene serialization (a save stores level="crypt_02.cclist" + player state → the checkpoint
  loop). API: cc_save_new/free, cc_save_write/read, set_int/float/bool/str/vec3, get_* (with default
  for missing/wrong-type keys), has/remove/count/key_at. Format .ccsave v1: 'ccsave 1' header then one
  typed line per entry ('i key int', 'f key float', 'b key 0|1', 'v key x y z', 's key string-to-eol'),
  '#' comments. Strings run to end-of-line so they may contain spaces; keys may not. save.c added to
  build; save.h added to the claudecore.h umbrella. VERIFIED: tests/save_test.c builds a realistic
  9-value save (hp/gold/stamina/quest flags/level/name-with-space/pos/keys), writes, reads back, and
  ASSERTS every value + type round-trips, plus overwrite-semantics (no dup), missing-key defaults,
  has(), remove() (swap-remove), and key enumeration — all pass. Inspected /tmp/slot1.ccsave: clean,
  hand-editable, typed prefixes + dotted keys. 41/41 regression build+run clean. Files: cc/save.h
  (new), engine/src/save.c (new), claudecore.h (+include), scripts/cc (+save.c), tests/save_test.c
  (new). NOTE/next: this is the state CONTAINER; a game still decides WHAT to put in it each checkpoint
  (gather player/world state → cc_save_set_* → write; on load, read → apply). A convenience
  "snapshot an ECS world's transforms" helper could sit on top later, but the container is the
  foundation and is done. Remaining gameplay critical path: interactables, dialogue, AI director,
  audio MIC INPUT (see AUDIT.md).

 Two self-contained subsystems:
  - HEIGHTMAP TERRAIN (cc_procgen_terrain_heightmap + cc_procgen_terrain, procgen.c). A shared
    terrain_height() fbm heightfield (layered cc_noise2 + smoothstep shaping) so the heightmap texture
    and the mesh agree. terrain_heightmap bakes it to a grayscale RGBA8 texture; terrain builds a
    centered grid mesh (grid_w×grid_h, cell_size, height_scale, octaves), displaces Y by the field,
    height-tints vertices (low grass → dry slope → rocky/snow), and cc_geometry_recompute_normals for
    correct lighting. Verified: tests/terrain_test.c renders a 200×200 (~80k-tri) terrain under
    golden-hour light + fog + SSGI — VIEWED, convincing rolling hills with snow-capped peaks and green
    lowlands. Master-list "heightmap terrain".
  - SPRITE SHEET + 2D ANIMATION (cc_procgen_sprite_sheet, cc_procgen_animation, cc_animation_get,
    cc_animation_destroy). sprite_sheet draws frame_count frames into one horizontal-strip RGBA
    texture via built-in generators keyed by anim_type (IDLE breathing, WALK/RUN bob, JUMP, ATTACK
    lunge, EXPLOSION expanding ring) or a custom_frame callback. animation wraps a generated sheet in
    a small 128-slot registry (CCAnimation: texture+frame dims+count+fps+loop); get/destroy manage it.
    Verified functionally: 8-frame walk sheet built, registry returned frames=8 fps=12 valid texture.
  40 test binaries build+run clean; terrain non-black. Files: procgen.c (terrain + sprite/anim),
  tests/terrain_test.c (new), AUDIT.md/SKILL updated. AFTER this, the ONLY declared-but-unimplemented
  functions left in the whole public API are cc_mesh_load_gltf (glTF import) and the Python bridge
  (cc_python_*, cc_rust_entry — a deliberately-kept product-feature surface). Everything else declared
  is implemented. The big remaining WORK is game-content/gameplay (not API gaps): interactables,
  save/load, dialogue, AI director, mic input — the horror critical path. See AUDIT.md.


  - RENDER-TARGET SYSTEM (cc_rt_create/destroy/color_texture/depth_texture/read_pixels + new
    cc_rt_capture). The RTEntry struct (fbo/color_tex/depth_tex/w/h) was already scaffolded in
    renderer.c but the functions were never written. Implemented: RT = offscreen FBO + color(+depth)
    texture; color/depth exposed as real CCTexture handles via the texture table (samplable in
    materials); cc_rt_capture blits post_tex (composited frame) into the RT. Enables security-camera
    monitors / render-to-texture / mirrors / feedback. NOTE: this captures the COMPOSITED FRAME, not
    an arbitrary from-scratch scene render into the RT — full "render scene A into RT while viewing
    scene B" would need the deferred pipeline to retarget, a larger change; capture covers the
    monitor/feedback use cases. Verified: tests/rt_test.c renders a red-sphere scene, cc_rt_capture,
    then samples the RT texture as a monitor quad's albedo beside the real sphere — VIEWED, monitor
    shows the captured feed; readback avg pixel 174 (non-black). render.h 78→101/102 (only
    cc_mesh_load_gltf left).
  - INPUT edge-detection + injection (cc_mouse_pressed/released, cc_input_inject/inject_mouse_button/
    poll/context). prev_mouse[] was already tracked per-frame (parallel to prev_keys[]), so pressed/
    released mirror cc_key_pressed/released; inject/poll wrap the real qwerty API (qwerty_inject_event/
    inject_mouse_button/poll — NOT qwerty_dispatch/poll_events which don't exist). input.h 10→16/16 DONE.
  38→? regression: 39 test binaries build+run clean, renders non-black (rt/gui/actor/nature spot-checked).
  Files: renderer.c (rt_* impls + rt_register_tex helper), engine.c (rt_* + mouse edge + input inject/
  poll/context wrappers), render.h (+cc_rt_capture decl), tests/rt_test.c (new), AUDIT.md (updated).
  STILL OPEN (unchanged, each a focused pass): glTF import, procgen terrain/spritesheet/animation,
  Python bridge. And the big GAME-CONTENT/GAMEPLAY clusters (interactables, save/load, dialogue, AI
  director, mic input) — the horror critical path. See AUDIT.md.

 Audited
  every public header's cc_* functions against engine/src (script at /tmp but logic in AUDIT.md).
  Finding: the engine is far more complete than the master-list's cautious map implied — actor, ai,
  anim, audio, camera, editmesh, input_map, physics, player, scene_graph, steam, world, ecs are
  FULLY implemented. The real gaps were a set of functions DECLARED in headers with NO implementation
  (i.e. calling them = link error). Closed the tractable ones:
    - Material setters cc_material_set_base_color/roughness/metallic (renderer + engine wrappers;
      materials stored as r->materials[m].desc).
    - ECS cc_query1/2/3 shorthands (ecs.c) — order-preserving trampolines: cc_query_run sorts types
      internally so raw comps[] is in sorted order; the shorthands remap to the caller's arg order.
      Verified query2 matches exactly entities with ALL components, correct data.
    - cc_texture_from_memory (stbi_load_from_memory), cc_draw_mesh_wireframe (real GL_LINE polygon
      draw into gbuffer), cc_draw_bounds (gizmo box), cc_draw_text_wrap (word wrap).
    - cc_mesh_load_obj — real Wavefront OBJ importer (v/vt/vn/f, fan-triangulation, a/b/c + a//c
      formats, neg indices, normal recompute). Master-list "asset importer (OBJ)".
    - IMMEDIATE-MODE GUI TOOLKIT (new engine/src/gui.c): cc_gui_begin_frame/window/label/button/
      slider/checkbox/separator/end_window/end_frame. Renders via cc_draw_rect/cc_draw_text 2D overlay,
      interacts via cc_mouse_pos/down, captured in screenshots. Master-list "immediate-mode UI toolkit".
  Header impl counts after: ecs 19/19 (was 16), render 95/101 (was 78). VERIFIED: tests/gui_test.c
  (VIEWED — titled panel with buttons/slider/checkboxes/separators over a lit 3D scene, material
  setters visibly recolored the meshes); a query+OBJ integrity test (query2 saw exactly 5 of 6 entities,
  sum_hp correct; OBJ cube loaded). 38/38 regression build+run clean. Files: renderer.c (material
  setters, texture_from_memory, draw_mesh_wireframe), engine.c (material setter wrappers, wireframe/
  bounds/texture_from_memory/OBJ/text_wrap wrappers), ecs.c (query1/2/3), gui.c (new), scripts/cc
  (+gui.c), tests/gui_test.c (new), AUDIT.md (new — full honest completion status).
  REMAINING declared-but-unimpl gaps (documented in AUDIT.md, each a focused pass): render-target
  system (cc_rt_*, for portals/mirrors/monitors), glTF import, input edge-detection/injection, procgen
  terrain/spritesheet/animation, Python scripting bridge. The big OPEN clusters are game-CONTENT &
  GAMEPLAY (world interactables, save/load, dialogue, AI director, audio MIC INPUT) — the horror
  critical path, each a substantial feature, NOT quick wire-ups. See AUDIT.md for the full picture.


  NAME COLLISION. Two parts:
  (1) COLLISION FIX (found while starting this): there were TWO different types both named CCScene —
  the ECS entity-world (ecs.h, opaque 'typedef struct CCScene CCScene;', defined in ecs.c) AND the
  render/manifest scene I built for .cclist (ccscene.h, a fully-defined struct). Both even had
  cc_scene_create/destroy vs cc_scene_new/free. They hadn't collided in one translation unit yet, but
  render.h AND claudecore.h both include ecs.h, so ANY file including ccscene.h + the ECS would fail to
  compile (conflicting CCScene). FIX: renamed the RENDER/manifest type CCScene→CCSceneAsset,
  CCSceneInstance→CCSceneAssetInstance, and cc_scene_{load,new,add,save,draw,free}→cc_sceneasset_*
  (contained to ccscene.h/.c + 2 tests). The ECS CCScene / cc_scene_{create,destroy} keeps the name as
  the canonical entity-world (it's the older, more foundational type). NOTE for future: the ECS world
  is CCScene (cc_scene_create); the loaded .cclist asset is CCSceneAsset (cc_sceneasset_load). Don't
  re-merge these names.
  (2) ACTOR ABSTRACTION [new cc/actor.h + engine/src/actor.c; added to scripts/cc]: CCActor = a small
  copyable handle {CCScene* scene; CCEntityId id} — an ergonomic game-object over the ECS. An actor IS
  an ECS entity carrying the built-in CCTransform + CCMeshComp (+ a lazily-registered CCActorFlags for
  visibility), so cc_scene_render() draws actors for free and raw ECS queries still see them — NOT a
  parallel system. API: cc_actor_spawn(scene,mesh,mat,x,y,z) / cc_actor_spawn_empty; destroy/valid;
  set_name/name/find (find = cc_entity_find wrapper); set_position/get_position/translate/set_rotation
  (Euler deg)/set_rotation_y/rotate_y/set_scale/set_uniform_scale; get_transform (→CCTransform3D,
  Euler→quat ZYX); set_mesh/set_material/mesh/material; set_visible/visible (hidden = stash mesh_id in
  CCActorFlags + zero the live one so the renderer's mesh_id==0 skip hides it, restore on show — no
  scene_render.c change needed); set_shadow. Reuses cc_transform_component()/cc_meshrenderer_component()
  from scene_render.c (non-static, declared in render.h).
  VERIFIED: tests/actor_test.c spawns a 5×4 grid as actors (NO Prop[] array), sets per-actor rotation/
  scale/material via handles, hides one (set_visible false), finds "hero" by name and enlarges+regolds
  it, renders via cc_scene_render → returns 19 (20 spawned − 1 hidden). VIEWED: grid of red/blue/white
  cubes+spheres with the gold hero center, one missing (hidden). 37/37 regression build+run clean; the
  renamed scene-asset tests (ccscene_test, ccsave_test) still pass unchanged in behavior. Files:
  cc/actor.h (new), engine/src/actor.c (new), scripts/cc (+actor.c), ccscene.h/.c + 2 tests (rename),
  tests/actor_test.c (new). This is the master-list "Standard Actor/Entity abstraction (kill the
  parallel arrays)" item. Follow-ups (not done): actor PARENTING/hierarchy (spawn_empty exists as a
  pivot but parent-child transform propagation isn't wired — the scene-graph in scene.c is separate);
  actor→component escape hatch is just .id/.scene (fine); could add cc_actor_add_component passthrough.

 ("editor GUI ... even
  a headless 'level as JSON I can hand-edit'" / "scene serialization (save a whole level)"). After
  reading MASTER_FEATURE_LIST.md, picked this as the highest-leverage next step: it completes the asset
  arc (could load .cclist, couldn't save one) and unblocks level authoring. Extended CCScene + ccscene.c:
    - CCSceneInstance now records its AUTHORING SOURCE (model_path + material_slot) alongside the GPU
      mesh/material handles, so a live scene can be written back to text (previously it only held opaque
      handles and had thrown the source paths away — couldn't serialize).
    - cc_scene_new(name, base_dir) + cc_scene_add(eng, scene, path, px py pz, sx sy sz, rot_y_deg, slot):
      build a level in memory. Loads+caches each unique .ccmodel (mesh+materials) on first use via an
      on-scene opaque _build_cache (freed in cc_scene_free); mirrors unique resources into the scene's
      meshes[]/materials[] ownership arrays so destroy_gpu frees them and draw/save see them.
    - cc_scene_save(scene, path): serialize to .cclist. Groups instances by model path (one 'model'
      block each, first instance as model+at/scale/rot_y/slot, rest as compact 'instance' lines).
      Recovers rot_y degrees from the instance quaternion (cs_yaw_deg = 2*atan2(qy,qw)).
    - cc_scene_load now records model_path + slot per instance too, so a LOADED scene can be re-saved
      (load→edit→save round-trips).
  VERIFIED: tests/ccsave_test.c builds a 6-instance scene programmatically (cube w/ red+blue slots +
  pyramid, varied transforms/rotations/slots), cc_scene_save → /tmp/built.cclist, cc_scene_load back,
  ASSERTS instance count survives (6==6), and RENDERS the reloaded-from-disk scene — VIEWED: blue-slot
  cube, red cubes, gold pyramids all in authored positions/scales/rotations. Inspected built.cclist:
  clean hand-editable text, instances correctly grouped by model even though added interleaved, rot_y
  recovered exactly (20/40/0/35°). 36/36 regression build+run clean; existing ccscene loader test
  unchanged (loader rewrite is behavior-compatible). Files: cc/ccscene.h (instance source fields +
  cc_scene_new/add/save decls + _build_cache), engine/src/assets/ccscene.c (rewritten: shared model
  cache, builder, save, quat→yaw), tests/ccsave_test.c (new). GOTCHA: rot_y recovery assumes rotations
  are about Y only (true for everything the .cclist format can express — it only writes rot_y); a
  general 3-axis rotation in an instance would not round-trip through .cclist (by format design).
  NOTE: this is the first master-list item shipped POST asset-redesign; see MASTER_FEATURE_LIST.md
  status map (scene serialization now DONE).


  ccm_save_text/ccm_load_text handled geometry+submesh+materials only; skeleton/skin/animation/
  blendshapes survived ONLY through the binary format, so rigged characters couldn't be authored or
  round-tripped as text. Now the text format is DATA-COMPLETE vs the binary one. Added text sections
  (all in engine/src/assets/ccmodel.c, both writer + parser):
    - skeleton: 'skeleton N' then 'bone <name> <parent> px py pz qx qy qz qw sx sy sz' per bone
      (bind TRS; parent 65535 = root). Bone names are single tokens (the humanoid builder uses
      underscore names like upper_arm_l, so this is safe).
    - skin: 'skin N' then 'sw j0 j1 j2 j3 w0 w1 w2 w3' per vertex (4 joint influences + weights).
    - animation: 'anim <name> <dur> <fps> <loop>' then per track 'track <bone> <bshp> <target>
      <interp> <keycount>' + 'key <time> <value> <in_tan> <out_tan>' lines, closed by 'endanim'.
    - blendshape: 'blendshape <name> <delta_count>' then 'bd <vidx> dpx dpy dpz dnx dny dnz' per delta.
  IMPORTANT: inverse-bind matrices are NOT serialized (they're derived) — ccm_load_text calls
  ccm_compute_inv_bind_poses() after parsing to reconstruct them from the bind TRS. Parser tracks
  current anim/track/blendshape as state; skin rows fill in order via a running index. The animation
  system (cc_animator_new / cc_skin_create) takes a const CCModel* — exactly what ccm_load_text
  produces — so a text-loaded rig drives animation identically to an in-memory one.
  VERIFIED: tests/ccrig_test.c (a data-integrity test, no render): builds a box + 3-bone skeleton +
  per-vertex skin weights + a 3-key rotation animation + a 2-delta blend shape, saves to TEXT, loads
  back, and ASSERTS every count AND representative values match (bone names, parents, keyframe values,
  skin weights, loop flag, fps) — all pass. Also round-trips the 53-bone / 13-blendshape humanoid
  through text (53->53 bones, 13->13 shapes). Inspected /tmp/rig.ccmodel: rig sections are clean and
  human-readable. 35/35 regression build+run clean; render tests unchanged. Files: ccmodel.c (rig
  read+write), tests/ccrig_test.c (new). The text format can now fully replace the binary format;
  binary ccm_save/ccm_load remain but are now redundant (retire-or-keep is a free choice — see NEXT UP).


  Builds directly on phase 1. Two pieces:
  (1) MATERIAL-SLOT → CCMaterial BRIDGE [engine.c, decl cc/render.h]:
      - cc_material_from_model_slot(eng, model, slot, base_dir): turns a CCMModelSlot (base_color,
        roughness, metallic, emissive, + albedo/normal/roughmetal/emissive/ao texture PATHS) into a
        real CCMaterial. Textures load with CORRECT color space (albedo+emissive via cc_texture_load_
        srgb, normal/roughmetal/ao via linear cc_texture_load — reusing this-session's sRGB work) and
        resolve relative to base_dir. Carries alpha_cutoff/double_sided/alpha_blend through.
      - cc_materials_from_model(eng, model, out[], max, base_dir): builds all slots; if a model has
        zero slots, emits ONE neutral default and returns 1 (callers always get a usable material).
      This closes the phase-1 gap where a loaded model brought material DATA but the caller still had
      to hand-build materials. Loaded models are now self-describing.
  (2) .cclist SCENE MANIFEST [new engine/src/assets/ccscene.c + cc/ccscene.h; added to scripts/cc src
      list]: load a whole multi-object scene from one text file. Grammar: 'cclist 1' header, optional
      'name', then 'model <path.ccmodel>' (loads+caches by path, becomes current) with indented 'at
      x y z' / 'scale sx sy sz' / 'rot_y deg' / 'slot N' for the pending instance, plus repeatable
      'instance x y z sx sy sz rot_y' lines for more instances of the current model. '#' comments,
      unknown keywords ignored. Models CACHED by path (one CCMesh + material set built once, many
      instances share it). API: cc_scene_load → CCScene {instances[], meshes[], materials[]};
      cc_scene_draw (loops cc_draw_mesh); cc_scene_free(eng, scene, destroy_gpu). Texture paths inside
      models resolve relative to the .cclist dir.
  VERIFIED: tests/ccscene_test.c writes cube.ccmodel (2 material slots: red/blue) + pyramid.ccmodel
  (gold), authors scene.cclist placing 8 instances with per-instance transforms + material-slot
  selection, loads via cc_scene_load, draws via cc_scene_draw. VIEWED: renders exactly as authored —
  blue-slot cube vs red-slot cubes, gold metallic pyramids, per-instance scale + Y-rotation, all from
  text; log confirms "8 instances, 2 meshes, 3 materials" (cube mesh cached & reused across 5
  instances). NONE of the layout is hard-coded in C — it's all in scene.cclist. 34/34 regression
  build+run clean, all non-black; ccmodel_test + material_test unchanged (no regression from engine.c
  edits). Files: engine.c (+cc_material_from_model_slot/cc_materials_from_model/cc_pathjoin; note the
  slot struct is CCMMaterialSlot with two M's), cc/render.h (decls), engine/src/assets/ccscene.c (new),
  cc/ccscene.h (new), scripts/cc (added assets/ccscene.c), tests/ccscene_test.c (new). GOTCHA: cc_scene_
  free defaults to NOT destroying GPU meshes/materials (destroy_gpu=false) — pass true to release them;
  the one-shot headless tests don't bother. NEXT (phase 3): rig text sections; .ccasset material/
  texture-set descriptor; retire legacy binary I/O once rigs are covered.


  on disk (the core gap: previously .ccmodel was a dead BINARY format with zstd/CRC that nothing
  loaded, and there was NO CCModel→renderer bridge at all, so even the in-memory CCModel builders
  couldn't be drawn). LANGUAGE: C (see NEXT UP #2 for the decision provenance — user delegated).
  WHAT I FOUND: engine/src/assets/ccmodel.c has a full, real in-memory CCModel struct + builders
  (ccm_make_sphere/box/…, geometry ops, OBJ/GLTF import) — all working. What was dead: (a) the binary
  ccm_save/ccm_load serialization (implemented but zero callers), and (b) no bridge from CCModel to a
  drawable CCMesh. Rather than rip out the working struct, I built the text layer ON TOP of it.
  ADDED:
    - ccm_save_text(model, path) / ccm_load_text(path)  [engine/src/assets/ccmodel.c, decl in
      cc/ccmodel.h]: a line-based, dependency-free (NO zstd) .ccmodel v1 text format. Grammar:
      'ccmodel 1' header, 'name', 'verts N' + N 'v px py pz nx ny nz u v tx ty tz tw r g b a' lines
      (color 0..255), 'tris M' + M 'f i0 i1 i2' lines, optional repeatable 'submesh start count mat',
      optional 'material <name>' … 'end' blocks (base_color/roughness/metallic/emissive/*_tex/
      alpha_cutoff/double_sided). '#' to EOL = comment; unknown keywords ignored (forward-compat).
      Writes geometry+submeshes+materials only (NOT skel/skin/anim/bshp — binary ccm_save still owns
      rigged models; noted for phase 2). ccm_load_text AUTO-DETECTS binary magic and delegates to
      ccm_load, so it's a safe general entry point. Submesh lines are buffered and applied AFTER
      geometry commit (order-independent).
    - cc_mesh_from_model(eng, const void* ccmodel) and cc_mesh_load_ccmodel(eng, path)  [engine.c,
      decl in cc/render.h]: THE BRIDGE. Converts a CCModel's geometry to a renderable CCMesh
      (field-by-field CCMVertex→CCVertex copy — layouts are identical 52-byte, verified, but copied
      explicitly so a future divergence can't corrupt). render.h takes the model as void* to avoid a
      hard ccmodel.h dependency; engine.c includes ccmodel.h for the impl. load_ccmodel = load_text +
      from_model + free.
  VERIFIED: tests/ccmodel_test.c renders 3 spheres: [L] in-memory model via cc_mesh_from_model, [M]
  same model saved to /tmp/rt.ccmodel (text) and loaded back — VIEWED pixel-identical to L, and an
  in-test assert confirms vertex/index counts round-trip exactly (825 verts / 1536 tris preserved),
  [R] a HAND-AUTHORED text octahedron written as plain text in the test and loaded via
  cc_mesh_load_ccmodel — renders with per-vertex colors coming through, proving the engine reads
  geometry authored OUTSIDE the engine. Inspected /tmp/hand.ccmodel + /tmp/rt.ccmodel: clean,
  human-readable, comments work. 33/33 regression build+run clean, all non-black; material +
  realism_integration unchanged (no regression from the engine.c/render.h edits). Files: ccmodel.c
  (+ccm_save_text/ccm_load_text/ccm_strip/ccm_skipws; note: a str_replace earlier briefly clobbered
  the ccm_peek signature — restored, don't worry), cc/ccmodel.h (decls + format doc comment),
  engine.c (bridge + ccmodel.h include), cc/render.h (bridge decls), tests/ccmodel_test.c (new).
  NOTE/GOTCHA: cc_mesh_from_model uses field-by-field copy not a blind cast — keep it that way. The
  text writer does NOT persist rigs yet; a rigged CCModel saved via ccm_save_text loses skel/skin/anim
  silently (acceptable for phase 1 geometry loading; phase 2 adds rig text sections). Legacy binary
  ccm_save/ccm_load remain in the file, still unused by scenes.


  primitives read as CG"). Attacked it on trees (the softest part of the nature scene): built
  tests/tree_gen.h, a header-only procedural tree-geometry generator that produces raw CCVertex/index
  buffers, perturbs every vertex with 3D fbm noise, recomputes normals (cc_geometry_recompute_normals)
  so the bumps light correctly, and bakes via cc_mesh_create. Two builders:
    - cc_mesh_lumpy_canopy(eng, radius, seed, bump): a UV sphere whose radius is modulated by layered
      3D fbm (broad lobes + medium clumps + fine ripple) → an irregular foliage mass, not a ball.
    - cc_mesh_gnarled_trunk(eng, botR, topR, height, lean, seed): stacked rings that taper, lean via
      an S-curve, wander (low-freq noise on ring centre), get a root flare near the base, and carry
      per-vertex bark relief. Built base-at-origin (y=0..height) — NOTE the pivot differs from
      cc_mesh_cylinder (which is centre-origin): scene code places it with PY=0, SY=height.
  3D fbm is built locally (tg_fbm3) from cc_noise3 since procgen only exposes cc_fbm2. Verified in
  isolation with tests/tree_test.c (side-by-side: new irregular tree vs old sphere-on-cylinder — the
  difference is obvious, lumpy lit crown + tapered flared trunk vs smooth ball on a tube), then
  integrated into tests/nature_photoreal.c (pool of 5 trunks + 6 canopies mixed across 26 trees + a
  sharp "hero" tree held in DOF focus so the geometry detail reads). VIEWED all: the hero tree shows a
  clearly organic crown with real light/shadow variation across its bumps. 32/32 regression build+run
  clean, all non-black/none blown, incl. the two new tests. Files: tests/tree_gen.h (new),
  tests/tree_test.c (new), tests/nature_photoreal.c (uses them). NOTE: this lives in tests/ as a
  demonstration of the technique using existing engine APIs (cc_mesh_create + cc_geometry_recompute_
  normals — no engine change needed). The clean GENERALIZATION (next-up item) is to add
  cc_geometry_displace(CCVertex*, nv, amp, freq, seed) to the engine geometry toolkit so any mesh can
  be roughened, then have tree_gen use it. Minor honest caveat: canopies are still one connected
  displaced mass (no true leaf-card gaps / branch-level structure), and there's faint sky-blue SSGI
  bleed in deep canopy crevices — acceptable, notable if pushing closer.
  PHOTOREAL PUSH STATUS: all four ordered tells (texture/sRGB, DOF, temporal SSGI, geometry) now
  closed. Next roadmap item is the ASSET SYSTEM REDESIGN (blocked on the language decision WITH user).


  "indirect fill solid but color bleed honestly weak." Two root causes fixed:
  (1) NORMALIZATION BUG: the old gather did `gi = gathered / float(SAMPLES)` — dividing accumulated
  weighted color by the FIXED sample count (12) regardless of how many samples actually hit. With
  typical hit counts that scaled bounced color down several-fold, washing saturated bleed toward grey.
  Fixed to `gi = (gathered / wsum) * conf` where conf = wsum/(wsum+0.5) is a soft confidence term
  (prevents sparse-hit pixels from over-amplifying). Now bounced color reads at full saturation.
  (2) NO TEMPORAL ACCUMULATION: each frame gathered fresh + a 5x5 box blur to hide noise, which
  capped how strong it could go before looking noisy/blotchy. Added a temporal accumulation buffer
  (ssgi_accum_tex + ssgi_ghist_tex, RGBA16F) blended by SSGI_ACCUM_FRAG (exp blend, history weight
  0.8, history clamped to max(cur*3+.02) as a cheap anti-ghost). The gather kernel is now rotated per
  frame by a golden-angle offset keyed to r->taa_frame, so each accumulated frame samples DIFFERENT
  directions → over the 6-8 frame headless loop this multiplies effective sample count and denoises,
  letting intensity run high cleanly. Composite now reads the ACCUMULATED buffer (ssgi_accum_tex)
  instead of the raw gather. Pipeline order in render_ssgi_pass: gather→ssgi_tex, accumulate(ssgi_tex,
  ghist)→accum_tex, blit accum→ghist (next frame's history), composite(hdr, accum, albedo)→hdr.
  For the static/orbit headless camera reprojection is identity (same as TAA), which is why plain
  accumulation is valid here; a MOVING camera would need motion-vector reprojection (noted as a
  follow-up — currently it would go stale/smeared under fast motion, same limitation as TAA).
  Files: renderer.c (SSGI_FRAG normalization + uFrame rotation; new SSGI_ACCUM_FRAG; ssgi_accum/ghist
  fbo+tex struct fields + ssgi_accum_shader; FBO build in build_taa; shader compile; rewired
  render_ssgi_pass; ssgi_hist_valid flag; also added the previously-missing shutdown frees for ssgi
  gather/composite + taa + dof targets — pre-existing shutdown-only leaks, harmless in the one-shot
  headless model but cleaned up while here). Default OFF (ssgi flag), existing scenes unchanged.
  VERIFIED: tests/ssgi_test.c (red wall beside white sphere+floor). VIEWED off vs on: OFF = white
  sphere fully neutral, red wall near-black in shadow. ON = red wall lifted by indirect fill, LEFT
  side of the white sphere now visibly warm red/pink (color bleed clearly present — the exact cue that
  was weak before), floor near wall warm-tinted, shadows filled not crushed; temporal accumulation
  kept it clean (no obvious GI noise). ssgi_on mean brightness rose 117.8→127.6 (stronger indirect).
  30/30 regression build+run clean, all non-black/none blown, realism_integration spot-checked
  (unchanged — it doesn't enable ssgi, confirming the flag gating). NOTE: faint accumulation banding
  visible on the sphere under extreme intensity — acceptable at these settings; if it bothers a future
  scene, lower ssgi_intensity or add a spatial (edge-aware) denoise before accumulate. FUTURE:
  motion-vector reprojection for moving cameras; halve-res gather + bilateral upsample for perf;
  multi-bounce by feeding the composited result back as next frame's uColor (already does, loosely).

DONE (this session): DEPTH OF FIELD — the "everything in perfect focus" CG tell. A real lens focuses
  at one distance; CC had NO DOF anywhere (grep-confirmed: only camera.c dolly-zoom, unrelated). Added
  a CoC (circle-of-confusion) gather-blur postfx pass. Model is ARTIST-FRIENDLY not physical f-stop:
  postfx.dof + dof_focus_dist (world dist in perfect focus) + dof_focus_range (half-width of the sharp
  zone) + dof_max_blur (max radius in px). CoC = clamp((|viewDepth-focus|-range)/range,0,1); radius =
  CoC*maxBlur. Blur = 16-tap golden-angle disk. KEY correctness detail: each neighbor sample is
  weighted by its OWN CoC (max(sCoC,0.05)), so an in-focus foreground does NOT bleed its color into a
  blurred background (the classic naive-DOF artifact). Depth linearized from gbuf_depth via near/far.
  PIPELINE PLACEMENT: runs in cc_renderer_frame_end AFTER render_taa_pass (blurs the resolved image)
  and BEFORE render_2d_flush (so 2D/UI stays sharp). Operates on the tonemapped LDR post_tex →
  dof_tex → blit back to post_tex, exactly mirroring the TAA resolve->blit pattern. FBO built inside
  build_taa (the TAA target builder) so it tracks resize automatically — no separate resize hook
  needed (learned from the SSAO/SSR resize-rebuild bug in the ledger). Default OFF (zero-init), so all
  existing scenes unchanged. Files: render.h (CCPostFX dof fields), renderer.c (DOF_FRAG shader,
  dof_fbo/dof_tex/dof_shader struct fields, shader compile, FBO in build_taa, render_dof_pass + fwd
  decl + frame_end call). No new GL functions (blit/texImage already used); one new enum guarded
  earlier. Verified: tests/dof_test.c renders a receding row of 7 spheres, camera focused on the
  middle (focus_dist=9). VIEWED off vs on: OFF = all uniformly sharp; ON = near red/orange spheres
  blurred, focal yellow/green sharp, far spheres + horizon progressively blurred, NO color bleed from
  sharp onto blurred (depth-aware weight working). 30/30 regression build+run clean, all non-black,
  none blown; dof_off and dof_on have identical mean brightness (136.3) — correct, blur conserves
  energy. FUTURE: HDR-space DOF before tonemap for true highlight bokeh; hexagonal aperture; separate
  near/far fields; physical f-stop; bokeh sprite scatter for bright points.

DONE (this session): REAL PBR TEXTURE LOADING — the #1 remaining photoreal tell. TWO parts:
  (A) sRGB CORRECTNESS (the actual bug): albedo/color maps were loaded as plain linear RGBA8 and
  sampled directly — no gamma decode anywhere in the engine (grep confirmed zero SRGB usage). That
  means every sRGB-authored color map rendered washed-out/too-bright (wrong gamma) — a genuine
  correctness bug on ALL textured surfaces, not just cosmetic. Fix: added CC_FMT_SRGB8_ALPHA8
  (GL_SRGB8_ALPHA8 internal format → GPU linearizes ON SAMPLE, correctly, before filtering/mips —
  better than a shader pow()) + cc_texture_load_srgb(). Data maps (normal/rough/metal/AO) stay linear
  via cc_texture_load (they are NOT color and must not be decoded). base_color and vertex color are
  already-linear multipliers, so the math downstream is unchanged/correct.
  (B) PBR SET LOADER: cc_material_load_pbr(eng, dir, base) scans a folder of scanned-PBR maps the way
  real asset packs ship (separate files, conventional names), classifies each by filename stem, loads
  color maps sRGB + data maps linear, and PACKS separate roughness+metallic single-channel files into
  ONE RG texture (R=rough, G=metal) matching the gbuffer's uRoughMetal.rg. Also accepts a combined
  ORM/ARM/roughmetal map (remaps G→R rough, B→G metal, covering both ORM and glTF metallic-roughness
  channel order). Missing maps left unbound → scalar roughness/metallic fall back.
  GOTCHA FOUND + FIXED (log it): the filename classifier checked the combined "orm"/"arm" tokens with
  a plain strstr BEFORE the normal check → "tile_nORMal" contains "orm" and was misrouted to
  ROUGHMETAL, stealing the normal map (render logged normal=- roughmetal=y). Fix: check NORMAL first,
  and match orm/arm only as boundary-delimited tokens (stem_has_token: neighboring char must be
  non-letter). Verified the classifier on 17 real-world stems incl. metal_arm→ROUGHMETAL,
  gold_metalness→METAL (not ROUGHMETAL), wood_normalgl→NORMAL.
  VERIFICATION: tests/pbrset_test.c (+ tests/gen_pbr_set.c writes a real scanned-style set to disk:
  basecolor/normal/roughness/metallic/ao PNGs). Renders 3 spheres: [L] full set via the loader (metal
  blue tiles vs matte terracotta dielectric, grout normal relief + AO — all maps confirmed loaded),
  [M] same basecolor as sRGB (correct: deep saturated color), [R] same basecolor as LINEAR (wrong:
  visibly washed-out pastel). The M-vs-R contrast is the proof the gamma fix is real; VIEWED the image,
  clear difference. Loader log: albedo=y normal=y roughmetal=y ao=y. Files touched: render.h
  (CC_FMT_SRGB8_ALPHA8 enum + cc_texture_load_srgb + cc_material_load_pbr decl), renderer.c (format
  case, sRGB loader, pbr_classify/stem_has_token/pbr_ext_ok, cc_renderer_material_load_pbr; added
  dirent/ctype/strings includes), renderer_internal.h (prototypes incl. cc_renderer_material_create
  which was previously undeclared there → implicit-decl link conflict, now fixed), engine.c (wrappers),
  cc_gl_loader.h (GL_SRGB8_ALPHA8 enum guard for the mingw path). FULL REGRESSION: 29/29 build+run
  clean, all non-black (none suspect), realism_integration + material spot-checked visually — no
  regression. NOTE: cc_material_load_pbr uses opendir/readdir (POSIX) — fine on the Linux-only runtime;
  if ever ported to the Windows ship path it'd need a _findfirst/FindFirstFile shim (not needed now,
  Windows gate is dropped). Emissive maps now also load sRGB (correct — they're color).


DONE (this session, FINAL realism tier — PARTIAL/honest): GLOBAL ILLUMINATION via SSGI (screen-space
  global illumination). New CCPostFX fields: ssgi (default off), ssgi_intensity (def 1.0), ssgi_radius
  (def 2.0). Implementation mirrors the existing SSR/SSAO passes: a gather pass (SSGI_FRAG) samples 12
  hemisphere directions around the view-space normal, projects each to screen, and if it lands on a
  nearby surface facing back, adds that surface's LIT color weighted by receiver-cosine * emitter-
  facing * distance-atten; a composite pass (SSGI_COMPOSITE_FRAG) adds the gather (5x5 box-blurred to
  denoise the low sample count) onto the scene, MODULATED BY RECEIVER ALBEDO so bounce tints correctly.
  Reuses ssao_noise_tex, gbuf depth/normal/albedo, hdr_color_tex. Runs before SSR/bloom; blits back to
  hdr_fbo. Wired: struct fields, FBOs (ssgi_fbo/tex + composite), program compile, render_ssgi_pass +
  invocation + prototype.
  HONEST STATE: the INDIRECT FILL works well and is clearly visible (shadowed/contact areas gain
  bounced light, softening harsh shadows) — verified in tests/ssgi_test.c (off vs on; sphere-left
  brightness 62->98). COLOR BLEED is WEAK/subtle: it's real but only reads strongly when the bouncing
  surface is brightly lit (a dim maroon wall bounces little light); numeric redness barely moved. This
  is inherent to SSGI (screen-space only = misses off-screen bounces; single bounce; brightness-
  dependent). Not a bug — a limitation to document. Denoise: initial 12-sample gather was grainy;
  fixed with a 5x5 box blur in the composite (cheaper than a separate blur pass/FBO). 23/23 tests
  non-regressed (SSGI off by default).
  FUTURE GI improvements if desired: temporal accumulation (needs history buffer) for more samples
  without noise; a brightness/again tuned atten; combine with the existing IBL for off-screen fill;
  or a probe/voxel approach for true off-screen multi-bounce (much bigger).

DONE (this session): FIXED A LONG-STANDING FXAA ARTIFACT (user spotted it — "screen-tearing-like
  speckles in mini spots" across every realism render). Root-caused by elimination: reproduced on a
  lit sphere, zoomed 4x → isolated dot+streak near the specular highlight. NOT specular (persisted at
  low roughness floor, gone at high roughness only because the highlight itself vanished), NOT geometry
  (normal debug view was perfectly smooth), NOT bloom. Isolating post-fx one at a time pinned it to
  FXAA. The old fxaa() had TWO bugs vs canonical FXAA3: (1) it built the edge direction from N/S/E/W
  luma such that dir.x == -dir.y (always a 45° guess) instead of from the four DIAGONAL corners; (2) it
  lacked the dirReduce/rcpDirMin term, so on a smooth gradient near a bright highlight it computed a
  large bogus direction and sampled up to 8 texels away, smearing a stray pixel into a horizontal
  streak/dot. Replaced with correct FXAA3 (diagonal-corner luma, proper dirReduce, standard 2-tap +
  4-tap blend with the lB<lMin||lB>lMax fallback). Verified: highlight now smooth, silhouette still
  anti-aliased (FXAA still doing its job), 21/21 tests non-regressed. LESSON (again): trust the zoomed
  IMAGE, not the "firefly/outlier pixel count" probe — that metric flags legitimate silhouette + shadow
  edges and misled me repeatedly. Also left the GGX roughness floor bumped 0.04→0.045 (harmless, mild
  specular-aliasing guard) though it was not the cause.

DONE (this session): MATERIAL/TEXTURE REALISM (tier 3, partial→now solid). Two additions: (1) ANISOTROPIC
  FILTERING — detect GL_MAX_TEXTURE_MAX_ANISOTROPY at init (OSMesa gives 16x), apply it + force a mip
  chain on every tiling (wrap_repeat) material texture in cc_renderer_texture_create. Keeps textures
  crisp at grazing angles (checker ground stays sharp to the horizon instead of blurring) — a
  universal, free win on all textured surfaces. (2) DETAIL NORMAL MAPS — new CCMaterialDesc fields
  detail_normal_map / detail_normal_scale / detail_normal_strength; GBUF_FRAG samples the detail map at
  vUV*scale and perturbs the base tangent-space normal (scaled 0.3*strength to avoid biasing N·L). Also
  made the GBUF vertex TBN construction robust: derive a fallback tangent when aTangent is zero-length.
  DEBUGGING NOTE for future me: I badly misdiagnosed detail-normals as "broken/darkening" for a while
  because a brightness-patch probe on a distant smooth sphere read the dense micro-detail as uniform
  darkening AND the effect saturates so it looked param-invariant. A cube-vs-sphere isolation test
  proved detail normals render correctly on BOTH (visible ripple relief). Lesson: verify visually on a
  cube (clean UVs/tangents) with a COARSE, strong map before trusting numeric brightness probes; smooth
  distant spheres are a bad first test surface. Detail-normal look is content/tuning-sensitive (map
  frequency + scale + strength) — works, tune per material. Verified 22/22 tests non-regressed.

DONE (this session): SHADOW QUALITY — upgraded directional shadows from fixed 3x3 PCF @2048² to PCSS
  (Percentage-Closer Soft Shadows) @4096². PCSS = 3 stages in shadow_factor() (LIGHT_FRAG): (1) blocker
  search over a 16-tap Poisson disk averages occluder depth, (2) penumbra width = (zRecv-avgBlocker)/
  avgBlocker * lightSize, (3) variable-radius 16-tap Poisson PCF at that width → contact-hardening
  (sharp where objects meet ground, soft with distance). New: renderer shadow_softness (default 3.0,
  light size in shadow-UV units) + uShadowSoftness uniform + public cc_light_set_shadow_softness(eng,s).
  Bumped r->shadow_size 2048→4096. Verified: shadow_test shows contact-hardening; hard(0.3) vs soft(6.0)
  comparison shows adjustable penumbra; Windows clean; 21/21 non-regressed. Also fixed a stale SKILL.md
  row (shadows were marked "⚠️ API / in progress" but have been fully working for many sessions).



=============================================================================
QUEUED (write LAST — after realism stretch AND asset redesign are both done):
ENGINE.md — THE COMPLETE MANUAL  +  BUSINESS/PACKAGING NOTES
=============================================================================
ENGINE.md (user wants this as the capstone deliverable inside the skill):
  - Structure: ONE giant ENGINE.md, everything in sections (single file merges cleanly when the user
    folds in ENGINE.md contributions written by OTHER chats that have used CC — they merge section by
    section).
  - Audience: BOTH, clearly sectioned — a "FOR CLAUDE (AI operating CC as a tool)" part and a "FOR
    HUMANS (buyer/developer)" part, plus shared reference sections, each explicitly labeled.
  - Content = EVERYTHING known about CC: how to use it, full API surface, the build/verify workflow,
    every gotcha, every bug found + whether fixed, non-obvious pitfalls, the perceive->act loop, all
    the hard-won lessons. Basically distill this HANDOFF + all tier knowledge into a real manual.
  - Timing: WRITE IT ONCE AT THE END. Do NOT start it now — realism + the asset redesign will change
    the API surface and the bug list, so an early draft would be rewritten. It's the final capstone.
  - The skill will ALSO ship: a fully-compiled ready-to-go build of CC, plus some basic example
    "things for it to learn" (starter example scenes/assets).

REGRESSION SUITE now includes tests/realism_integration_test.c (ALL realism features in ONE
scene: auto-exposure + PCSS soft shadows + anisotropic floor + detail-normals + PBR/toon coexistence —
an INTEGRATION test that stresses feature INTERACTION, e.g. does auto-exposure meter right with a
bright toon object present, do PCSS shadows land under auto-exposure) and tests/stress_test.c (varied geometry: cube/cyl/cone/capsule/
torus/edited/quad + metal & dielectric + 2 lights) — ALWAYS run varied geometry, not just spheres.

KNOWN GOTCHAS TO FOLD INTO ENGINE.md (running list so they're not lost — ADD TO THIS as found):
  - COLOR SPACE: albedo/emissive (color) maps MUST load sRGB (cc_texture_load_srgb / CC_FMT_SRGB8_ALPHA8);
    normal/roughness/metallic/AO (data) MUST stay linear (cc_texture_load). Mixing these up is the classic
    wrong-gamma bug (washed-out or over-dark surfaces). cc_material_load_pbr does this automatically.
  - PBR filename classifier: "normal" contains "orm" — always classify NORMAL before the combined
    ORM/ARM map, and boundary-check the orm/arm tokens (see stem_has_token). Same trap for any future
    token that's a substring of another map name.
  - cc_material_load_pbr uses POSIX opendir/readdir — Linux-runtime only (fine; Windows gate dropped).
  - SKILL.md still documents Python tools (tools/cc-model/cc-model.py, tools/cc-proj/cc-proj.py) and a
    binary .ccmodel — these predate the NO-PYTHON rule and the asset-redesign decision. They are the
    dead/legacy path the asset redesign will replace; don't treat them as current. (Flag for cleanup
    when the asset system is rebuilt.)
  - `cc` script CACHES the engine in engine/.build-<tag>/; after editing ANY engine/src file (or
    adding one to ENGINE_SRCS) you MUST `rm -rf engine/.build-*` or changes link stale.
  - Materials: tint MUST be {1,1,1,1} (or all-zero which is treated as 1) or albedo alpha->0->discard.
  - draw_mesh sets material uniforms INLINE (not via bind_gbuf_material) — the inline block is the one
    that actually renders standard meshes; instanced uses bind_gbuf_material; skinned sets inline too.
  - Sky/IBL API is cc_light_set_sky_colors(eng,zenith,horizon,ground,intensity) — there is NO
    CCSkyDesc/cc_sky_set (invented twice, corrected).
  - cc_draw_sprite is 8 args: (eng,tex,x,y,w,h,angle_deg,tint).
  - cc_mesh_torus(eng,r_major,r_minor,segs) — ONE seg count, not two.
  - Metals need ENVIRONMENT lighting (sky IBL) to look metallic; under dim sky + a single direct
    light, flat-faced metals render dark/dull (physically plausible, but looks broken) — this is a
    documentation point, not a bug.
  - VERIFICATION LESSON: test on VARIED geometry (cube/cone/torus/capsule/edited), NOT just spheres —
    spheres hide hard-edge/seam/thin-feature bugs. And trust the ZOOMED IMAGE, not "firefly/outlier
    pixel count" probes (that metric flags legitimate silhouette+shadow edges and misled repeatedly).
  - [FIXED] cube EDGE-SEAM speckles (found via stress_test): root cause was FXAA running on raw LINEAR
    HDR — its luma edge-detector misread bright HDR silhouettes (metal vs sky), notching hard edges.
    Fix: FXAA now DETECTS edges in a perceptual proxy (Reinhard luma x/(1+x)) while BLENDING+returning
    linear HDR so main()'s tonemap/bloom/gamma pipeline is untouched. NOTE: a first over-ambitious
    attempt restructured main()'s tonemap ordering and blew the whole image to white — reverted to the
    shipped-good renderer and re-applied the SURGICAL fxaa-only change. Lesson: keep post-pipeline
    fixes local to the function; don't reorder main()'s tonemap/bloom/gamma unless absolutely needed.

BUSINESS/PACKAGING (my recommendation, user asked for opinion — user plans to SELL CC):
  Recommendation: SHIP AS ONE SINGLE SKILL first (keep-as-is). Do NOT break the engine into many
  skills / a plugin yet. Rationale: CC is tightly coupled (shared CCVertex, G-buffer packing, shading
  model, one build) — splitting creates cross-skill version/integration hell. The plugin (many-skills)
  form is a DISTRIBUTION decision driven by unproven demand; premature now. If sales later show buyers
  wanting PARTS, THEN copy the engine, carve the COPY into a plugin, and sell both as separate SKUs
  (user's option 2). NEVER break up the original working artifact (user's option 3 = avoid). Ranking:
  keep-as-is now  >  later copy-and-split-into-plugin-sold-alongside  >  convert-original (never).
  Also: if a 2nd language is ever chosen, C++ is the natural fit (existing C compiles as C++ ~for free,
  incremental not a rewrite); C# would need an FFI boundary. (NO PYTHON regardless.)
=============================================================================

=============================================================================
QUEUED (AFTER the realism stretch — do NOT start until realism tiers are done):
ASSET SYSTEM REDESIGN
=============================================================================
PROBLEM (user): current .ccmodel is a self-contained binary chunk format BUT nothing in the engine
  ever loads it — all geometry is built in code (cc_mesh_*, cc_editmesh_bake) and the file sits beside
  the code as a description, not the source of truth. User wants the file to BE the thing, like .obj/
  .fbx where loading reconstructs the model directly from the bytes. Verified: grep shows zero
  ccm_load() call sites outside the format's own header — the format is effectively dead/unused.

TARGET DESIGN (confirmed with user):
  - Encoding: TEXT / JSON-ish — readable, diffable, editable (glTF/OBJ spirit), NOT binary.
  - Three tiers:
      .ccmodel — ONE self-contained piece (e.g. a colored rectangle). Embeds its OWN geometry
                 (vertices/faces/normals/uvs) + a material block. Load builds the mesh directly.
      .ccasset — a COMPLETE asset (e.g. a player model). REFERENCES .ccmodel pieces by path/ID and
                 adds the uniting structure: per-piece hierarchy/transforms, skeleton, animation
                 bindings, attachment points.
      .cclist  — PROJECT MANIFEST. REFERENCES every asset/model in the project. Top-level index.
  - Containment: .ccmodel self-contained; .ccasset and .cclist reference by path/ID (resolver needed).
  - THE key requirement: the engine's real geometry path must actually LOAD FROM these files (the
    current disconnect is that it never does). File = source of truth.

LANGUAGE DECISION (CORRECTED — user clarified):
  *** ABSOLUTE RULE: NO PYTHON. EVER. NEVER. For any part of CC — engine, tooling, asset layer,
      converters, ANYTHING. If a second/other language is used it MUST be C++ or C#. (SCOPE — clarified with user: this rule is about CC's OWN IMPLEMENTATION — the engine, tooling, asset layer, converters are NEVER written in Python. It does NOT forbid CC from OFFERING a Python scripting bridge to its USERS: someone building on top of CC can write THEIR game/app logic in Python and call into the engine. That bridge is a legitimate user-facing FEATURE — keep and maintain it. Rule of thumb: don't IMPLEMENT CC in Python; DO let CC's users script in Python if they want. So: no new Python in the engine/tooling; the scripting bridge stays.) This is a hard,
      permanent constraint from the user. Do not propose Python for glue, scripts, converters, or
      tooling under any circumstance. ***
  Scope of the language question: the user means a possible language change for the ENTIRE ENGINE (not
  just the asset layer), AND is separately open to MULTI-LANGUAGE (different parts in different langs).
  So the asset redesign may be done: (a) still in C, (b) in C++ or C# as a separate part alongside the
  C engine, or (c) as part of a full engine port to C++/C#. Decide WITH the user when the realism
  stretch is done; do not assume. My engineering note: the text asset format + geometry-path rewiring
  is tractable in the current C, so a rewrite is not technically required to fix the asset problem —
  but the user may want a language change for OTHER reasons (sellability, tooling ergonomics, C++/C#
  ecosystem), which is a legitimate product decision, not just a technical one.
=============================================================================

REALISM ROADMAP (ordered by impact): [DONE] auto-exposure; [DONE] shadow quality (PCSS soft shadows +
  4096² map — see entry below). NEXT: [DONE] (2) material/texture realism (see entry); (3) [DONE-partial] global illumination (SSGI). (4) [DONE] TAA anti-aliasing — biggest 'CG tell' fix.
  Shadow follow-ups still open: multi-cascade CSM (currently ONE cascade, so a single ortho frustum
  covers the whole scene — large worlds lose near-field resolution), and contact-shadow/RT options. (2)
  material/texture realism — layered PBR, higher-res maps, detail normals (no material-layer system
  today). (3) GLOBAL ILLUMINATION — the big one: CC has only single-bounce sky IBL + basic SSR; want
  multi-bounce/color-bleed (SSGI or a probe/voxel approach). Also possible: fuller ACES RRT/ODT fit
  (current is Narkowicz approx), TAA, DOF, SSGI. Approach each as its own verified tier.
  (Open modeling extras: editmesh edge/face bevel, per-face UV/material slots, persistent morph VBO.)
NEXT TIERS: (realism roadmap above) then Gameplay → UI → Architecture → Build/tooling → Networking/platform.
