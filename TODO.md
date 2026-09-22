# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.
> Qualification milestones also require the final qualified tree to match the committed tree.
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

## Status

```text
Current:   —  awaiting authorization for the next phase
Next:      —  a scope decision, not Claude's to make
Then:      —

Released:  v0.1.0 — P0–P10
Qualified: P11, P12, P13

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
* [x] `P13-REFMOD-001` — Production assembly reference models

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
docs/verification/P13-REFMOD-001/
```

---

# DONE — P13-REFMOD-001

## Production Assembly Reference Models

* [x] Define production reference-model suite — eight models, RM-A .. RM-H, catalogued and committed beside the twelve part models; the same builder, the same fixed document IDs, the same example program. No parallel framework
* [x] Add simple grounded two-component assembly — RM-A, both plates grounded, so every expected value is the placement itself
* [x] Add fully constrained multi-component assembly — RM-B, 12 unknowns against 12 equations, two instances of one part, both arms landing where the arithmetic says
* [x] Add under-constrained reference assembly — RM-C, a concentric mate leaving exactly the slide and the turn, with the off-axis offset removed
* [x] Add mechanical-mate reference assembly — RM-D, all four joints, each displaced in a freedom it keeps **and** one it must remove, so a joint behaving like another is caught by what survived
* [x] Add configuration / suppression reference assembly — RM-E, three configurations; `Loose` suppresses a **mate** rather than a component, which is the distinction a configuration has to keep
* [x] Add stable-reference / regeneration reference assembly — RM-F, a lid on a named face: drive `body_h` and the lid follows, restore it and the lid comes back; remove the feature and it fails explicitly
* [x] Add mixed production-scale assembly — RM-G, 8 components, 4 parts, 31 mates, both mate kinds, a named-face reference, repeated instances, two configurations
* [x] Validate deterministic regenerate / solve results — bit for bit, and identical across all three presets
* [x] Validate save → load → regenerate → solve — from the committed files; bytes equal on re-save, `equivalent()` on the documents, positions equal bit for bit
* [x] Validate CLI workflows on committed models — in process **and** as a real process; exit codes follow the document, so an unsolvable assembly is a non-zero exit with its conflicting mates named
* [x] Validate STEP export / read-back on committed models — through an independent reader; products and instances counted separately, totals against closed forms, and suppressed components measurably absent
* [x] Validate expected DOF / placements independently — every count derived from the qualified mate equation table before it was measured; two placement **conventions** were wrong and the measurement is what found them
* [x] Validate failure/reference cases — RM-H is committed BROKEN: inconsistent, both mates named, nothing published, and the export and CLI provably unable to pretend otherwise
* [x] Adversarial review PASS — 7 findings, all resolved; 0 in previously qualified code
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence in `docs/verification/P13-REFMOD-001/`

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

Met: 1674/1674 on `debug`, `release` and `debug-shared`, each from clean, and
986/986 five times over in `release` and `debug`; 0 compiler warnings in all
three builds; 14/14 qualification stages exit 0; all 43 new tests confirmed by
name in every ctest log; the qualified source trees identical before and after.

**`src/`, `include/` and `apps/` were not touched.** Their tree hashes are the
ones `P13-STEP-001` qualified. Eight real assemblies now go end to end —
built, configured, solved, saved, loaded, driven from a command line and
exported to STEP — on capabilities that were already qualified, and the engine
needed no change to carry them. The two conventions that turned out to be
wrong were wrong in the new models, not in the solver.

Worth carrying forward: a principal plane's normal is the cross product of its
axes, so the XZ plane faces **−Y** and a positive distance across it moves a
component to negative y. That put every located component on the wrong side of
its deck while every test still passed, and it was caught by reading the
measured positions against what the assembly was supposed to look like, not by
a failure. Two other capabilities had no model behind them at all — a
parameter-driven placement, and commands with undo — and the coverage matrix
is what made that obvious.

```text
P13-REFMOD-001 → [x]
Next → P13-QUAL-001
```

---

---

# DONE — P13-QUAL-001

## Full P13 Qualification

* [x] Freeze final P13 source/test tree — `9cd2330`, the tree `P13-REFMOD-001` qualified, byte for byte; recorded before the first build, after the last test run, and after the documentation
* [x] Audit all P13 milestone evidence — 15/15 PASS; all 17 quoted commit hashes resolve and every recorded tree ID matches its implementation commit
* [x] Verify all P13 TODO items complete — the only `[ ]` in the file were this milestone's own
* [x] Verify all ADR contracts satisfied — ADR-002 … ADR-009, each with its implementation and the test that pins it. ADR-006's two mandated document updates had never been made; done here
* [x] Run clean Debug qualification — 1674/1674, 0 warnings, 437 TUs from a 454-file clean
* [x] Run clean Release qualification — 1674/1674, 0 warnings
* [x] Run clean Debug-shared qualification — 1674/1674, 0 warnings, links clean
* [x] Run repeated determinism qualification — the whole suite `--repeat until-fail:5` in release and debug, 1674/1674 each; the three presets agree byte for byte on every solve
* [x] Validate production reference models — 83/83 per preset; all 8 byte-identical to their builders, every DOF re-derived on paper here before it was measured
* [x] Validate persistence round trips — from the committed files; bytes equal on re-save, derived state recomputed identically; no `transform` or `solved` key in any model
* [x] Validate assembly solver / DOF classification — all five states represented and distinguished; RM-H's rank-deficient DOF of 5 confirmed against the rank argument
* [x] Validate configuration / suppression behavior — A → B → A byte-identical; suppressed components measurably absent from the export
* [x] Validate stable references / recovery — including an identical face offered in the target's place and refused
* [x] Validate regeneration / failure propagation — every trigger, and the all-or-nothing publication
* [x] Validate undo / redo workflows — four edits unwound one at a time with canonical state checked at every position, then replayed
* [x] Validate CLI end-to-end workflows — 121/121 per preset, real process per step; a failed edit and a half-failed batch both leave the file byte-identical
* [x] Validate STEP export / read-back — occurrence counts from the file itself; RM-H writes nothing; `Bare` writes 3 of 8
* [x] Run final adversarial review — 13 findings, **0 production defects**; 3 fixed, 10 recorded
* [x] Confirm 0 unexpected compiler warnings — 0 literal occurrences in all three build logs
* [x] Confirm qualified tree == committed tree — 8/8 tree IDs identical across three recordings
* [x] Record final evidence in `docs/verification/P13-QUAL-001/`
* [x] Mark P13 qualified

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

Met: 1674/1674 on `debug`, `release` and `debug-shared`, each from clean, and
1674/1674 five times over in `release` and in `debug`; 0 compiler warnings in
all three builds; 17/17 qualification stages exit 0; 271 further checks per
preset against expectations derived on paper, identical in all three; the
eight qualified tree IDs recorded three times and identical every time.

The code came through clean: **0 production defects** in an adversarial
review that worked through the integration-failure list one item at a time and
probed each against the implementation rather than against a passing suite.
Everything the review found was documentation, and the three worth fixing were
fixed here — `ARCHITECTURE.md` and `docs/architecture.md` still published the
layer table `ADR-006` replaced fifteen milestones ago, and both were named in
that ADR's own Consequences as must-updates; `ADR-003` still said `P13` would
build no external-reference field, which `P13-REF-001` then did under this
file's authority.

Worth carrying forward: the phase's hardest guarantee — that a solved
transform never becomes persisted intent — is held by the type system rather
than by care. `assembly::solve()` takes a `const Document&`, so it cannot
write to the document even by mistake; the serializer whitelists three keys;
and the resolved transform is a function with no store, so there is no cache
to leak. Three tests pin it by grepping the saved bytes, but the reason it
holds is the shape, not the tests.

```text
P13-QUAL-001 → [x]
P13 → QUALIFIED
Next → STOP; the next phase is a scope decision
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
ADR-009 — The CLI edit is a document transaction
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
