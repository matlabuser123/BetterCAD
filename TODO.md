# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.
> Qualification milestones require the final qualified tree to match the committed tree.
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

## Status

```text
Current:   P14 — Technical Drawings
Next:      P14-VIEW-001 — Base and projected drawing views
Then:      P14-VIEW-002 — Section / detail / auxiliary views

Released:  v0.1.0 — P0–P10
Qualified: P11, P12, P13
```

Completed milestones and their evidence live in [ROADMAP.md](ROADMAP.md).

---

# P14 — Technical Drawings

## Goal

Produce manufacturing-ready 2D drawings from BetterCAD parts and assemblies while preserving stable links to the 3D model.

```text
3D model
→ drawing document
→ sheets
→ projected views
→ annotations
→ dimensions
→ sections/details
→ BOM/balloons
→ export/print
```

Drawing geometry is **derived from model intent**.

Do not duplicate or replace the authoritative 3D model.

---

# P14 Sequence

```text
P14-ARCH-001     Drawing architecture and contracts
P14-SHEET-001    Drawing document / sheets / formats
P14-VIEW-001     Base and projected drawing views
P14-VIEW-002     Section / detail / auxiliary views
P14-HLR-001      Hidden-line / visible-edge generation
P14-DIM-001      Linear / angular / radial / diameter dimensions
P14-ANNO-001     Notes / symbols / centerlines / centermarks
P14-TOL-001      Tolerances / fits / GD&T foundation
P14-ASM-001      Assembly drawing views
P14-BOM-001      BOM tables / item balloons
P14-STREF-001    Stable drawing-to-model references
P14-REGEN-001    Drawing regeneration / model-change propagation
P14-CMD-001      Drawing commands / undo / redo
P14-PERSIST-001  Save / load drawing intent
P14-CLI-001      Headless drawing workflows
P14-EXPORT-001   PDF / SVG / DXF export
P14-REFMOD-001   Production drawing reference models
P14-QUAL-001     Full P14 qualification
```

Do not implement a milestone until its predecessor passes.

---

# DONE — P14-ARCH-001

## Drawing Architecture and Contracts

* [x] Define drawing document ownership model — ADR-010: drawing objects are `DocumentObject`s in the model's own document; a separate drawing document needs cross-document *execution*, which does not exist
* [x] Define canonical vs derived drawing state — ADR-011: intent persisted, every projected curve and every measured value derived and never written
* [x] Define sheet / view / annotation identity model — ADR-017: four `DocumentObject` kinds, because a dimension's dependency on model geometry must be a graph edge; tables and balloons deliberately not allocated yet
* [x] Define model-to-drawing stable-reference contract — ADR-012: ADR-004's vocabulary only. There is **no stable edge name** in the codebase, and `EdgeSignature` breaks on the ordinary parametric edit
* [x] Define units / scale / coordinate conventions — ADR-013: view normal faces the viewer, so the principal frames *are* Front/Right/Top; scale is an exact `paper:model` pair. 26/26 verified
* [x] Define drawing regeneration contract — ADR-014: objects get handlers, geometry is built on demand. The final-pass mechanism cannot carry a drawing's result and its ordering is alphabetical coincidence
* [x] Define drawing module layering — ADR-015: `drawing` 4, `io` → 5, renderer/scripting → 6; projection in `core/geometry`. Second forced renumber
* [x] Define export architecture — ADR-016: one neutral self-validating scene; writers transcribe and compute nothing
* [x] Record architecture decisions as ADRs — ADR-010 … ADR-017, plus a supersession note on ADR-006
* [x] Architecture adversarial review PASS — 19 questions, 5 findings, 0 blocking
* [x] Evidence in `docs/verification/P14-ARCH-001/`

### Gate

```text
drawing architecture coherent
+ canonical/derived separation clear
+ stable model references defined
+ coordinate/scale conventions fixed
+ regeneration contract defined
+ module layering valid
+ export boundary defined
+ ADRs complete
```

Met: eight ADRs, each against two or three serious candidates, and no
executable source or test file changed — the eight qualified tree IDs are the
ones `P13-QUAL-001` recorded.

Four findings from reading the code changed what was designed rather than
confirming it. **There is no stable edge reference in this codebase** —
`EdgeSignature` matches a curve, and its own header says it breaks when the
curve moves, which is the ordinary parametric edit a drawing exists to track.
ADR-004 already refuses `FaceSignature` on that reasoning, so ADR-012 refuses
`EdgeSignature` too and accepts a real capability gap instead of a silent wrong
answer. **The final-pass mechanism cannot carry a drawing's result** — its
return type is `std::map<ComponentId, RigidTransform3D>` and passes run in
alphabetical name order, so a drawing pass would follow the assembly solve by
coincidence; ADR-014 avoids the mechanism entirely. **The layer table has no
room again**, for the second time. And **a drawing subsystem is three places,
not one**: projection behind the OCCT adapter at layer 0, the domain at 4, the
writers at 5.

Worth carrying forward: the standard views are not a new convention. Under
"the view frame's normal points at the viewer", `Frame3D::xz()`, `yz()` and
`xy()` *are* Front, Right and Top — and that `xz()` faces **−Y**, the fact that
put every component on the wrong side of its deck in `P13-REFMOD-001`, is the
same fact that makes a Front view come out right here.

```text
P14-ARCH-001 → [x]
Next → P14-SHEET-001
```

---

# DONE — P14-SHEET-001

## Drawing Documents / Sheets / Formats

* [x] Apply the ADR-015 layer renumber — `drawing` 4, `io` 5, renderer/scripting 6; no existing file's includes had to change
* [x] Update `ARCHITECTURE.md` **and** `docs/architecture.md` with the new table — and `CLAUDE.md`, found stale by two renumbers
* [x] Create the `drawing` module per ADR-015 — links `core` only, contains no OCCT; a new checker fixture fails the build if `drawing` ever includes `io`
* [x] Implement strong sheet ID — `SheetId` widens to `ObjectId`; two compile-failure cases pin what it is not. **No `DrawingId`:** ADR-017 defines none
* [x] Implement sheet creation/deletion — through the ordinary document object lifecycle; a rejected sheet consumes no ID
* [x] Implement standard sheet sizes — ISO 216 A0–A4, the standard's own rounded values, checked against the halving property
* [x] Implement portrait / landscape orientation — format and orientation stored, size derived, so a round trip cannot drift
* [x] Implement drawing units and scale — an exact `paper:model` pair, so `1:3` survives and `2:4` stays distinct from `1:2`
* [x] Implement margins / borders — four lengths; the usable region is derived and margins that leave no room are refused
* [x] Implement title-block data model — eight semantic fields; the scale text, sheet number and count are derived, never stored
* [x] Validate multiple sheets per drawing — deleting the middle sheet moves every number and no ID
* [x] Validate deterministic sheet geometry — bit-identical across 5 formats × 2 orientations, and across a file round trip
* [x] Validate save/load — existing envelope, no new key, no version bump; 11 malformed files refused; all 32 committed models still load
* [x] Adversarial review PASS — 16 questions, 4 findings, 3 production defects, all fixed
* [x] Regression PASS — 1720/1720 on three presets from clean, 0 warnings, 17/17 stages exit 0
* [x] Evidence recorded — `docs/verification/P14-SHEET-001/`

### Gate

```text
sheet model correct
+ identity stable
+ format geometry correct
+ units/scale correct
+ multi-sheet behavior correct
+ persistence PASS
+ determinism PASS
```

Met: 46 new tests; 1720/1720 on `debug`, `release` and `debug-shared`, each
from clean; 539/539 five times over in `release` and `debug`; 0 compiler
warnings in all three builds; 17/17 stages exit 0; the no-op rebuild compiled
0 and linked 0 in every preset.

Three production defects, found three different ways. **The shared build did
not link** — `DrawingScale::label()` was defined out-of-line on a struct with
no export macro, so it was hidden under `-fvisibility=hidden`. Debug passed,
Release passed, both their full suites passed; only `debug-shared` caught it,
which is the entire argument for keeping it a hard gate. **`sheetNumber()` and
`sheetCount()` were `noexcept` while calling a function that allocates**, so
an allocation failure would have terminated the process rather than
propagating — found by reading the diff, not by a test. And **`Sheet::size()`
dereferenced an optional unchecked**, unreachable today and undefined
behaviour the day a format is added without a size.

A fourth was caught by a test doing its job: the writer dispatch arm was an
early `return` instead of a branch in the chain, so a sheet's data replaced
the `{id, type, name, data}` envelope rather than filling it.

Worth carrying forward: what is stored is the **format and the orientation**,
never a width and a height. That is why portrait → landscape → portrait is
bit-identical rather than nearly so — there is nothing stored that could
drift. The same reasoning made the scale an exact pair instead of a double.

```text
P14-SHEET-001 → [x]
Next → P14-VIEW-001
```

---

# P14-VIEW-001

## Base / Projected Views

* [ ] Implement base drawing view
* [ ] Implement Front / Top / Right / Left / Bottom / Rear orientations
* [ ] Implement projected views
* [ ] Implement arbitrary isometric view
* [ ] Implement per-view scale
* [ ] Implement view placement on sheet
* [ ] Preserve model-to-view identity
* [ ] Validate orthographic projection mathematically
* [ ] Validate projected-view alignment
* [ ] Validate part and assembly geometry
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
projection mathematically correct
+ orientation conventions correct
+ scale correct
+ view alignment correct
+ model references stable
+ deterministic output
```

---

# P14-VIEW-002

## Section / Detail / Auxiliary Views

* [ ] Implement full section view
* [ ] Implement half section
* [ ] Implement offset section foundation
* [ ] Implement detail/cropped view
* [ ] Implement auxiliary view
* [ ] Implement cutting-plane representation
* [ ] Implement section hatch generation
* [ ] Validate section geometry independently
* [ ] Validate stable source references
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
section geometry correct
+ cutting planes correct
+ detail extraction correct
+ hatch behavior correct
+ stable references preserved
```

---

# P14-HLR-001

## Visible / Hidden Line Generation

* [ ] Implement visible-edge extraction
* [ ] Implement hidden-edge extraction
* [ ] Implement silhouette edges
* [ ] Implement tangent-edge policy
* [ ] Implement per-view hidden-line toggle
* [ ] Handle overlapping/projected edges
* [ ] Validate against independent geometry cases
* [ ] Validate assemblies with occlusion
* [ ] Deterministic edge classification PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
visible edges correct
+ hidden edges correct
+ silhouettes correct
+ occlusion correct
+ deterministic classification
```

---

# P14-DIM-001

## Dimensions

* [ ] Implement linear dimensions
* [ ] Implement horizontal / vertical dimensions
* [ ] Implement aligned dimensions
* [ ] Implement angular dimensions
* [ ] Implement radius dimensions
* [ ] Implement diameter dimensions
* [ ] Implement ordinate dimension foundation
* [ ] Implement dimension precision / formatting
* [ ] Implement model-driven dimension values
* [ ] Validate dimensions against 3D geometry
* [ ] Preserve dimension references across regeneration
* [ ] Missing reference fails explicitly
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
dimension math correct
+ units correct
+ formatting correct
+ model values correct
+ references stable
+ no silent rebinding
```

---

# P14-ANNO-001

## Drawing Annotations

* [ ] Implement text notes
* [ ] Implement leaders
* [ ] Implement centerlines
* [ ] Implement centermarks
* [ ] Implement hole callouts
* [ ] Implement surface-finish symbol foundation
* [ ] Implement datum symbol foundation
* [ ] Implement annotation placement
* [ ] Validate scale-independent text sizing
* [ ] Validate deterministic annotation layout
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---

# P14-TOL-001

## Tolerances / Fits / GD&T Foundation

* [ ] Implement dimensional ± tolerance
* [ ] Implement limit dimensions
* [ ] Implement fit notation foundation
* [ ] Implement datum feature symbols
* [ ] Implement feature-control-frame data model
* [ ] Implement core geometric characteristic symbols
* [ ] Associate tolerances with stable model references
* [ ] Validate semantic persistence
* [ ] Validate export representation
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
tolerance intent preserved
+ datum references stable
+ GD&T data model coherent
+ persistence correct
+ export representation correct
```

---

# P14-ASM-001

## Assembly Drawing Views

* [ ] Generate views from solved assembly state
* [ ] Respect active configuration
* [ ] Respect component suppression
* [ ] Preserve occurrence identity
* [ ] Validate multiple instances of one part
* [ ] Validate assembly hidden-line behavior
* [ ] Validate sectioned assemblies
* [ ] Validate regeneration after assembly changes
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---

# P14-BOM-001

## BOM / Balloons

* [ ] Implement assembly BOM model
* [ ] Implement unique item numbering
* [ ] Group identical part definitions correctly
* [ ] Preserve separate component occurrences
* [ ] Implement quantity calculation
* [ ] Implement BOM table
* [ ] Implement item balloons
* [ ] Link balloons to BOM rows
* [ ] Respect configurations / suppression
* [ ] Validate deterministic numbering
* [ ] Validate save/load
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
BOM contents correct
+ quantities correct
+ occurrence grouping correct
+ balloon mapping correct
+ configuration behavior correct
+ deterministic numbering
```

---

# P14-STREF-001

## Stable Drawing References

* [ ] Stable drawing view → model reference
* [ ] Stable dimension → model geometry reference
* [ ] Stable annotation → model reference
* [ ] Stable balloon → assembly occurrence reference
* [ ] Preserve references across model regeneration
* [ ] Preserve references across configuration switching
* [ ] Preserve references across save/load
* [ ] Missing geometry becomes unresolved
* [ ] Prevent silent rebinding
* [ ] Validate target recovery
* [ ] Deterministic resolution PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
drawing references stable
+ no index-based identity
+ no silent rebinding
+ unresolved/recovery correct
+ persistence PASS
+ determinism PASS
```

---

# P14-REGEN-001

## Drawing Regeneration

* [ ] Define dirty-propagation triggers
* [ ] Rebuild views after model changes
* [ ] Update dimensions after model changes
* [ ] Update annotations/BOM where required
* [ ] React to configuration changes
* [ ] Regenerate only affected drawing state where feasible
* [ ] Preserve canonical drawing intent
* [ ] Handle unresolved references explicitly
* [ ] Validate failure atomicity
* [ ] Validate deterministic regeneration
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
model changes propagate correctly
+ drawing intent preserved
+ affected state refreshed
+ stale geometry impossible
+ failures atomic
+ determinism PASS
```

---

# P14-CMD-001

## Commands / Undo / Redo

* [ ] Sheet commands
* [ ] View create/delete/move commands
* [ ] Dimension commands
* [ ] Annotation commands
* [ ] BOM / balloon commands
* [ ] Undo restores exact drawing intent
* [ ] Redo restores exact post-command state
* [ ] Failed commands are atomic
* [ ] Redo invalidation correct
* [ ] Regeneration integrates correctly
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---

# P14-PERSIST-001

## Drawing Persistence

* [ ] Define canonical drawing schema
* [ ] Persist sheets
* [ ] Persist views / scales / placements
* [ ] Persist dimensions
* [ ] Persist annotations
* [ ] Persist tolerances / GD&T
* [ ] Persist BOM / balloon intent
* [ ] Persist stable references
* [ ] Keep generated drawing geometry derived
* [ ] Validate malformed-file rejection
* [ ] Validate deterministic serialization
* [ ] Validate full round trip
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---

# P14-CLI-001

## Headless Drawing Workflows

* [ ] CLI create/load/save drawing
* [ ] CLI add/remove sheets
* [ ] CLI create/edit views
* [ ] CLI add/edit dimensions
* [ ] CLI annotations
* [ ] CLI regenerate
* [ ] CLI BOM generation
* [ ] CLI export
* [ ] Structured diagnostics
* [ ] Correct process exit codes
* [ ] Validate CLI/core equivalence
* [ ] End-to-end scripted workflow PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---

# P14-EXPORT-001

## PDF / SVG / DXF Export

* [ ] Implement vector drawing scene representation
* [ ] Implement PDF export
* [ ] Implement SVG export
* [ ] Implement DXF drawing export
* [ ] Preserve sheet size / scale
* [ ] Preserve line types / weights
* [ ] Preserve dimensions / text / symbols
* [ ] Preserve hidden-line representation
* [ ] Validate PDF dimensions independently
* [ ] Validate SVG geometry structurally
* [ ] Validate DXF entities/read-back
* [ ] Validate deterministic geometric output
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
PDF PASS
+ SVG PASS
+ DXF PASS
+ sheet geometry correct
+ scale correct
+ drawing entities complete
+ independent read-back PASS
```

---

# P14-REFMOD-001

## Production Drawing Reference Models

* [ ] Define production drawing suite
* [ ] Add simple machined-part drawing
* [ ] Add multi-view dimensioned part
* [ ] Add section/detail drawing
* [ ] Add hole / pattern drawing
* [ ] Add toleranced / GD&T drawing
* [ ] Add assembly drawing
* [ ] Add BOM / balloon drawing
* [ ] Add configuration-dependent drawing
* [ ] Validate model-change regeneration
* [ ] Validate save/load
* [ ] Validate CLI workflows
* [ ] Validate PDF / SVG / DXF output
* [ ] Independently validate dimensions / scale / geometry
* [ ] Adversarial review PASS
* [ ] Three-preset regression PASS
* [ ] Evidence recorded

---

# P14-QUAL-001

## Full P14 Qualification

* [ ] Freeze final P14 tree
* [ ] Audit all P14 milestone evidence
* [ ] Verify all TODO items complete
* [ ] Verify all P14 ADR contracts
* [ ] Clean Debug qualification
* [ ] Clean Release qualification
* [ ] Clean Debug-shared qualification
* [ ] Repeated determinism qualification
* [ ] Validate production drawing reference suite
* [ ] Validate dimensions / annotations
* [ ] Validate stable drawing references
* [ ] Validate drawing regeneration
* [ ] Validate persistence
* [ ] Validate undo / redo
* [ ] Validate BOM / balloons
* [ ] Validate CLI workflows
* [ ] Validate PDF / SVG / DXF exports
* [ ] Final adversarial review
* [ ] Confirm 0 unexpected warnings
* [ ] Confirm qualified tree == committed tree
* [ ] Evidence in `docs/verification/P14-QUAL-001/`
* [ ] Mark P14 qualified

### Final Gate

```text
all P14 milestones PASS
+ drawing architecture PASS
+ projection/view generation PASS
+ hidden-line generation PASS
+ dimensions PASS
+ annotations PASS
+ tolerance/GD&T foundation PASS
+ assembly drawings PASS
+ BOM/balloons PASS
+ stable drawing references PASS
+ regeneration PASS
+ persistence PASS
+ undo/redo PASS
+ CLI PASS
+ PDF/SVG/DXF PASS
+ production reference models PASS
+ Debug PASS
+ Release PASS
+ Debug-shared PASS
+ determinism PASS
+ adversarial review PASS
+ 0 unexpected warnings
+ qualified tree == committed tree
```

---

# P14 Core Invariants

* 3D model remains authoritative.
* Drawing views are derived from model geometry.
* Drawing annotations/dimensions preserve engineering intent.
* Derived 2D geometry is not authoritative model state.
* Model references must use stable identity, never topology index alone.
* Missing geometry becomes unresolved; never silently rebind.
* Assembly drawings use solved assembly state.
* Suppressed components do not appear in active drawing/BOM output.
* Dimensions must be unit-safe.
* Drawing regeneration must never publish stale geometry as current.
* Export must preserve physical sheet scale and geometry.

---

# Inherited Constraints

These are properties of the qualified P11–P13 system, not P14 work. P14 must
respect them, and `P14-ARCH-001` has to decide what each means for a drawing.

* Assemblies operate inside one `Document`; cross-document dependencies do not execute.
* One document-global configuration system; a component cannot select a different configuration of its part.
* Solved component transforms are derived state, recomputed on open — a drawing of an assembly costs a solve.
* An over-constrained assembly reports its redundant mates and publishes no positions; a drawing of one has nothing to project.
* A broken assembly publishes no transforms at all, not a partial set.
* Missing intended geometry fails explicitly; never silently rebind.
* Mate targets use only ADR-004-qualified reference types.
* STEP read-back is verification infrastructure, not general STEP import.

Full list with evidence: [docs/verification/P13-QUAL-001/README.md](docs/verification/P13-QUAL-001/README.md).

---

# Architecture Decisions

```text
ADR-001 — Reference-model datum placement
ADR-002 — Assembly/document model
ADR-003 — Internal/external reference contract
ADR-004 — Mate-reference semantics
ADR-005 — Placement intent / derived transforms
ADR-006 — Module layering
ADR-007 — One configuration system
ADR-008 — Assembly solve as regeneration final pass
ADR-009 — The CLI edit is a document transaction
ADR-010 — Drawings live in the document
ADR-011 — Drawing intent is canonical, projection is derived
ADR-012 — Drawing references name semantic geometry only
ADR-013 — View orientation and drawing scale
ADR-014 — Drawing geometry is built on demand
ADR-015 — Drawing module and layer (supersedes ADR-006's table)
ADR-016 — The drawing scene is the export boundary
ADR-017 — Drawing identity model
```

`P14-ARCH-001` allocated ADR-010 to ADR-017. The next milestone that needs
one continues from ADR-018.

---

# Carried Forward

Recorded gaps with no owning milestone. Fold into a P14 milestone only where
P14 actually needs them; otherwise they stay here.

* `Document::modifyObject` bypasses the document-level reference check (from `P13-COMP-001`).
* No CLI resolver for external parts (from `P13-REF-001`).
* No mate rebinding/repair command (from `P13-STREF-001`).
* `info` does not show where a component sits (from `P13-XFORM-001`).
* No compile-fail case pins `MateTarget`'s exclusion of `FaceSignature` (from `P13-QUAL-001`).

---

# Deferred CAD Work

Not currently authorized:

* Fillet setback controls
* Selectable fillet corner transitions
* Loft end conditions
* Full semantic topology
* General STEP import
* DXF / IGES / OBJ interoperability

`P14-EXPORT-001` authorizes DXF **drawing export** only. DXF import, and the
rest of the interoperability list, stay deferred.

---

# Future Phases

```text
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
→ long-term direction, and completed phases with their evidence

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
