# ADR-060 — A CFD analysis input is validated once, the fluid is a P15 material, and possession is the proof

```text
STATUS:    Accepted
DATE:      2026-10-27
MILESTONE: P19-ARCH-001
TOUCHES:   ADR-027, ADR-028 (the material model; ADR-028 names CFD as a future
           consumer and defines nothing for it), ADR-036, ADR-051 (the same
           device for structural and thermal), ADR-033, ADR-058, ADR-059
```

## Context

P16 records a limitation it cannot close on its own: `Mesher::mesh()` hands back a
stale mesh deliberately ("it is old, not wrong", because the viewer inspects stale
meshes), so a function taking a `const VolumeMesh&` cannot tell a current mesh
from one describing a body the user has since changed. P17 closed that gap with
one device, *possession is the proof* (`requireStructuralModel`, ADR-036); P18
adopted it (`requireThermalModel`, ADR-051). A CFD solver that took a mesh would
reopen the gap a third time, and a cell-centred solver amplifies it: a topology,
a set of face areas and a flux field all index into whatever mesh they were
built from.

CFD adds a question the other two did not have: where does the fluid come from?
ADR-028 is explicit that "a downstream solver maintains no second authoritative
material database", names CFD as a consumer, and defines nothing for it. Three
facts are true today:

- a material carries a **density** (`MechanicalPropertyKind::Density`);
- `core/units` already has `DynamicViscosity` and `KinematicViscosity`;
- **no material carries a viscosity**, and `ConsumerKind` has no CFD value
  (`Completeness.hpp`: "a CFD consumer will also need a dynamic-viscosity
  property, which no material carries yet").

## Constraints

- One validation boundary, one set of refusals, delegated to P16 and P15 and not
  reimplemented; the same seven checks in the same order as the other two.
- No fluid data of its own in the CFD module: no default air, no typical water,
  no viscosity table (ADR-028).
- The boundary may not claim to prove what it does not check.
- This milestone adds no property to P15, no mesh type, no field and no solve.

## Options

### A — Take a mesh and a viscosity

Fast, and exactly the shape that lets a stale mesh and a hand-typed `mu` reach a
solver. Rejected on both counts.

### B — Keep the fluid in the CFD analysis definition

`rho` and `mu` as numbers in the analysis. Simple, and a second material database:
the same fluid is then two different numbers in two places, and a material edit
would no longer stale the result. Rejected by ADR-028.

### C — The fluid is a P15 material; the boundary resolves it; possession is the proof

The analysis uses **the document's P15 material assignment** (one per document in v1). The boundary resolves it through P15 and the
model holds the resolved values by value, as a derived view. A P15 viscosity
property and a CFD consumer kind are added by P19-FLUID-001, in P15's own
vocabulary, where every other consumer already lives.

## Decision

**Option C.**

```text
requireCfdModel(document, regenerator, mesher, control) -> Result<CfdModel>
cfdInputProblem(document, regenerator, mesher, control) -> optional<InputProblem>
```

`CfdModel` has no public constructor and one friend; it is move-only; it borrows
the mesher's storage and must not outlive an edit.

### What possession proves, at the moment of construction

```text
the control exists
the body is eligible -- regenerated, current, a solid, non-empty, valid, and not
  behind a configuration override                       (P16 geometryIneligibility)
a mesh is held for the control and its currency is Current        (P16 currency)
the mapping and the quality report came from that same mesh (three accessors keyed by the same control, read from one held entry)
the document's material resolves, and yields a usable DENSITY      (P15 requireDensity)
```

The checks run in that order, geometry first, so a body that has changed and not yet been
regenerated is reported as ineligible, and a regenerated change as a stale mesh, rather than as whatever else is wrong with the mesh. Seven
`InputProblem`s, every one reachable: `ControlNotFound`, `GeometryIneligible`,
`NoMesh`, `MeshStale`, `MeshGenerationFailed`, `NoMaterialAssigned`,
`FluidDensityUnusable`.

### What possession does NOT prove, named rather than stubbed

- **That the fluid has a viscosity.** No canonical type exists for it yet.
  P19-FLUID-001 adds the property to P15, a CFD consumer kind that requires
  `{ Density, DynamicViscosity }`, the resolution (`rho > 0`, `mu > 0`, finite,
  nothing defaulted), and the new `InputProblem`. Until then the boundary
  proves a density and says so; it does not pretend a CFD solve may start.
- that the mesh is good enough for a finite-volume method (P19-MESH-001,
  P19-VALID-001), that boundary conditions resolve (P19-BC-001), that the system is
  well posed, that the solve converged, or that mass is conserved. These land in
  a **staged validation entry after the boundary** (ADR-053's shape), not inside
  it.
- that the material *is* a fluid. A fluid is a material that carries a viscosity;
  there is no other marker and no CFD-side flag.

### The fluid

Canonical state is **the document's P15 material assignment**: P15 has one material
per document, and per-body materials and cross-document references are deferred. So
in v1 a CFD analysis is run on a document whose single assignment *is* the fluid, and
the body the control names is its flow region. The model holds the resolved values by
value; every consumer re-resolves from the document and a mismatch makes the derived
object stale, so a fluid edit stales a CFD result and does not stale geometry. There is
no `CfdFluid` database and no default. A document with a fluid region *and* a solid
housing of a different material cannot be analysed until per-body materials exist;
that is recorded as a prerequisite and is not worked around with a CFD-side fluid.

### Why the boundary is a third copy and not a shared seam

ADR-051 said that if P19 needed the same seven checks, "the extraction will have three
callers and the seam will be worth its cost". The natural seam is the mesh half
(control, geometry, currency, one held lookup) in `meshing` (40), below all three. It
is **not extracted in this milestone**, for two reasons: it would modify P16, a
qualified module, from inside an architecture milestone whose rule is that nothing
qualified changes; and the checks that differ (the material) are the ones that would
stay. The risk the extraction would remove - three copies drifting - is guarded
instead by a test that walks one document through every mesh and geometry state and
requires the CFD boundary to name the same reason as the thermal one and as the
structural one, step for step. The extraction is proposed to P19-MESH-001, which
touches the mesh contract anyway.
## Rationale

Possession-as-proof has now survived two full qualifications as the way to make
"the input is current" a compile-time fact rather than a convention a reviewer must
remember. Choosing it a third time costs one small header and buys the whole
chain the same guarantee. Saying plainly what the proof does *not* cover is the
lesson of P18-ARCH-001: a boundary that claims a transient-complete material when
it checked only a conductivity would have made every later gate trust something
untested.

## Consequences

- Every CFD entry point takes a `CfdModel` or the document and the mesher; none
  takes a mesh.
- P19-FLUID-001 is the first milestone with a P15 change: a property kind, a
  consumer kind, a completeness table row. ADR-028's mechanism extends to it
  without a second framework, as that ADR predicted.
- `CfdModel` is a snapshot. Holding one across a remesh or an edit is a convention
  violation the type cannot prevent (the same open point as the other two models,
  P18-QUAL-001 finding D3), and every production path rebuilds it.

## Verification

`tests/cfd/CfdAnalysisBoundaryTests.cpp`: the boundary accepts a current mesh with
a usable density; refuses a missing control, no mesh, a mesh of changed geometry
a changed sizing intent, a failed generation of
eligible geometry, an ineligible body **before** looking at the mesh, no material,
and a material without a density; reaches every `InputProblem`; and proves, by
`static_assert`, that a `CfdModel` cannot be default-made, copied or built from
its parts. Evidence: `docs/verification/P19-ARCH-001/`.
