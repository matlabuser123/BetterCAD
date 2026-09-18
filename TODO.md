# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially
> reviewed + regression clean + evidence recorded; a milestone with a
> qualification gate also needs a qualified final tree. The full definition is
> in [CLAUDE.md](CLAUDE.md#definition-of-done), which this line summarises.
> `[ ]` = incomplete.
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

## Status

```text
Current: None — P12 qualified
Next:    Explicit scope decision required

Released:       v0.1.0 — P0–P10
Qualified:      P11, P12
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

# DONE — P12-REF-001

## Production Reference Models

Build realistic mechanical parts using the complete P12 feature set.
Six parts, in `examples/reference_models/`; evidence in
[docs/verification/P12-REF-001/](docs/verification/P12-REF-001/README.md).

* [x] Create production reference-model suite
* [x] Cover all applicable P12 capabilities
* [x] Include configuration-driven part family
* [x] Exercise cross-feature dependencies
* [x] Exercise stable face references
* [x] Validate geometry independently
* [x] Validate configuration changes
* [x] Validate parameter regeneration
* [x] Validate failure/recovery paths
* [x] Validate undo/redo
* [x] Validate save/load/regenerate
* [x] Validate deterministic rebuilds
* [x] Validate STEP read-back
* [x] Validate CLI workflows
* [x] Run full regression
* [x] Record evidence in `docs/verification/P12-REF-001/`

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

Met: 1259/1259 on `debug`, `release` and `debug-shared` from clean, 0
warnings; the adversarial review found and fixed 8 defects, each with a
regression test; the qualified tree IDs match the committed tree.

```text
P12-REF-001 → [x]
```

---

# DONE — P12-QUAL-001

## P12 Phase Qualification

Freeze the final P12 implementation and qualify the entire phase.
Qualified on `15d7f75`; evidence in
[docs/verification/P12-QUAL-001/](docs/verification/P12-QUAL-001/README.md).

* [x] Freeze final source tree
* [x] Clean Debug build
* [x] Clean Release build
* [x] Clean Debug-shared build
* [x] Full test suite — Debug
* [x] Full test suite — Release
* [x] Full test suite — Debug-shared
* [x] 0 unexpected compiler warnings
* [x] P0–P11 regression unchanged
* [x] All P12 milestone tests PASS
* [x] Production reference models PASS
* [x] Independent geometry validation PASS
* [x] Failure-path validation PASS
* [x] Persistence validation PASS
* [x] Determinism validation PASS
* [x] Cross-preset comparison PASS
* [x] CLI smoke PASS
* [x] STEP export/read-back PASS
* [x] Documentation consistent with implementation
* [x] Final evidence in `docs/verification/P12-QUAL-001/`
* [x] Qualified tree == committed tree
* [x] Commit and push qualification closeout

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

`CLAUDE.md` tracks **how development work is performed**, and
`docs/engineering/` holds the templates it is worked through.

`README.md` explains **what BetterCAD is**.
