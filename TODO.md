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
           none. P18-ARCH-001 — Thermal Architecture / Scope. COMPLETE at
           19 of 19 and qualified 2026-10-16 against a3b721b plus the
           working tree: 4024/4024 x 3 presets, 1177 x 5 in two, 0 warnings
           over 660 objects, 17 stages exit 0, 3h29m47s, fingerprints
           before == after. A thermal module at layer 45, BELOW structural
           (ADR-050), the scope (ADR-049) and the input boundary
           requireThermalModel (ADR-051); 11 boundary cases, 4 checker
           fixtures, 497 files and 0 layering violations.
           Evidence: docs/verification/P18-ARCH-001/.

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
           P18-DATA-001 — Thermal Analysis Data Model. NOT AUTHORIZED:
           P18-ARCH-001 passing is not permission to start it; each
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

* [ ] Define `ThermalAnalysisId`
* [ ] Define `ThermalLoadId`
* [ ] Define `ThermalBoundaryConditionId`
* [ ] Define analysis state
* [ ] Define steady solver settings
* [ ] Define transient solver settings
* [ ] Define initial-condition representation
* [ ] Define temperature-result representation
* [ ] Define gradient-result representation
* [ ] Define heat-flux-result representation
* [ ] Define heat-balance-result representation
* [ ] Define provenance/source fingerprints
* [ ] Define result-currentness semantics
* [ ] Define stale-result semantics
* [ ] Define failure-result semantics
* [ ] Units explicit
* [ ] Deterministic ordering
* [ ] Regression PASS
* [ ] Evidence recorded

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

* [ ] Resolve thermal conductivity from P15
* [ ] Resolve density
* [ ] Resolve specific heat
* [ ] Define which properties steady analysis requires
* [ ] Define which properties transient analysis requires
* [ ] Reject missing conductivity
* [ ] Reject missing density for transient analysis
* [ ] Reject missing specific heat for transient analysis
* [ ] Reject non-finite values
* [ ] Require `k > 0`
* [ ] Require `rho > 0` where required
* [ ] Require `cp > 0` where required
* [ ] Preserve material provenance
* [ ] Material edit stales thermal result
* [ ] Material edit does NOT stale P16 mesh
* [ ] No P18 material database
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-DOF-001

## Temperature DOF Model

Initial thermal model: 1 scalar temperature DOF per node.

* [ ] Define thermal `DofIndex`
* [ ] Define deterministic node → thermal DOF mapping
* [ ] Define constrained/free thermal equations
* [ ] Define temperature constraint map
* [ ] Bind DOF map to exact P16 mesh source
* [ ] Remesh invalidates old thermal DOF map
* [ ] NodeId is not used as raw equation number
* [ ] No thermal DOF IDs persisted as canonical intent
* [ ] Sparse NodeId tests
* [ ] Determinism PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-ELEM-001

## Linear Tet4 Thermal Element

```text
steady conduction      Ke = integral (grad N)^T k (grad N) dV
                          = V G^T k G   for constant isotropic k on a Tet4
transient capacity     Ce = integral rho cp N^T N dV
units                  Ke -> W/K      Ce -> J/K
```

* [ ] Define local node order
* [ ] Define Jacobian convention
* [ ] Define signed volume convention
* [ ] Reject inverted Tet
* [ ] Reject degenerate Tet
* [ ] Define shape-function gradients
* [ ] Implement conduction matrix
* [ ] Implement transient capacity matrix
* [ ] Define consistent vs lumped capacity policy
* [ ] Check symmetry
* [ ] Check positive-semidefinite conduction behaviour
* [ ] Check capacity matrix positivity
* [ ] Translation invariance
* [ ] Rotation invariance for isotropic `k`
* [ ] Geometry scaling validation
* [ ] Conductivity scaling validation
* [ ] Independent analytical oracle
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-LOAD-001

## Thermal Loads

Initial supported loads: surface heat flux `q''` [W/m2] with one frozen
positive-direction convention; volumetric heat generation `q'''` [W/m3], with
`Q_element = q''' V` for constant generation. Convection's source term is
handled with the BC milestone.

* [ ] Define heat-flux load
* [ ] Define volumetric heat-generation load
* [ ] Define `GeometryReference` authority
* [ ] Resolve current P16 boundary mapping
* [ ] Convert face heat flux to equivalent nodal heat input
* [ ] Convert volumetric generation to equivalent nodal heat input
* [ ] Preserve total heat rate
* [ ] Define sign conventions
* [ ] Explicit SI units
* [ ] Multiple loads superpose deterministically
* [ ] Remesh re-resolves canonical geometry references
* [ ] No facet/NodeId persistence
* [ ] Unsupported target fails explicitly
* [ ] Stale prepared loads rejected
* [ ] Analytical resultant validation
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-BC-001

## Thermal Boundary Conditions

Initial supported BCs: prescribed temperature; convection / Robin,
`-k grad(T) . n = h (T - T_inf)`, assembled as the qualified equivalent
boundary matrix and source terms.

* [ ] Define prescribed-temperature BC
* [ ] Define convection BC
* [ ] Define ambient temperature
* [ ] Define convection coefficient
* [ ] Define sign convention
* [ ] Map BCs through canonical `GeometryReference`
* [ ] Prescribed temperature resolves to current thermal DOFs
* [ ] Duplicate constraints handled deterministically
* [ ] Conflicting prescribed temperatures rejected
* [ ] Convection contributes correct matrix/source terms
* [ ] Unresolved reference fails explicitly
* [ ] No nearest-face rebinding
* [ ] Remesh invalidates derived mappings
* [ ] Analytical validation
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-ASSEMBLY-001

## Global Thermal Assembly

```text
steady      K T = Q
transient   C dT/dt + K T = Q
```

* [ ] Assemble global thermal conductivity matrix
* [ ] Assemble global thermal capacity matrix
* [ ] Assemble global heat-load vector
* [ ] Deterministic sparse assembly
* [ ] Duplicate sparse contributions SUM
* [ ] Validate dimensions
* [ ] Validate finite entries
* [ ] Validate symmetry
* [ ] Single-Tet analytical assembly
* [ ] Two-Tet shared-node assembly
* [ ] Independent dense oracle for small systems
* [ ] Heat-load resultant preservation
* [ ] Source compatibility checks
* [ ] No silent regularisation
* [ ] No production dense global matrix
* [ ] Large-mesh sparse smoke
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P18-SOLVE-001

## Steady Thermal Solver

Solve `K T = Q` after prescribed-temperature constraints. Validate the reduced
free system, `r = Kff Tf - Qf`; never qualify from solver-success status
alone. ADR-050 fixes the reuse constraint: thermal (45) cannot include
structural (50), so P17's solver is reused by EXTRACTION below 45 or by an
independent private link, never by a hand copy.

* [ ] Audit available linear algebra
* [ ] Reuse qualified compatible solver infrastructure
* [ ] Define solver tolerances
* [ ] Apply temperature constraints correctly
* [ ] Detect singular thermal system
* [ ] Detect missing temperature datum where applicable
* [ ] Detect disconnected thermal region
* [ ] Reject non-finite solution
* [ ] Structured solver diagnostics
* [ ] Deterministic solve behaviour measured
* [ ] Independent residual validation
* [ ] Independent small-system validation
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

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
