# qwerty — a standalone low-latency input library

qwerty is a small, dependency-free C input library. It is developed alongside
Chlorlite but is **not part of the engine**: it has zero dependencies on any
Chlorlite header (`cc/…`) and builds into its own static library (`libqwerty.a`).
You can drop it into any C/C++ project.

## Why it's separate
Same rule as `netkit/`: a component that shares no engine-owned data lives outside
the engine as a sibling library, and the dependency points one way only —
Chlorlite depends on qwerty, never the reverse. That keeps qwerty reusable on its
own and keeps the engine from growing an input system it can't sever.

## What it provides
- A **push (callback) API** — the recommended path for games: you get an event the
  instant a key/mouse/gamepad transition happens (zero-copy from the backend).
- A **poll API** — drain an internal lock-free ring buffer of `QEvent`s when you
  want to, e.g. once per frame.
- Mirrored **level state** (`qwerty_key_down`, `qwerty_mouse_down`, `qwerty_mods`)
  for the common "is this held right now" query, O(1).
- Backends: evdev and X11 on Linux, plus a headless backend for tests/CI. The host
  app can also feed events in directly via `qwerty_dispatch` (how Chlorlite routes
  windowed GLFW key events into qwerty).

## Design goals
Lowest achievable input latency and a clean event model: the callback path avoids
per-frame polling of device state entirely — cost is proportional to input that
actually happened, not to how many keys exist. Level-state mirrors are updated on
the same dispatch so both styles stay consistent.

## Build (standalone)
```sh
cc -c -O2 -std=gnu17 -Iinclude src/qwerty.c src/backend_x11.c src/backend_evdev.c
ar rcs libqwerty.a *.o
# link libqwerty.a into your program; include <qwerty/qwerty.h>
```
See `examples/poll_example.c` and `examples/headless_example.c`.

## Using it from your own code
```c
#include <qwerty/qwerty.h>
QConfig cfg = qwerty_default_config();
QContext* q = qwerty_init(&cfg);
/* push style: */
qwerty_set_callback(q, my_event_fn, my_userdata);
/* or poll style, once per frame: */
QEvent evs[256];
uint32_t n = qwerty_poll(q, evs, 256);
/* or just query level state: */
if (qwerty_key_down(q, QKEY_SPACE)) { /* ... */ }
qwerty_shutdown(q);
```

## License / status
Developed with Chlorlite; usable independently. No warranty implied.
