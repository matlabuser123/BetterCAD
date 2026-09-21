# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.
> Qualification milestones also require the final qualified tree to match the committed tree.
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

## Status

```text
Current:   P13 — Assemblies
Next:      P13-REFMOD-001 — Production assembly reference models
Then:      P13-QUAL-001 — Full P13 qualification

Released:  v0.1.0 — P0–P10
Qualified: P11, P12

P13 architecture:
ADR-002 → ADR-008
```

---

# Qualified Baseline

## P12 — Parametric CAD Completion

* [x] `P12-PARAM-001` — Parameter expressions
* [x] `P12-SKETCH-001` — Advanced sketch constraints
* [x] `P12-SKETCH-002` — Ellipse and spline entities
* [x] `P12-DATUM-001` — Datum planes / axes / coordinate systems
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
* [x] `P12-PARAM-002` — Design equations / configurations
* [x] `P12-REF-001` — Production reference models
* [x] `P12-QUAL-001` — Full P12 qualification

```text
Evidence: docs/verification/P12-*/
Qualified commit: 15d7f75
```

---

# P13 — Assemblies

## Completed

* [x] `P13-ARCH-001` — Assembly architecture and contracts
* [x] `P13-COMP-001` — Component definitions and instances
* [x] `P13-XFORM-001` — Component transforms
* [x] `P13-REF-001` — Internal / external reference infrastructure
* [x] `P13-MATE-001` — Basic assembly constraints
* [x] `P13-SOLVE-001` — Assembly constraint solver
* [x] `P13-MATE-002` — Mechanical mates
* [x] `P13-CONF-001` — Assembly configurations / suppression
* [x] `P13-STREF-001` — Stable assembly references
* [x] `P13-REGEN-001` — Dependency / regeneration
* [x] `P13-CMD-001` — Commands / undo / redo
* [x] `P13-PERSIST-001` — Save / load assembly intent
* [x] `P13-CLI-001` — Headless assembly workflows
* [x] `P13-STEP-001` — Assembly STEP export / read-back

Evidence:

```text
docs/verification/P13-ARCH-001/
docs/verification/P13-COMP-001/
docs/verification/P13-XFORM-001/
docs/verification/P13-REF-001/
docs/verification/P13-MATE-001/
docs/verification/P13-SOLVE-001/
docs/verification/P13-MATE-002/
docs/verification/P13-CONF-001/
docs/verification/P13-STREF-001/
docs/verification/P13-REGEN-001/
docs/verification/P13-CMD-001/
docs/verification/P13-PERSIST-001/
docs/verification/P13-CLI-001/
docs/verification/P13-STEP-001/
```

---

# CURRENT — P13-REFMOD-001

## Production Assembly Reference Models

* [ ] Define production reference-model suite
* [ ] Add simple grounded two-component assembly
* [ ] Add fully constrained multi-component assembly
* [ ] Add under-constrained reference assembly
* [ ] Add mechanical-mate reference assembly
* [ ] Add configuration / suppression reference assembly
* [ ] Add stable-reference / regeneration reference assembly
* [ ] Add mixed production-scale assembly
* [ ] Validate deterministic regenerate / solve results
* [ ] Validate save → load → regenerate → solve
* [ ] Validate CLI workflows on committed models
* [ ] Validate STEP export / read-back on committed models
* [ ] Validate expected DOF / placements independently
* [ ] Validate failure/reference cases
* [ ] Adversarial review PASS
* [ ] Debug / Release / Debug-shared regression PASS
* [ ] Evidence in `docs/verification/P13-REFMOD-001/`

### Gate

```text
representative assembly suite complete
+ basic mates covered
+ mechanical mates covered
+ configurations/suppression covered
+ stable references covered
+ regeneration covered
+ persistence covered
+ CLI covered
+ STEP read-back covered
+ expected geometry/DOF independently validated
+ determinism PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

---

# NEXT — P13-QUAL-001

## Full P13 Qualification

* [ ] Freeze final P13 source/test tree
* [ ] Audit all P13 milestone evidence
* [ ] Verify all P13 TODO items complete
* [ ] Verify all ADR contracts satisfied
* [ ] Run clean Debug qualification
* [ ] Run clean Release qualification
* [ ] Run clean Debug-shared qualification
* [ ] Run repeated determinism qualification
* [ ] Validate production reference models
* [ ] Validate persistence round trips
* [ ] Validate assembly solver / DOF classification
* [ ] Validate configuration / suppression behavior
* [ ] Validate stable references / recovery
* [ ] Validate regeneration / failure propagation
* [ ] Validate undo / redo workflows
* [ ] Validate CLI end-to-end workflows
* [ ] Validate STEP export / read-back
* [ ] Run final adversarial review
* [ ] Confirm 0 unexpected compiler warnings
* [ ] Confirm qualified tree == committed tree
* [ ] Record final evidence in `docs/verification/P13-QUAL-001/`
* [ ] Mark P13 qualified

### Gate

```text
all P13 milestones PASS
+ all evidence complete
+ all production reference models PASS
+ Debug PASS
+ Release PASS
+ Debug-shared PASS
+ determinism PASS
+ adversarial review PASS
+ 0 unexpected warnings
+ final qualified tree == committed tree
```

---

# Accepted P13 Constraints

* Assemblies currently operate inside one `Document`.
* Cross-document dependencies are not implemented.
* External-reference identity uses stable identity/resolver concepts, not filesystem paths.
* One document-global configuration system is used.
* Components cannot independently select separate part configurations yet.
* Mate targets use only ADR-004-qualified reference types.
* Missing intended geometry fails explicitly; never silently rebind.
* Solved component transforms are derived state, not canonical persisted intent.
* Assembly regeneration may require a solve.
* Assembly solving is document-level/global rather than per-component.
* Derived assembly state is published atomically.
* STEP read-back remains verification infrastructure, not general STEP import.

---

# P13 Architecture Decisions

```text
ADR-002 — Assembly/document model
ADR-003 — Internal/external reference contract
ADR-004 — Mate-reference semantics
ADR-005 — Placement intent / derived transforms
ADR-006 — Module layering
ADR-007 — One configuration system
ADR-008 — Assembly solve as regeneration final pass
```

---

# Deferred CAD Work

Not currently authorized:

* Fillet setback controls
* Selectable fillet corner transitions
* Loft end conditions
* Full semantic topology
* General STEP import
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
→ current authorized work

ROADMAP.md
→ long-term direction

ARCHITECTURE.md
→ system architecture / invariants

CLAUDE.md
→ engineering process / Definition of Done

docs/architecture/decisions/
→ durable architecture decisions

docs/verification/<milestone>/
→ milestone evidence

docs/engineering/
→ reusable engineering templates
```
