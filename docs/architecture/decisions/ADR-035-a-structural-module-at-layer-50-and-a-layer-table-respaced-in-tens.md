# ADR-035 — A `structural` module at layer 50; the layer table is respaced in tens

```text
STATUS:    Accepted
DATE:      2026-10-06
MILESTONE: P17-ARCH-001
TOUCHES:   ADR-006 (assembly's layer; io moved up), ADR-015 (drawing's layer; io
           moved up again), ADR-033 (the volume mesher behind a boundary)
```

## Context

P17 needs a module that **consumes** P16's meshing: it reads the Tet4 volume
mesh, the geometry/mesh mapping and the quality measurements, and it reaches
geometry currency through P16's own boundary. It must also be consumable by
`io`, which will serialize its analysis intent, and by `renderer`, which will
display its results.

`tests/architecture/CheckLayering.cmake` enforces layering on `#include`
directives, and rule 3 is **strictly lower**:

```cmake
elseif(DEFINED layer_${file_module} AND NOT target_module STREQUAL file_module
       AND NOT layer_${target_module} LESS layer_${file_module})
```

So a cross-module include is a violation unless the target's layer is strictly
below the includer's. Two modules on the same layer cannot include each other
**at all** — which is exactly why `drawing` and `meshing` were allowed to share
layer 4: they are siblings, neither using the other.

The table at the start of this milestone:

```text
core 0   sketch 1   features 2   assembly 3
drawing 4   meshing 4   io 5   renderer 6   scripting 6
```

A structural module must therefore be **above 4 and below 5**, and there is no
integer there.

This is the third time. `ADR-006` introduced `assembly` and moved `io` from 3 to
4. `ADR-015` introduced `drawing` and moved `io` from 4 to 5. Each ADR records
the renumber as a cost it had to pay because consecutive integers left no room.
The roadmap then has `P18` thermal, `P19` CFD, `P20` optimisation and `P21`
semantic topology, several of which sit in the same band between a derived
representation and persistence.

## Constraints

- Relative order must not change. Every existing dependency direction is
  qualified behaviour through P16, and a respace that reordered anything would
  be a silent architecture change rather than a renumber.
- `io` must stay above `drawing`, `meshing` and `structural`; `renderer` and
  `scripting` above `io`.
- An unregistered module fails the build (`unknown module '<x>'`), so whatever
  is chosen must land in the table in the same change as the module.
- The numbers are quoted in `ADR-006` and `ADR-015` as the tables of their day.
  Those are historical records and must stay as they are.

## Options

### A — shift the upper layers by one

`structural` 5, `io` 6, `renderer` 7, `scripting` 7. Exactly what ADR-006 and
ADR-015 did.

Smallest conceptual change, and a reader comparing the three ADRs sees one
consistent habit. But it is the third payment of the same cost, and it
guarantees a fourth: a thermal module that couples to structural in either
direction needs a number between them, and there will not be one.

### B — respace the whole table in tens

```text
core 0   sketch 10   features 20   assembly 30
drawing 40   meshing 40   structural 50   io 60   renderer 70   scripting 70
```

One change, and nine free integers between any two neighbours. A module can be
inserted anywhere in the stack for the foreseeable life of the project without
touching another module's number.

The cost is a nine-line diff to the table plus the module comments that quote
their own layer, and the number stops reading as "distance from core". It is an
ordinal with gaps, which a reader must be told.

### C — share layer 40 with meshing

Rejected on a measured fact rather than a preference: rule 3 forbids same-layer
cross-module includes, and `structural` must include `meshing`. The fixture
`architecture/fixtures/meshing-to-structural` demonstrates the rule firing in
the other direction, and sharing would make the permitted direction fire too.

## Decision

**Option B.** The layer table is respaced in tens and `structural` takes 50.

```text
core        0     sketch     10    features   20    assembly  30
drawing    40     meshing    40    structural 50    io        60
renderer   70     scripting  70
```

The checker's comment states that the value is an ordinal with gaps and that a
new module should take a free integer rather than shift the table.

## Rationale

Two renumbers in two module additions is a pattern, not a coincidence, and the
roadmap says it recurs. Option A is smaller today and larger over the next four
phases; Option B pays once. The deciding factor is not elegance but that
**a renumber touches modules that have nothing to do with the change being
made** — ADR-015 moved `io` for a reason entirely internal to `drawing` — and
every such edit is a chance to reorder something by accident.

What would have made Option A right: if the stack were finished. It is not; the
roadmap has eleven phases left.

What would have made Option C right: if a structural analysis could be
formulated without reading a mesh. It cannot.

The respace is also the cheapest it will ever be. The numbers appear in exactly
one authoritative place — the checker — plus prose in `ARCHITECTURE.md` and the
module `CMakeLists.txt` comments. Waiting makes that list longer.

## Consequences

**Easy.** Inserting a module: pick a free integer. `P18`'s thermal module is
expected at 50 beside `structural` if the two are siblings that do not use each
other, and at 45 or 55 if a thermomechanical coupling says otherwise — and that
decision now needs no renumber, which is precisely the choice ADR-006 and
ADR-015 did not have.

**Hard.** A reader who assumes the number means depth is wrong. Mitigated by
saying so in the checker, which is where the table lives.

**Committed.** The library depends on applications in one direction only. Rule 6
is added in the same change, because rules 2 and 3 could not express it: rule 2
keys on Qt's header shape, so it stops `#include <QWidget>` in `src/structural/`
but not `#include "../../apps/bettercad_cli/Commands.hpp"`, and `apps/` is not a
module so rule 3 has no layer to compare. A library file reaching a CLI parser
or a file dialog would have passed. Nothing under `include/` or `src/` had such
an include when the rule was added, so it costs nothing and closes the gap for
every module.

## Verification

```text
architecture.layering                               447 files, 0 violations
architecture.checker.valid                          structural -> meshing,
                                                    features, core ALLOWED, and
                                                    the layer-table entry is
                                                    held in place by it
architecture.checker.structural-layer-violation     structural -> io REFUSED
architecture.checker.meshing-to-structural          meshing -> structural
                                                    REFUSED (the cycle)
architecture.checker.structural-backend-leak        nglib.h in src/structural/
                                                    REFUSED
architecture.checker.library-reaches-apps           src/ -> apps/ REFUSED
```

Each fixture tree holds exactly one violation and asserts the message naming
it, so a rule that stopped firing would fail rather than pass quietly. There is
deliberately no structural-specific Qt fixture: rule 2 keys on a header's shape
and not on a module, so `architecture.checker.qt-leak` already covers every
module including this one.

Evidence: [docs/verification/P17-ARCH-001/](../../verification/P17-ARCH-001/README.md).
