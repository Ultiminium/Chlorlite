---
name: cc-engine
description: >
  Chlorlite is a self-contained, production-grade game engine skill stack. Use this skill
  whenever anyone asks you to build a game, create a game engine feature, render a 3D/2D
  scene, make anything interactive with graphics, write game logic, add physics, generate
  game assets (sprites, textures, audio), troubleshoot rendering visually, or prototype
  any interactive experience. This skill gives Claude a REAL game engine with a deferred
  PBR renderer, ECS, input, procedural generation, and headless screenshot-based debugging
  — all running inside the Claude sandbox with zero installation on the user's machine.
  Trigger on: "make a game", "build a game engine", "render a scene", "3D graphics",
  "2D game", "sprite", "shader", "ECS", "game loop", "physics", "procedural generation",
  "game assets", "headless renderer", or any variation of interactive visual content.
---

# Chlorlite — what this is

**Chlorlite is a game engine.** A game engine is the machine that makes a video game
run: it draws the pictures on the screen, moves things around, plays sounds, reads the
player's buttons, and keeps the rules of the game. If someone wants a game, this is the
tool that builds it.

Everything needed is already inside this one folder. You do not install anything. You do
not download anything. You do not need a graphics card or a screen. It all runs right
here, on its own.

**You do not need to know what "code" is to use Chlorlite.** You describe the game you
want. Chlorlite is the thing that turns that description into a real, running game.

---

## What Chlorlite can make

Think of Chlorlite as a box holding every part a game is built from. Here is what is in
the box, in plain words:

**Pictures (the graphics).**
- It draws 3D worlds — rooms, characters, landscapes, objects — with real light, shadows,
  reflections, fog, and glow. This is the "how it looks" part, and it looks good.
- It also draws flat 2D things — menus, health bars, text, buttons, score numbers.
- It can make people/creatures that **bend and move** (arms swing, legs walk) instead of
  being stiff statues. This is called animation.

**Movement and rules (how the game behaves).**
- Things can **fall, bump, slide, and collide** like real objects (this is "physics").
- A character can be **walked around** by the player without walking through walls.
- The game can have a **goal**, a way to **win**, a way to **lose**, and keep **score**.
- Enemies can **chase, react, and make decisions** (this is the "AI" — computer players).

**Sound.**
- It plays music and sound effects, and makes them sound like they come from a place in
  the world (a noise on your left sounds like it's on your left). Walls can muffle sound.

**The player's controls.**
- It reads the keyboard, the mouse, and game controllers, instantly and accurately.

**Feel (why a game feels good, not just works).**
- When something gets hit, the game can **freeze for a split second, shake the screen, and
  knock the target back** — the little touches that make action feel powerful. Chlorlite
  has a whole part just for making things *feel* good to play, and every part of that feel
  is a number you can adjust until it's right.

**Making the world.**
- It can **build worlds automatically** (random levels, terrain, trees, caves) so you
  don't have to place every rock by hand.

**Saving.**
- Games can be **saved and loaded**, so a player can stop and come back later.

**Sharing the finished game.**
- A finished Chlorlite game can be **turned into a single program** that runs on its own,
  for Linux or for Windows, that you can give to someone else.

If it belongs in a game, Chlorlite very likely already has a part for it. There are over
fifty separate parts, all listed and explained in `engine/README.md`.

---

## How you actually see the game (this matters)

There is usually no screen here. So instead of *watching* a game live, Chlorlite **takes
pictures** of the game — screenshots — and it can take a whole **series of pictures over
time** to show motion, like a flip-book. That is how the game is checked and shown: it
runs, it takes pictures, and you look at the pictures.

This is important: **the pictures are how anyone (including Claude) confirms the game
actually looks right.** Never trust a description of how a game looks. Look at the actual
picture, every time.

---

## The most important rule (read this)

The single biggest mistake made with this engine is **saying something is fixed or looks
good without actually checking.** Someone will write "the character looks smooth and
round" when the character actually looks like flat cardboard. Words are not proof. A
picture you glanced at is barely proof.

**Chlorlite comes with tools that MEASURE things and give exact numbers**, so "it looks
round" becomes "roundness is 0.05 — that is flat cardboard, FAIL." A number can't be
argued with. **Before ever claiming something is correct, measure it; and if the
measurement says FAIL, it is NOT fixed — no matter how good the description sounds.**

The main measuring tools:
- **modelcheck** — checks the *shape* of a character/object from its actual structure
  (is it round or flat? are there holes? is it built correctly?). This is the truth.
- **canonframe** — checks *where* something is and *how it's turned*, as an exact
  coordinate on a fixed grid, so "is it centered?" becomes an exact number.
- **Deboog** — a separate, standalone checking tool for anything mathematical or
  geometric (is there a broken number hiding in here? is this shape valid?). It lives in
  its own folder and can be used in other projects too.

The rule in one line: **the data is the truth; a picture is a sanity check; a description
is neither.**

---

## What Chlorlite is NOT

- It is **not** a program with buttons you click. It is operated by describing what you
  want and letting the engine build it.
- It is **not** meant to run on a random person's home computer as-is — it runs in
  Claude's own environment (Ubuntu Linux). Finished games *can* be exported to give to
  others.
- It does **not** need the internet, a graphics card, or a screen to work.

---

## Where to look (the other documents)

Chlorlite comes with companion documents. Use the right one for what you need:

- **`engine/README.md`** — *the catalog.* Explains **every single feature** the engine
  has, what each does, and how they connect. Also explains **every test** (the little
  programs that prove each feature works) — how each one works and why. Read this to
  understand *what exists*.

- **`engine/USAGE.md`** — *the how-to.* Step-by-step, dummy-proof instructions for
  **actually doing everything**: build a game, run it, take pictures, check it, ship it.
  Read this to understand *how to do things*.

- **`references/`** — deeper notes: the overall architecture (how the big pieces fit) and
  troubleshooting (what to do when something goes wrong).

- **`deboog/README.md`** — the standalone mathematical/geometric checking tool.

---

## The one-paragraph summary

Chlorlite is a complete, self-contained game engine that runs entirely inside Claude's
environment with nothing to install. It builds real 2D and 3D games — graphics, lighting,
animation, physics, sound, controls, enemies, worlds, saving, and the "feel" of action —
and can export the finished game as a standalone program. Because there's usually no
screen, it works by taking pictures of the running game, and it comes with measuring tools
that give exact numbers so nobody has to guess whether something is correct. To learn what
it has, read `engine/README.md`. To learn how to use it, read `engine/USAGE.md`.
