# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + evidence recorded.
> `[ ]` = incomplete.
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

## Status

```text
Current: P12 — Parametric CAD Completion
Next:    P12-REF-001 — Production Reference Models
Then:    P12-QUAL-001 — Phase Qualification

Released:       v0.1.0 — P0–P10
Qualified:      P11
P12:            In progress
```

---

# P12 — Parametric CAD Completion

## Completed

* [x] `P12-PARAM-001` — Parameter expressions
* [x] `P12-SKETCH-001` — Advanced sketch constraints
* [x] `P12-SKETCH-002` — Ellipse and spline entities
* [x] `P12-DATUM-001` — Datum planes, axes and coordinate systems
* [x] `P12-STREF-001` — Stable feature-face references
* [x] `P12-SKETCH-003` — Sketches on planar feature faces

### Features

* [x] `P12-FEAT-001` — Through-all extrude
* [x] `P12-FEAT-002` — Split body / combine
* [x] `P12-FEAT-003` — Shell
* [x] `P12-FEAT-004` — Draft
* [x] `P12-FEAT-005` — Rib
* [x] `P12-FEAT-006` — Variable-radius fillet

Deferred:

* Fillet setback controls
* Selectable fillet corner transitions

### Production Modeling

* [x] `P12-HOLE-001` — Threads, spotface, standard sizes and tolerance classes
* [x] `P12-PATTERN-001` — Advanced patterns
* [x] `P12-SWEEP-001` — Guide curves, twist and non-planar paths
* [x] `P12-LOFT-001` — Differing section shapes and smooth interpolation

Deferred:

* Loft end conditions

### Parametric Design

* [x] `P12-PARAM-002` — Design equations and configurations

---

# NEXT — P12-REF-001

## Production Reference Models

Build realistic mechanical parts using the complete P12 feature set.

* [ ] Create production reference-model suite
* [ ] Cover all applicable P12 capabilities
* [ ] Include configuration-driven part family
* [ ] Exercise cross-feature dependencies
* [ ] Exercise stable face references
* [ ] Validate geometry independently
* [ ] Validate configuration changes
* [ ] Validate parameter regeneration
* [ ] Validate failure/recovery paths
* [ ] Validate undo/redo
* [ ] Validate save/load/regenerate
* [ ] Validate deterministic rebuilds
* [ ] Validate STEP read-back
* [ ] Validate CLI workflows
* [ ] Run full regression
* [ ] Record evidence in `docs/verification/P12-REF-001/`

### Gate

```text
All reference models valid
+ independent validation PASS
+ persistence PASS
+ determinism PASS
+ stable references PASS
+ CLI/STEP PASS
+ full regression PASS
+ 0 unexpected warnings
```

Only then:

```text
P12-REF-001 → [x]
```

---

# THEN — P12-QUAL-001

## P12 Phase Qualification

Freeze the final P12 implementation and qualify the entire phase.

* [ ] Freeze final source tree
* [ ] Clean Debug build
* [ ] Clean Release build
* [ ] Clean Debug-shared build
* [ ] Full test suite — Debug
* [ ] Full test suite — Release
* [ ] Full test suite — Debug-shared
* [ ] 0 unexpected compiler warnings
* [ ] P0–P11 regression unchanged
* [ ] All P12 milestone tests PASS
* [ ] Production reference models PASS
* [ ] Independent geometry validation PASS
* [ ] Failure-path validation PASS
* [ ] Persistence validation PASS
* [ ] Determinism validation PASS
* [ ] Cross-preset comparison PASS
* [ ] CLI smoke PASS
* [ ] STEP export/read-back PASS
* [ ] Documentation consistent with implementation
* [ ] Final evidence in `docs/verification/P12-QUAL-001/`
* [ ] Qualified tree == committed tree
* [ ] Commit and push qualification closeout

### Gate

```text
P12 = implemented
    + integrated
    + tested
    + independently validated
    + deterministic within documented contracts
    + regression clean
    + evidence recorded
```

Only then:

```text
P12-QUAL-001 → [x]
P12 → QUALIFIED
```

---

# Known Deferred Work

Not part of P12:

* [ ] Fillet setback controls
* [ ] Fillet corner-transition controls
* [ ] Loft end conditions
* [ ] Full semantic topology
* [ ] STEP import
* [ ] DXF / IGES / OBJ interoperability
* [ ] Assemblies
* [ ] Technical drawings
* [ ] Materials
* [ ] Meshing
* [ ] FEA
* [ ] Thermal analysis
* [ ] CFD integration
* [ ] Optimization
* [ ] Python API
* [ ] Plugin system
* [ ] Version control / collaboration
* [ ] AI engineering agent
* [ ] CAM
* [ ] Production hardening

These require explicit future scope authorization.

---

# Known Limitations

* Edge references are not yet full semantic-topology references.
* Some geometry-moving edits may invalidate geometric edge references.
* Missing semantic geometry fails rather than binding to another face.
* Variable-radius fillets support only the independently verified safe subset.
* Fillet setback/corner controls are deferred.
* Loft end conditions are deferred.
* Configuration switching restores parameter state exactly; geometry may show bounded last-bit variation from the existing sketch-solver warm start.
* STEP is currently an export/validation path, not full import interoperability.

---

# Future Phases

Do not start without explicit authorization.

```text
P13  Assemblies
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

Exact milestone IDs and scope are allocated only when authorized.

---

# Workflow

For every milestone:

```text
Inspect
→ Implement
→ Targeted tests
→ Independent validation
→ Failure paths
→ Persistence
→ Determinism
→ Full regression
→ Evidence
→ [x]
→ Commit
→ Push
→ Next
```

If a gate fails:

```text
STOP
→ reproduce
→ root cause
→ regression test
→ fix
→ revalidate
```

Never mark work complete because it merely compiles.

---

# Evidence

Detailed results belong in:

```text
docs/verification/<milestone>/
```

`TODO.md` tracks **what is next**.

`ROADMAP.md` tracks **where BetterCAD is going**.

`ARCHITECTURE.md` tracks **how BetterCAD is built**.

`CLAUDE.md` tracks **how development work is performed**.

`README.md` explains **what BetterCAD is**.
