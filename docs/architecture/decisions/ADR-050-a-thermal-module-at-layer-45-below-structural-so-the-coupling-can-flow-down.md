# ADR-050 — A `thermal` module at layer 45, below `structural`, so the coupling can flow down

```text
STATUS:    Accepted
DATE:      2026-10-16
MILESTONE: P18-ARCH-001
TOUCHES:   ADR-035 (the layer table respaced in tens, which reserved this
           decision), ADR-039 (Eigen private to the modules that link it),
           ADR-033 (the mesher behind a boundary)
```

## Context

P18 needs a module that **consumes** P16's meshing exactly as `structural`
does: it reads the Tet4 volume mesh, the geometry/mesh mapping and the quality
measurements, and reaches geometry currency through P16's own boundary. It
must be consumable by `io` and `renderer`, as `structural` is.

ADR-035 respaced the layer table in tens for precisely this moment and left
the choice open in writing:

> `P18`'s thermal module is expected at 50 beside `structural` if the two are
> siblings that do not use each other, and at 45 or 55 if a thermomechanical
> coupling says otherwise -- and that decision now needs no renumber.

The rule is strictly-lower: a module may include only modules on a strictly
lower layer. So the number decides, for the life of the stack, which of the
two solvers may ever read the other's results.

The roadmap already says which way the coupling runs: "temperature fields
eventually couple into structural analysis for thermal expansion and stress".
A thermal strain is a structural LOAD computed from a temperature field, so the
consumer is `structural` and the thing consumed is a `thermal` result.

## Constraints

- Relative order of every existing module must not change (ADR-035); the table
  has nine free integers between any two neighbours, and this decision must
  use one rather than shift anything.
- `thermal` must be strictly above `meshing` (40) and strictly below `io` (60).
- Eigen, if `P18-SOLVE-001` admits it, is PRIVATE and never in a public header
  (rule 7); that rule is module-agnostic and needs no change.
- Nothing in this milestone may link linear algebra or perform a solve.

## Options

### A — 50, a sibling of `structural`

Neither may include the other. Clean today. The first thermo-mechanical
milestone would then need `structural` above `thermal`, which means moving
`structural` to 55 -- a renumber of a qualified module for a reason internal
to another, the exact cost ADR-035 was written to stop paying.

### B — 45, below `structural`

`structural` (50) may include `thermal` (45); `thermal` may not include
`structural`. The coupling direction the roadmap names is open without a
renumber. The cost is the mirror image: P17's sparse-solver API, which lives in
`structural`, is unreachable from `thermal` by include.

### C — 55, above `structural`

`thermal` could reuse P17's `solveSymmetricSparse`, its `SolveProblem`
classification and its residual metrics by include. But the coupling would
then run the wrong way: a thermal strain in a structural load would need
`structural` to include `thermal`, which 55 forbids forever without a renumber.
Convenient for P18-SOLVE-001, wrong for the roadmap.

## Decision

**Option B.** `thermal` takes layer 45.

```text
core        0     sketch     10    features   20    assembly  30
drawing    40     meshing    40    thermal    45    structural 50
io         60     renderer   70    scripting  70
```

The checker's table gains `set(layer_thermal 45)` with the reason beside it;
no other entry moves.

### What this fixes about solver reuse, and what it leaves to P18-SOLVE-001

P17 built, qualified and probed a sparse symmetric solve behind a pivot gate
and an independent residual (`structural::solveSymmetricSparse`, ADR-039). A
thermal steady solve is the same shape of problem -- symmetric positive
definite after constraints, one null mode before them -- and reusing that
qualified work is the right instinct.

It cannot be reused BY INCLUDE from layer 45. The honest options are left to
the milestone whose boxes name them ("audit available linear algebra", "reuse
qualified compatible solver infrastructure"):

```text
extraction     move the generic sparse symmetric solve, its problem
               classification and its residual metrics to a module at or
               below 45 (a `numeric` module, or core), with structural
               consuming it from above. A change to a qualified module, so
               P17-SOLVE-001 is requalified by the full run that every
               milestone performs anyway -- and the extraction must be
               MECHANICAL: the same code, the same tests passing unchanged
independent    a second private Eigen link in thermal with its own thin
               wrapper. No change to P17, at the cost of a duplicated gate
               that the two modules could then let drift apart
```

This ADR records the constraint and the two admissible answers. It does not
pick one, because the audit that picks it belongs to `P18-SOLVE-001`.

## Rationale

The deciding fact is that the roadmap has already named the coupling
direction, and a layer number is the one architectural choice that a later
milestone cannot revise cheaply: moving a qualified module's layer is the cost
ADR-035 paid once so that it would never be paid again. Option A pays it at the
first coupling; Option C makes the coupling impossible without it; Option B
pays nothing now and nothing then.

What Option B costs -- P17's solver out of reach by include -- is a cost to one
milestone, is bounded, and has two honest answers. What Option C costs is
permanent.

What would have made Option A right: if thermo-mechanical coupling were out of
the roadmap. It is the roadmap's next sentence.

## Consequences

**Easy.** A thermo-mechanical milestone writes `#include <bettercad/thermal/...>`
from `src/structural/` and the checker allows it. No renumber.

**Hard.** `P18-SOLVE-001` must decide reuse by extraction or by an independent
link, and must say so with the measurement that decides it. A third option --
copying P17's solver into `thermal` by hand -- is a second authoritative
implementation of a qualified gate and is refused by this ADR.

**Committed.** To `thermal` never including `structural`, enforced by a fixture
that holds exactly that include and asserts the message naming it; and to
`meshing` never including `thermal`, the cycle fixture, for the second consumer
of a mesh.

## Verification

```text
architecture.layering                           every file under include/, src/,
                                                apps/, examples/: 0 violations
architecture.checker.valid                      thermal -> meshing, features,
                                                core ALLOWED, and the entry
                                                set(layer_thermal 45) is held in
                                                place by it
architecture.checker.thermal-layer-violation    thermal -> io REFUSED
architecture.checker.thermal-to-structural      thermal -> structural REFUSED
                                                (the direction this ADR fixes)
architecture.checker.meshing-to-thermal         meshing -> thermal REFUSED
                                                (the cycle)
architecture.checker.thermal-backend-leak       nglib.h in src/thermal/ REFUSED
```

Each fixture tree holds exactly one violation and asserts the message naming
it. There is deliberately no thermal-specific Qt, Eigen or apps/ fixture: rules
2, 6 and 7 key on a header's shape or location and not on a module, so the
existing fixtures cover this module already.

Evidence: [docs/verification/P18-ARCH-001/](../../verification/P18-ARCH-001/README.md).
