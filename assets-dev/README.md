# assets-dev — the in-development asset library

This is the working asset store for whatever CC project is being built. Drop assets
here by type:

- `fonts/`    — `.ttf`, `.otf`
- `models/`   — `.gltf`, `.glb`, `.obj`, `.ccmodel`
- `textures/` — `.png`, `.jpg`, `.tga`, `.bmp`
- `anims/`    — `.ccanim` (animation clips; note most anims ride inside model files)

## How it's used

A game references an asset by **bare name**:

```c
CCTexture hero = cc_texture_load(e, cc_asset("hero.png"));
CCFont    ui   = cc_font_load(e, cc_asset("title.ttf"), 32.0f);
CCModel*  m    = ccm_import_gltf(cc_asset("enemy.gltf"), "enemy");
```

`cc_asset("name")` resolves the same call in both contexts:

- **During development** (running from the skill): finds the file here in
  `assets-dev/<type>/name`.
- **In the shipped game**: finds it in the game's bundled `assets/` folder.

## Automatic bundling of only-used assets

When you `cc bundle` a game, the bundler scans the game's source for every
`cc_asset("…")` reference and copies **exactly those files** from this library into
the shipped `assets/` folder. The dev library can hold hundreds of assets; a shipped
game only carries the ones it actually references. Referenced-but-missing assets are
reported as warnings at bundle time.

So: put everything you might use here during development; ship only what you use,
automatically.
