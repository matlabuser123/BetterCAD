# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.
> Qualification milestones also require the final qualified tree to match the committed tree.
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

## Status

```text
Current:   P13 — Assemblies
Next:      P13-SOLVE-001 — Assembly constraint solver
Then:      P13-MATE-002 — Mechanical mates

Released:  v0.1.0 — P0–P10
Qualified: P11, P12

P13 architecture:
ADR-002 → ADR-006
```

---

# Qualified Baseline

## P12 — Parametric CAD Completion

* [x] `P12-PARAM-001` — Parameter expressions
* [x] `P12-SKETCH-001` — Advanced sketch constraints
* [x] `P12-SKETCH-002` — Ellipse and spline entities
* [x] `P12-DATUM-001` — Datum planes, axes and coordinate systems
* [x] `P12-STREF-001` — Stable feature-face references
* [x] `P12-SKETCH-003` — Sketches on feature faces
* [x] `P12-FEAT-001` — Through-all extrude
* [x] `P12-FEAT-002` — Split / combine
* [x] `P12-FEAT-003` — Shell
* [x] `P12-FEAT-004` — Draft
* [x] `P12-FEAT-005` — Rib
* [x] `P12-FEAT-006` — Variable-radius fillet
* [x] `P12-HOLE-001` — Advanced holes
* [x] `P12-PATTERN-001` — Advanced patterns
* [x] `P12-SWEEP-001` — Advanced sweep
* [x] `P12-LOFT-001` — Advanced loft
* [x] `P12-PARAM-002` — Design equations and configurations
* [x] `P12-REF-001` — Production reference models
* [x] `P12-QUAL-001` — P12 qualification

Evidence:

```text
docs/verification/P12-*/
```

P12 qualified on `15d7f75`.

---

# P13 — Assemblies

> Architecture is decided. Implement milestones sequentially.
> Only the current milestone is authorized.

## Completed

* [x] `P13-ARCH-001` — Assembly architecture and contracts
* [x] `P13-COMP-001` — Component definitions and instances
* [x] `P13-XFORM-001` — Component transforms
* [x] `P13-REF-001` — Internal / external reference infrastructure
* [x] `P13-MATE-001` — Basic assembly constraints

Decisions:

```text
ADR-002 — assembly/document model
ADR-003 — internal/external reference contract
ADR-004 — mate-reference semantics
ADR-005 — solve-state / transform policy
ADR-006 — module layering
```

Evidence:

```text
docs/verification/P13-ARCH-001/
docs/verification/P13-COMP-001/
docs/verification/P13-XFORM-001/
docs/verification/P13-REF-001/
docs/verification/P13-MATE-001/
```

---

# DONE — P13-COMP-001

## Component Definitions and Instances

The canonical component-instance model. Evidence in
[docs/verification/P13-COMP-001/](docs/verification/P13-COMP-001/README.md).

* [x] `ComponentInstance` participates as a `DocumentObject`
* [x] Strong `ComponentId` identity
* [x] Component identity distinct from referenced part identity
* [x] Multiple instances may reference one internal part
* [x] Internal part references validated
* [x] Foreign-document references rejected — a component's reference never
  resolves outside its own document, and an ID this document does not have is
  refused `NotFound`, changing nothing. A foreign ID that *collides* with a
  local object is undetectable and binds locally: inherent to `ObjectId`,
  tested, and the reason ADR-003 defers cross-document references to
  `P13-REF-001`.
* [x] Correct dependency-graph participation
* [x] Invalid creation is atomic and recoverable
* [x] Component deletion validated
* [x] Save/load preserves canonical component state
* [x] Pre-P13 files remain compatible
* [x] No unnecessary `.bcad` schema/version change
* [x] Deterministic behavior validated
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence recorded in `docs/verification/P13-COMP-001/`

### Gate

```text
component model correct
+ identity stable
+ document ownership correct
+ internal references correct
+ persistence PASS
+ legacy compatibility PASS
+ determinism PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

Met: 1282/1282 on `debug`, `release` and `debug-shared` from clean, and
632/632 five times over in `release` and `debug`; 0 compiler warnings; the
adversarial review found and fixed 4 defects, each with a regression test;
the qualified tree IDs match the committed tree.

```text
P13-COMP-001 → [x]
Next → P13-XFORM-001
```

---

# DONE — P13-XFORM-001

## Component Transforms

A component's placement: canonical intent, derived transform. Evidence in
[docs/verification/P13-XFORM-001/](docs/verification/P13-XFORM-001/README.md).

* [x] Define canonical rigid-transform representation
* [x] Implement component local placement
* [x] Keep solved/world transforms derived, not canonical
* [x] Validate transform composition/order
* [x] Validate rotation convention and units
* [x] Multiple instances transform independently
* [x] Validate identity/default placement
* [x] Save/load preserves canonical placement
* [x] Invalid transforms fail atomically
* [x] Deterministic transform results
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence in `docs/verification/P13-XFORM-001/`

### Gate

```text
rigid-transform model correct
+ canonical/derived state separation correct
+ composition mathematically correct
+ multiple-instance behavior correct
+ persistence PASS
+ failure atomicity PASS
+ determinism PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

Met: 1301/1301 on `debug`, `release` and `debug-shared` from clean, and
708/708 five times over in `release` and `debug`; 0 compiler warnings; the
adversarial review found 3 gaps and 0 production defects, closing two of them
with new regression tests; the qualified tree IDs match the committed tree.

```text
P13-XFORM-001 → [x]
Next → P13-REF-001
```

---

# DONE — P13-REF-001

## Internal / External Reference Infrastructure

Reference identity wider than `ObjectId`. Evidence in
[docs/verification/P13-REF-001/](docs/verification/P13-REF-001/README.md).

* [x] Define canonical reference identity model
* [x] Implement same-document internal references
* [x] Define unresolved-reference state explicitly
* [x] Implement injectable resolver interface for future external references
* [x] Use stable UUID/document identity, never filesystem-path identity
* [x] Keep external-reference resolution separate from `ObjectId`
* [x] Reject accidental cross-document `ObjectId` binding — for a reference
  that carries a document identity. A *bare* `ObjectId` is still defined as
  local and still binds locally; `P13-COMP-001`'s test of that is retained.
* [x] Validate reference type / target compatibility
* [x] Validate missing / deleted target behavior
* [x] Validate reference recovery after target becomes available again
* [x] Prevent silent rebinding to another object
* [x] Preserve references through save/load
* [x] Preserve pre-P13 file compatibility
* [x] Validate deterministic reference resolution
* [x] Validate failure atomicity
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence in `docs/verification/P13-REF-001/`

### Gate

```text
reference model correct
+ internal references PASS
+ external-resolution contract correct
+ unresolved state explicit
+ no path-based identity
+ no silent rebinding
+ persistence PASS
+ determinism PASS
+ failure atomicity PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

Met: 1323/1323 on `debug`, `release` and `debug-shared` from clean, and
730/730 five times over in `release` and `debug`; 0 compiler warnings; the
adversarial review found 2 gaps and 0 production defects, closing both with
regression tests; the qualified tree IDs match the committed tree.

Carried forward: external references do not regenerate, because the
dependency graph speaks in `ObjectId` (ADR-003). Such a component fails
regeneration with the state that says why, rather than appearing to succeed.

```text
P13-REF-001 → [x]
Next → P13-MATE-001
```

---

# DONE — P13-MATE-001

## Basic Assembly Constraints

The constraint model: seven kinds, what they may point at, and what they
refuse. Nothing moves. Evidence in
[docs/verification/P13-MATE-001/](docs/verification/P13-MATE-001/README.md).

* [x] Implement strong `MateId`
* [x] Implement canonical `MateConstraint` as document state
* [x] Implement Fixed constraint
* [x] Implement Coincident constraint
* [x] Implement Concentric constraint
* [x] Implement Parallel constraint
* [x] Implement Perpendicular constraint
* [x] Implement Distance constraint
* [x] Implement Angle constraint
* [x] Mate targets obey ADR-004 reference rules — enforced by the type: a
  `FaceSignature` has no field it could occupy in a `MateTarget`
* [x] Reject invalid component/reference combinations
* [x] Validate dimensional values and units
* [x] Missing mate targets become unresolved, never silently rebound
* [x] Mate dependencies participate correctly in the document graph
* [x] Invalid mate creation/modification is atomic and recoverable
* [x] Save/load preserves mate engineering intent
* [x] Deterministic mate representation validated
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence in `docs/verification/P13-MATE-001/`

### Gate

```text
mate model correct
+ all 7 basic constraints represented
+ reference semantics correct
+ units/dimensions correct
+ unresolved-state behavior correct
+ dependencies correct
+ persistence PASS
+ determinism PASS
+ failure atomicity PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

Met: 1349/1349 on `debug`, `release` and `debug-shared` from clean, and
771/771 five times over in `release` and `debug`; 0 compiler warnings; the
adversarial review found 2 gaps and 0 production defects, closing both with
regression tests; the qualified tree IDs match the committed tree.

Carried forward: nothing is solved. A mate is intent, and no mate is checked
for whether it can be satisfied — over-constraint, redundancy and degrees of
freedom are `P13-SOLVE-001`.

```text
P13-MATE-001 → [x]
Next → P13-SOLVE-001
```

---

# NEXT — P13-SOLVE-001

## Assembly Constraint Solver

Solve mate intent into component transforms.

```text
P13-MATE-001   stores assembly intent
P13-SOLVE-001  solves that intent into derived component transforms
```

A solved transform is **derived state**: recomputed by regeneration, held
beside the bodies keyed by `ComponentId`, dropped when a component's
regeneration fails exactly as a body is, and **never written to `.bcad`**.

### What ADR-005 already binds

[ADR-005](docs/architecture/decisions/ADR-005-placement-is-intent-transforms-are-derived.md)
decided this milestone's hardest constraints while rejecting the easier
options, and says so in terms. They are not open questions:

* **No seed, no warm start.** ADR-005 considered persisting the last solved
  transform as a solver seed — what the sketch solver actually does — and
  rejected it. The solver must converge **from placement intent alone**.
  ADR-005 calls this "the honest cost" and "a real constraint on
  `P13-SOLVE-001`": intent must be a good enough starting point, and the
  solver must be robust from it.
* **Why the seed was rejected**, so it is not quietly reintroduced: a seeded
  solve depends on save history, so two documents with identical intent could
  solve differently. `P12-PARAM-002` measured warm-starting's path-dependence
  at **1.3e-15** per configuration cycle — bounded, but real enough to be a
  recorded limitation. Repeating a known wart in a subsystem designed from
  scratch would be choosing it deliberately.
* **Deterministic start and deterministic iteration order.** The dependency
  graph already breaks ties by ascending ID; ADR-005 requires "the assembly
  solve must be equally order-free".
* **Grounding is reported, never assumed.** At least one component must be
  grounded or the assembly is free to translate and rotate as a whole —
  "an under-constrained state the solver must *report*, not silently pin".
  Grounding is the `Fixed` mate from `P13-MATE-001`; there is no component
  flag and none is needed.

### Five states, never a boolean

`CLAUDE.md` requires sketch solving to distinguish under-constrained, fully
constrained, over-constrained, inconsistent and solver failure, and to report
conflicts, redundancy, unresolved DOF and residuals. The assembly solver is
held to the same standard: a solve that failed and a solve that succeeded
into an under-constrained assembly are different answers, and collapsing
either into `false` loses the one thing the engineer needs.

* [ ] Define solver input/output contracts
* [ ] Convert mate intent into a constraint problem
* [ ] Implement component DOF representation
* [ ] Implement residual evaluation for all 7 basic mate types
* [ ] Implement Jacobian / derivative path
* [ ] Implement nonlinear solve loop
* [ ] Apply solved transforms as derived state only
* [ ] Preserve canonical component placement intent
* [ ] Detect fully constrained assemblies
* [ ] Detect under-constrained assemblies / remaining DOF
* [ ] Detect inconsistent / over-constrained systems
* [ ] Detect redundant constraints where feasible
* [ ] Validate convergence criteria and tolerances
* [ ] Validate deterministic solver results
* [ ] Validate failure atomicity / no partial solved state
* [ ] Independently validate analytic assembly cases
* [ ] Validate unresolved mate/reference handling
* [ ] Adversarial review PASS
* [ ] Debug / Release / Debug-shared regression PASS
* [ ] Evidence in `docs/verification/P13-SOLVE-001/`

### Gate

```text
solver model correct
+ all 7 basic mate types solved
+ residuals/Jacobian validated
+ DOF classification correct
+ under/fully/over-constrained behavior correct
+ canonical/derived state separation preserved
+ convergence validated
+ independent analytical cases PASS
+ failure atomicity PASS
+ determinism PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

ADR-005 also states what verifying it looks like: a saved and reloaded
assembly solves to the same transforms as one built in memory; no transform
appears in the `.bcad` JSON; a parameter change moves a component and
restoring the parameter restores the transform; solving twice gives
identical transforms; and an assembly with no grounded component reports an
under-constrained state rather than choosing one.

Tolerances need a reason, and a Jacobian is not well-conditioned algebra:
expect to justify the convergence tolerance against the conditioning of the
system rather than inheriting `1e-12` from the transform tests.

Only then:

```text
P13-SOLVE-001 → [x]
Next → P13-MATE-002
```

---

# Planned P13 Sequence

```text
P13-ARCH-001     Assembly architecture and contracts          DONE
P13-COMP-001     Component definitions and instances          DONE
P13-XFORM-001    Component transforms                         DONE
P13-REF-001      Internal / external reference infrastructure DONE
P13-MATE-001     Basic assembly constraints                   DONE
P13-SOLVE-001    Assembly constraint solver                   OPEN
P13-MATE-002     Mechanical mates
P13-CONF-001     Assembly configurations / suppression
P13-STREF-001    Stable assembly references
P13-REGEN-001    Dependency / regeneration
P13-CMD-001      Commands / undo / redo
P13-PERSIST-001  Save / load assembly intent
P13-CLI-001      Headless assembly workflows
P13-STEP-001     Assembly STEP export / read-back
P13-REFMOD-001   Production assembly reference models
P13-QUAL-001     Full P13 qualification
```

Do not implement a milestone until its predecessor passes.

---

# Accepted P13 Constraints

* Assemblies currently operate inside one `Document`.
* Cross-document dependencies are not implemented.
* Future external references use stable identity/resolver concepts, not filesystem-path identity.
* Component configuration remains document-global for now.
* A component cannot independently select another part configuration yet.
* Mate targets may only use reference types allowed by ADR-004.
* Missing intended geometry must fail explicitly; never rebind to nearest geometry.
* Solved assembly positions are derived state and are not authoritative persisted intent.
* Opening/regenerating an assembly may therefore require a solve.

---

# Deferred CAD Work

Not currently authorized:

* Fillet setback controls
* Selectable fillet corner transitions
* Loft end conditions
* Full semantic topology
* STEP import
* DXF / IGES / OBJ interoperability

---

# Future Phases

```text
P14  Technical Drawings
P15  Materials / Engineering Data
P16  Meshing
P17  Structural FEA
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

Do not start without explicit authorization.

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
→ authorized/current work

ROADMAP.md
→ long-term direction

ARCHITECTURE.md
→ system architecture and invariants

CLAUDE.md
→ engineering process and Definition of Done

docs/architecture/decisions/
→ durable architectural decisions

docs/verification/<milestone>/
→ implementation and qualification evidence

docs/engineering/
→ reusable engineering templates
```
