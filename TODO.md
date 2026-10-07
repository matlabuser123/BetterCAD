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
           None. P17-LOAD-001 is PASS (2026-10-08). Finishing a milestone is
           a stop condition: P17-BC-001 is next in the sequence and is NOT
           authorized until this file says so.

Qualified:
           P0–P10 — BetterCAD v0.1.0
           P11 — Advanced Part Modelling
           P12 — Production Part Modelling
           P13 — Assemblies
           P14 — Technical Drawings
           P15 — Materials / Engineering Data
           P16 — Meshing

Next:
           P17-BC-001 — Structural Restraints. NOT AUTHORIZED: P17-LOAD-001
           passing is not permission to start it. Authorizing it is a scope
           decision.

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

* [x] Define P17 solver scope
* [x] Define linear-static assumption
* [x] Define small-strain assumption
* [x] Define isotropic linear-elastic material assumption
* [x] Define Tet4 as the initial structural element
* [x] Define CAD / mesh / solver authority boundaries
* [x] Define analysis-definition ownership
* [x] Define derived-result ownership
* [x] Define the geometry-currentness requirement
* [x] Define the mesh-currentness requirement
* [x] Define the material-currentness requirement
* [x] Define the solver-input validation boundary
* [x] Define P16 mapping consumption
* [x] Define P15 material consumption
* [x] Decide the module's layer and update `CheckLayering.cmake` in the same
      change (see P17 Prerequisites: there is no integer between meshing and io)
* [x] Define unsupported physics explicitly
* [x] ADR(s) completed
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

**COMPLETE 2026-10-07.** 20/20. Evidence:
[docs/verification/P17-ARCH-001/](docs/verification/P17-ARCH-001/README.md).

Three presets clean-rebuilt, 3401/3401 tests passed in each with 0 warnings
over 598 objects, 579-test blast radius x5 repeats in release and debug,
qualified tree == committed tree at `536e14b5`.

**The audit was the substance, and it found that three things P17 was asked to
design already existed, built by the earlier phases for this consumer by
name.** `requireLinearElasticConstants` is documented "the boundary a
structural solver consumes (P17)" and already refuses a non-finite or
non-positive E and a Poisson ratio outside -1 < nu < 0.5; `ConsumerKind` already
splits `FeaLinearStatic` from `FeaLinearStaticWithGravity`; and ADR-028 already
forbade a solver holding material data, naming P17. So `P17-MAT-001`'s
validation is satisfied by consumption, not implementation. See
[DEPENDENCY_AUDIT.md](docs/verification/P17-ARCH-001/DEPENDENCY_AUDIT.md).

**What nothing upstream could provide is a boundary that cannot be bypassed.**
P16's recorded gap is about CALLERS -- `Mesher::mesh()` returns a stale mesh
deliberately, because P16-VIZ-001 inspects stale meshes -- so no function in
`structural` takes a `VolumeMesh`. `requireStructuralModel(document,
regenerator, mesher, control)` is the only entry, and `StructuralModel` has one
private constructor with one friend, so a function that wanted to skip the
checks could not construct its argument (ADR-036).

**The layer table was respaced in tens** (ADR-035). A structural module USES
meshing, so it cannot share meshing's layer -- the rule is strictly-lower -- and
there was no integer between `meshing` 4 and `io` 5. That is the third time:
ADR-006 and ADR-015 each moved `io` up, and four roadmap phases sit in the same
band. Relative order is unchanged and the checker gives the same verdict on the
same tree. Rule 6 was added in the same change: the library must not include
anything under `apps/`, which rules 2 and 3 could not express.


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

* [x] Define `AnalysisId`
* [x] Define `LoadCaseId` if required
* [x] Define `LoadId`
* [x] Define `RestraintId`
* [x] Define solver / result identity
* [x] Define DOF identity
* [x] Define nodal displacement representation
* [x] Define reaction-force representation
* [x] Define element strain representation
* [x] Define element stress representation
* [x] Define units for every field
* [x] Define result-currentness semantics
* [x] Define result invalidation dependencies
* [x] Define the analysis state machine
* [x] Prevent mesh IDs becoming permanent CAD identity
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

**COMPLETE 2026-10-07.** 19/19. Evidence:
[docs/verification/P17-DATA-001/](docs/verification/P17-DATA-001/README.md).

Three presets clean-rebuilt, 3434/3434 tests passed in each with 0 warnings
over 603 objects, 715-test blast radius x5 repeats in release and debug,
qualified tree == committed tree at `fcf2ea16`.

**Three identity domains, made true in the type system.** `AnalysisId`,
`LoadId` and `RestraintId` are document identities in `core/Id.hpp`; `NodeId`
and `ElementId` stay P16's mesh-local handles; `DofIndex` is solver-local and
deliberately NOT in `core/Id.hpp`, because a restraint that stored "DOF 1042"
would constrain unrelated material after a remesh. Only `AnalysisId` widens to
`ObjectId`, because only the analysis is a document object -- `LoadId` and
`RestraintId` follow `BoundarySetId`, which is the member-identity precedent
already in the tree. Twelve compile-fail cases enforce it.

**`LoadCaseId` is NOT REQUIRED**, decided rather than skipped: one analysis IS
one load case in the authorized scope, so the type would have been a named
identity nothing allocates and nothing resolves. `grep -rn LoadCaseId` is 0.

**`Translation3D` already existed** and is the nodal displacement; only
`Force3D` was new, and it went to `core/math/Vector.hpp` beside its sibling.
The tensor component order is frozen `XX YY ZZ XY YZ ZX` -- deliberately NOT
`InertiaTensor`'s order, which is said out loud -- and the engineering shear
convention is carried by the FIELD NAMES (`gammaXy`), so the rename that would
reintroduce the ambiguity stops the test suite compiling.

**Undo semantics were audited, not invented**: P16's own tests measure two
different rules, and P17 inherits both rather than defining a third. See
[CURRENTNESS_MODEL.md](docs/verification/P17-DATA-001/CURRENTNESS_MODEL.md).


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

* [x] Resolve the structural material for the active body
* [x] Validate E > 0
* [x] Validate -1 < nu < 0.5
* [x] Reject NaN / infinity
* [x] Unit-safe material inputs
* [x] Define missing-material behaviour
* [x] Define custom-material behaviour
* [x] Define provenance behaviour
* [x] Define material-change result invalidation
* [x] Verify a material change does NOT require a remesh
* [x] Verify a material change DOES invalidate the structural result
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

**COMPLETE 2026-10-07.** 14/14. Evidence:
[docs/verification/P17-MAT-001/](docs/verification/P17-MAT-001/README.md).

Three presets clean-rebuilt, 3453/3453 tests passed in each with 0 warnings
over 605 objects, 734-test blast radius x5 repeats in release and debug,
qualified tree == committed tree at `4d4698b3`.

**The audit changed what this milestone could honestly claim.** The checklist
asks P17 to validate E > 0, validate -1 < nu < 0.5 and reject NaN and infinity.
P15 already does all of it AT THE POINT OF ENTRY -- `createMaterial` and
`setMaterialMechanical` both refuse an unusable value, and a refused edit leaves
the previous one intact -- so such a value cannot be in a document at all. That
was found by eleven failing tests, not by reading: the first draft asserted the
resolver refused them and every case failed at `createMaterial`. The boundary
tests now assert the guarantee that EXISTS, and the delegation is proved through
the reachable half of P15's contract, a required property that is absent.

**What the milestone adds is the three things P15 cannot know**:
`StructuralAnalysisMode` (intent, so it is a field of the analysis definition --
a density on the material does not make a problem a self-weight problem),
`resolveStructuralMaterial` giving one solver-ready view per mode, and the
integration proof. The density requirement is READ from P15's
`requiredProperties` rather than hardcoded, which is the one line that makes the
two impossible to disagree.

**The hard requirement, measured.** An E edit of 210 -> 190 GPa leaves
`Mesher::currency` Current, the MeshStamp unchanged, the geometry revision
unchanged and the whole MeshControlDefinition equal by value, while
`staleReasons` reports exactly `{ Material }`. And the same MaterialId with a
changed modulus is a different solver input: revision 1 -> 2, stale, which is
why the source stamp carries a revision and not just an id.

Invalidation is dynamic, never imperative: no `markMeshStale` or
`invalidateResult` hook exists anywhere in the tree.


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

* [x] Define deterministic DOF numbering
* [x] Map `NodeId` to its three DOFs
* [x] Define constrained and free DOFs
* [x] Define DOF ordering
* [x] Reject missing nodes
* [x] Reject duplicate DOFs
* [x] Define active / free equation numbering
* [x] Determinism PASS
* [x] Scale to the reference meshes
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

**COMPLETE 2026-10-07.** 12/12. Evidence:
[docs/verification/P17-DOF-001/](docs/verification/P17-DOF-001/README.md).

Three presets clean-rebuilt, 3493/3493 tests passed in each with 0 warnings
over 609 objects, 765-test blast radius x5 repeats in release and debug,
10 mutation probes all killed, qualified tree == committed tree at `9301b643`.

**The node ordinal was ADOPTED, not invented.** P16 documents that node handles
may be sparse and that `Mesh` enumerates in ascending `NodeId`; P17-DATA-001's
`StructuralResult` had already keyed its displacement array "parallel to
mesh.nodes()". So `dof = 3 * nodeId.value() + component` is forbidden -- a mesh
whose nodes are 3, 1000 and 9000000 has NINE degrees of freedom and that formula
would build 27000003 rows -- and the ordinal is the mesh's own enumeration
index, which is also the one a result already uses. A test compares the two
through the result's OWN lookup, so a future divergence fails the suite instead
of writing node i's answer into node j's slot.

**The numbering is interleaved per node and that is now permanent**
(`3k+1, 3k+2, 3k+3`), because an element's stiffness couples the DOFs of its own
four nodes. Asserted WITH its negation: the second node's Ux is asserted to be 4
and asserted NOT to be 2, since the two conventions agree at ordinal 0 and a
formula check there would pass under either. `ADR-037` records the four
decisions and the eight rejected alternatives.

**One production defect, found by reading rather than by a failing test.** A
`MeshStamp` identifies the BUILDER, not the snapshot: `MeshBuilder` sets it in
its constructor and `build()` is a snapshot, so two snapshots share a stamp and
may differ in size. A stamp-only binding check accepted a constraint set from
the larger and then numbered every DOF as free -- a restrained model assembled
as an unrestrained one. Fixed with a range check and a node-count check, both
confirmed load-bearing by mutation.

**Three types, not one**: `MeshDofMap` (which DOFs exist), `ConstraintSet`
(which are prescribed, carrying no VALUE), `FreeEquationMap` (which are
unknown, and their row). Each has one friend, so possession is the evidence.
And a row of the reduced system is a DIFFERENT TYPE from a degree of freedom --
`FreeEquationIndex`, zero-based with no invalid value -- because the two agree
for an unrestrained model, so a test on a free-floating body would not catch
the confusion.

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

* [x] Derive the Tet4 shape functions
* [x] Derive the Jacobian
* [x] Derive the B matrix
* [x] Define the isotropic D matrix
* [x] Implement the element stiffness matrix
* [x] Verify Ke is symmetric
* [x] Verify the rigid-body modes
* [x] Verify the unconstrained Ke is positive semidefinite
* [x] Verify element volume handling
* [x] Reject an inverted Tet
* [x] Reject a degenerate Tet
* [x] Validate coordinate-scale behaviour
* [x] Analytical single-Tet test
* [x] Independent reference implementation / test
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

**COMPLETE 2026-10-08.** 18/18. Evidence:
[docs/verification/P17-ELEM-001/](docs/verification/P17-ELEM-001/README.md).

Three presets clean-rebuilt, 3522/3522 tests passed in each with 0 warnings
over 612 objects, 797-test blast radius x5 repeats in release and debug,
13 mutation probes with 11 killed by tests, qualified tree == committed tree
at `845b118c`.

**The derivation was written BEFORE the implementation**, and the tests check
the code against it rather than the other way round:
[TET4_DERIVATION.md](docs/verification/P17-ELEM-001/TET4_DERIVATION.md) fixes
the reference element, the Jacobian convention (columns are the edge vectors),
`grad_x N = J^-T grad_xi N`, `V = det J / 6` with no absolute value, the B
block, the local DOF order and `Ke = V B^T D B`.

**P16's degeneracy criterion has NO TOLERANCE, and that answered the brief
differently than it expected.** `MeshValidation.hpp` refuses a volume that is
exactly zero, deliberately -- "a thin tetrahedron is data-valid and is
P16-QUALITY-001's to complain about" -- so there is no scale-aware policy to
reuse and inventing one here would refuse elements a qualified mesh publishes.
A 1000:1 thin Tet is ACCEPTED, with a test saying so. `invalid` is not `poor
quality`.

**`mu` already existed.** P15's `LinearElasticConstants::shearModulus` IS
`E/(2(1+nu))`, so production carries P15's value instead of recomputing it --
the fourth milestone running in which the audit found the quantity already
built. `lambda` comes from the frozen formula and is cross-checked against
P15's OTHER derived constant through `lambda = K - 2 mu / 3`.

**One production defect, found by my own test.** The derivation claimed `det J`
was "character-for-character" `meshing::signedVolume`; the bit-identity
assertion failed ONE ULP apart on a skew Tet, because a first-row cofactor
expansion is the same algebra and not the same floating-point operations. Fixed
by matching P16's association rather than relaxing the test: the two are
predicates on the SAME boundary, so a Tet whose volume rounds near zero must
not be data-valid for the validator and inverted for the solver. Now exactly
zero disagreement on every element of three reference meshes.

**No Eigen in production**, and that is a scope decision: no public header in
the repository includes it, so `Ke`, `B` and `D` could not be Eigen types
anyway, and `src/structural/CMakeLists.txt` records that admitting it here is
P17-SOLVE-001's decision with an open licence question. The TESTS use it for
eigenvalues, SVD and an independent reference that inverts a 4x4 coordinate
matrix -- so it never forms a Jacobian and cannot mirror a transposition.

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

* [x] Define the load schema
* [x] Define force units
* [x] `GeometryReference`-based face loads
* [x] Nodal-force policy explicit
* [x] Surface traction
* [x] Pressure
* [x] Direction convention explicit
* [x] Pressure-normal convention explicit
* [x] Convert mapped facets to equivalent nodal forces
* [x] Preserve the resultant force
* [x] Preserve the resultant moment where mathematically required
* [x] Gravity / body-force decision explicit
* [x] Reject an unresolved load reference
* [x] Load-change result invalidation
* [x] Deterministic load vector
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

**COMPLETE 2026-10-08.** 18/18. Evidence:
[docs/verification/P17-LOAD-001/](docs/verification/P17-LOAD-001/README.md).

Three presets clean-rebuilt, 3545/3545 tests passed in each with 0 warnings
over 615 objects, 820-test blast radius x5 repeats in release and debug,
13 mutation probes with 12 killed, qualified tree == committed tree at
`27afe8d3`.

**THREE of the brief's requirements name states that DO NOT EXIST**, and
establishing that was most of the design work. `MappingState` has exactly
`Resolved` and `Unresolved`: P16's header says a `FaceName` "can be ambiguous
... and that is exactly why this layer does not map through one", and
`Unsupported` is absent because "attribution needs no surface kind at all". And
a stale-mapping check here is unreachable, because `requireStructuralModel`
already refuses a stale mesh and proves the map came from the same lookup
(ADR-036). So `LoadProblem` has EIGHT values rather than the eleven sketched,
and the three omissions are recorded with P16's own words instead of shipped as
placeholders.

**A facet handle is never load authority, and it is a compile-time
assertion.** Mirror structs fix the permitted members of the two face loads, so
an added field changes the `sizeof`; neither is constructible from an
`ElementId`; and the canonical header contains no `ElementId` outside comments
and exactly one `NodeId` -- `NodalForceLoad::node`, which is MESH-LOCAL by
declaration and carries its `MeshStamp`.

**Load-change invalidation needed NO new mechanism.** The loads went into
`StructuralAnalysisDefinition`, where P17-DATA-001 left `// Loads --
P17-LOAD-001` and explained why: "an edit to any of them moves the owning
object's revision ... Three independent counters would give three chances to
forget one." That broke the two assertions P17-MAT-001 PREDICTED it would
break, and whose comment named its own replacement.

**Pressure follows the current normal; a traction does not.** `t = -p n_out`,
so the block's two opposite caps under ONE positive scalar give opposite
forces -- which a global-direction pressure cannot produce. On RM-MESH-06 the
pressure resultant rotates as `R F` and the traction resultant does not, and
the test says which is which rather than requiring either of the other.

**Gravity is IMPLEMENTED**, because P17-MAT-001 had already made the density
available for this consumer by name; the only thing missing was the integral.
The load carries an acceleration and no density (ADR-028), and a missing
density is refused -- there is no 7850 anywhere in the module.

**The moment conservation gate earned itself.** Mutation probe M2 puts the
whole facet force on one corner, which preserves the total force EXACTLY and
is invisible to every force assertion in the suite. It is caught only through
the first moment.

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
P17-LOAD-001 is PASS. The next step is a scope decision, not an
implementation.
```

P17-ARCH-001 gave the structural phase a boundary a stale mesh cannot cross, a
module whose dependencies are enforced rather than described, and three ADRs
fixing the solver's scope. P17-DATA-001 then fixed the identity model -- three
domains that do not convert into one another -- the result representation, and
the one place that decides whether a result still describes the model. Neither
built a solver, which was the point of both.
Evidence: [docs/verification/P17-ARCH-001/](docs/verification/P17-ARCH-001/README.md),
[docs/verification/P17-DATA-001/](docs/verification/P17-DATA-001/README.md).

What it consumes is P16: a validated Tet4 volume mesh from authoritative CAD
geometry, with sizing controls, quality metrics, geometry/mesh correspondence,
boundary regions, undo/redo, a headless CLI and eight independently validated
reference models.
[docs/verification/P16-QUAL-001/](docs/verification/P16-QUAL-001/README.md).

## What is authorized, and what is not

```text
P17-ARCH-001     PASS 2026-10-07. Qualified at 536e14b5, 3401/3401 x 3.
P17-DATA-001     PASS 2026-10-07. Qualified at fcf2ea16, 3434/3434 x 3,
                 0 warnings over 603 objects. Evidence recorded.

P17-MAT-001      PASS 2026-10-07. Qualified at 4d4698b3, 3453/3453 x 3,
                 0 warnings over 605 objects. Evidence recorded.

P17-DOF-001      PASS 2026-10-07. Qualified at 9301b643, 3493/3493 x 3,
                 0 warnings over 609 objects, 10 mutation probes killed.
                 Evidence recorded, plus ADR-037.

P17-ELEM-001     PASS 2026-10-08. Qualified at 845b118c, 3522/3522 x 3,
                 0 warnings over 612 objects, 13 mutation probes with 11
                 killed by tests. Evidence recorded.

P17-LOAD-001     PASS 2026-10-08. Qualified at 27afe8d3, 3545/3545 x 3,
                 0 warnings over 615 objects, 13 mutation probes with 12
                 killed. Evidence recorded.

P17-BC-001       NOT AUTHORIZED. Being next in the sequence is not
                 permission, and P17-LOAD-001 passing is not either.
                 Authorizing it is a scope decision.

everything after it
                 likewise.
```

### What P17-ARCH-001 leaves in place for the next milestone

```text
the input boundary      structural::requireStructuralModel is the single
                        entry, and the gates P17-DATA/LOAD/BC/VALID define
                        attach to it rather than to each new consumer
the module              src/structural/, layer 50, linking core, features and
                        meshing only. No io, no renderer, no Qt, no Eigen
the enforcement         5 layering fixtures, including the meshing ->
                        structural CYCLE, and rule 6 (library -> apps/)
the identity model      AnalysisId / LoadId / RestraintId are document
                        identities; NodeId / ElementId stay mesh-local;
                        DofIndex is solver-local and NOT in core/Id.hpp.
                        12 compile-fail cases enforce it
the result model        Translation3D / Force3D / Strain6 / Stress6, the
                        component order XX YY ZZ XY YZ ZX frozen, and
                        engineering shear carried by the FIELD NAMES
the currentness answer  currentResultSource / resultCurrency / analysisState,
                        which call requireStructuralModel and so inherit every
                        ADR-036 gate
what is still undecided  the DOF numbering, the sparse representation, the
                        solver and its LICENCE, the acceptance thresholds,
                        whether gravity is in scope, and the per-body material
                        story. The Voigt ordering is now DECIDED (P17-DATA-001)
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
