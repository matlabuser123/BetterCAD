# ADR-059 — A `cfd` module at layer 42, below `thermal` and `structural`, so its results can flow up

```text
STATUS:    Accepted
DATE:      2026-10-27
MILESTONE: P19-ARCH-001
TOUCHES:   ADR-035 (the layer table respaced in tens, which reserved this
           decision), ADR-050 (the same reasoning, applied to thermal), ADR-052
           (the generic solve lives in linalg, layer 5), ADR-033 (the mesher
           behind a boundary)
```

## Context

P19 needs a module that **consumes** P16's meshing exactly as `thermal` and
`structural` do: it reads the Tet4 volume mesh, its geometry/mesh mapping and its
quality measurements, and reaches geometry currency through P16's one geometry
boundary. `io` must be able to serialise its intent and `renderer` to display its
results.

The layering rule is strictly-lower: a module may include only modules on a
strictly lower layer. The number therefore decides, for the life of the stack,
which of three solvers may read which other's results. ADR-035's table and
ADR-050's decision left these integers free:

```text
core 0   linalg 5   sketch 10   features 20   assembly 30   drawing 40   meshing 40
thermal 45   structural 50   io 60   renderer 70   scripting 70
```

Which one-way couplings does the roadmap make likely?

- **CFD -> structural.** A pressure field on a wall is a structural load. This is
  the most common one-way fluid-structure coupling, and it needs `structural` to
  *read* a CFD result.
- **CFD -> thermal.** A wall heat-transfer coefficient or wall heat flux is a
  thermal boundary condition. It needs `thermal` to *read* a CFD result. (v1 CFD is
  isothermal and has no energy equation, so it cannot *produce* either; this coupling
  is a later possibility that the layer keeps open, not something v1 uses. Only
  CFD -> structural is real in v1.)

Layering is independent of how documents are arranged: P15 has one material per
document and per-body materials and cross-document references are deferred, so a
fluid region and its solid housing cannot yet live in one analysed document. That is
a prerequisite of any CFD -> structural *workflow*; it does not change which module
may read which.
- **thermal -> CFD** (a wall temperature for buoyancy or a temperature-dependent
  property) and conjugate heat transfer. The roadmap's "thermal flow" step puts an
  energy equation **in** the CFD solver, and conjugate heat transfer is an
  *exchange* between two solvers, orchestrated by something that sits above both.
  Neither needs one solver to include the other.

## Constraints

- The relative order of every existing module must not change (ADR-035); the
  decision uses a free integer and shifts nothing.
- `cfd` must be strictly above `meshing` (40) and strictly below `io` (60).
- The generic sparse solve is reached through `BetterCAD::linalg` (layer 5), by
  an independent PRIVATE link, never by an include of `structural` or `thermal`
  and never by a hand copy (ADR-052). No link is added in this milestone.
- A mesh-backend header (`nglib.h`) is confined to `src/meshing/<backend>/` and
  stays so.
- Nothing in this milestone may link linear algebra or perform a solve.

## Options

### A — 47 or 55, above `thermal` (and above `structural`)

CFD could include either. But the two couplings the roadmap makes likely run the
other way: a pressure load for `structural` and a heat-transfer coefficient for
`thermal` each need that module to *read* CFD. Above them, neither is possible
without a renumber of a qualified module - the cost ADR-035 was written to stop
paying. The only thing 47 buys is CFD reading a thermal result, which the energy
equation inside CFD replaces.

### B — 42, below `thermal` and `structural`

Both `thermal` (45) and `structural` (50) may include `cfd`; `cfd` may include
neither. Both likely one-way couplings are open with no renumber. The cost is
the mirror image: a CFD solve cannot read a thermal or a structural result by
include. Conjugate heat transfer and buoyancy are deferred (ADR-058), and the day
one is wanted it is an orchestrating module above both, which this layer does not
prevent.

### C — 40, a sibling of `meshing`

Not possible: the rule is strictly-lower, so a module on layer 40 cannot include
`meshing`, which `cfd` must.

## Decision

**Option B: `cfd` takes layer 42.** Enforced by the layering checker (a new
entry in its table) with fixtures that each hold exactly one violation and assert
the message naming it:

```text
cfd -> io                    refused      ("layering violation: 'cfd' must not depend on 'io'")
cfd -> structural            refused      ("... 'cfd' must not depend on 'structural'")
cfd -> thermal               refused      ("... 'cfd' must not depend on 'thermal'")
meshing -> cfd               refused      (the cycle: "... 'meshing' must not depend on 'cfd'")
cfd + nglib.h                refused      (the mesh backend stays contained)
thermal -> cfd, structural -> cfd   ALLOWED, proved by the valid tree
```

and by the link-graph checker, which refuses a `cfd` link to the structural or the
thermal target (a link is a second way to depend on a module that no include
shows).

## Rationale

The reader sits above the read: a layer is the statement of who may know whose
results, and the likely readers of a CFD result are the two solvers that already
exist. The decision is cheap to reverse *before* any CFD code exists and costly
afterwards, which is why it is taken in the architecture milestone and not
discovered by the first coupling.

What would have made a different option right: a requirement that the CFD solver
itself read a structural deformation (a moving boundary) or a thermal field
(buoyancy) *as an include* rather than through an orchestrator. ADR-058 defers
both; if either is admitted the module is renumbered by an explicit decision, as
ADR-035 anticipated.

## Consequences

- P17's `structural` and P18's `thermal` are unchanged and may start to read a CFD
  result in a later milestone with no renumber.
- The CFD module cannot reach any thermal or structural symbol, so the generic
  solver must come from `linalg`; P19-LINSOLVE-001 decides whether it suffices.
- Eight fixtures (five layering, three link-graph) join the architecture suite and the zero-match guard
  counts them.

## Verification

`architecture.layering`, `architecture.checker.valid`,
`architecture.checker.cfd-layer-violation`, `.cfd-to-structural`, `.cfd-to-thermal`,
`.meshing-to-cfd`, `.cfd-backend-leak`, and `architecture.targets.cfd-links-thermal`,
`.cfd-links-structural` and `.cfd-links-io` (the link graph is an allow-list as well as
two named refusals). Evidence: `docs/verification/P19-ARCH-001/LAYERING.md`.
