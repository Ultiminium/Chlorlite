# netkit — networking library for Chlorlite games

netkit is a **library that ships alongside Chlorlite, not part of the engine.**
It contains the parts of networking a *game* assembles on top of the engine's
primitives:

- **`netkit/netcmd.h`** — client→server command channel + server-side validation
  seam (where authoritative rules / anti-cheat live).
- **`netkit/nettransport.h`** — thin UDP socket transport (POSIX + Winsock).
- **`netkit/netpredict.h`** — client-side prediction + reconciliation (makes an
  authoritative game feel responsive).

## Why these are a library and not part of the engine

The line is drawn at **data ownership**, not "is it useful to games."

The engine owns exactly one networking thing: **`cc/net.h` (replication)**. That
one belongs inside the engine because it reaches into the ECS — it creates
entities and reads/writes components, and only the engine owns those. It is the
*seam* the engine is obligated to provide so that a game *can* be networked.

Everything in netkit touches **no engine-owned data**:

- `netpredict` and `nettransport` reference zero engine symbols — pure blobs,
  callbacks, and OS sockets.
- `netcmd` passes a `CCScene*` through to game callbacks but never dereferences
  it; its only real dependency on the engine is the replication *types* in
  `cc/net.h`.

A module that depends on nothing the engine owns has no reason to live *in* the
engine. And crucially, "how a client predicts movement," "how a command is
validated," and "how bytes cross a socket" are decisions a **game** makes — they
differ completely between an RTS, a shooter, and a turn-based game. Baking one
opinion into the engine would be the same closed-enum mistake CC's design
philosophy rejects, just at the module level. The engine provides the seam and
stays out of the policy.

Dependency direction is strictly one way: **netkit builds on the engine; the
engine never depends on netkit.**

## Using it

A game opts in just by including a netkit header. The `cc` build tool detects any
`#include <netkit/...>` and links the library automatically:

```c
#include "cc/claudecore.h"     // the engine
#include "cc/net.h"            // the engine's replication seam
#include "netkit/netcmd.h"     // + the netkit pieces you want
#include "netkit/netpredict.h"
```

```sh
cc build mygame.c        # netkit linked automatically because mygame includes it
```

Games that never include a `netkit/` header pay nothing for it.
