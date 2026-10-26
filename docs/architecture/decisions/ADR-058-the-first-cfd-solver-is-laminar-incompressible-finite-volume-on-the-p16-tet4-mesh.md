# ADR-058 — The first CFD solver is laminar incompressible finite-volume flow on the P16 Tet4 mesh

```text
STATUS:    Accepted
DATE:      2026-10-27
MILESTONE: P19-ARCH-001
TOUCHES:   ADR-027, ADR-028 (the material model and its consumers; P19 is the
           consumer ADR-028 named and left undefined), ADR-032 (geometry
           references, never mesh ids), ADR-033 (the mesher behind a boundary),
           ADR-049 (the thermal scope, the pattern this follows), ADR-053
           (acceptance in one place)
```

## Context

P19 adds the third physics. Two solvers already exist and have set the rules a
third must obey: CAD geometry, the P15 material and the analysis intent are
canonical; the mesh, every numbering, every matrix, every field and every report
are derived and never persisted; staleness is derived from source identity;
acceptance is decided in one place; the command line is an adapter.

CFD differs from structural and thermal analysis in four ways that decide
the scope, and each one is a place where a quiet shortcut would create a second
authority.

1. **The unknowns are not at the nodes.** A finite-volume method keeps pressure
   and velocity at cell centres and a mass flux at faces. A cell is not a node,
   a face is not a node, and neither is a mesh `ElementId`. A numbering that
   quietly reused one of them would repeat the mistake P17 and P18 each took a
   milestone to rule out.
2. **Conservation is a gate, not a by-product.** A converged linear solve says
   nothing about whether mass is conserved. The linear residual and the mass
   imbalance are different facts and both must pass.
3. **The problem is nonlinear and iterative.** Steady flow is solved by an outer
   loop (pressure-velocity coupling) around linear solves. "Converged", "diverged"
   and "stopped at the iteration limit" are three outcomes, only one of which
   publishes a result.
4. **The fluid is not a solid.** No material of P15 carries a viscosity, and the
   geometry the roadmap calls "fluid-volume extraction" does not exist.

## Constraints

- No second geometry authority: the fluid domain is CAD geometry, reached through
  P16's one geometry boundary. No CFD-side geometry may be built from a mesh.
- No second material authority (ADR-028): density and viscosity come from the P15
  material or not at all. No CFD-only fluid table, no default air.
- No mesh-local identity in canonical intent (ADR-032): a boundary condition names
  a `GeometryReference`, never a cell, a face or a patch index.
- A persisted file and the command history hold intent only.
- **An unresolved or stale reference is a named refusal.** A boundary condition
  whose `GeometryReference` does not resolve against the body as it is now, or
  resolves to a mesh generation it was not made for, makes the input refused. It is
  never re-bound to a nearby face (ADR-032), and nothing is published.
- **Every boundary facet has exactly one condition.** A boundary facet that no
  condition names, or that two conditions name, makes the input refused. There is
  no default wall, no default outlet and no "unassigned means no-slip": the silent
  default is the classic way for a CFD result to be a number about a different
  problem. (The cardinality check is P19-BC-001's; the rule is decided here.)
- Nothing in this milestone adds a mesh type, a field, a matrix, a boundary
  condition or a solve.

## Options

### A — Everything at once

Steady and transient, laminar and turbulent, thermal coupling, multiple
schemes. A reviewer cannot tell which of pressure-velocity coupling, mesh
geometry, boundary mapping or turbulence closure is wrong when a benchmark
misses, and every layer's tests would lean on the others.

### B — Laminar incompressible only, qualified completely, turbulence afterward

The smallest physics that still contains the hard parts of CFD: a cell-centred
collocated finite-volume discretisation, a pressure-velocity coupling, face-flux
conservation, a nonlinear outer loop, and the engineering integrals (pressure
drop, flow split) the use case needs. Turbulence then adds one more layer to a
base that is already proved.

### C — Compressible or thermal flow first

The energy equation and an equation of state change the continuity equation, the
meaning of pressure and the boundary conditions at once. Rejected for the same
reason as A.

## Decision

**Option B.** The scope of P19 v1, and everything outside it, is stated here so a
later milestone cannot widen it by implication.

### Supported

```text
3D, single phase, INCOMPRESSIBLE, NEWTONIAN fluid
constant density rho                    kg/m^3
constant dynamic viscosity mu           Pa s  =  kg/(m s)
STEADY laminar                          SIMPLE-family pressure-velocity coupling
TRANSIENT laminar                       backward Euler, fixed step, actual last step
cell-centred collocated finite volume   a cell is one P16 tetrahedron
first-order upwind convection           (higher orders only after it is qualified)
body force: gravity as a canonical vector, applied as rho g
boundary conditions                     velocity inlet, static-pressure inlet,
                                        static-pressure outlet, no-slip wall,
                                        MOVING wall (a prescribed tangential velocity),
                                        slip / symmetry
initial conditions                      uniform velocity and pressure
```

The pressure inlet and the moving wall are in scope because the reference problems
the plan names need them: a fully developed Poiseuille profile is a pressure-driven
flow between a pressure inlet and a pressure outlet (a uniform-velocity inlet gives a
*developing* flow, a different problem), and the lid-driven cavity and Couette flow
have a wall that moves. P19-BC-001 qualifies each; a condition it cannot qualify is
refused, not approximated.

### The pressure datum, compatibility and the pressure convention

An incompressible flow determines pressure only up to a constant when no boundary
fixes it, and requires the prescribed net flux through the boundary to be zero. These
are decided here because a solver that "handles" them silently produces a plausible
wrong answer.

- **Reference pressure is canonical intent** (it is in the analysis definition) and
  is **required if and only if no boundary condition fixes the pressure**. If a
  static-pressure inlet or outlet exists, a reference pressure is *refused* as
  overdetermined.
- **Incompatible prescribed fluxes are refused, never rescaled.** If every boundary
  condition prescribes the velocity (so the net volume flux is determined by the
  inputs) and the net flux is not zero to the tolerance P19-MASSBAL-001 defines, the
  input is refused. The solver does not scale an inlet to make it balance.
- **`p` is the static gauge pressure** (relative to the reference). Gravity enters as
  the body force `rho g`, so the hydrostatic part is *inside* `p`, and a prescribed
  boundary pressure is a static gauge pressure at that boundary. A reference height
  is therefore part of the pressure datum when gravity is non-zero and is canonical.

### The conserved flux, and the interpolation that keeps it so

On a collocated arrangement the conserved quantity is the **face mass flux `phi`**,
not the cell-centred velocity interpolated to a face. The face flux must come from a
momentum-weighted interpolation that couples pressure to velocity (Rhie-Chow type);
plain linear interpolation of the cell velocity admits checkerboard pressure. That
interpolation is part of P19-PVCOUPLE-001 and is named here so it is not discovered
as an instability.

### Mass balance means a discrete measure, not a sum of inlets and outlets

With pressure-only boundaries the outflow is an *outcome*, so the check cannot be
"outlet equals the prescribed inlet". Mass balance is: (a) the **cell continuity
residual** `sum_f phi_f` per cell, and (b) the **global boundary imbalance**
`sum over boundary faces of phi`, each made relative to the throughput
(`0.5 * sum |phi|` over the boundary) with a floor. P19-MASSBAL-001 fixes the numbers;
the measures are fixed here, and neither is implied by the linear-solver residual.

### Deferred, and REFUSED rather than approximated

```text
compressible flow         multiphase              species
combustion                radiation               conjugate heat transfer
moving mesh               rotating machinery      non-Newtonian flow
LES  DES  DNS             porous media            free surface
cavitation                temperature-dependent properties
```

An unsupported request is an explicit diagnostic: there is no best-effort answer,
no silent downgrade, and no warning printed beside a number. The refusal is by
**absence** where it can be (there is no field, no enumerator and no function for
radiation or for a viscosity law), and by a named diagnostic where the request
is expressible.

### Turbulence

None in v1. P19-TURB-ARCH-001 defines the interface (mode, model fields, wall
treatment, boundary conditions, results, persistence, commands, CLI,
validation) **before** any model; `k-epsilon` and `k-omega SST` are separate
milestones and are deferred in full if v1 stays laminar. A solve whose Reynolds
number is beyond the laminar regime is **refused by name at validation** (the
threshold is fixed by P19-VALID-001 from a reference), never accepted with a
warning and never answered by quietly enabling a model: a steady laminar iteration
at high Reynolds number can converge to a non-physical answer, and "no best-effort
answer" applies to it as to every unsupported request.

### Thermal coupling

None in v1. The fluid is isothermal and its properties constant, so v1 produces a
pressure and a velocity field and nothing thermal. The one coupling v1 *can* feed is
CFD -> structural (a wall pressure is a structural load). CFD -> thermal (a wall
heat-transfer coefficient or heat flux) needs an energy equation CFD does not have;
the layer in ADR-059 leaves it open without building it. Buoyancy and conjugate heat
transfer are deferred.

### The geometry: the fluid domain is CAD

P19 analyses the body a meshing control names. That body is the **fluid
region**, modelled as CAD. The roadmap's "fluid-volume extraction" (deriving the
flow volume from the cavity of a solid) is a CAD operation, not a CFD one; if it
is built it lands as a CAD feature that regenerates like any other and is
reached through P16's geometry boundary. It is never reconstructed from a mesh
and never held by the CFD module.

**Which material is the fluid, in v1.** P15 has one material assignment per document;
per-body materials and cross-document references are deferred. So in v1 a CFD analysis
is run on a document **whose single material assignment is the fluid** (ADR-060), and
the body the meshing control names is its flow region. A document that holds both a
fluid region and a solid housing with different materials is not representable until
per-body materials exist; that is a prerequisite, recorded as such, for any
fluid-and-solid workflow (including CFD -> structural in one document), not something
P19 works around with a CFD-side fluid.

### The mesh: P16's Tet4 mesh serves, through a derived topology

The critical question of P19-MESH-001 is answered in principle here and
qualified there. A P16 mesh carries nodes, tetrahedra and boundary triangles with
their owning tetrahedron, and `tetrahedralBoundary` already derives faces from
connectivity (as node triples, for the faces used by exactly one tetrahedron; which
tetrahedron owns a boundary triangle comes through the geometry/mesh map's
`owningTetrahedraOf`). So **a finite-volume cell is a tetrahedron**, a face is a
triangle shared by two tetrahedra or owned by one, and the **interior** faces, the
owner/neighbour topology, the face area vectors, the cell volumes and the centroids
are **new, derived** work (P19-TOPO-001, P19-GEOM-001) keyed to the mesh stamp; P16
provides none of the interior topology, and its quality metrics are finite-element
metrics, so non-orthogonality and skewness are new measurements too. P19 does not
create a second mesher, a second geometry authority or a second mesh identity.
Tetrahedral finite-volume meshes have worse orthogonality and skewness than
hex-dominant ones; that is a measured quality question for P19-MESH-001 and
P19-VALID-001, with the answer allowed to be "this mesh is refused". If a
qualified result requires another cell type, the answer is an adapter from the
P16 mesh, never a parallel representation.

### Canonical and derived

```text
CANONICAL (document, history, file)
  the CFD analysis definition and its mode
  the fluid                                  the document's P15 material assignment, never a value
  boundary conditions                        id, kind, GeometryReference, typed values
  initial conditions                         uniform velocity and pressure
  gravity                                    a vector, or none, and the reference height
  the pressure datum                         a reference pressure, present only when no
                                             boundary condition fixes the pressure
  steady / transient settings, convergence settings, linear-solver settings
  output settings

DERIVED (never persisted, never in history, never an authority)
  the finite-volume topology                 cells, faces, owner/neighbour, patches
  face areas, normals, centroids, cell volumes, non-orthogonality
  every CellIndex, FaceIndex, BoundaryFaceIndex and field map
  momentum and pressure-correction matrices, sources, factorisations
  pressure, velocity, mass flux, gradients, turbulence fields
  residual histories, mass imbalance, forces, pressure drop, flow split
  validation reports and every render buffer
```

### Field locations and indexing

```text
pressure p     cell-centred scalar      indexed by CellIndex
velocity U     cell-centred vector      indexed by CellIndex (three components, a fixed order)
mass flux phi  face-centred scalar      indexed by FaceIndex, positive from owner to neighbour
```

`CellIndex`, `FaceIndex` and `BoundaryFaceIndex` are compact (0..N-1), assigned
by one deterministic enumeration of the mesh, and **distinct types that do not
convert** to each other or to `ElementId` or `NodeId`. No array is sized by a
maximum mesh id. A numbering belongs to one mesh stamp and is stale with it.

### Units

SI throughout, in the type system: metre, kilogram, second, pascal; `rho` kg/m^3,
`mu` Pa s, kinematic viscosity `nu` m^2/s **derived only** (`mu / rho`, never
stored), velocity m/s, mass flux kg/s, force N. Millimetres exist only at the
OCCT presentation boundary. Gauge pressure is the working variable of an
incompressible solver, and the reference pressure is canonical intent.

### Currentness

Every derived object records its source by value: the geometry revision, the mesh
stamp, the fluid material id and revision, and the analysis definition it was
computed from. A result is current exactly when a freshly computed source equals
the recorded one; counts, ids and addresses are never a substitute. The two
regimes of ADR-055 (content-keyed derived objects, revision-keyed reports) are
inherited unchanged, and the open owner decision about them (P18-QUAL-001,
finding D1) applies to P19 on the day it is taken.

### Failure

A solve returns an accepted result or a structured failure naming the stage and
the reason: input refused, mesh refused, system ill-posed, a linear solve
failed, the nonlinear loop **diverged**, it **reached the iteration limit**, a
field became non-finite, continuity was not closed, or the mass balance failed.
A failed attempt publishes nothing, leaves any older result stale and is itself a
state. "The solver returned" is never the criterion for acceptance.

### Determinism

Cell, face and boundary enumeration, assembly order, aggregation order and
tie-breaks are fixed functions of the mesh and the definition: no wall clock, no
pointer identity, no unordered iteration, no thread count. The same inputs give
the same bits in one process and across processes. Across the three build presets
the claim is limited to what is *assembled and summed in a fixed order* (topology,
geometry, matrix coefficients, aggregates): an iterative nonlinear solve around a
sparse factorisation is not promised to be bit-identical between optimisation levels,
and its qualification is by agreement to a stated tolerance, not by bits.

### Evidence

Each milestone leaves `docs/verification/P19-<name>-001/` with a README, the
audit, the adversarial review, the freeze and the three-preset qualification
logs, following P18's structure; the final milestone leaves the cross-milestone
record.

## Rationale

The scope is the smallest one in which a wrong answer cannot hide: laminar
incompressible flow has closed-form solutions (Poiseuille, Couette, the lumped
start-up) that are independent of every line of the solver, and conservation can
be checked from the faces alone. It still contains every structural hazard of a
larger CFD scope - a collocated pressure-velocity coupling, a nonlinear loop, a
face flux that must agree with the cell velocity, a mapping from CAD faces to
boundary faces - so qualifying it proves the architecture rather than a special
case. Making a cell a P16 tetrahedron gives up some numerical quality and in
return keeps one geometry authority and one mesh identity, which is the
property the project has paid for three times.

What would have made a different option right: a requirement for a body-fitted
hexahedral or prism-layer boundary mesh would have made an adapter, and then
a second mesher, unavoidable; this ADR does not forbid that, it forbids doing it
by building a second geometry authority.

## Consequences

- P15 must learn a dynamic viscosity before any fluid can be resolved
  (P19-FLUID-001). ADR-060 records why that is a material property and not a CFD
  table.
- `CellIndex` and friends must be distinct strong types from the first line, with
  compile-fail cases, as `ThermalDofIndex` was.
- The pressure-velocity coupling, the linear solves and the integrals each get a
  milestone and an independent oracle; none is accepted on "the solver
  converged".
- Higher-order convection, turbulence and any coupling are each their own decision
  and their own qualification.

## Verification

P19-ARCH-001 proves the boundary (ADR-060) and the layering (ADR-059) with tests
and checker fixtures. The scope itself is enforced as the later milestones land:
each unsupported item stays absent from the types, which the compile-fail groups
of those milestones assert.
