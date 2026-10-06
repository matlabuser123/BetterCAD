# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.  
> Qualification milestones require the final qualified tree to match the committed tree.  
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

# Status

```text
Current:
           P17 — Structural FEA

Prerequisite:
           P16 — Meshing — QUALIFIED (P16-QUAL-001, 2026-10-06)

Current milestone:
           P17-ARCH-001 — Structural FEA Architecture. AUTHORIZED by this
           document. Nothing after it is: finishing a milestone is a stop
           condition, and the next one needs this file to say so.

Qualified:
           P0–P10 — BetterCAD v0.1.0
           P11 — Advanced Part Modelling
           P12 — Production Part Modelling
           P13 — Assemblies
           P14 — Technical Drawings
           P15 — Materials / Engineering Data
           P16 — Meshing

Next:
           P17-DATA-001 — Analysis / Result Data Model, after P17-ARCH-001
           passes its gates and its evidence is recorded. Not before.

P17 objective:
           authoritative CAD / material / mesh state
           → structural analysis definition
           → linear-elastic FE assembly
           → boundary conditions / loads
           → linear solve
           → displacements / reactions
           → strain / stress recovery
           → structural validation
           → inspectable, persistent, headless result

P17 does NOT implement:
           nonlinear material behaviour, plasticity, large deformation,
           contact, buckling, fatigue, fracture, composites, dynamics or
           modal analysis, transient structural analysis, topology
           optimisation, adaptive remeshing, shell or beam elements,
           multiple-solid contact assemblies

P16 milestones:
           recorded in ROADMAP.md with their evidence links. A completed
           phase does not accumulate in this file.
```

---

# Carried

```text
1. FileIo atomic replace

   A document save can still lose to a file synchroniser when Windows
   temporarily refuses the final rename.

   Keep carried.
   Do not silently fold this into P17.


2. Configuration regeneration

   A configuration parameter override can change the effective parameter
   without rebuilding the geometry that reads it.

   This matters directly to P17:
   a structural result must NEVER be computed from stale configuration
   geometry, and a mesh must never be generated from it either.

   Until fixed:
   configuration-sensitive meshing and analysis must explicitly refuse
   stale geometry, following the same safety principle used by P15 mass
   properties. P16 does; P17 must do the same.


3. Hole POSITION dimensions unsupported.

4. GD&T symbols not fully embedded in PDF/DXF.

5. Cross-preset drawing export byte identity not guaranteed.
```

---

# Completed Phases

```text
P0–P10   BetterCAD v0.1.0
P11      Advanced Part Modelling              QUALIFIED
P12      Production Part Modelling            QUALIFIED
P13      Assemblies                           QUALIFIED
P14      Technical Drawings                   QUALIFIED
P15      Materials / Engineering Data         QUALIFIED
P16      Meshing                              QUALIFIED
```

Detailed completed history belongs in:

```text
ROADMAP.md
docs/verification/
docs/architecture/decisions/
```

Do not expand completed-phase implementation diaries in `TODO.md`.

---

# P17 — Structural FEA

Give BetterCAD a trustworthy linear-static structural solver that consumes
authoritative CAD geometry, canonical P15 material data and a validated P16
Tet4 mesh, and produces displacements, strains, stresses and reactions that an
engineer can check against an analytical answer.

```text
CAD model intent
→ regenerated authoritative geometry
+ P15 material definition
+ P16 current validated Tet4 mesh
+ geometry-based load / restraint intent
→ structural analysis preparation
→ DOF numbering
→ Tet4 element matrices
→ global K / F assembly
→ boundary-condition application
→ linear solve            K u = F
→ displacement field
→ reactions
→ strain
→ stress
→ validation
→ derived result
→ P18 Thermal Analysis
```

P17 owns:

```text
structural analysis definition
analysis / result data model
structural material resolution
DOF numbering and the constraint model
the Tet4 linear-elastic element
structural loads
structural restraints
global assembly
the linear solver
displacement / strain / stress recovery
reaction forces and equilibrium
structural validation and acceptance policy
FEA inspection
FEA commands
the structural persistence contract
headless workflows
reference models
qualification
```

P17 does NOT own geometry, meshing, or material data. It consumes all three.

## The first supported solver

```text
small-strain
linear-static
isotropic linear-elastic
Tet4
single solid
```

Anything else is out of scope until a later milestone authorizes it.

## P17 does NOT initially implement

```text
nonlinear material behaviour     plasticity
large deformation                contact
buckling                         fatigue
fracture                         composites
dynamics / modal analysis        transient structural analysis
topology optimisation            adaptive remeshing
shell / beam elements            multiple-solid contact assemblies
```

---

# P17 Core Invariants

* CAD geometry remains authoritative.
* P15 engineering material data remains authoritative.
* The P16 Tet4 mesh remains derived state.
* P17 solver results remain derived state.
* Loads and restraints reference CAD geometry intent, never a persisted
  `NodeId` or `ElementId`.
* `NodeId` / `ElementId` are valid only for the mesh that issued them.
* Every solver entry point re-checks geometry currentness, mesh currentness,
  mesh validity, material validity and analysis-definition validity.
* A stale mesh must never be solved as current.
* Generated stiffness matrices, vectors and results are never canonical
  document authority.
* P17 consumes P16's mesh and mapping APIs. P17 does not create a competing
  mesher.
* A material edit invalidates the FEA result. It does NOT necessarily
  invalidate the mesh.
* An under-constrained model fails explicitly. It never reports a large finite
  displacement as a solution.
* The solver's own success status is never sufficient. The residual is checked.
* Engineering and tensor shear-strain conventions are fixed once, written
  down, and shared by assembly and post-processing.
* Equilibrium is a gate, not a diagnostic: for a static model the applied and
  reaction forces must sum to zero.

---

# P17 Scope Boundaries

## In scope

```text
linear-static structural analysis on a single solid
Tet4 constant-strain elements
isotropic linear elasticity
nodal force, face traction, face pressure
optional gravity / body force
fixed and component-selective zero-displacement restraints
displacement, strain, stress, von Mises, principal stresses
support reactions and equilibrium validation
the structural mesh-acceptance policy P16 deliberately left unowned
```

## Out of scope

```text
every item in "P17 does NOT initially implement"
a second mesher, a second material store, a second document model
prescribed non-zero displacement, unless a milestone authorizes it
multi-body contact, which P16 refuses at the mesh level today
```

---

# P17 Sequence

```text
P17-ARCH-001      Structural FEA Architecture
P17-DATA-001      Analysis / Result Data Model
P17-MAT-001       Structural Material Resolution
P17-DOF-001       DOF Numbering / Constraint Model
P17-ELEM-001      Tet4 Linear Elastic Element
P17-LOAD-001      Structural Loads
P17-BC-001        Structural Restraints
P17-ASSEMBLY-001  Global Matrix / Vector Assembly
P17-SOLVE-001     Linear System Solver
P17-POST-001      Displacement / Strain / Stress Recovery
P17-REACTION-001  Reaction Forces / Equilibrium
P17-VALID-001     Structural Validation / Acceptance
P17-VIZ-001       FEA Visualisation / Inspection
P17-CMD-001       Commands / Undo / Redo
P17-PERSIST-001   Structural Analysis Persistence
P17-CLI-001       Headless Structural FEA
P17-REFMOD-001    Structural Reference Models
P17-QUAL-001      Full P17 Qualification
```

The order deliberately puts the element formulation, the load and restraint
mapping, assembly, solve and validation policy BEFORE the GUI: a trustworthy
core first, then inspection, persistence, CLI and qualification around it.

---
# P17 Prerequisites Already In The Tree

Read before `P17-ARCH-001`. Each line below was checked against the code, not
assumed, because BetterCAD's generic infrastructure has repeatedly already
covered the next case. None of this is authorization to skip a milestone; it
is the starting point each one inherits.

## P15 already names P17 as a consumer

`include/bettercad/core/materials/Completeness.hpp` defines:

```text
ConsumerKind::FeaLinearStatic              E and nu ONLY
ConsumerKind::FeaLinearStaticWithGravity   the same, plus a density
ConsumerKind::FeaYieldStrength
MechanicalPropertyKind::{Density, YoungsModulus, PoissonsRatio, ...}
features::requireDensity(document, id)
CompletenessState::{Ready, Incomplete, Invalid}
IssueKind::{MissingProperty, InconsistentValues, OrphanProvenance, ...}
```

Its stated rule is "MISSING DATA IS REPORTED, NEVER FILLED. No default
modulus, no typical density." So `P17-MAT-001` is mostly a question of
consuming the right `ConsumerKind` and honouring its answer — and the
gravity/no-gravity split the brief asks for already exists as two consumers.
Verify what is there before adding anything.

## Eigen 5.0.1 is already a dependency, and is private to one module

```text
cmake/BetterCADDependencies.cmake:50   BetterCAD::eigen, header-only,
                                       SHA256-pinned, "private to
                                       bettercad_sketch"
```

So `P17-SOLVE-001`'s library audit starts from a pinned, already-qualified
dense/sparse library rather than a blank page. Making it available to a
structural module is an explicit decision, not a side effect.

## A new `fea` module cannot share a layer with meshing

`tests/architecture/CheckLayering.cmake` reports a violation when the included
module differs from the includer AND its layer is **not strictly less**
(lines 129-130). The table today:

```text
core 0   sketch 1   features 2   assembly 3
drawing 4   meshing 4   io 5   renderer 6   scripting 6
```

`drawing` and `meshing` may share layer 4 only because neither uses the other.
A structural module **does** use meshing, so it must be strictly above 4 and
strictly below `io` 5 — **and there is no integer between them.** So
`P17-ARCH-001` faces the renumbering that `ADR-006` and `ADR-015` each had to
perform, and it should decide deliberately rather than discover it:

```text
move io / renderer / scripting up      the ADR-006 / ADR-015 precedent
respace the table                      a wider change, once
some other containment                 must be argued, not assumed
```

An unregistered module fails the build, so the table must be updated in the
same change that creates the module.

## The licence decision a solver choice would make

`LICENSE` grants no permission to distribute, so the project's licence is still
the owner's to choose. Adding a copyleft solver would make that choice by
accident. Eigen is MPL-2.0 and passes the project's weak-copyleft admission
rule; several common sparse direct solvers do not. `P17-SOLVE-001` must state
the licence of anything it proposes and leave a GPL/AGPL choice to the owner.

## What P16 hands over, and what it refuses

```text
HANDS OVER   a validated Tet4 VolumeMesh with outward-oriented, closed,
             manifold boundary; a complete GeometryMeshMap from CAD faces to
             boundary facets; NamedBoundarySet resolution from FaceName;
             MeshCurrency and meshing::isStale(); report-only quality metrics

REFUSES      a body with more than one solid, explicitly and tested
             a mesh of stale, failed, blocked or non-solid geometry
             configuration-overridden geometry
             a drilled hole's cylindrical wall as a FaceName target
```

The refusals are P17's constraints, not P17's to lift.

---
# P17-ARCH-001

## Structural FEA Architecture

* [ ] Define P17 solver scope
* [ ] Define linear-static assumption
* [ ] Define small-strain assumption
* [ ] Define isotropic linear-elastic material assumption
* [ ] Define Tet4 as the initial structural element
* [ ] Define CAD / mesh / solver authority boundaries
* [ ] Define analysis-definition ownership
* [ ] Define derived-result ownership
* [ ] Define the geometry-currentness requirement
* [ ] Define the mesh-currentness requirement
* [ ] Define the material-currentness requirement
* [ ] Define the solver-input validation boundary
* [ ] Define P16 mapping consumption
* [ ] Define P15 material consumption
* [ ] Decide the module's layer and update `CheckLayering.cmake` in the same
      change (see P17 Prerequisites: there is no integer between meshing and io)
* [ ] Define unsupported physics explicitly
* [ ] ADR(s) completed
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
solver scope explicit
+ authority boundaries explicit
+ stale-input protection explicit
+ P15/P16 contracts reused rather than duplicated
+ module layer decided and enforced
+ unsupported behaviour explicit
```

---

# P17-DATA-001

## Analysis / Result Data Model

* [ ] Define `AnalysisId`
* [ ] Define `LoadCaseId` if required
* [ ] Define `LoadId`
* [ ] Define `RestraintId`
* [ ] Define solver / result identity
* [ ] Define DOF identity
* [ ] Define nodal displacement representation
* [ ] Define reaction-force representation
* [ ] Define element strain representation
* [ ] Define element stress representation
* [ ] Define units for every field
* [ ] Define result-currentness semantics
* [ ] Define result invalidation dependencies
* [ ] Define the analysis state machine
* [ ] Prevent mesh IDs becoming permanent CAD identity
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Required result-currentness dependency

An FEA result is current only if ALL of these match:

```text
geometry revision
mesh revision
meshing intent / source
material revision
load / restraint revision
solver-settings revision
```

Note which of these are document identities and which are mesh-local handles:
`AnalysisId`, `LoadId` and `RestraintId` are persisted document identities and
belong in `core/Id.hpp`; a DOF index is derived from the current mesh and must
not be, for the same reason `NodeId` is not (ADR-031).

---

# P17-MAT-001

## Structural Material Resolution

Consume qualified P15 engineering material data. At minimum `E`, `nu`, and
`rho` where self-weight is required.

* [ ] Resolve the structural material for the active body
* [ ] Validate E > 0
* [ ] Validate -1 < nu < 0.5
* [ ] Reject NaN / infinity
* [ ] Unit-safe material inputs
* [ ] Define missing-material behaviour
* [ ] Define custom-material behaviour
* [ ] Define provenance behaviour
* [ ] Define material-change result invalidation
* [ ] Verify a material change does NOT require a remesh
* [ ] Verify a material change DOES invalidate the structural result
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Critical rule

```text
material edit  →  FEA result stale

NOT
material edit  →  mesh stale
```

### Start from what exists

`ConsumerKind::FeaLinearStatic` and `FeaLinearStaticWithGravity` already exist
in `core/materials/Completeness.hpp`, and P15's rule is that missing data is
reported and never filled. Probe before adding: the validation this milestone
needs may already be a `CompletenessState` away.

---

# P17-DOF-001

## DOF Numbering / Constraint Model

Three translational DOFs per node: `ux`, `uy`, `uz`.

* [ ] Define deterministic DOF numbering
* [ ] Map `NodeId` to its three DOFs
* [ ] Define constrained and free DOFs
* [ ] Define DOF ordering
* [ ] Reject missing nodes
* [ ] Reject duplicate DOFs
* [ ] Define active / free equation numbering
* [ ] Determinism PASS
* [ ] Scale to the reference meshes
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
each current mesh node  →  exactly 3 translational DOFs
+ deterministic numbering
+ constraints representable
+ no permanent solver identity survives a remesh
```

---

# P17-ELEM-001

## Tet4 Linear Elastic Element

Constant-strain tetrahedral elasticity. For a linear Tet4 `B` is constant over
the element, so:

```text
Ke = integral of B^T D B dV   =   V B^T D B
```

* [ ] Derive the Tet4 shape functions
* [ ] Derive the Jacobian
* [ ] Derive the B matrix
* [ ] Define the isotropic D matrix
* [ ] Implement the element stiffness matrix
* [ ] Verify Ke is symmetric
* [ ] Verify the rigid-body modes
* [ ] Verify the unconstrained Ke is positive semidefinite
* [ ] Verify element volume handling
* [ ] Reject an inverted Tet
* [ ] Reject a degenerate Tet
* [ ] Validate coordinate-scale behaviour
* [ ] Analytical single-Tet test
* [ ] Independent reference implementation / test
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Conventions, fixed once

```text
lambda = E nu / ((1 + nu)(1 - 2 nu))
mu     = E / (2 (1 + nu))

strain  [ exx eyy ezz gxy gyz gzx ]^T      ENGINEERING shear
stress  [ sxx syy szz txy tyz tzx ]^T
```

Write the Voigt ordering down and share it between assembly and
post-processing. Do not mix engineering and tensor shear strain.

Note for the orientation check: P16's adapter already corrects Netgen's
inverted node order with one swap, so a mesh arriving here has positive signed
volume. This milestone must still reject an inverted Tet on its own account —
it must not rely on an upstream guarantee it does not verify.

### Gate

```text
Tet4 stiffness analytically correct
+ symmetric
+ rigid-body modes correct
+ invalid Tet rejected
+ units correct
```

---
# P17-LOAD-001

## Structural Loads

Keep the initial set small:

```text
nodal force
distributed force / traction on a CAD face
pressure on a CAD face
gravity / body force, if the density path is ready
```

* [ ] Define the load schema
* [ ] Define force units
* [ ] `GeometryReference`-based face loads
* [ ] Nodal-force policy explicit
* [ ] Surface traction
* [ ] Pressure
* [ ] Direction convention explicit
* [ ] Pressure-normal convention explicit
* [ ] Convert mapped facets to equivalent nodal forces
* [ ] Preserve the resultant force
* [ ] Preserve the resultant moment where mathematically required
* [ ] Gravity / body-force decision explicit
* [ ] Reject an unresolved load reference
* [ ] Load-change result invalidation
* [ ] Deterministic load vector
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Critical mapping rule

```text
CAD face load intent
→ P16 GeometryReference
→ current boundary facets
→ equivalent current nodal load vector
```

Facet IDs are never persisted as load authority. A `FaceName` is; a facet
index is not. P16's `NamedBoundarySet` already resolves a `FaceName` to the
current facets, and `GeometryMeshMap` is complete for every reference model —
use them rather than classifying geometrically with a tolerance.

Note the known gap: a drilled hole's cylindrical wall has no `FaceName`, so it
cannot be a load target today. Do not fake targeting for unsupported topology.

---

# P17-BC-001

## Structural Restraints

```text
fix ux        fix uy        fix uz
fixed support
component-selective displacement = 0
```

Prescribed non-zero displacement is deliberately deferred.

* [ ] Define the restraint schema
* [ ] `GeometryReference`-based restraints
* [ ] Resolve a CAD region to the current mesh nodes
* [ ] Fixed support
* [ ] X restraint
* [ ] Y restraint
* [ ] Z restraint
* [ ] Combined component restraints
* [ ] Duplicate restraint handling
* [ ] Conflicting restraint handling
* [ ] An unresolved reference fails explicitly
* [ ] Restraint-change result invalidation
* [ ] Deterministic constrained DOF set
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
restraint intent attached to CAD geometry
+ the current node set derived from P16 mapping
+ constrained DOFs deterministic
+ unresolved references explicit, never rebound to a nearby face
```

---

# P17-ASSEMBLY-001

## Global Matrix / Vector Assembly

Assemble `K u = F`.

* [ ] Define the sparse-matrix representation
* [ ] Define the vector representation
* [ ] Assemble the Tet4 `Ke`
* [ ] Assemble the nodal force vector
* [ ] Assemble the surface-load contribution
* [ ] Assemble the body-force contribution if supported
* [ ] Deterministic element traversal
* [ ] Deterministic DOF insertion
* [ ] Validate K dimensions
* [ ] Validate K symmetry
* [ ] Validate every entry is finite
* [ ] Validate the unconstrained rigid-body singularity
* [ ] No duplicate-entry loss
* [ ] No race-sensitive assembly
* [ ] Large-mesh smoke
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Representation

Prefer a solver-compatible sparse format (CSR or the selected library's own).
Do not write a custom sparse-matrix library unless a recorded reason requires
it.

Determinism here is not a formality: BetterCAD's determinism rule forbids
depending on unordered iteration, and a sparse assembly that sums in map order
can differ bitwise between presets. Traversal order must be an explicit
decision.

---

# P17-SOLVE-001

## Linear Static Solver

Solve `K u = F` after restraints.

* [ ] Audit available linear algebra libraries
* [ ] State the licence of anything proposed (see P17 Prerequisites)
* [ ] Select a direct or iterative solver
* [ ] Define solver tolerances
* [ ] Define convergence criteria
* [ ] Define maximum iterations where relevant
* [ ] Apply constraints correctly
* [ ] Detect a singular system
* [ ] Detect an under-constrained model
* [ ] Detect a non-finite solution
* [ ] Structured solver diagnostics
* [ ] Deterministic solve behaviour MEASURED
* [ ] Residual validation
* [ ] Independent small-system validation
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Required residual check

```text
r = K u - F        on the free system
```

The solver's own success status is never sufficient evidence. Record the
residual.

---

# P17-POST-001

## Displacement / Strain / Stress Recovery

```text
u_e  →  epsilon = B u_e
        sigma   = D epsilon
```

constant within each linear Tet4.

* [ ] Recover the nodal displacement vector
* [ ] Recover Tet strain
* [ ] Recover Tet stress
* [ ] Define the Voigt component order (same as P17-ELEM-001)
* [ ] Define tensor conventions
* [ ] Compute displacement magnitude
* [ ] Compute principal stresses
* [ ] Compute von Mises stress from the full 3D state
* [ ] Define hydrostatic stress if useful
* [ ] Define principal strain if useful
* [ ] Units explicit
* [ ] All results finite
* [ ] Independent analytical validation
* [ ] Deterministic result ordering
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

Implement von Mises from the full 3D stress state and validate it
independently. No 2D simplification.

---

# P17-REACTION-001

## Reaction Forces / Equilibrium

* [ ] Recover support reactions
* [ ] Define the sign convention
* [ ] Sum the external applied force
* [ ] Sum the reaction force
* [ ] Validate force equilibrium
* [ ] Validate moment equilibrium where applicable
* [ ] Reaction components inspectable per restraint
* [ ] Multiple support regions
* [ ] Deterministic aggregation
* [ ] Analytical reference cases
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Fundamental equilibrium gate

```text
sum(F external) + sum(F reaction)  ~  0
sum(M external) + sum(M reaction)  ~  0      where relevant
```

State the tolerance and why. BetterCAD's rule: 1e-12 for well-conditioned
double-precision algebra, 1e-9 for geometric accumulation, larger only with a
documented reason. A solver residual is neither of those by default, so derive
the tolerance from a measurement rather than picking one.

---
# P17-VALID-001

## Structural Validation / Acceptance

**This milestone owns the P17 acceptance policy.** P16's quality thresholds
are deliberately report-only: `P16-QUALITY-001` ships metrics and no
accept/reject line, because the phase that consumes a mesh owns them. This is
that phase.

* [ ] Define the FEA mesh-acceptance policy
* [ ] Define structural-invalid mesh rejection
* [ ] Define the quality warning policy
* [ ] Define the quality failure policy if used
* [ ] Define under-constrained detection
* [ ] Define over- / conflicting-constraint behaviour
* [ ] Define material-validity checks
* [ ] Define load-validity checks
* [ ] Define stale-input checks
* [ ] Define solver-residual acceptance
* [ ] Define equilibrium acceptance
* [ ] Define result-finiteness checks
* [ ] Structured validation report
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Solver entry-point safety

Every solve validates:

```text
geometry current
mesh current
mesh structurally valid
mesh quality acceptable under P17 policy
material current and valid
loads resolved
restraints resolved
system sufficiently constrained
```

P16 recorded that nothing FORCES a mesh holder to call `isStale` — a
`VolumeMesh` records its source and revision and answers truthfully, but only
code that asks finds out. So P17's entry point takes the document and the
feature and re-checks, rather than trusting a mesh a caller handed it.

### A note on thresholds

P16 measured what the metrics can and cannot see, and P17's policy should
start from those measurements rather than from intuition: `l_max/l_min`
saturates at sqrt(3) and cannot detect a sliver, while the radius ratio
collapses by three orders of magnitude on the same element. A policy written
on aspect ratio alone would accept a sliver. A cylinder's chord-polygon
boundary also gives a lower minimum dihedral than a thin plate, so a threshold
tuned on one body shape will reject a legitimate mesh of another.

---

# P17-VIZ-001

## FEA Visualisation / Inspection

* [ ] Undeformed mesh display
* [ ] Deformed-shape display
* [ ] Deformation scale factor
* [ ] Displacement magnitude contour
* [ ] Ux contour
* [ ] Uy contour
* [ ] Uz contour
* [ ] von Mises contour
* [ ] Normal stress components
* [ ] Shear stress components
* [ ] Principal stress inspection
* [ ] Element inspection
* [ ] Node displacement inspection
* [ ] Support reaction inspection
* [ ] Load arrows
* [ ] Restraint symbols
* [ ] Boundary-set highlighting
* [ ] Worst / highest-result navigation
* [ ] Clear stale-result visual state
* [ ] Rendering does not mutate results
* [ ] The GUI does not duplicate canonical result data
* [ ] Regression PASS
* [ ] Evidence recorded

### Critical visualisation rule

```text
deformed display = a visual transform of a derived solver result

NOT
deformed display → mutation of CAD or model geometry
```

Two P16 traps apply directly: `MeshView` works in SI metres while OCCT model
space is millimetres, and a contour that classifies nothing reports everything
as valid. Check the units at the boundary and check that a classifier
actually classifies.

---

# P17-CMD-001

## Commands / Undo / Redo

Canonical history contains analysis definitions, loads, restraints and solver
settings. It does NOT contain `K`, `F`, displacement arrays, stress arrays or
any generated solver result.

* [ ] Create-analysis command
* [ ] Add-load command
* [ ] Edit-load command
* [ ] Remove-load command
* [ ] Add-restraint command
* [ ] Edit-restraint command
* [ ] Remove-restraint command
* [ ] Solver-settings command
* [ ] Undo exact
* [ ] Redo exact
* [ ] A failed command is atomic
* [ ] Redo invalidation correct
* [ ] Geometry references preserved
* [ ] The FEA result is invalidated correctly
* [ ] No solver arrays in canonical history
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

P16 enforced the equivalent rule with compile-fail cases rather than review —
a command API that mentions a node or an element does not compile. Consider the
same instrument here.

---

# P17-PERSIST-001

## Structural Analysis Persistence

* [ ] Define the P17 schema
* [ ] Persist the analysis definition
* [ ] Persist loads
* [ ] Persist restraints
* [ ] Persist `GeometryReference`s
* [ ] Persist solver settings
* [ ] Preserve units
* [ ] Preserve unresolved references explicitly
* [ ] Exclude K / F / u / stress arrays as authority
* [ ] Malformed files rejected
* [ ] Invalid values rejected
* [ ] Duplicate IDs rejected
* [ ] Backward compatibility
* [ ] Deterministic serialization
* [ ] Full round trip
* [ ] Save / load / re-solve
* [ ] Compare re-solved semantics
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Derived-state rule

```text
save  →  structural-analysis intent
load  →  regenerate or reuse a valid current mesh
      →  solve again when requested
```

The persistence test runs the real round trip: create, save, destroy, load,
re-solve, compare. Never file size or object count alone. P16's equivalent
gate was that the saved file is byte-identical for a 361-element and a
1977-element mesh — the analogous P17 check is that it is byte-identical
whether or not a solve has been run.

---

# P17-CLI-001

## Headless Structural FEA

Follow BetterCAD's existing CLI naming; the P16 set is ten `mesh-*` commands.

* [ ] CLI inspect analysis definition
* [ ] CLI add / remove loads
* [ ] CLI add / remove restraints
* [ ] CLI solve
* [ ] CLI displacement summary
* [ ] CLI stress summary
* [ ] CLI reaction summary
* [ ] CLI validation report
* [ ] Structured diagnostics
* [ ] Correct process exit codes
* [ ] Stale-geometry failure
* [ ] Stale-mesh failure
* [ ] Missing-material failure
* [ ] Under-constrained failure
* [ ] Invalid load / restraint failure
* [ ] CLI / core equivalence
* [ ] Multi-process save / load / re-solve
* [ ] No CLI-only FEA implementation
* [ ] Fresh-binary proof
* [ ] Zero-match test protection
* [ ] End-to-end scripted workflow PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

Machine-readable output is fragile for a reason P16 had to find: Netgen writes
a sizing warning to STDOUT, where `2>/dev/null` cannot remove it, and it
corrupts any structured output. Silence a backend's chatter in the adapter,
never in the consumer.

---
# P17-REFMOD-001

## Structural Reference Models

```text
RM-FEA-01   Single Tet patch test
RM-FEA-02   Axial bar / prism in tension
RM-FEA-03   Cantilever beam
RM-FEA-04   Block under uniform pressure
RM-FEA-05   Gravity / body-force block
RM-FEA-06   Transformed structural model
RM-FEA-07   Local-refined cantilever / stress field
RM-FEA-08   Under-constrained model -- explicit failure
RM-FEA-09   Invalid / stale mesh -- explicit failure
RM-FEA-10   Unresolved boundary reference -- explicit failure
```

* [ ] Define the reference suite
* [ ] Single-Tet analytical case
* [ ] Constant-strain patch test
* [ ] Axial tension
* [ ] Cantilever bending
* [ ] Pressure resultant
* [ ] Reaction equilibrium
* [ ] Displacement agreement
* [ ] Stress agreement
* [ ] von Mises validation
* [ ] Rigid-transform invariance
* [ ] Mesh-refinement behaviour
* [ ] Local refinement behaviour
* [ ] Under-constrained failure
* [ ] Stale-mesh failure
* [ ] Unresolved-reference failure
* [ ] Save / load / re-solve
* [ ] CLI validation
* [ ] Independent analytical validation
* [ ] Determinism PASS
* [ ] Three-preset regression PASS
* [ ] Evidence recorded

### RM-FEA-01 -- constant-strain patch test

An affine displacement field:

```text
u(x,y,z) = [ a1 + a2 x + a3 y + a4 z
             b1 + b2 x + b3 y + b4 z
             c1 + c2 x + c3 y + c4 z ]
```

A linear Tet4 must reproduce this exactly to floating point. Validate constant
strain, constant stress, exact nodal interpolation and internal force
consistency. This is the strongest single test of the implementation.

### RM-FEA-02 -- axial bar

```text
delta = F L / (A E)        sigma = F / A        epsilon = sigma / E
```

Validate tip displacement, axial stress, strain, and reaction = -F.

### RM-FEA-03 -- cantilever beam

```text
delta_tip ~ F L^3 / (3 E I)        sigma = M y / I
```

3D continuum FEA and beam theory differ on a coarse mesh. Use a convergence
study, coarse / medium / fine, and do NOT expect equality on one coarse mesh.

**Assert the levels really differ before comparing errors.** P16-REFMOD-001
found a convergence table that passed on one unit in the last place, because
two different surface deflections produced the identical boundary. Show a
structural difference -- strictly increasing element counts -- then compare.

### RM-FEA-04 -- uniform pressure

Pressure p on a known face area A must give resultant F = p A. Validate that
the nodal load conversion preserves the resultant, and validate the support
reactions.

### RM-FEA-05 -- gravity / body force

Only if gravity is in the initial scope. For density rho and volume V the
expected weight is W = rho V g; validate the equivalent nodal body force and
the reactions. Otherwise defer this model EXPLICITLY, as a deferral with a
reason rather than as a gap.

### RM-FEA-06 -- rigidly transformed model

Invariant: strain, stress invariants, strain energy, element stiffness physics.
Transformed: displacement vectors, reaction vectors.

Judge node populations separately, as P16-REFMOD-001 had to: boundary node
positions are CAD-determined and follow R x + t closely, while an interior node
is the mesher's own choice and is NOT equivariant under rotation -- 2.66e-4 mm
on a 108 mm body, four thousand times the arithmetic noise. One tolerance over
both either fails a correct result or gives up the check.

### RM-FEA-07 -- local refinement / convergence

Validate that the solve stays valid, the target region is really refined, the
result trend is sensible, and the mapping is preserved. Do not claim stress
convergence at a mathematical singularity.

Measuring refinement needs care: a refined face's own boundary facets do not
show it, because OCCT gives a planar face two triangles whatever the
deflection. Measure a band in the volume, and prove it with a mirror model
that refines the opposite face.

### RM-FEA-08 -- under-constrained model

```text
REQUIRED   the solve FAILS explicitly, with a singular / rigid-body diagnostic
FORBIDDEN  a huge finite displacement reported as a successful solution
```

### RM-FEA-09 -- stale / invalid mesh

A mesh made stale by a geometry or settings edit. The solve must be refused
BEFORE assembly. This is the model that protects against P16's recorded
stale-cache limitation.

### RM-FEA-10 -- unresolved geometry reference

A load or restraint naming deleted or invalid geometry. The solve fails
explicitly. No nearest-face rebinding.

---
# P17-QUAL-001

## Full P17 Qualification

* [ ] Freeze the final P17 tree
* [ ] Audit every P17 milestone
* [ ] Verify all TODO items complete
* [ ] Verify all P17 ADRs
* [ ] Audit CAD / material / mesh / solver authority
* [ ] Audit solver identity and currentness
* [ ] Audit the Tet4 formulation
* [ ] Audit the DOF model
* [ ] Audit loads
* [ ] Audit restraints
* [ ] Audit assembly
* [ ] Audit the solver
* [ ] Audit displacement recovery
* [ ] Audit strain / stress recovery
* [ ] Audit reactions
* [ ] Audit the validation policy
* [ ] Audit stale-input protection
* [ ] Audit visualisation
* [ ] Audit commands
* [ ] Audit persistence
* [ ] Audit the CLI
* [ ] Re-run all reference models
* [ ] Independent analytical validation
* [ ] Force equilibrium PASS
* [ ] Moment equilibrium PASS where relevant
* [ ] Patch test PASS
* [ ] Axial test PASS
* [ ] Cantilever convergence PASS
* [ ] Under-constrained failure PASS
* [ ] Stale-mesh failure PASS
* [ ] Clean Debug qualification
* [ ] Clean Release qualification
* [ ] Clean Debug-shared qualification
* [ ] Repeated determinism qualification
* [ ] Cross-preset equivalence
* [ ] Final adversarial review
* [ ] 0 unexpected warnings
* [ ] Qualified tree == committed tree
* [ ] Evidence recorded
* [ ] Mark P17 qualified

### P17 Final Gate

```text
all P17 milestones PASS

+ CAD geometry remains authority
+ P15 material remains canonical
+ P16 mesh remains derived
+ P17 results remain derived

+ stale geometry rejection PASS
+ stale mesh rejection PASS

+ DOF model PASS
+ Tet4 element formulation PASS
+ stiffness symmetry PASS
+ rigid-body-mode behaviour PASS

+ loads PASS
+ restraints PASS
+ geometry-reference mapping PASS

+ global assembly PASS
+ linear solver PASS
+ residual validation PASS

+ displacement PASS
+ strain PASS
+ stress PASS
+ von Mises PASS

+ reaction-force recovery PASS
+ static equilibrium PASS

+ mesh-quality acceptance policy explicit

+ visual inspection PASS

+ undo/redo PASS
+ generated solver arrays excluded from history

+ persistence PASS
+ derived solver arrays excluded as authority

+ CLI PASS
+ CLI/core equivalence PASS

+ reference models PASS
+ independent analytical validation PASS
+ patch test PASS

+ Debug PASS
+ Release PASS
+ Debug-shared PASS

+ determinism PASS
+ cross-preset equivalence PASS
+ adversarial review PASS
+ 0 unexpected warnings

+ qualified tree == committed tree
```

### P17 Final Adversarial Questions

At minimum, attack:

```text
Can P17 solve a stale mesh?
Can P17 solve geometry that has failed regeneration?
Can a material edit leave an old result current?
Can a load edit leave an old result current?
Can a restraint edit leave an old result current?
Can NodeId become permanent CAD load identity?
Can ElementId survive a remesh as solver intent?
Can a load silently move to another face after regeneration?
Can an unresolved support bind to a nearby face?
Can an inverted Tet enter stiffness assembly?
Can a zero-volume Tet produce finite-looking stiffness?
Can engineering and tensor shear conventions get mixed?
Can B or D ordering disagree with post-processing?
Can global K lose symmetry during assembly?
Can duplicate sparse entries overwrite instead of accumulate?
Can an under-constrained model appear solved?
Can the solver return success with a large residual?
Can reactions fail force equilibrium?
Can pressure conversion fail to preserve the resultant?
Can a rigid transform change stress invariants?
Can displacement scaling mutate actual result data?
Can the GUI recalculate stresses differently from core?
Can undo restore a stiffness matrix instead of analysis intent?
Can save/load trust old displacement / stress arrays?
Can the CLI implement separate Tet stiffness logic?
Can the CLI return zero on a singular solve?
Can mesh-quality thresholds differ between GUI and CLI?
Can stale binaries produce qualification evidence?
Can a zero-test filter pass qualification?
Can source change after the final qualification?
```

For every credible defect:

```text
reproduce -> regression test -> root cause -> general fix
-> requalify the affected milestone -> rerun P17-QUAL
```

---

# P17 Carried Constraints

Inherited on entering P17, and not P17's to lift without an explicit scope
decision:

```text
1. P16's VolumeMesh currentness must be re-checked at the solver entry point.
   Never trust a cached mesh because a caller handed it over.

2. Configuration-sensitive geometry currently REFUSES a stale configuration
   state. P17 respects the same refusal until configuration regeneration is
   fixed. (Carried defect 2.)

3. P16 quality thresholds are report-only. P17-VALID-001 owns the structural
   mesh-acceptance policy.

4. P16 REFUSES a body with more than one solid. Initial P17 therefore stays
   single-solid unless a prerequisite architecture change is authorized.

5. A drilled-hole feature provides no stable hole-wall reference. Do not fake
   load or restraint targeting for unsupported topology.

6. This MinGW toolchain has no ASan/UBSan. Record the limitation; do not
   pretend sanitizer qualification exists.

7. A clean checkout cannot re-fetch dependencies here -- CMake's bundled curl
   carries no CA trust anchors -- so clean-tree checks configure from the same
   SHA256-verified local sources. The source tree is verified; the download
   path is not.

8. The project has no licence. A solver dependency's licence is an owner
   decision, not an implementation detail. (P17-SOLVE-001.)
```

---

# Future Phases

```text
P18  Thermal Analysis
P19  CFD Integration
P20  Design Optimization
P21  Semantic Topology
P22  Versioning / Collaboration
P23  Python / Automation
P24  AI Engineering Agent
P25  Manufacturing / CAM
P26  Performance / GPU / Scale
P27  Production Hardening
P28  BetterCAD 1.0
```

---

# Workflow

```text
UNDERSTAND
→ ARCHITECT
→ BLAST RADIUS
→ IMPLEMENT
→ TARGETED TESTS
→ INDEPENDENT VALIDATION
→ FAILURE PATHS
→ PERSISTENCE
→ DETERMINISM
→ ADVERSARIAL REVIEW
→ FULL REGRESSION
→ EVIDENCE
→ [x]
→ COMMIT
→ PUSH
```

If a gate fails:

```text
STOP
→ reproduce
→ regression test
→ root cause
→ fix
→ revalidate
```

Never mark work complete because it merely compiles.

---

# Project Authority

```text
TODO.md
→ current authorized work

ROADMAP.md
→ long-term direction + completed phases

ARCHITECTURE.md
→ architecture / invariants

CLAUDE.md
→ engineering process / Definition of Done

docs/architecture/decisions/
→ durable ADRs

docs/verification/<milestone>/
→ qualification evidence

docs/engineering/
→ reusable engineering templates
```

---

# CURRENT NEXT STEP

```text
P17-ARCH-001 — Structural FEA Architecture.
AUTHORIZED. P17 is the current phase.
```

P16 left BetterCAD able to generate, inspect, persist and headlessly drive a
validated Tet4 volume mesh from authoritative CAD geometry, with sizing
controls, quality metrics, geometry/mesh correspondence, boundary regions,
undo/redo, a CLI and eight independently validated reference models. That is
what P17 consumes. Evidence:
[docs/verification/P16-QUAL-001/](docs/verification/P16-QUAL-001/README.md).

## What is authorized, and what is not

```text
P17-ARCH-001     AUTHORIZED. Start here. Architecture only: decide the
                 module and its layer, the authority boundaries, the
                 validation boundary, and what P17 does not do. Write the
                 ADRs. Build no solver.

everything after it
                 NOT AUTHORIZED YET. Each milestone is authorized when
                 the one before it has passed its gates and recorded its
                 evidence, and this file says so. Being next in the
                 sequence is not permission.
```

Read `# P17 Prerequisites Already In The Tree` before starting. Three things
are already settled and one is a decision waiting: P15 names
`ConsumerKind::FeaLinearStatic` and `FeaLinearStaticWithGravity`; Eigen 5.0.1
is already a pinned dependency, private to `bettercad_sketch`; P16's handover
and its four refusals are fixed. The decision: a module that USES meshing
cannot share meshing's layer, and the layering table has no integer between
`meshing` 4 and `io` 5.

## Still owed, from earlier reviews

```text
F6 (P16-ARCH)   DISCHARGED by P16-QUAL-001: a MeshControl whose body is
                deleted reports ineligibility object_not_found, refuses
                generation, and keeps the mesh it already held marked stale.
                One test proves the sequence.
F6 (P16-GEOM)   partMassProperties has no currency check, so mass properties can
                be computed from stale geometry after an unregenerated edit. A
                P15 behaviour change, so a scope decision
cross-preset    DISCHARGED by P16-REFMOD-001: the CLI reference fixtures run
determinism     in a fresh process in every preset and assert exact element
                counts and SI volume doubles, so a mesh IS now compared across
                Debug, Release and Debug-shared.
no sanitizers   this MinGW ships no libasan/libubsan, so neither Netgen nor
                BetterCAD has ASan/UBSan coverage. Worth a different toolchain
                before a volume mesh is trusted numerically. (INFRA-NETGEN-001)
deps/ unfingerprinted
                the qualification fingerprints 8 paths, and deps/ is not one of
                them, so a changed Netgen patch would not void a qualification.
                Pre-existing in kind -- a differently-rebuilt dependency prefix
                was never detectable either. (INFRA-NETGEN-001)
clean-checkout  a clean checkout cannot re-fetch Catch2 or nlohmann_json,
fetch           because CMake's bundled curl carries no CA trust anchors in
                this environment. Verified instead with
                FETCHCONTENT_SOURCE_DIR_* pointing at the same SHA256-verified
                archives. (P16-REFMOD-001, P16-QUAL-001)
stale cache     nothing FORCES a holder to call isStale. A VolumeMesh records
                its source and revision and answers truthfully, and the request
                path refuses stale geometry, so this bites only code that
                CACHES a mesh. P17's solver entry point should take the
                document and feature, or re-check. (P16-VOL-001)
concurrency     calls into the volume backend are serialised by a mutex because
                nglib keeps global state; no test runs two threads through it.
                (P16-VOL-001)
multiple solids a body with more than one solid is REFUSED, explicitly and
                tested. ADR-032's one-region-per-solid remains the eventual
                design. (P16-VOL-001)
sphere          no fixture, so a degenerate pole edge is the one geometry where
                exact-coordinate node unification is untested
boundary edges  protected by exactly one test, because closed solids cannot
                detect the counter's removal
quality         P16-QUALITY-001's policy is deliberately report-only and
thresholds      ships no accept/reject thresholds. The phase that consumes a
                mesh owns them. (P16-QUALITY-001, P16-QUAL-001)
drilled hole    a hole cut by a feature has no stable face reference, so a
wall            boundary set cannot name it. A tube's bore can. (P16-MAP-001)
RM-MESH-09      the configuration reference case is DEFERRED, not failed: a
                configuration override is a refusal by design, so the case
                would assert a refusal rather than a mesh. It belongs with
                carried defect 2. (P16-REFMOD-001)
```

## Carried defects, neither P16's

```text
the FileIo replace defect     a document save can still lose to a file
                              synchroniser. Arguably ahead of any new milestone.

configuration regeneration    a configuration override does not rebuild the
                              geometry it changes. Mass properties and meshing
                              both REFUSE under one, with the same guard.
```

Also carried from earlier phases: hole POSITION dimensions are unsupported, GD&T symbols are not
fully embedded in PDF/DXF, and cross-preset export byte identity is not guaranteed for drawings.
