# Deboog — mathematical & geometric debugging

**When an AI (or anyone) builds something spatial or numerical that needs debugging,
this is what you turn to.** Deboog answers questions with *exact numbers*, never with
a picture:

- Is this mesh actually **manifold**? Are there holes, non-manifold edges, degenerate triangles?
- Is this arm actually **round**, or a flat ribbon? (roundness 1.0 = tube, 0.0 = paper)
- Is this matrix actually **orthonormal**? Collapsed? Mirrored?
- Is there a **NaN or Inf** anywhere in this buffer? (the silent killer of renders)
- Is this object actually **where the math says** it should be? (canonical coordinates)
- Do these skin weights actually **sum to 1** and reference **valid bones**?

Deboog is the debugging analog of a good input library: **standalone, severable, and
dependency-free.** It speaks only in **primitive types** — `float` arrays, `float[16]`
matrices, `float[4]` quaternions, `uint32_t` indices — so it has **zero coupling** to
any engine or framework. Your project writes a ~10-line adapter that unpacks its own
data into these primitives and calls Deboog. Nothing depends on Deboog's types; Deboog
depends on nothing but `libm`.

## Why it exists

The failure Deboog is built to kill: **claiming something is correct because it *looks*
correct.** A render is downstream of the math and is easy to misread — a flat-ribbon
arm gets described as "round," a NaN'd transform renders as a black box, an off-center
object "looks fine." Pixels lie. **The geometry doesn't.** Deboog measures the data
itself, so "looks round" becomes "roundness = 0.05 → FLAT RIBBON, FAIL" — a number you
cannot narrate away.

## Build (standalone — no engine required)

```sh
make test      # builds libdeboog.a and runs the self-test
```

That's it — plain C compiler + libm. Link `libdeboog.a`, include `<deboog/deboog.h>`.

## What it checks

| Domain | Function | Answers |
|---|---|---|
| Numeric integrity | `deboog_scan_floats`, `deboog_check_range` | NaN / Inf / denormals / out-of-range |
| Matrix | `deboog_check_matrix` | finite? invertible? orthonormal? mirrored? scale? |
| Quaternion | `deboog_check_quat` | normalized? finite? |
| Mesh topology | `deboog_check_mesh` | manifold? closed? holes? degenerates? unused verts? |
| Roundness | `deboog_cross_section` | round tube vs flat ribbon (a hard ratio) |
| Symmetry | `deboog_symmetry` | mirror-symmetry across a plane |
| Skin weights | `deboog_check_skin` | sum to 1? bones in range? unweighted verts? dead bones? |
| Spatial | `deboog_canon_project` / `_format` | exact signed coordinate in a fixed frame (`±XXX-±YYY:RRR`) |
| Invariants | `deboog_invariants_*` | assert facts that must always hold; catch the break at its frame |

Every function returns a **result struct of numbers** (the product). Optional
`deboog_report_*` printers format them and return a FAIL count so you can gate on it.
Because results are data, Deboog **wraps trivially from Python/JS/other languages.**

## Using it (example)

```c
#include <deboog/deboog.h>

/* is this limb a ribbon? */
float axis_point[3]={0,0,0}, axis_dir[3]={0,1,0};
DbCrossSection cs = deboog_cross_section(limb_verts, n, /*stride*/3,
                                         axis_point, axis_dir, /*ribbon<*/0.45);
if (cs.is_ribbon) { /* roundness cs.roundness — do NOT call this round */ }

/* any NaN in a transform before you upload it? */
DbNumeric num = deboog_scan_floats(matrix, 16);
if (!num.ok) { /* first bad at num.first_bad_index */ }
```

## Adapters

Deboog stays engine-agnostic; each engine bridges in with a tiny adapter. See
`examples/` for the pattern (unpack your vertex/bone structs into flat arrays, call
Deboog). The Chlorlite adapter is one file.

## License / status
Standalone, dependency-free, usable in any project. v0.1.0. No warranty implied.
