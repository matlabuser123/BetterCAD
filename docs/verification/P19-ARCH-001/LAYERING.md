# P19-ARCH-001 - layering

## The table after this milestone

```text
core 0   linalg 5   sketch 10   features 20   assembly 30   drawing 40   meshing 40
cfd 42   thermal 45   structural 50   io 60   renderer 70   scripting 70
```

`cfd` takes **42**: the free integer between `meshing` (40, which it consumes) and `thermal` (45). No module was
renumbered; ADR-035 reserved the gap. The rule is strictly-lower (a module may include a module on a strictly lower
layer, never an equal or higher one), so the number decides who may read whose results:

| may include | |
|---|---|
| `cfd` -> `meshing`, `features`, `core` (and `linalg`, once P19-LINSOLVE-001 links it) | yes |
| `thermal` -> `cfd`, `structural` -> `cfd` | **yes**, which is why 42 and not above them |
| `cfd` -> `thermal`, `structural`, `io`, `renderer` | **no** |
| `meshing` -> `cfd` | **no** (the cycle) |
| `cfd` + a mesh-backend header (`nglib.h`) | **no** |

Why 42 and not 47 or 55: the two one-way couplings the roadmap makes likely run **out of** CFD - a wall pressure is a
structural load and a wall heat-transfer coefficient is a thermal boundary condition - so each reader must sit above the
read (ADR-059). The cost is that CFD cannot read a thermal or structural result by include; buoyancy and conjugate heat
transfer are deferred and, when wanted, are an exchange orchestrated by a module above all three. This is cheap to
reverse before any CFD code exists and expensive afterwards, which is why it is decided now.

## How it is enforced

| rule | gate | proof it can fail |
|---|---|---|
| the layer entry exists, so `src/cfd` files are checked | `set(layer_cfd 42)` in `CheckLayering.cmake` | the `valid` tree's `src/cfd/Analysis.cpp` is an unknown module without it |
| `cfd` -> `io` refused | `architecture.checker.cfd-layer-violation` | fixture holds one violation; message `'cfd' must not depend on 'io'` |
| `cfd` -> `structural` refused | `architecture.checker.cfd-to-structural` | `'cfd' must not depend on 'structural'` |
| `cfd` -> `thermal` refused | `architecture.checker.cfd-to-thermal` | `'cfd' must not depend on 'thermal'` |
| `meshing` -> `cfd` refused | `architecture.checker.meshing-to-cfd` | `'meshing' must not depend on 'cfd'` |
| backend header refused in `src/cfd` | `architecture.checker.cfd-backend-leak` | `mesh backend header <nglib.h>` |
| `thermal` and `structural` MAY include `cfd` | `architecture.checker.valid`: the valid tree's thermal and structural files include `bettercad/cfd/CfdAnalysis.hpp` and the checker passes | a wrong direction would be a violation there |
| `cfd` links neither `bettercad_thermal` nor `bettercad_structural` (a link is a dependency no include shows) | `CheckTargetDependencies.cmake` rule 4; `architecture.targets.cfd-links-thermal`, `.cfd-links-structural`, `.valid`, `.link-graph` | each fixture holds exactly one such link |
| `cfd` names nothing outside core, features, meshing, linalg and eigen (an allow-list, so a link to io, renderer or scripting is refused too) | `CheckTargetDependencies.cmake` rule 4; `architecture.targets.cfd-links-io` | the fixture links `BetterCAD::io`; one violation |

The whole source tree passes the layering checker (554 files, 0 violations) and the link-graph checker.

## What the module links today

`BetterCAD::core`, `BetterCAD::features`, `BetterCAD::meshing`, public, as `thermal` did at P18-ARCH-001. Nothing else:
no `linalg`, no Eigen - there is no matrix. Admitting the generic solver is P19-LINSOLVE-001's decision and its
mechanism is already fixed: `BetterCAD::linalg` (layer 5, ADR-052), PRIVATE, never an include of `structural` or
`thermal`, never a hand copy.
