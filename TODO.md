# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.  
> Qualification milestones require the final qualified tree to match the committed tree.  
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

# Status

```text
Current:
           P18 — Thermal Analysis. AUTHORIZED 2026-10-16 by an explicit
           scope decision ("start P18"), with the phase plan below.

Prerequisite:
           P17 — Structural FEA — QUALIFIED (P17-QUAL-001, 2026-10-15):
           seventeen milestones PASS, 4009/4009 x 3 presets, qualified tree
           == committed tree. Three measured findings stay with the owner,
           unchanged: F1 (the 1e-12 moment-equilibrium threshold crossed by
           a correct bending solve), F2 (linear Tet4 locks in bending), F3
           (OCCT never subdivides a developable wall along its ruling).

Current milestone:
           none. P18-CMD-001 — Thermal Commands / Undo-Redo. COMPLETE at 22
           of 22 and qualified 2026-10-24 against 6b675ec plus the working
           tree: 5432/5432 x 3 presets, 2182 x 2 in two, 0 warnings over 725
           objects, 17 stages exit 0, 6h35m56s, fingerprints at the freeze ==
           before == after, third attempt (the first stopped after Smart App
           Control blocked the unsigned bettercad.exe, the second after a test
           failed to link on the shared-library preset; see the evidence). Ten
           undoable commands that edit CANONICAL thermal intent on the existing
           command framework: create an analysis; add, edit and remove a load;
           add, edit and remove a boundary condition; set the initial
           temperature, the solver settings and the transient settings. Undo and
           redo assign a stored definition, so nothing is inverted and nothing
           drifts; a failed command changes no field, no revision and no stack;
           the GeometryReference (a FaceName) is stored as given and never
           resolved; the history holds no node, element, DOF index or solver
           array. Edits invalidate exactly the derived layers whose recorded
           source contains what they changed, and never the mesh. TWO currentness
           regimes were found and left as they are: derived objects compare
           their recorded intent by value (an undo makes an old result current
           again), the validation report compares the monotonic analysis
           revision (it stays stale). Nothing qualified changed.
           P18-ARCH-001 PASSED 2026-10-16; P18-DATA-001, P18-MAT-001,
           P18-DOF-001 PASSED 2026-10-17; P18-ELEM-001, P18-LOAD-001 PASSED
           2026-10-18; P18-BC-001, P18-ASSEMBLY-001 PASSED 2026-10-19/20;
           P18-SOLVE-001 PASSED 2026-10-20; P18-TRANSIENT-001, P18-POST-001
           PASSED 2026-10-21; P18-BALANCE-001 PASSED 2026-10-22; P18-VALID-001,
           P18-VIZ-001 PASSED 2026-10-23; P18-CMD-001 PASSED 2026-10-24
           (docs/verification/P18-*/).
Qualified:
           P0–P10 — BetterCAD v0.1.0
           P11 — Advanced Part Modelling
           P12 — Production Part Modelling
           P13 — Assemblies
           P14 — Technical Drawings
           P15 — Materials / Engineering Data
           P16 — Meshing
           P17 — Structural FEA

Next:
           P18-PERSIST-001 — Thermal Persistence / Round-Trip. NOT AUTHORIZED:
           P18-CMD-001 passing is not permission to start it; each
           milestone is started by the owner, as every P17 milestone was.

P17 objective, as delivered:
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

P16 and P17 milestones:
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
P17      Structural FEA                       QUALIFIED
```

Detailed completed history belongs in:

```text
ROADMAP.md
docs/verification/
docs/architecture/decisions/
```

Do not expand completed-phase implementation diaries in `TODO.md`.

---

# P17 — Structural FEA — QUALIFIED

`P17-QUAL-001` PASS, 2026-10-15. The seventeen milestones -- `P17-ARCH-001`
through `P17-REFMOD-001` and this gate -- are recorded in `ROADMAP.md`
("Structural FEA — Qualified") with their evidence links, as this file's own
rule requires. The phase diary that stood here, every milestone's checklist
and PASS record, is in git history at `a99f0b6` and in each milestone's
`docs/verification/<milestone>/` directory; it is not repeated.

```text
P17 objective, delivered:
    authoritative CAD / material / mesh state
    → structural analysis definition
    → linear-elastic FE assembly
    → boundary conditions / loads
    → linear solve
    → displacements / reactions
    → strain / stress recovery
    → structural validation
    → inspectable, persistent, headless result

P17 did NOT implement, by decision (ADR-034), and refuses rather than
approximates: nonlinear material behaviour, plasticity, large deformation,
contact, buckling, fatigue, fracture, composites, dynamics or modal
analysis, transient structural analysis, topology optimisation, adaptive
remeshing, shell or beam elements, multiple-solid contact assemblies.
```

---
# P17-QUAL-001 — PASS

## Full P17 Qualification

* [x] Freeze the final P17 tree
* [x] Audit every P17 milestone
* [x] Verify all TODO items complete
* [x] Verify all P17 ADRs
* [x] Audit CAD / material / mesh / solver authority
* [x] Audit solver identity and currentness
* [x] Audit the Tet4 formulation
* [x] Audit the DOF model
* [x] Audit loads
* [x] Audit restraints
* [x] Audit assembly
* [x] Audit the solver
* [x] Audit displacement recovery
* [x] Audit strain / stress recovery
* [x] Audit reactions
* [x] Audit the validation policy
* [x] Audit stale-input protection
* [x] Audit visualisation
* [x] Audit commands
* [x] Audit persistence
* [x] Audit the CLI
* [x] Re-run all reference models
* [x] Independent analytical validation
* [x] Force equilibrium PASS
* [x] Moment equilibrium PASS where relevant
* [x] Patch test PASS
* [x] Axial test PASS
* [x] Cantilever convergence PASS
* [x] Under-constrained failure PASS
* [x] Stale-mesh failure PASS
* [x] Clean Debug qualification
* [x] Clean Release qualification
* [x] Clean Debug-shared qualification
* [x] Repeated determinism qualification
* [x] Cross-preset equivalence
* [x] Final adversarial review
* [x] 0 unexpected warnings
* [x] Qualified tree == committed tree
* [x] Evidence recorded
* [x] Mark P17 qualified

```text
PASS 2026-10-15. Qualified against a99f0b6 plus the working tree, tests
fingerprint 90f93c52, the other seven paths byte-identical to the
P17-REFMOD-001 freeze: 4009/4009 x 3 presets, 1138 x 5 in two, 0 warnings
over 658 objects, 17 stages exit 0, 3h49m57s, fingerprints before == after.
17 of 17 milestones PASS from their own harness verdicts; 206 ADR clauses,
195 implemented, 11 text drifts amended, 0 invariant violations; three
final cross-milestone gates (tests/qualification/P17StructuralQualificationTests.cpp);
7 mutation probes on the production tree, 7 killed; 44 adversarial
questions, 0 production defects, 0 gate-blocking. Evidence:
docs/verification/P17-QUAL-001/.

The "Cantilever convergence PASS" box is ticked as the brief defines it --
a convergence study with strictly increasing meshes, a monotone trend and a
falling error -- and NOT as agreement with beam theory, which linear Tet4
does not reach on these meshes (F2, F3). "Moment equilibrium PASS where
relevant" is ticked for every solved reference model under the production
default, with F1 recorded beside it.
```

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

The thirty adversarial questions the gate named, and fourteen more, are
answered one by one with the attack, the test and the result in
`docs/verification/P17-QUAL-001/ADVERSARIAL_REVIEW.md`.

---

# P18 — Thermal Analysis

Authorized 2026-10-16 by an explicit scope decision ("start P18"), with the
phase plan below supplied by the owner. Milestones are worked top to bottom
and each one is authorized by its predecessor's PASS being recorded here and
the owner saying so -- P17's rule, unchanged.

Objective: qualified 3D solid thermal finite-element analysis using the
existing BetterCAD authority chain:

```text
CAD geometry
→ canonical material
→ derived P16 volume mesh
→ canonical P18 thermal-analysis intent
→ thermal FE assembly
→ solve
→ temperature / gradient / heat-flux recovery
→ heat-balance validation
→ visualisation / CLI / persistence
```

Authority rule:

```text
CAD geometry        = canonical
P15 material        = canonical
P16 mesh            = derived
P18 analysis intent = canonical

thermal DOF maps, K, C, Q, temperature arrays, temperature gradients,
heat-flux arrays, heat-balance reports, render buffers   = derived
```

Initial thermal formulation:

```text
3D solid conduction          linear Tet4            isotropic conductivity
steady-state conduction      transient conduction   prescribed temperature
surface heat flux            volumetric heat generation
convection / Robin boundary condition
```

Initially excluded unless explicitly added later:

```text
radiation                phase change             thermal contact
anisotropic conductivity nonlinear material properties
fluid/CFD coupling       thermo-mechanical coupling
moving heat sources      Joule heating            chemical heat generation
adaptive remeshing
```

---
# P18-ARCH-001

## Thermal Architecture / Scope

* [x] Define initial thermal physics scope
* [x] Define steady-state scope
* [x] Define transient scope
* [x] Define supported thermal loads
* [x] Define supported thermal boundary conditions
* [x] Define deferred thermal physics explicitly
* [x] Define canonical vs derived state
* [x] Define CAD / material / mesh / thermal-result authority
* [x] Define module boundaries
* [x] Define solver orchestration
* [x] Define currentness / stale-state rules
* [x] Define unit conventions
* [x] Define deterministic ordering rules
* [x] Define diagnostics model
* [x] ADRs recorded
* [x] Architecture tests PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-16. Qualified against a3b721b plus the working tree:
4024/4024 x 3 presets, 1177 x 5 in two, 0 warnings over 660 objects,
17 stages exit 0, 3h29m47s, fingerprints before == after. A thermal
module at layer 45 (ADR-050, below structural so the roadmap's coupling
can flow structural -> thermal), the scope ADR-049, and the input
boundary requireThermalModel / ThermalModel (ADR-051) with 11 boundary
cases and 4 checker fixtures; 497 files, 0 layering violations.
Adversarial review: 40 questions, 0 architecture defects, 3 findings
closed or recorded before the freeze. Evidence:
docs/verification/P18-ARCH-001/.
```

Core authority:

```text
CAD → P15 material → P16 mesh → P18 thermal intent → derived thermal
system/results
```

No P18 solver-local identity becomes permanent CAD/document authority.

---
# P18-DATA-001

## Thermal Analysis Data Model

* [x] Define `ThermalAnalysisId`
* [x] Define `ThermalLoadId`
* [x] Define `ThermalBoundaryConditionId`
* [x] Define analysis state
* [x] Define steady solver settings
* [x] Define transient solver settings
* [x] Define initial-condition representation
* [x] Define temperature-result representation
* [x] Define gradient-result representation
* [x] Define heat-flux-result representation
* [x] Define heat-balance-result representation
* [x] Define provenance/source fingerprints
* [x] Define result-currentness semantics
* [x] Define stale-result semantics
* [x] Define failure-result semantics
* [x] Units explicit
* [x] Deterministic ordering
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-17. Qualified against e6fded3 plus the working tree:
4084/4084 x 3 presets, 1213 x 5 in two, 0 warnings over 665 objects,
17 stages exit 0, 3h40m44s, fingerprints at the freeze == before ==
after. ThermalAnalysisId (a document object), ThermalLoadId and
ThermalBoundaryConditionId in core/Id.hpp; TemperatureGradient, HeatFlux,
HeatTransferCoefficient and VolumetricHeatSource in core/units; the
canonical ThermalAnalysis document object with strict per-mode
validation; the immutable ThermalResult bound to its mesh stamp with a
revision-tuple source (body, control, geometry, mesh, material + revision,
analysis + revision, mode); currentness by source identity, never arrays
or sizes -- a remesh with identical NodeIds is stale; six derived states
and ThermalResultSlot, where a failure is never current and never
resurrects a stale result. 36 data cases, 24 compile-fail cases, 25
mutation probes (23 killed, 1 equivalent, 1 recorded), 42 adversarial
questions, 0 production defects. Evidence: docs/verification/P18-DATA-001/.
```

```text
CANONICAL   thermal analysis definition, loads, boundary conditions, initial
            condition, solver settings
DERIVED     thermal DOF map, K, C, Q, T, grad(T), q, balance results,
            visualisation buffers
```

---
# P18-MAT-001

## Thermal Material Resolution

Reuse P15 canonical materials. Required properties: `k` [W/(m K)], `rho`
[kg/m3], `cp` [J/(kg K)].

* [x] Resolve thermal conductivity from P15
* [x] Resolve density
* [x] Resolve specific heat
* [x] Define which properties steady analysis requires
* [x] Define which properties transient analysis requires
* [x] Reject missing conductivity
* [x] Reject missing density for transient analysis
* [x] Reject missing specific heat for transient analysis
* [x] Reject non-finite values
* [x] Require `k > 0`
* [x] Require `rho > 0` where required
* [x] Require `cp > 0` where required
* [x] Preserve material provenance
* [x] Material edit stales thermal result
* [x] Material edit does NOT stale P16 mesh
* [x] No P18 material database
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-17. Qualified against 843469a plus the working tree:
4115/4115 x 3 presets, 1244 x 5 in two, 0 warnings over 667 objects,
17 stages exit 0, 3h51m07s, fingerprints at the freeze == before ==
after. NO P15 SCHEMA CHANGE: k, rho and cp are P15's, validated at entry
by P15, and resolved through P15's own require* functions.
resolveThermalMaterial(document, body, mode) returns a derived,
source-bound ThermalMaterial -- MaterialId, revision, mode, k always, a
rho/cp ThermalCapacity exactly when the mode is transient -- with the
requirement READ from P15's table: steady needs k only, transient needs
k, rho and cp. The result source now resolves the material for the
analysis's mode, so a transient analysis on a k-only material is
InputsUnavailable where a steady one is Ready. A k, rho, cp or E edit
leaves the P16 mesh current (same stamp, same NodeIds) and stales the
result for reason material alone, under whole-material revision as P17.
No default, clamp, abs, epsilon, cache or database. 31 cases, 14
mutation probes (13 killed, 1 refused by the compiler, 0 survived), 38
adversarial questions, 0 production defects. Evidence:
docs/verification/P18-MAT-001/.
```

---
# P18-DOF-001

## Temperature DOF Model

Initial thermal model: 1 scalar temperature DOF per node.

* [x] Define thermal `DofIndex`
* [x] Define deterministic node → thermal DOF mapping
* [x] Define constrained/free thermal equations
* [x] Define temperature constraint map
* [x] Bind DOF map to exact P16 mesh source
* [x] Remesh invalidates old thermal DOF map
* [x] NodeId is not used as raw equation number
* [x] No thermal DOF IDs persisted as canonical intent
* [x] Sparse NodeId tests
* [x] Determinism PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-17. Qualified against d40001c plus the working tree:
4163/4163 x 3 presets, 1277 x 5 in two, 0 warnings over 670 objects,
17 stages exit 0, 3h44m12s, fingerprints at the freeze == before ==
after. ThermalDofIndex is the node's ordinal in the P16 mesh's own
enumeration -- zero-based, no default, no invalid value -- and indexes
P18-DATA-001's nodal temperature array directly; it is a distinct type
from NodeId, ElementId, ThermalFreeEquationIndex and P17's DofIndex (15
compile-fail cases). Sparse handles 7 / 104 / 9001 / 500000 number to
0 / 1 / 2 / 3 with four handles of storage; a handle near 2^32 gives a
two-entry map. ThermalDofMap is bound to the mesh's stamp AND node count:
a remesh with identical counts and NodeIds is refused by stamp, two
snapshots of one builder by count; material, load, solver and BC edits
leave it current and equal to a rebuild. The constraint map stores the
prescribed temperature in kelvin -- 300 K and 350 K survive exactly, a
free unknown has no prescribed value rather than 0 K -- collapses exact
duplicates, refuses conflicts naming both, and refuses NaN, infinity and
sub-zero by the absolute-temperature rule; nothing and everything
prescribed are representable. The free map numbers rows compactly in
ascending unknown order, a constrained unknown has no row, agreesWith
re-derives the complement, and reconstruction rebuilds [310 300 320 330
350]. Element-local unknowns keep the element's node order. No thermal
DOF index is persisted or constructible from the definition. 33 cases
(21 480 assertions), 17 mutation probes (17 killed, 0 survived), 37
adversarial attacks, 2 test-method findings, 0 production defects.
Evidence: docs/verification/P18-DOF-001/.
```

---
# P18-ELEM-001

## Linear Tet4 Thermal Element

```text
steady conduction      Ke = integral (grad N)^T k (grad N) dV
                          = V G^T k G   for constant isotropic k on a Tet4
transient capacity     Ce = integral rho cp N^T N dV
units                  Ke -> W/K      Ce -> J/K
```

* [x] Define local node order
* [x] Define Jacobian convention
* [x] Define signed volume convention
* [x] Reject inverted Tet
* [x] Reject degenerate Tet
* [x] Define shape-function gradients
* [x] Implement conduction matrix
* [x] Implement transient capacity matrix
* [x] Define consistent vs lumped capacity policy
* [x] Check symmetry
* [x] Check positive-semidefinite conduction behaviour
* [x] Check capacity matrix positivity
* [x] Translation invariance
* [x] Rotation invariance for isotropic `k`
* [x] Geometry scaling validation
* [x] Conductivity scaling validation
* [x] Independent analytical oracle
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-18. Qualified against d4800f7 plus the working tree:
4240/4240 x 3 presets, 1337 x 5 in two, 0 warnings over 673 objects,
17 stages exit 0, 4h10m35s, fingerprints at the freeze == before ==
after; second attempt on the one frozen candidate, the first (one
unrelated Windows file-replace flake in a drawing CLI test, 30/30 in
isolation) preserved under qualification/run1-failed/.
Local order [n1,n2,n3,n4] as P16 stores it, never sorted. J has the edge
vectors from corner 1 as columns; det J = e1.(e2 x e3) grouped as
meshing::signedVolume, and V = det J / 6 agrees with it bit for bit; no
absolute value repairs a sign. G is 3x4 (row = axis, column = corner),
grad T = G Te, mapped by J^-T. Ke = V k G^T G [W/K]: symmetric
(max |Kij - Kji| = 0), row sums 2.7e-16, constant null mode, positive
semidefinite with rank 3 / nullity 1, energy identity Te^T Ke Te = k V |G Te|^2
against an independent right-hand side. Capacity policy = CONSISTENT: Ce =
rho cp V/20 [2 1 1 1; 1 2 1 1; 1 1 2 1; 1 1 1 2] [J/K], symmetric positive
definite, rank 4, spectrum rho cp V/4 (once) and rho cp V/20 (three
times), total heat capacity rho cp V, and NOT the one-point centroid
matrix (rank 1, same row sums). Translation, proper rotation, even
permutation covariance and geometry scaling (V s^3, G 1/s, Ke s, Ce s^3)
hold; the six odd permutations and the reflected tetrahedron are refused.
The independent oracle is the [1 x y z] inverse plus hand matrices; it
shares no code with production. Steady Ke needs only k; Ce needs only rho
and cp. DEGENERACY: P16 and P17's exact-zero rule accepted 885 of 2000
exactly-degenerate tetrahedra (rounding noise in the determinant of
non-integer coordinates); the kernel uses |det J| <= 16 eps |e1||e2||e3|,
derived from the rounding bound (analytic 4 eps, measured 1.01 eps). The
same gap in P17's Tet4Element.cpp is recorded, not changed. Results are
bitwise identical across the three presets. 60 cases, 17 compile-fail
cases, 33 mutation probes (33 killed), 44 adversarial attacks, 6 findings.
Evidence: docs/verification/P18-ELEM-001/.
```

---
# P18-LOAD-001

## Thermal Loads

Initial supported loads: surface heat flux `q''` [W/m2] with one frozen
positive-direction convention; volumetric heat generation `q'''` [W/m3], with
`Q_element = q''' V` for constant generation. Convection's source term is
handled with the BC milestone.

* [x] Define heat-flux load
* [x] Define volumetric heat-generation load
* [x] Define `GeometryReference` authority
* [x] Resolve current P16 boundary mapping
* [x] Convert face heat flux to equivalent nodal heat input
* [x] Convert volumetric generation to equivalent nodal heat input
* [x] Preserve total heat rate
* [x] Define sign conventions
* [x] Explicit SI units
* [x] Multiple loads superpose deterministically
* [x] Remesh re-resolves canonical geometry references
* [x] No facet/NodeId persistence
* [x] Unsupported target fails explicitly
* [x] Stale prepared loads rejected
* [x] Analytical resultant validation
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-18. Qualified against f10f5f0 plus the working tree:
4325/4325 x 3 presets, 1403 x 5 in two, 0 warnings over 677 objects, 17
stages exit 0, 4h21m28s, fingerprints at the freeze == before == after; one
run, every stage exit 0. The canonical load was already frozen by P18-DATA-001
(SurfaceHeatFluxLoad {FaceName, HeatFlux}, VolumetricHeatSourceLoad
{VolumetricHeatSource}); positive is heat INTO the body, a negative value
extracts, and nothing takes an absolute value. A load holds no facet, node,
element or unknown: sizeof mirrors, static_asserts and five compile-fail cases.
The GeometryReference is the FaceName; P16's boundaryFacetsOf is the only
resolver, and an unresolved face is a refusal, never the nearest face (a hole
bottom, an unknown object and a malformed selector each refused, with both end
caps present). Surface: Q_i = q'' A / 3 per facet corner with A the unsigned P16
area in m2; volume: Q_i = q''' V / 4 per tetrahedron corner with V the signed
volume of a validated element in m3. A 3 m2 triangle at 12 W/m2 gives 12 W per
corner, 36 W; a unit tetrahedron at 24 W/m3 gives 1 W per corner, 4 W; the
block's end cap carries 0.6 W at 500 W/m2 and its volume 60 W at 2.5e6 W/m3,
the sums equal to q'' sum(A) and q''' sum(V) to a few ulps. Geometry scaling
gives s^2 and s^3, so no millimetre factor exists. Loads superpose by ordinal in
the order given; signed cancellation is exact for surfaces and 0.83 eps for a
volume. The vector is indexed by ThermalDofMap, never by NodeId (ids 7, 104,
9001, 500000 and 4 294 967 294 tested). The prepared loads carry the body, the
mesh stamp and node count, the geometry revision and the loads; a remesh with the
same counts and the same ids is stale, a material, solver or boundary-condition
edit is not. Coarse and fine cylinders genuinely differ (156 against 900 nodes,
140 against 572 boundary facets). One bad load refuses the whole set. Results are
bitwise identical across the three presets. 66 cases, 19 compile-fail cases, 46
mutation probes (45 killed, 1 dead code removed), 44 adversarial attacks, 9
findings. Limits recorded: a hole's cylindrical wall cannot be named (P16 has no
role for it); two failure values are reachable only through the integration
seams. No convection, no prescribed temperature, no assembly, no solve.
Evidence: docs/verification/P18-LOAD-001/.
```

---
# P18-BC-001

## Thermal Boundary Conditions

Initial supported BCs: prescribed temperature; convection / Robin,
`-k grad(T) . n = h (T - T_inf)`, assembled as the qualified equivalent
boundary matrix and source terms.

* [x] Define prescribed-temperature BC
* [x] Define convection BC
* [x] Define ambient temperature
* [x] Define convection coefficient
* [x] Define sign convention
* [x] Map BCs through canonical `GeometryReference`
* [x] Prescribed temperature resolves to current thermal DOFs
* [x] Duplicate constraints handled deterministically
* [x] Conflicting prescribed temperatures rejected
* [x] Convection contributes correct matrix/source terms
* [x] Unresolved reference fails explicitly
* [x] No nearest-face rebinding
* [x] Remesh invalidates derived mappings
* [x] Analytical validation
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-19. Qualified against b4714a4 plus the working tree:
4448/4448 x 3 presets, 1495 x 5 in two, 0 warnings over 682 objects, 17
stages exit 0, 4h48m16s, fingerprints at the freeze == before == after;
run 2 of 2 on the corrected tree. Run 1 passed debug-ext and release-ext
and failed the debug-shared-ext BUILD: PreparedConvectionBoundary, a plain
struct with member functions defined in the library, lacked
BETTERCAD_THERMAL_EXPORT, so a DLL build could not find area(); static
builds ignore the macro, which is why nothing else saw it. Fixed, logs
preserved under qualification/run1-failed/, whole qualification rerun.
The canonical conditions already existed (P18-DATA-001) and are unchanged:
PrescribedTemperature {FaceName, Temperature K} and ConvectionBoundary
{FaceName, h W/(m2 K), T_inf K}; no facet, node, element or unknown is
canonical (sizeof mirrors, seven compile-fail cases). The GeometryReference
is the FaceName; P16's boundaryFacetsOf, through ThermalTarget, is the only
resolver, and an unresolved face is refused for both kinds, never bound to
the nearest. Prescribed temperature: GeometryReference -> facets -> unique
NodeIds -> ThermalDofIndex -> ThermalConstraintMap, ONE constraint per
unknown (the end cap's six corners are four constraints), nonzero values
preserved (350 K stays 350 K, a free node has no temperature), equal
prescriptions deduplicated with both conditions recorded, a contradiction
refused by exact number equality with a per-pair report (end cap 350 K and a
side wall 300 K share two unknowns); no penalty, no epsilon, no-Dirichlet
sets valid. Convection: -k grad(T).n = h (T - T_inf), n outward; weak form
(K + Kconv) T = Q + Qconv derived in WEAK_FORM.md; per linear triangle
Kconv = (hA/12) [2 1 1; 1 2 1; 1 1 2] W/K and Qconv = (h T_inf A/3) [1 1 1]
W, exact, with no conductivity. Hand triangle A = 3 m2, h = 4, T_inf = 300
K: Kconv = [2 1 1; 1 2 1; 1 1 2], Qconv = [1200 1200 1200], error 0. The
centroid matrix hA/9 and the lumped matrix hA/3 I have the correct row sums
and are rejected by the whole-matrix oracle. T = T_inf gives a zero
residual; 350 K gives +600 W outward, 250 K gives -600 W. Convections add
(identical ones double), a prescribed temperature and a convection on one
face are both kept. Scaling s^2, h and T_inf hold. A canonical h = 0 stays
refused (P18-DATA-001's rule); the kernel and seam give zeros for h = 0.
Prepared boundary bound to body, MeshStamp, node count, geometry revision
and the conditions by value: stale after a remesh with the same counts and
the same ids, not stale after a material, load, solver or transient edit,
and the mesh is not regenerated. 92 cases, 31 compile-fail cases, 58
mutation probes (all killed; a share-order survivor closed by two
assertions), 40 + 9 adversarial questions, 7 findings, 1 production defect.
Results are bitwise identical across the three presets. Limits recorded: a
hole's cylindrical wall cannot be named; TargetWithoutFacets is reachable
only through the seams; nothing yet forces the currentness call. No
assembly, no solve, no penalty, no radiation, no time-varying BC, no
contact. Evidence: docs/verification/P18-BC-001/.
```

---
# P18-ASSEMBLY-001

## Global Thermal Assembly

```text
steady      K T = Q
transient   C dT/dt + K T = Q
```

* [x] Assemble global thermal conductivity matrix
* [x] Assemble global thermal capacity matrix
* [x] Assemble global heat-load vector
* [x] Deterministic sparse assembly
* [x] Duplicate sparse contributions SUM
* [x] Validate dimensions
* [x] Validate finite entries
* [x] Validate symmetry
* [x] Single-Tet analytical assembly
* [x] Two-Tet shared-node assembly
* [x] Independent dense oracle for small systems
* [x] Heat-load resultant preservation
* [x] Source compatibility checks
* [x] No silent regularisation
* [x] No production dense global matrix
* [x] Large-mesh sparse smoke
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-20. Qualified against a5b8cc2 plus the working tree:
4546/4546 x 3 presets, 1557 x 5 in two, 0 warnings over 687 objects, 17
stages exit 0, 5h08m45s, fingerprints at the freeze == before == after;
first run on the frozen tree (an earlier launch died in under a second for
want of PATH, built nothing and is not counted). Global system, unconstrained:
K = sum Ke + sum Kconv (W/K), C = sum Ce (J/K, transient only), Q = prepared
heat input + sum Qconv (W), over Ndof = Nnode unknowns from ThermalDofMap;
NodeId is never an index and the dimension is never the largest handle (a
mesh with max id 4 294 967 294 and four nodes gives 4 x 4). BetterCAD CSR,
64-bit indices, strictly ascending columns, a validating constructor, typed
so K is not C; a symbolic pass then a numeric += pass in a frozen serial
order, so duplicates SUM by construction. Single unit tet: K = [3 -1 -1 -1;
-1 1 0 0; -1 0 1 0; -1 0 0 1], C = [2 1 1 1; ...], Q = 1 W per corner, error
0. Two tets: K(1,1) = 1.0 + 1.5 = 2.5, C(1,1) = 2 + 4 = 6, Q(1) = 1 + 2 = 3 W;
independent dense oracle error 0. Real block (9 unknowns, 12 tets): Q = 0.6 +
60 + 8.7945 = 69.3945 W, ||K 1|| = 2.2e-16, 1^T C 1 = 86.664 J/K = rho cp V,
T^T K T and v^T C v equal the sums of the local energies. Steady k-only
assembles with no C; transient without rho/cp is refused (material_unavailable),
never a zero C. Stale numbering, loads, boundary or material refused before the
first element, in a fixed order; a remesh with the same counts and the same
ids is stale by stamp; a prescribed temperature edit does not stale the system
because it never enters K, C or Q (bit-identical with and without). No
Dirichlet, penalty, epsilon, pin, symmetrisation or pruning; no dense global
matrix (22^3 grid: 10 648 unknowns, nnz/Ndof^2 = 1.3e-3). 62 cases, 36
compile-fail cases, 75 mutation probes (74 killed; A61, the symmetry gate on a
correct scatter, is an explained equivalent; five test gaps found and closed),
35 + 6 adversarial questions, 5 findings, 0 production defects. Results are
bitwise identical across the three presets (fingerprints 6398b41313705d28,
c6a042bf47d865e0, 07c4bb5e30c65a38). Limits recorded: the seam takes
convection facets as data (exterior-ness is P18-BC's), thermalAssemblyFailure
costs a full assembly, inconsistent_system is reachable only through a
defective scatter. No solve, no elimination, no C/dt, no residual, no
reaction, no result field. Evidence: docs/verification/P18-ASSEMBLY-001/.
```

---
# P18-SOLVE-001

## Steady Thermal Solver

Solve `K T = Q` after prescribed-temperature constraints. Validate the reduced
free system, `r = Kff Tf - Qf`; never qualify from solver-success status
alone. ADR-050 fixes the reuse constraint: thermal (45) cannot include
structural (50), so P17's solver is reused by EXTRACTION below 45 or by an
independent private link, never by a hand copy.

* [x] Audit available linear algebra
* [x] Reuse qualified compatible solver infrastructure
* [x] Define solver tolerances
* [x] Apply temperature constraints correctly
* [x] Detect singular thermal system
* [x] Detect missing temperature datum where applicable
* [x] Detect disconnected thermal region
* [x] Reject non-finite solution
* [x] Structured solver diagnostics
* [x] Deterministic solve behaviour measured
* [x] Independent residual validation
* [x] Independent small-system validation
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-20. Qualified against 241c13a plus the working tree: 4638/4638 x 3
presets, 1623 x 5 in two, 0 warnings over 693 objects, 17 stages exit 0,
5h12m08s, fingerprints at the freeze == before == after; first run on the
frozen tree. AUDIT: the generic sparse solve belonged to structural (50);
thermal (45) cannot include it (ADR-050). EXTRACTION (ADR-052): P17's
runKernel moved mechanically to a new module linalg (layer 5; core PUBLIC,
Eigen PRIVATE); structural::solveSymmetricSparse is now a wrapper that puts
P17's units on the numbers and rebuilds P17's three unit- or structure-bearing
messages from the kernel's structured failure, byte for byte; 415 structural
cases / 271 414 assertions pass unchanged in all three presets (P17-SOLVE,
reaction, validation, post and the reference models requalified by the
unfiltered runs). NO hand copy: src/thermal holds no factorisation; no
structural include (architecture.layering, 525 files) and no structural link
(architecture.targets.link-graph, a fixture holds the forbidden private link
and is refused). DIRICHLET: Kff Tf = Qf - Kfc Tc over ThermalFreeEquationMap,
free and prescribed unknowns interleaved; one-free-node oracle K=[2 -2;-2 2],
Q=[0 100], T0=300: Kff=[2], Kfc=[-2], beff=700, T1=350 K exactly (50 K if the
term is dropped); 3-node chains 350 K and 366.667 K; Robin-only K=[4],
Q=[1240] gives 310 K with no Dirichlet; fully prescribed Nfree=0 gives T=Tc
with the kernel not invoked. WELL POSEDNESS: pure Neumann (compatible or not)
-> missing_temperature_datum; a region cut off from every datum while another
is anchored -> unanchored_thermal_region; anchored disconnected regions
solve; stored exact zeros join no regions; the pivot gate independently
refuses indefinite, exactly singular and tiny-pivot systems. RESIDUAL:
||r||_2 / (||Kff|| ||Tf|| + ||beff||), independent of the solver and scale
free (K, Q scaled 1e-6..1e6 give the same T to 5.7e-14 K and a normalized
residual in [1.7e-17, 6.3e-17]); a finite answer one kelvin off is refused
while solver status is Success; NaN/Inf, an overflowing residual and a
prescribed value one ulp off are refused. FE: 6^3 grid linear profile to
1.25e-12 K, affine patch to 2.84e-13 K, the real block 400->300 K to 5.7e-14 K,
Dirichlet in series with convection to 1.7e-13 K, 60 W generation leaves
through the film, 5832-unknown grid and the 900-unknown cylinder solve
sparsely (nnz(Kff) 70 766 vs Nfree^2 26.9 million). Stale system, boundary,
constraints and numbering refused before any arithmetic, including after a
remesh with the same counts and ids; a T_bar edit stales the solution but
not the assembled system; a failed re-solve leaves the old result stale and
creates no current one. No epsilon, pin, penalty, pseudo-inverse or hidden
datum; no dense reduced matrix; no gradient, flux, reaction, balance or
time stepping. Results are bitwise identical across the three presets.
51 + 10 cases, 26 compile-fail, 5 architecture tests, 43 mutation probes
(all killed; two test gaps closed), 30 + 7 adversarial questions, 5
findings, 0 production defects. Limits recorded: the solve accepts any
finite field including one below 0 K (POST/VALID's policy), the datum
classification rests on a measured 1e-10 tolerance. Evidence:
docs/verification/P18-SOLVE-001/ and ADR-052.
```
---
# P18-TRANSIENT-001

## Transient Thermal Solver

Initial implicit formulation, backward Euler:
`(C/dt + K) T[n+1] = Q[n+1] + (C/dt) T[n]`.

* [x] Define initial temperature
* [x] Define timestep
* [x] Define end time
* [x] Define output times
* [x] Implement implicit integration
* [x] Define transient constraint treatment
* [x] Define time-varying load policy
* [x] Define time-varying BC policy
* [x] Validate `dt > 0`
* [x] Validate time ordering
* [x] Reject non-finite temperatures
* [x] Define transient residual
* [x] Define energy-balance check
* [x] Deterministic timestepping
* [x] Constant-temperature solution test
* [x] Lumped thermal benchmark
* [x] Time-step refinement study
* [x] Steady-limit comparison
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-21. Qualified against 147d355 plus the working tree: 4745/4745 x 3
presets, 1695 x 5 in two, 0 warnings over 697 objects, 17 stages exit 0,
5h03m11s, fingerprints at the freeze == before == after; first run on the
frozen tree. NOTHING QUALIFIED CHANGED: the steady solver, the generic kernel
and the structural wrapper are byte-identical to P18-SOLVE-001's tree (one
comment in ThermalData.hpp now says how the end time is reached).
FORMULATION: (C/dt + K) T[n+1] = Q + (C/dt) T[n], history from T[n], every step
through the same reduceThermalSystem (Aff Tf = bf - Afc Tc, A = C/dt + K, so
Afc = Cfc/dt + Kfc) and the same linalg::solveSymmetricSparse as the steady
solve. ORACLES: one unknown C=10 G=2 Tinf=300 T0=400 dt=1 gives 4600/12 =
383.3333; the non-uniform pair C=[2 1;1 2] K=[1 -1;-1 1] T0=[300 330] dt=1 gives
[310 320] (a lumped C gives [306 324]); the prescribed value dominates T0 at
t=0 (1000/3, not 300). SINGULARITY: a pure-Neumann model and disconnected
positive-capacity regions SUCCEED where the steady solve refuses them; only a
region with no capacity at all is refused (singular_system at its step); the
steady datum rule is not applied (analyseAnchoring is not called; the faithful
leak, mutation TA21, is killed by 10 cases). TIME GRID: t_k = k dt (a
product), t_N = t_end exactly, the last step shortened and using its own dt,
0.3/0.1 is three steps, dt=0.3 t_end=1 is 0.3 0.3 0.3 0.1, one tolerance
(1e-9), at most 1 000 000 steps, integratedSpan() says what was integrated.
ACCEPTANCE: two independent residuals from the original K, C, Q (the solve of
the system that was built; the heat equation on the free rows); a step built
wrongly (C*dt, a flipped history sign) and solved perfectly is refused;
explicit Euler, Crank-Nicolson, a stagnant and a steady candidate are refused;
the boundary heat at prescribed rows is not a failure. CHECKS: first-order
refinement (orders 0.951 0.976 0.988), the lumped body and a lumped steel
block (tau 666.646 s, mean within 0.057 K of the discrete exponential, energy
identity 2.9e-13), stored energy +dt sum(Q) per step on insulated bodies, the
steady limit to 5.7e-14 K of the qualified steady solver, a constant
temperature in equilibrium stays constant, 216 unknowns against a dense
oracle, a 900-unknown cylinder. PROVENANCE: the history is bound to the
system, the prescribed conditions, the initial temperature, the time settings
and the solver settings; each edit stales exactly its own reason (a specific
heat edit stales it although K is unchanged); a remesh with the same counts
and ids is caught; a failed run publishes nothing and leaves an older result
stale. Typed K and C; no gradient, flux, reaction, balance, interpolation,
restart, adaptivity, retry, regularisation, lumping or time-dependent
coefficient anywhere. Bitwise identical across the three presets. 72 + 35
cases, 85 mutation probes (all killed; three test gaps closed), 30
adversarial questions, 6 findings, 0 production defects. Limits recorded: a
step whose temperature change is below the rounding of the temperature (dt/tau
~ 1e-12) is refused by the physical residual and says so; every step
refactorises; the solve accepts any finite field including one below 0 K.
Evidence: docs/verification/P18-TRANSIENT-001/.
```
---
# P18-POST-001

## Temperature / Gradient / Heat Flux Recovery

For each Tet4: `grad(T) = G T_e`; for isotropic conductivity `q = -k grad(T)`.
Units: `T` K, `grad(T)` K/m, `q` W/m2.

* [x] Recover nodal temperature
* [x] Recover temperature magnitude/value
* [x] Recover element temperature gradient
* [x] Recover heat-flux vector
* [x] Recover heat-flux magnitude
* [x] Define component order
* [x] Define sign convention
* [x] Units explicit
* [x] All results finite
* [x] Constant-gradient patch validation
* [x] Independent Fourier-law validation
* [x] Deterministic result ordering
* [x] No GUI recomputation
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-21. Qualified against 7a9ec06 plus the working tree: 4820/4820 x 3
presets, 1739 x 5 in two, 0 warnings over 701 objects, 17 stages exit 0,
5h38m59s, fingerprints at the freeze == before == after; first run on the
frozen tree. NOTHING QUALIFIED CHANGED: thirteen files of the solvers, the
element, the data and the DOF model are byte-identical to HEAD.
FORMULATION: grad(T) = G Te per Tet4 (Tet4Geometry::gradientFrom, P18-ELEM-001's
path; Te gathered through elementDofs in the tetrahedron's own node order),
q = -k grad(T) written 0 - k g, |q| = hypot(qx, qy, qz). Units K, K/m, W/m2,
W/m2; components [x, y, z]; sign: q . grad(T) = -k |grad(T)|^2 <= 0, heat flows
toward lower temperature. q is the conductive flux inside the solid: it holds
no convection (film exchange 2671.25 W/m2 against a conductive 3.27e-10 W/m2),
no generation (a 60 W body heats 69.2 K with max |q| 9.58e-10 W/m2) and no
storage (uniform heating: dT/dt != 0, max |q| 1.79e-09 W/m2). ORACLES: the
affine patch T = 307.3 + 7.2x - 3.5y + 11.4z, k = 5 gives (7.2, -3.5, 11.4) and
(-36, 17.5, -57) on 162 different tetrahedra to 2.73e-13 K/m and 1.36e-12
W/m2; the three axis fields; the test's own dense 4x4 solve on a scattered field
to 2.45e-13 K/m; a constant 325.7 K field has zero gradient (3.22e-13);
(3, 4, 12) has |q| = 13 (L1 would be 19). SLABS: 400 K -> 300 K, k = 45 on a
jittered 1 m grid (qz = +4500, reversed -4500 W/m2, 4.64e-11), the 20 mm block
(+225 000 W/m2, 3.27e-10), the cylinder on two meshes of 156 and 900 nodes
(1.68e-06), and the transient block at t_end. TRANSFORMS: translation, a proper
rotation (gradient and flux rotate, 2.90e-13 / 1.45e-12), geometry scale,
conductivity scale (gradient bit-identical, flux x4), temperature offset and
difference. KELVIN: 350 K and 280 K come back as 350 K and 280 K, bit for bit,
checked on their own because an offset cancels in every gradient. KEYING:
sparse handles 7, 42, 104, 9001 and ElementIds 2 and 3 give arrays of the node
and tetrahedron counts; the lookup is a binary search, never the raw id.
ATOMIC: one invalid tetrahedron, three overflows (gradient, flux, magnitude),
a non-finite or below-0 K temperature, in any state, fail the whole recovery;
a bad late transient state is named and nothing earlier is published.
CURRENTNESS: the solution's own comparison; a remesh with the same counts and
ids, a load, condition, settings, conductivity or time-step edit, a model older
than a material edit and a steady/transient confusion are refused; a failed
recovery leaves an older result stale. OWNERSHIP: CheckRecoveryOwnership.cmake
(497 files scanned, 0 violations; fixtures that fail it). Results are bitwise
identical across the three presets. 40 + 31 + 4 cases, 43 mutation probes (42
killed, 1 void; three test gaps closed), 34 adversarial questions, 6 findings,
0 production defects. Limits recorded: element-constant values are
discontinuous across faces (smoothing is P18-VIZ-001's display only); a field
below 0 K cannot be recovered; each state copies the nodal temperatures.
Evidence: docs/verification/P18-POST-001/.
```
---
# P18-BALANCE-001

## Heat / Energy Balance

```text
steady      heat in + generated heat - heat out ≈ 0   (one frozen signed form)
transient   heat input + generation - heat output ≈ rate of stored thermal
            energy
```

* [x] Define heat-flow sign convention
* [x] Recover prescribed-temperature reaction heat flow
* [x] Sum applied heat flux
* [x] Sum volumetric heat generation
* [x] Sum convection heat transfer
* [x] Validate steady heat balance
* [x] Validate transient stored-energy balance
* [x] Define tolerances from measurement
* [x] Multiple thermal boundaries
* [x] Deterministic aggregation
* [x] Analytical reference cases
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-22. Qualified against fa15d36 plus the working tree: 4927/4927 x 3
presets, 1813 x 5 in two, 0 warnings over 705 objects, 17 stages exit 0,
5h24m15s, fingerprints at the freeze == before == after; first run on the
frozen tree. NOTHING QUALIFIED CHANGED: twenty-five files of the solvers, the
recovery, the element, the data, the DOF model, the loads, the boundaries and
the assembly are byte-identical to HEAD.
FORMULATION: heat is positive INTO the body. The reaction at a prescribed
unknown is R = +r from the full residual (steady r = K T - Q; transient
r = C (T1 - T0)/dt + K T1 - Q), one per unique unknown, never the remainder
that closes the books. Convection per condition is -(Kconv T - Qconv), not the
ambient source (a 1200 W source for a face that exchanges 0 W at equilibrium).
Surface heat and generation come from P18-LOAD-001's contributions, the global
Q is never added on top, and sum Q = sum loads + sum Qconv is checked before
any residual is formed (an omitted or doubled category is refused). Stored
energy is 1^T C dT over every row with the consistent C and the ACTUAL step
(1 J over a 0.1 s last step is 10 W, not 3.33 W). A FinalOnly history of
several steps is refused; nothing is re-integrated. ORACLES: a g = 2 network at
400/300 K gives +200 W and -200 W, each asserted alone; the block's Dirichlet
reaction 270 W once for duplicate temperature conditions (not 540); the
first-step reaction on the block 334.3 W (it feeds the warm-up); the capacity in
the transient reaction (-30 W against the steady -20 W; lumped -22.5 W); 18 J
with the full C against 12 J with diag(C). TOLERANCE: |B| <= 1e-12 W +
1e-13 (gross + arithmetic), gross being the sum of individual magnitudes so a
+1e6 / -1e6 W cancellation cannot shrink it. MEASURED, twelve cases in three
presets: worst |B|/(gross + arithmetic) 1.7e-16, the policy 583x above it,
every allowed residual at least 584x the measured one, identical bits in the
three presets; an earlier 1e-10 was found 10^6 too loose and tightened. A
balance PASS is not a solve PASS: K = I, Q = 0, T = [+5, -5] balances with a
5 W local error; the accepted-solution type is the gate. CURRENTNESS: the
solution's own comparison; a remesh with the same counts and ids, a load,
condition, settings, conductivity or time-step edit and a steady/transient
confusion are refused; a failed recovery leaves an older result stale. DERIVED
only: not canonical, not persisted, no balance setting in its source; no GUI
recomputation (CheckBalanceOwnership.cmake: 497 files scanned, 0 violations;
fixtures that fail it). 69 + 33 + 5 cases, 48 mutation probes (45 killed, 1
void, 2 removed as redundant, 6 first-pass survivors closed), 36 adversarial
questions, 12 findings (three defects of the new code, all fixed before the
freeze), 0 qualified-module defects. Limits recorded: the books closing does
not prove the physics (P18-VALID-001's); prescribed temperatures are constant
in time in this version; no reaction per boundary condition; a FinalOnly
multi-step history cannot be balanced.
Evidence: docs/verification/P18-BALANCE-001/.
```
---
# P18-VALID-001

## Thermal Validation / Acceptance

This milestone owns P18 thermal-analysis acceptance. Every solve validates:
geometry current, mesh current, mesh structurally valid, thermal mesh quality
acceptable, material current and thermally complete, loads resolved, boundary
conditions resolved, thermal system well posed.

* [x] Define thermal mesh-acceptance policy
* [x] Reuse P16 quality metrics
* [x] Define invalid mesh rejection
* [x] Define warning policy
* [x] Define thermal material validation
* [x] Define thermal-load validation
* [x] Define thermal-BC validation
* [x] Define stale-input checks
* [x] Define singular thermal-system detection
* [x] Define steady solver residual acceptance
* [x] Define transient residual acceptance
* [x] Define heat-balance acceptance
* [x] Define result-finiteness checks
* [x] Structured validation report
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-23. Qualified against bc62b0c plus the working tree: 5106/5106 x 3
presets, 1959 x 5 in two, 0 warnings over 711 objects, 17 stages exit 0,
5h12m47s, fingerprints at the freeze == before == after; first run on the
frozen tree. NOTHING QUALIFIED CHANGED: 63 files (every other thermal source and
header, all of P16 meshing, the linalg and structural solve files) are
byte-identical to HEAD.
DECISION: validateThermalAnalysis(document, regenerator, mesher, analysis,
derived state) is the only place a thermal analysis is accepted. 15 ordered
rules with stable ids; a report whose status, counts and acceptance are derived
every time; severity is a function of the code (no field to change it); an
input failure skips the system, solve, result and balance rules and a skipped
rule is never a pass. accepted() (needs the recovered fields and the balance),
solveAccepted() and inputsAccepted() are three different answers.
MESH: P16 measures, P18-VALID decides. A temperature of 307 K carries the
rounding of a 307 K number, so a flattened or stretched element amplifies it:
relative gradient error <= C / sqrt(3r/R) with C <= 8.62e-13 and <= C * aspect
with C <= 4.28e-13 at 14 K/m, three orders above the isolating field the
structural kernel measured. Inverted at 1e-9 with a factor of three: failure
3r/R < 7.0e-6 or aspect > 700; warning 3.0e-4, 81, 6.0e-3 rad, 3.125 rad (the
qualified envelope, rounded away); the dihedral angles carry no failure bound;
a value on a bound is on the good side; a NaN is never on it. Qualified worst
case 3.226e-4 (46x the floor) and aspect 80.0 (8.7x inside the ceiling).
RULES: steady needs k only, a transient k, rho and cp; no load and no condition
are information; pure-Neumann steady FAILS (no datum), pure-Neumann transient
PASSES (the capacity), steady Robin-only and fully prescribed PASS; a remesh with
the same counts and the same ids is stale, found by identity. The residual is
re-evaluated independently for every step against the canonical solver
tolerance and the balance through P18-BALANCE-001's policy, consumed and never
copied; a balance that nearly cancels (|B| 6.2e-14 W) does not rescue a free
residual of 1.5e-4, and the reverse. A FinalOnly history of several steps can be
solve-accepted and cannot be fully accepted. No GUI decision: the gate scans
497 files and has 0 violations, with seven fixtures that fail it. 138 + 33 + 8
cases, 19 zero-match filters, 78 mutation probes all killed (3 first-pass
survivors were test gaps, closed), 35 adversarial questions, 4 findings (one
model gap, now BalanceAttributionMismatch, and three test gaps), 0 qualified-
module defects. Limits recorded: single-solid meshes only; prescribed
temperatures are constant in time; the mesh bounds guarantee gradient recovery
at a stated condition, not discretisation or conditioning; the dihedrals have no
failure bound; the books closing does not prove the physics.
Evidence: docs/verification/P18-VALID-001/.
```
---
# P18-VIZ-001

## Thermal Visualisation / Inspection

Critical rule: thermal visualisation = a view of derived P18 data, NOT a new
thermal-analysis authority.

* [x] Undeformed thermal mesh display
* [x] Temperature contour
* [x] Temperature-gradient magnitude contour
* [x] Heat-flux magnitude contour
* [x] Heat-flux vector display
* [x] Element inspection
* [x] Node temperature inspection
* [x] Heat-flux boundary arrows
* [x] Prescribed-temperature symbols
* [x] Convection BC symbols
* [x] Boundary-set highlighting
* [x] Highest / lowest temperature navigation
* [x] Highest heat-flux navigation
* [x] Transient playback
* [x] Clear stale-result visual state
* [x] Rendering does not mutate results
* [x] GUI does not duplicate canonical thermal data
* [x] SI metre → OCCT millimetre boundary verified
* [x] Classifier cardinality verified
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-23. Qualified against c56aef0 plus the working tree: 5269/5269 x 3
presets, 2071 x 5 in two, 0 warnings over 721 objects, 17 stages exit 0,
6h05m33s, fingerprints at the freeze == before == after; second attempt on the
frozen tree (the first, stopped by the author after cli.assembly.batch failed
once after 34 s and nine dependants did not run, is in qualification/attempt1/;
the test passes alone in 0.22 s and in attempt 2). NOTHING QUALIFIED CHANGED: 265
files (the whole thermal, meshing, structural, linalg and core modules, the P17
renderer files and the OCCT mesh presentation) are byte-identical to HEAD; the
Viewer gained one additive overload.
DECISION: a read-only view. Four headless files (ThermalResultView, ThermalContour,
ThermalInspection, ThermalOverlay) read the stored nodal temperature, element
gradient vector, heat-flux vector and magnitude, the prepared loads and conditions,
the validation report and the core's currentness; they compute nothing but a display
norm and arrow geometry. The unit and location of every channel are the core's
descriptors; the gradient magnitude is the display norm of the stored vector (a NaN
component stays NaN - std::hypot(0, NaN, 0) is 0 here, found by a test and fixed);
element values are unsmoothed; the legend tiles its range exactly and a constant range
is one band; a manual range clamps a colour and never a value; the cache key holds the
whole source and the view. q is drawn from the stored vector, never flipped or
normalised; an applied q'' points along -n_out for q'' > 0 and is a different overlay;
the outward normal is verified against the owning tetrahedron, also on a curved wall.
CURRENTNESS: the display state is recomputed from the core on every call; a remesh with
the same counts or the same ids, a material, load or condition edit, and a failed
recompute never leave a result drawn as current; the validation badge is the core's
verdict, belongs to its analysis and its result, and goes Stale with the model. CARDINALITY:
expected / classified / rendered on every field and glyph; zero-classified, partial and
unresolved are never complete, nothing is rebound to the nearest face. UNITS: K, K/m and
W/m^2 never rescaled; the one metre -> millimetre conversion is the OCCT adapter's,
measured against a 1 x 2 x 3 mm and a 40 mm CAD solid. PLAYBACK: the stored times
(0, 0.3, 0.6, 0.8999999999999999, 1 s - not 1.2), one state per frame, no interpolation.
The Qt application has no thermal panel yet (as for P17-VIZ-001); the GUI smoke is the
headless layer plus offscreen renders of the production viewer. One gate scans 43 files
with 0 violations and has eleven fixtures that fail it. 100 + 51 + 12 cases, 15 zero-match
filters, 75 mutation probes (71 killed, 3 of them by an abort; 4 unreachable by construction;
3 first-pass survivors were test gaps, closed), 50 adversarial questions, 13 findings (an
independent review found four holes - cache key without the view, glyphs not bound to the
prepared loads, a badge not bound to its analysis, an overflowing arrow length - all fixed
with tests; a test with literal \n sequences had never run its last paragraph, repaired),
0 qualified-module defects. Limits recorded: no Qt panels; inspect* default resultState to
Current (the caller states it); a glyph and an extreme carry no mesh stamp; four guards
are unreachable through any public construction; the core's HeatFlux3::magnitude() has the
hypot NaN property (untouched, POST validates first).
Committed 7ba06a7 (qualified == committed: all eight subtree hashes read back
from the commit equal the frozen ones).
Evidence: docs/verification/P18-VIZ-001/.
```

---
# P18-CMD-001

## Commands / Undo / Redo

Canonical history contains thermal analysis definitions, thermal loads,
thermal boundary conditions, initial conditions, solver settings and time
settings. It does NOT contain K, C, Q, temperature arrays, temperature-gradient
arrays, heat-flux arrays or heat-balance arrays.

* [x] Create-analysis command
* [x] Add-load command
* [x] Edit-load command
* [x] Remove-load command
* [x] Add-BC command
* [x] Edit-BC command
* [x] Remove-BC command
* [x] Initial-temperature command
* [x] Solver-settings command
* [x] Transient-settings command
* [x] Undo exact
* [x] Redo exact
* [x] Failed command atomic
* [x] Redo invalidation correct
* [x] `GeometryReference` preserved
* [x] Thermal result invalidated correctly
* [x] No thermal solver arrays in canonical history
* [x] Compile-fail mesh-identity misuse where practical
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-24. Qualified against 6b675ec plus the working tree: 5432/5432 x 3
presets, 2182 x 2 in two, 0 warnings over 725 objects, 17 stages exit 0, 6h35m56s,
fingerprints at the freeze == before == after; third attempt (attempt 1, stopped by
the author after 11 gui/deploy tests could not start because Windows Smart App Control
blocked the unsigned bettercad.exe - the owner then turned it off; attempt 2, which
passed debug-ext and release-ext and then could not link bettercad_tests on
debug-shared-ext because a test read a static member the thermal DLL does not export,
fixed and re-frozen; both are in qualification/attempt1 and attempt2). NOTHING
QUALIFIED CHANGED: 319 of the 322 thermal, meshing, structural, linalg, core and
renderer files and apps are byte-identical to HEAD, 2 are new and 1 differs by one line
(src/thermal/CMakeLists.txt).
DECISION: no new framework, no new invalidation mechanism, no new identity type. Ten
commands on Command / CommandHistory / AddObjectCommand / Document::modifyObject /
ThermalAnalysis::setDefinition. A command is keyed on a ThermalAnalysisId and addresses a
record by its stable id, never by position; an add appends, an edit replaces the whole
record in place, a removal's undo returns the record to its position. Undo and redo
ASSIGN a stored definition (no inverse, no text round trip, 50 cycles do not drift) and
CHECK that the analysis is as the command left it, refusing instead of discarding an
out-of-history edit. A failed command moves no field, revision or stack; a no-op
succeeds, moves nothing and is recorded; a steady analysis refuses an initial
temperature and transient settings (mode before value). A FaceName is stored as given
(eight selector forms round-trip, copies in order, an unresolved one too), never
resolved; the command layer refuses the invalid feature handle, which the data model
lets through. The payload is two definitions: independent of mesh and result size.
CURRENTNESS, measured on real solves: a load edit stales loads, system, solution, post,
balance and report; a held temperature the boundary, solution, post, balance and report
(not the system); a convection also the system; settings, initial temperature and
transient settings only solution, post, balance and report; the mesh never. TWO REGIMES:
derived objects compare their recorded intent by value (an undo makes an old result
current again, and a fresh solve is bit-identical), the validation report compares the
monotonic revision (it stays stale) - recorded, not changed. The Qt application has no
thermal panel. 95 + 52 + 16 cases (52 compile-fail with a control; a gate with 15
fixtures), 14 zero-match filters, 44 + 14 mutation probes all killed (3 first-run
survivors were a missing test and a missing fixture, closed), 21 adversarial questions,
7 review defects and 2 mutation findings, 0 qualified-module defects. Limits recorded:
a create accepts a dangling mesh-control handle; validate(TransientSettings) has no
step-count bound; no mode-switch, delete or whole-definition command (not on the list).
Committed fe16f9d (qualified == committed: all eight subtree hashes read back
from the commit equal the frozen ones).
Evidence: docs/verification/P18-CMD-001/.
```
---
# P18-PERSIST-001

## Thermal Analysis Persistence

Persist thermal analysis intent, loads, boundary conditions,
`GeometryReference`s, initial temperature, steady settings, transient settings.
Do NOT persist as authority: K, C, Q, T, grad(T), heat flux, heat-balance
arrays. Mandatory byte-identity test: the same canonical document saved before
a thermal solve == saved after it, byte for byte.

* [ ] Define P18 schema
* [ ] Persist analysis definition
* [ ] Persist thermal loads
* [ ] Persist thermal BCs
* [ ] Persist `GeometryReference`s
* [ ] Persist initial condition
* [ ] Persist steady solver settings
* [ ] Persist transient settings
* [ ] Preserve units
* [ ] Preserve unresolved references explicitly
* [ ] Exclude thermal solver/result arrays
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

---
# P18-CLI-001

## Headless Thermal Analysis

Follow existing BetterCAD CLI naming.

* [ ] Inspect thermal analysis
* [ ] Add / remove thermal loads
* [ ] Add / remove thermal BCs
* [ ] Steady solve
* [ ] Transient solve
* [ ] Temperature summary
* [ ] Gradient summary
* [ ] Heat-flux summary
* [ ] Heat-balance report
* [ ] Validation report
* [ ] Structured diagnostics
* [ ] Correct process exit codes
* [ ] Stale-geometry failure
* [ ] Stale-mesh failure
* [ ] Missing-material failure
* [ ] Singular thermal-model failure
* [ ] Invalid load / BC failure
* [ ] CLI / core equivalence
* [ ] Multi-process save / load / re-solve
* [ ] No CLI-only thermal implementation
* [ ] Fresh-binary proof
* [ ] Raw structured STDOUT clean
* [ ] Zero-match protection
* [ ] End-to-end scripted workflow PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-REFMOD-001

## Thermal Reference Models

```text
RM-THERM-01  Single Tet linear-temperature patch test
RM-THERM-02  1D slab conduction
RM-THERM-03  Composite wall / layered conduction
RM-THERM-04  Uniform volumetric heat generation
RM-THERM-05  Convection / Robin boundary benchmark
RM-THERM-06  Rigidly transformed thermal model
RM-THERM-07  Local refinement / thermal-gradient field
RM-THERM-08  Transient thermal benchmark
RM-THERM-09  Singular thermal model / missing datum
RM-THERM-10  Stale mesh / unresolved thermal boundary failure
```

* [ ] Define reference suite
* [ ] Single-Tet thermal patch test
* [ ] Constant temperature-gradient validation
* [ ] Fourier-law validation
* [ ] 1D slab conduction
* [ ] Composite-wall conduction
* [ ] Volumetric heat generation
* [ ] Convection benchmark
* [ ] Heat-balance validation
* [ ] Temperature agreement
* [ ] Heat-flux agreement
* [ ] Rigid-transform invariance
* [ ] Mesh-refinement behaviour
* [ ] Local-refinement behaviour
* [ ] Transient analytical benchmark
* [ ] Singular-system failure
* [ ] Stale-mesh failure
* [ ] Unresolved-reference failure
* [ ] Save / load / re-solve
* [ ] CLI validation
* [ ] Independent analytical validation
* [ ] Determinism PASS
* [ ] Three-preset regression PASS
* [ ] Evidence recorded

### RM-THERM-01 — linear temperature patch test

Affine `T(x,y,z) = a + b x + c y + d z`; a linear Tet4 must reproduce it
exactly: `grad(T) = [b, c, d]^T`, `q = -k [b, c, d]^T`. Validate exact nodal
interpolation, constant gradient, constant heat flux, element-matrix
consistency, energy / weak-form consistency. The strongest single P18
element test.

### RM-THERM-02 — 1D slab conduction

`Qdot = k A (T_hot - T_cold) / L`; `T(x)` linear for constant `k` and no
generation. Validate the profile, the flux, the total heat rate, the boundary
heat reaction, the heat balance.

### RM-THERM-03 — composite wall

`R_total = sum L_i / (k_i A)`, `Qdot = dT / R_total`. Validate interface
temperatures, total heat flow, piecewise gradient, flux continuity. NOTE:
P16 refuses a multi-solid body (ADR-049 constraint), so the layered wall is
built on one solid with the layers expressed through what one material and
one solid can represent, or the case records what it cannot.

### RM-THERM-04 — volumetric heat generation

`q'''` [W/m3], `Qdot_gen = q''' V`, compared with the analytical temperature
behaviour where a closed form exists.

### RM-THERM-05 — convection / Robin boundary

`q'' = h (T_surface - T_inf)` against an analytical resistance-network / slab
case: surface temperature, heat rate, conduction/convection balance.

### RM-THERM-06 — rigid transform

Invariant: temperature, heat-flux magnitude, thermal energy, heat balance.
Transformed: `grad(T)' = R grad(T)`, `q' = R q`. Judge P16 boundary and
interior mesh populations separately.

### RM-THERM-07 — local refinement

Baseline, target-refined and mirror-refined documents with a 3D volume band;
never inferred from target planar surface triangles alone. Validate band
Tet size/count, mapping preserved, thermal solve valid, temperature/gradient
trend sensible.

### RM-THERM-08 — transient benchmark

A lumped-capacitance body with its assumptions deliberately satisfied, or a
1D transient slab with analytical series data. Validate `T(t)`, time-step
refinement, energy balance, the steady-state limit.

### RM-THERM-09 — singular thermal model

A conduction problem with no temperature datum. Required: the solve FAILS
explicitly. Forbidden: arbitrary finite temperatures returned as success.

### RM-THERM-10 — stale / unresolved inputs

A stale P16 mesh, an unresolved thermal load target, an unresolved
temperature/convection target. Expected: solve refused before assembly where
applicable. No nearest-face rebinding.

---
# P18-QUAL-001

## Full P18 Qualification

* [ ] Freeze final P18 tree
* [ ] Audit every P18 milestone
* [ ] Verify all TODO items complete
* [ ] Verify all P18 ADRs
* [ ] Audit CAD / material / mesh / thermal authority
* [ ] Audit thermal result currentness
* [ ] Audit thermal DOF model
* [ ] Audit Tet4 thermal formulation
* [ ] Audit thermal loads
* [ ] Audit thermal BCs
* [ ] Audit global assembly
* [ ] Audit steady solver
* [ ] Audit transient solver
* [ ] Audit temperature recovery
* [ ] Audit gradient recovery
* [ ] Audit heat-flux recovery
* [ ] Audit heat balance
* [ ] Audit validation policy
* [ ] Audit stale-input protection
* [ ] Audit visualisation
* [ ] Audit commands
* [ ] Audit persistence
* [ ] Audit CLI
* [ ] Re-run all thermal reference models
* [ ] Independent analytical validation
* [ ] Steady residual PASS
* [ ] Heat balance PASS
* [ ] Thermal patch test PASS
* [ ] Slab conduction PASS
* [ ] Convection benchmark PASS
* [ ] Transient benchmark PASS
* [ ] Singular thermal-model failure PASS
* [ ] Stale-mesh failure PASS
* [ ] Clean Debug qualification
* [ ] Clean Release qualification
* [ ] Clean Debug-shared qualification
* [ ] Repeated determinism qualification
* [ ] Cross-preset equivalence
* [ ] Final adversarial review
* [ ] Zero-match protection PASS
* [ ] 0 unexpected warnings
* [ ] Qualified tree == committed tree
* [ ] Evidence recorded
* [ ] Mark P18 qualified

### P18 Final Gate

```text
all P18 milestones PASS

+ CAD geometry remains authority
+ P15 material remains canonical
+ P16 mesh remains derived
+ P18 results remain derived

+ stale geometry rejection PASS
+ stale mesh rejection PASS

+ thermal DOF model PASS
+ Tet4 conduction formulation PASS
+ Tet4 capacity formulation PASS

+ conductivity matrix symmetry PASS
+ thermal-null-mode behaviour PASS

+ thermal loads PASS
+ thermal BCs PASS
+ GeometryReference mapping PASS

+ global thermal assembly PASS

+ steady solver PASS
+ residual validation PASS

+ transient solver PASS
+ time-step validation PASS
+ energy storage behaviour PASS

+ temperature PASS
+ gradient PASS
+ heat flux PASS

+ steady heat balance PASS
+ transient energy balance PASS

+ thermal mesh-quality acceptance policy explicit

+ visual inspection PASS

+ undo/redo PASS
+ generated thermal arrays excluded from history

+ persistence PASS
+ generated thermal arrays excluded as authority

+ CLI PASS
+ CLI/core equivalence PASS

+ reference models PASS
+ independent analytical validation PASS
+ thermal patch test PASS

+ Debug PASS
+ Release PASS
+ Debug-shared PASS

+ determinism PASS
+ cross-preset equivalence PASS
+ adversarial review PASS
+ zero-match protection PASS
+ 0 unexpected warnings

+ qualified tree == committed tree
```

### P18 Final Adversarial Questions

At minimum attack:

```text
Can P18 solve a stale mesh?
Can P18 solve geometry that failed regeneration?
Can a conductivity edit leave an old result current?
Can density/cp edits leave an old transient result current?
Can a thermal-load edit leave an old result current?
Can a thermal-BC edit leave an old result current?
Can NodeId become permanent thermal-load identity?
Can ElementId become permanent thermal intent?
Can a thermal boundary silently move to another CAD face?
Can an unresolved thermal BC bind to a nearby face?
Can an inverted Tet enter thermal assembly?
Can a zero-volume Tet generate finite-looking conductivity/capacity matrices?
Can thermal conductivity units be confused?
Can K lose symmetry?
Can duplicate sparse thermal contributions overwrite instead of accumulate?
Can a steady thermal model with a free temperature null mode appear solved?
Can the solver return success with a large thermal residual?
Can heat balance fail while the solve is accepted?
Can surface heat flux fail to preserve total heat rate?
Can volumetric generation fail to preserve q'''V?
Can convection use an incorrect sign?
Can a rigid transform change temperature or heat-flux magnitude?
Can GUI visualization recalculate heat flux differently from core?
Can visualization units mutate model coordinates?
Can undo restore thermal matrices/results instead of analysis intent?
Can save/load trust old temperature or heat-flux arrays?
Can the CLI implement separate Tet4 thermal logic?
Can the CLI return zero on a singular thermal solve?
Can mesh-quality thresholds differ between GUI and CLI?
Can transient time-step changes fail to stale the old result?
Can save-before-solve and save-after-solve differ?
Can stale binaries produce qualification evidence?
Can a zero-test filter pass qualification?
Can source change after final qualification?
```

For every credible defect: reproduce → regression test → root cause →
general fix → requalify the affected milestone → rerun P18-QUAL.

---
# P18 Carried Constraints

Inherited on entering P18, and not P18's to lift without an explicit scope
decision:

```text
1. P16's VolumeMesh currentness is re-checked at the solver entry point
   (requireThermalModel, ADR-051). Never trust a cached mesh because a
   caller handed it over.

2. Configuration-sensitive geometry REFUSES a stale configuration state.
   P18 inherits the refusal through P16's geometry boundary (carried
   defect 2).

3. P16 quality thresholds are report-only. P18-VALID-001 owns the thermal
   mesh-acceptance policy, as P17-VALID-001 owned the structural one.

4. P16 REFUSES a body with more than one solid. P18 is single-solid, which
   bears on RM-THERM-03 (ADR-049).

5. A drilled-hole feature provides no stable hole-wall reference. Do not
   fake load or boundary-condition targeting for unsupported topology.

6. This MinGW toolchain has no ASan/UBSan. Record the limitation.

7. A clean checkout cannot re-fetch dependencies here; clean-tree checks
   configure from the same SHA256-verified local sources.

8. The project has no licence. A solver dependency's licence is an owner
   decision (P18-SOLVE-001, as P17-SOLVE-001).

9. thermal (45) may not include structural (50) (ADR-050). P17's solver is
   reused by extraction below 45 or by an independent private link; a hand
   copy is refused.

10. P17's findings F1, F2 and F3 stand unchanged. A thermal flux
    concentration on Tet4 inherits F2's mechanism and F3's cause.
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
P17 — Structural FEA is QUALIFIED. P17-QUAL-001 PASS, 2026-10-15.

    seventeen milestones audited from their own recorded verdicts, 17 PASS
    206 ADR clauses checked against the code: 195 implemented, 11 sentences
      overtaken by later milestones and amended, 0 invariant violations
    three final cross-milestone gates: solving leaves every reference
      document byte-identical; every dependency edit stales the result in
      one sequence with undo in the middle; a FAILED regeneration never
      yields a current result
    seven mutation probes on the production tree -- duplicate-entry
      overwrite, pivot gate removed, residual gate removed, inverted
      element admitted, material staleness dropped, CLI exit 0 on a
      singular solve, reaction sign flipped -- seven killed
    44 adversarial questions, 0 production defects, 0 gate-blocking
    qualification: 4009/4009 x 3 presets, 1138 x 5 in two, 0 warnings over
      658 objects, 17 stages exit 0, 3h49m57s, fingerprints before == after,
      suite fingerprints and CLI solve payloads bit-identical across presets
    NO file under src/, include/, apps/ or examples/ changed

There is no open milestone and nothing is authorized. P18 — Thermal
Analysis is next in the sequence and is NOT AUTHORIZED; P17 being qualified
is not permission to start it. F1, F2 and F3 (P17-REFMOD-001) are owner
decisions it should weigh first.
```

What P17 hands over: a result keyed to the mesh stamp, the material revision
and the analysis revision it was computed from; a material resolved per
analysis mode through P15's consumer model; one validation entry whose gates
a later phase inherits rather than re-asks; and ten reference models with
closed-form oracles that cannot call BetterCAD.
Evidence: [docs/verification/P17-QUAL-001/](docs/verification/P17-QUAL-001/README.md).

What it consumes is P16: a validated Tet4 volume mesh from authoritative CAD
geometry, with sizing controls, quality metrics, geometry/mesh correspondence,
boundary regions, undo/redo, a headless CLI and eight independently validated
reference models.
[docs/verification/P16-QUAL-001/](docs/verification/P16-QUAL-001/README.md).

## What is authorized, and what is not

```text
P17-ARCH-001 .. P17-REFMOD-001
                 PASS, each at its own recorded candidate and harness
                 verdict; the chain and the figures are in
                 docs/verification/P17-QUAL-001/MILESTONE_AUDIT.md and the
                 evidence links in ROADMAP.md.

P17-QUAL-001     PASS 2026-10-15. Qualified against a99f0b6 plus the
                 working tree: 4009/4009 x 3, 1138 x 5 in two, 0 warnings
                 over 658 objects, 17 stages exit 0. Committed as ae8c2f7,
                 and all eight subtree hashes of that commit equal the
                 frozen fingerprint (apps a95876bc, include c9696d26, src
                 f3c1245d, tests 90f93c52, examples a955cf65, cmake
                 5a382115, CMakeLists.txt 3ec3c10f, CMakePresets.json
                 3229f0f8): qualified tree == committed tree. Evidence
                 recorded.

P18              NOT AUTHORIZED. Being next in the sequence is not
                 permission, and P17 being qualified is not either.
                 Authorizing it is a scope decision, and F1-F3 are inputs
                 to it.

everything after it
                 likewise.
```


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
stale cache     DISCHARGED for P17 by P17-ARCH-001 and re-proved by
                P17-QUAL-001: the solver entry takes the document, the
                regenerator and the mesher and re-checks mesh currency
                through requireStructuralModel on every call; a failed
                regeneration and a stale mesh are both refused in sequence.
                Still true in general: nothing FORCES a holder of a
                VolumeMesh to call isStale, so a future consumer that CACHES
                a mesh owns the same check. (P16-VOL-001)
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
quality         DISCHARGED for P17 by P17-VALID-001 (ADR-042): the
thresholds      structural acceptance policy rejects on measured accuracy
                laws, one bound per degeneration family, and warns at the
                qualified envelope. P16's own policy stays report-only, so
                the next consumer of a mesh (P18) owns its own thresholds
                the same way. (P16-QUALITY-001, P16-QUAL-001)
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
