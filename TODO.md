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
           none. P18-SOLVE-001 — Steady Thermal Solver. COMPLETE at 15 of 15
           and qualified 2026-10-20 against 241c13a plus the working tree:
           4638/4638 x 3 presets, 1623 x 5 in two, 0 warnings over 693
           objects, 17 stages exit 0, 5h12m08s, fingerprints at the freeze
           == before == after, first run. K T = Q by the exact reduction
           Kff Tf = Qf - Kfc Tc over P18-DOF's maps (the Kfc Tc term P17 did
           not need: a prescribed temperature is not zero), through P17's
           qualified symmetric sparse solve EXTRACTED, mechanically, out of
           structural (50) into a new module linalg (5) so that thermal (45)
           reaches it without including or linking structural (ADR-052;
           ADR-050 kept; the 415 structural cases pass unchanged in all three
           presets). Independent residual, normalized and scale free, that a
           finite wrong answer cannot pass; prescribed values held exactly;
           Nfree = 0 solves, a convection-anchored model with no Dirichlet
           solves, a disconnected model solves when every region is anchored;
           a model with no absolute-temperature datum is REFUSED
           (missing_temperature_datum / unanchored_thermal_region), never
           given a level. 51 + 10 cases, 26 compile-fail, 5 architecture
           tests, 43 mutation probes (all killed), 525 files and 0 layering
           violations. Evidence: docs/verification/P18-SOLVE-001/.
           P18-ARCH-001 PASSED 2026-10-16; P18-DATA-001, P18-MAT-001,
           P18-DOF-001 PASSED 2026-10-17; P18-ELEM-001, P18-LOAD-001 PASSED
           2026-10-18; P18-BC-001, P18-ASSEMBLY-001 PASSED 2026-10-19/20
           (docs/verification/P18-*/).Qualified:
           P0–P10 — BetterCAD v0.1.0
           P11 — Advanced Part Modelling
           P12 — Production Part Modelling
           P13 — Assemblies
           P14 — Technical Drawings
           P15 — Materials / Engineering Data
           P16 — Meshing
           P17 — Structural FEA

Next:
           P18-TRANSIENT-001 — Transient Thermal Solver. NOT AUTHORIZED:
           P18-SOLVE-001 passing is not permission to start it; each
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

* [ ] Define initial temperature
* [ ] Define timestep
* [ ] Define end time
* [ ] Define output times
* [ ] Implement implicit integration
* [ ] Define transient constraint treatment
* [ ] Define time-varying load policy
* [ ] Define time-varying BC policy
* [ ] Validate `dt > 0`
* [ ] Validate time ordering
* [ ] Reject non-finite temperatures
* [ ] Define transient residual
* [ ] Define energy-balance check
* [ ] Deterministic timestepping
* [ ] Constant-temperature solution test
* [ ] Lumped thermal benchmark
* [ ] Time-step refinement study
* [ ] Steady-limit comparison
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-POST-001

## Temperature / Gradient / Heat Flux Recovery

For each Tet4: `grad(T) = G T_e`; for isotropic conductivity `q = -k grad(T)`.
Units: `T` K, `grad(T)` K/m, `q` W/m2.

* [ ] Recover nodal temperature
* [ ] Recover temperature magnitude/value
* [ ] Recover element temperature gradient
* [ ] Recover heat-flux vector
* [ ] Recover heat-flux magnitude
* [ ] Define component order
* [ ] Define sign convention
* [ ] Units explicit
* [ ] All results finite
* [ ] Constant-gradient patch validation
* [ ] Independent Fourier-law validation
* [ ] Deterministic result ordering
* [ ] No GUI recomputation
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-BALANCE-001

## Heat / Energy Balance

```text
steady      heat in + generated heat - heat out ≈ 0   (one frozen signed form)
transient   heat input + generation - heat output ≈ rate of stored thermal
            energy
```

* [ ] Define heat-flow sign convention
* [ ] Recover prescribed-temperature reaction heat flow
* [ ] Sum applied heat flux
* [ ] Sum volumetric heat generation
* [ ] Sum convection heat transfer
* [ ] Validate steady heat balance
* [ ] Validate transient stored-energy balance
* [ ] Define tolerances from measurement
* [ ] Multiple thermal boundaries
* [ ] Deterministic aggregation
* [ ] Analytical reference cases
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-VALID-001

## Thermal Validation / Acceptance

This milestone owns P18 thermal-analysis acceptance. Every solve validates:
geometry current, mesh current, mesh structurally valid, thermal mesh quality
acceptable, material current and thermally complete, loads resolved, boundary
conditions resolved, thermal system well posed.

* [ ] Define thermal mesh-acceptance policy
* [ ] Reuse P16 quality metrics
* [ ] Define invalid mesh rejection
* [ ] Define warning policy
* [ ] Define thermal material validation
* [ ] Define thermal-load validation
* [ ] Define thermal-BC validation
* [ ] Define stale-input checks
* [ ] Define singular thermal-system detection
* [ ] Define steady solver residual acceptance
* [ ] Define transient residual acceptance
* [ ] Define heat-balance acceptance
* [ ] Define result-finiteness checks
* [ ] Structured validation report
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-VIZ-001

## Thermal Visualisation / Inspection

Critical rule: thermal visualisation = a view of derived P18 data, NOT a new
thermal-analysis authority.

* [ ] Undeformed thermal mesh display
* [ ] Temperature contour
* [ ] Temperature-gradient magnitude contour
* [ ] Heat-flux magnitude contour
* [ ] Heat-flux vector display
* [ ] Element inspection
* [ ] Node temperature inspection
* [ ] Heat-flux boundary arrows
* [ ] Prescribed-temperature symbols
* [ ] Convection BC symbols
* [ ] Boundary-set highlighting
* [ ] Highest / lowest temperature navigation
* [ ] Highest heat-flux navigation
* [ ] Transient playback
* [ ] Clear stale-result visual state
* [ ] Rendering does not mutate results
* [ ] GUI does not duplicate canonical thermal data
* [ ] SI metre → OCCT millimetre boundary verified
* [ ] Classifier cardinality verified
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-CMD-001

## Commands / Undo / Redo

Canonical history contains thermal analysis definitions, thermal loads,
thermal boundary conditions, initial conditions, solver settings and time
settings. It does NOT contain K, C, Q, temperature arrays, temperature-gradient
arrays, heat-flux arrays or heat-balance arrays.

* [ ] Create-analysis command
* [ ] Add-load command
* [ ] Edit-load command
* [ ] Remove-load command
* [ ] Add-BC command
* [ ] Edit-BC command
* [ ] Remove-BC command
* [ ] Initial-temperature command
* [ ] Solver-settings command
* [ ] Transient-settings command
* [ ] Undo exact
* [ ] Redo exact
* [ ] Failed command atomic
* [ ] Redo invalidation correct
* [ ] `GeometryReference` preserved
* [ ] Thermal result invalidated correctly
* [ ] No thermal solver arrays in canonical history
* [ ] Compile-fail mesh-identity misuse where practical
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

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
