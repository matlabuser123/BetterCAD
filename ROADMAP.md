# BetterCAD — Roadmap

> Where BetterCAD is going, what it has reached, and how the capabilities fit together.

`ROADMAP.md` describes long-term capability direction.

`TODO.md` authorizes implementation work and owns milestone IDs.

---

## Vision

BetterCAD is a unified mechanical CAD/CAE platform built around one persistent
engineering document model.

```text
Parametric CAD
→ Assemblies
→ Technical Drawings
→ Materials
→ Meshing
→ FEA / Thermal / CFD
→ Optimization
→ Automation / Python
→ Versioning / Collaboration
→ AI Engineering
→ Manufacturing
→ Production BetterCAD
```

The long-term workflow is:

```text
requirements
→ parametric design
→ assembly
→ verification / simulation
→ optimization
→ technical drawings
→ manufacturing
→ revision / collaboration
```

An engineering agent may eventually drive this workflow through the same
validated public APIs used by the CLI, GUI, tests and automation.

---

## Engineering Principles

1. **Correctness before features.**
   Every subsystem requires deterministic tests, regression coverage,
   independent validation where practical, failure diagnostics, and recorded
   evidence.

2. **Engineering intent is canonical.**
   Parameters, constraints, features, mates, drawing annotations,
   configurations and dependencies are persisted. Geometry and solved state
   are derived.

3. **Native units.**
   Engineering quantities retain their physical dimensions.

4. **Stable identity.**
   Persistent engineering objects use typed stable IDs rather than array
   indices, transient kernel handles, addresses or traversal order.

5. **Dependency-driven regeneration.**
   Changes propagate only to state that depends on them where mathematically
   possible.

6. **Headless engineering core.**
   CLI, GUI, tests, Python and AI operate through the same production engine.

7. **Failure must be explicit.**
   Missing or invalid engineering references become unresolved or failed
   states; BetterCAD must never silently guess a replacement.

8. **Qualification is stronger than implementation.**
   Code existing is not enough. A milestone is complete only after
   implementation, verification, adversarial review, regression, evidence and
   committed-tree qualification.

---

## Current State

BetterCAD has qualified:

```text
P0–P10  Foundation
P11     Parametric Part Modeling
P12     Parametric CAD Completion
P13     Assemblies
P14     Technical Drawings
P15     Materials / Engineering Data
P16     Meshing
P17     Structural FEA
```

Current authorized development:

```text
P19 — CFD Integration, authorized 2026-10-27 by the owner's "start
P19-ARCH-001": P19-ARCH-001 and P19-DATA-001 (2026-10-27), P19-FLUID-001
(2026-10-28), P19-MESH-001, P19-TOPO-001 and P19-GEOM-001 (all 2026-10-31) and
P19-DOF-001 and P19-BC-001 (both 2026-11-05) and P19-IC-001 and P19-DIFF-001
(both 2026-11-06) and P19-GRAD-001 and P19-DIV-001 (both 2026-11-07) have
PASSED.
P19-MESH-001 was qualified by a second attempt on a replacement machine: its
first attempt, on 2026-10-28, failed 5 of 17 stages when the previous machine's
temp volume filled and no test suite ran, and that machine was then permanently
replaced, so the replacement was qualified in its own right before the gate was
re-run. P19-TOPO-001 and P19-GEOM-001 each qualified from clean on the first
attempt, on the same harness. P19-DOF-001 needed a second attempt as well, for a
measured resource reason rather than any defect in the tree: the machine ran out
of Windows commit, so a compile exited 1 with no diagnostic at all, and the dead
build cost the repeat stage its build-time test discovery, which then selected
352 tests instead of 2991 and passed every one. Its launcher reported success
while two of seventeen stages had failed, which is why the harness's own verdict
is what is read. One line of that milestone's own run driver differs between its
two attempts, and no frozen file does. P19-BC-001 qualified from clean on the
first attempt, on that same settled harness. P19-IC-001 needed THREE attempts
and no defect was involved in either failure. Its first was REFUSED BY THE
PREFLIGHT before anything was built, because the harness had been copied through
a filtered directory listing that silently dropped one of its six files, and the
report that "all five are byte-identical" was true and incomplete. Its second was
KILLED BY A SESSION TEARDOWN at test 5589 of 6240: the exit codes name the cause
as DBG_TERMINATE_PROCESS and STATUS_DLL_INIT_FAILED_LOGOFF, so the process was
killed rather than failing, and that attempt is void — though its first preset
had already passed 6240 of 6240, independently confirming the predicted count,
and its repeat stages, pointed at a build tree that no longer existed, selected
0 tests and were correctly COUNTED AS FAILED rather than passing as ctest's "No
tests were found!!!". The third ran detached and qualified every stage.
P19-DIFF-001 qualified from clean on the FIRST attempt, launched detached from
the start because of what had happened to P19-IC-001's second, and it is the
first P19 milestone since P19-BC-001 to need only one. P19-GRAD-001 did the same,
on one attempt, and its freeze went further than its predecessor's in one
respect worth recording: all FIVE harness hashes that run-qualification.cmd
records for itself were verified rather than only the two preflight ones, after
it turned out that the first two 40-hex strings in that file are not the pair a
reader would assume. P19-DIV-001 needed TWO attempts, and for the first time in
P19 the reason was neither a machine nor a defect in the tree but the ORDER THE
WORK WAS DONE IN: its first attempt passed every stage before either
independent review had been run, and the reviews then found five defects in the
implementation and eight in the test suite. Fixing them changed source and
tests, which voids a qualification under the freeze's own rule, so the gate was
re-run from clean on the repaired tree. That first attempt's logs are kept under
qualification/attempt-1/ rather than erased, because it passed genuinely and the
reason it does not count is the useful part. The harness has now run SIXTY-ONE
milestones unchanged, and is self-checked before each run, because an unchanged
harness is not the same as a working one. No milestone is open, and nothing
after P19-DIV-001 is authorized; see TODO.md.

The phases before it, and how each was qualified:

P18 — Thermal Analysis, authorized 2026-10-16.
P17 — Structural FEA was QUALIFIED 2026-10-15 by P17-QUAL-001: seventeen
milestones PASS, 4009/4009 tests in three presets, qualified tree ==
committed tree. P18-ARCH-001 (2026-10-16), P18-DATA-001, P18-MAT-001 and
P18-DOF-001 (2026-10-17), P18-ELEM-001 and P18-LOAD-001 (2026-10-18),
P18-BC-001 (2026-10-19), P18-ASSEMBLY-001 and P18-SOLVE-001 (2026-10-20),
P18-TRANSIENT-001 and P18-POST-001 (2026-10-21), P18-BALANCE-001 (2026-10-22),
P18-VALID-001 and P18-VIZ-001 (2026-10-23), P18-CMD-001 (2026-10-24),
P18-PERSIST-001 (2026-10-25), P18-CLI-001 and P18-REFMOD-001 (2026-10-26) have
PASSED, and P18 — Thermal Analysis was QUALIFIED on 2026-10-27 by P18-QUAL-001:
eighteen milestones PASS, 5804/5804 tests in three presets, qualified tree ==
committed tree.
```

Released: `v0.1.0` — Foundation.

Qualified but not yet separately released: `P11`, `P12`, `P13`, `P14`, `P15`, `P16`, `P17`, `P18`.

---

## Capability Roadmap

| Capability | Milestone | Status |
| --- | --- | --- |
| Foundation | `P0`–`P10` | **Qualified / Released** |
| Parametric Part Modeling | `P11` | **Qualified** |
| Parametric CAD Completion | `P12` | **Qualified** |
| Assemblies | `P13` | **Qualified** |
| Technical Drawings | `P14` | **Qualified** |
| Materials / Engineering Data | `P15` | **Qualified** |
| Meshing | `P16` | **Qualified** |
| Structural FEA | `P17` | **Qualified** |
| Thermal Analysis | `P18` | **Qualified** |
| CFD Integration | `P19` | In progress |
| Design Optimization | `P20` | Planned |
| Semantic Topology | `P21` | Planned |
| Versioning / Collaboration | `P22` | Planned |
| Python / Automation | `P23` | Planned |
| AI Engineering Agent | `P24` | Planned |
| Manufacturing / CAM | `P25` | Planned |
| Performance / GPU / Scale | `P26` | Planned |
| Production Hardening | `P27` | Planned |
| BetterCAD 1.0 | `P28` | Planned |

```text
Qualified   implemented + tested + independently validated
            + adversarially reviewed + regression clean
            + evidence recorded + committed tree verified

In Progress authorized by TODO.md

Planned     intended future capability; not yet authorized
```

A phase number here is a **sequencing placeholder**. The milestone IDs inside a
phase (`P14-ARCH-001`) are allocated by [TODO.md](TODO.md) when the phase is
authorized, never in advance.

### Deferred, not numbered

Three capabilities are **Planned** but cut across phases rather than forming
one, so they carry no phase number. They are tracked in [TODO.md](TODO.md)
under *Deferred CAD Work*.

**Interchange.** STEP and STL **export** are delivered. Outstanding: STEP
import; DXF import; IGES and OBJ; 3MF, glTF, Parasolid and JT where licensing
permits; and schema migration between native document format versions. Every
external format lives behind an adapter — see
[ARCHITECTURE.md](ARCHITECTURE.md). The only STEP reader in the repository
today is test-only tooling that reads exports back to verify them; it is not an
import path. `P14-EXPORT-001` adds DXF **export** only.

**Advanced Surface Modeling.** Blend work the kernel does not offer and
BetterCAD would build itself: fillet **setback** distances and **selectable
corner transitions** where blended edges meet. OCCT 8.0.1 computes those
corners internally and exposes no input for either, so both need a
BetterCAD-owned surface-patch layer (trimmed blends, N-sided tangent fills,
sewing, validation). Both were deferred from `P12-FEAT-006`
([investigation](docs/verification/P12-FEAT-006/investigation/README.md)), as
were loft end conditions.

**Desktop Application.** The desktop executable is a placeholder shell. The
goal is the first practical interactive environment on Qt 6: command system,
model tree, property editor, 3D viewport, sketch environment, diagnostics
panel; orbit/pan/zoom/standard views; selection of bodies, faces, edges,
vertices, sketches and features. **Gate.** A user creates a parametric
mechanical part entirely from the GUI, with the GUI owning none of the
engineering state.

---

## Foundation — Qualified

Foundation established:

* C++ build and repository structure
* unit-safe engineering quantities
* strong IDs
* document model
* parameter system
* commands / undo / redo
* Open CASCADE geometry kernel integration
* sketch system
* constraint solving
* feature regeneration
* persistence
* CLI
* STEP / STL export

Released as `v0.1.0`.

Evidence: `docs/verification/P0-*/` … `docs/verification/P10-*/`.

---

## Parametric Part Modeling — Qualified

`P11` established production feature modeling beyond the original extrude
foundation. Each feature stores its definition rather than its result,
regenerates from parameters, fails atomically with a structured diagnostic,
round-trips through save/load, and is validated against independently computed
volumes.

`P11-QUAL-001`: 20 gates, 20 passed. 738/738 tests and 0 compiler warnings over
274 translation units in each of Debug, Release and Debug-shared, each rebuilt
clean.

| Milestone | Delivered | Evidence |
| --- | --- | --- |
| `P11-FEAT-001` | Revolve | [P11-FEAT-001](docs/verification/P11-FEAT-001/README.md) |
| `P11-FEAT-002` | Chamfer — equal-distance, two-distance, distance-angle | [P11-FEAT-002](docs/verification/P11-FEAT-002/README.md) |
| `P11-FEAT-003` | Fillet — constant radius | [P11-FEAT-003](docs/verification/P11-FEAT-003/README.md) |
| `P11-FEAT-004` | Hole — simple, counterbore, countersink; through, blind | [P11-FEAT-004](docs/verification/P11-FEAT-004/README.md) |
| `P11-FEAT-005` | Linear pattern — one or two directions | [P11-FEAT-005](docs/verification/P11-FEAT-005/README.md) |
| `P11-FEAT-006` | Circular pattern — full circle, included angle, angle step | [P11-FEAT-006](docs/verification/P11-FEAT-006/README.md) |
| `P11-FEAT-007` | Mirror — feature and body scope | [P11-FEAT-007](docs/verification/P11-FEAT-007/README.md) |
| `P11-FEAT-008` | Sweep — planar path, follow-path orientation | [P11-FEAT-008](docs/verification/P11-FEAT-008/README.md) |
| `P11-FEAT-009` | Loft — ruled, parallel same-shape sections | [P11-FEAT-009](docs/verification/P11-FEAT-009/README.md) |
| `P11-REF-001` | Six mechanical reference models | [P11-REF-001](docs/verification/P11-REF-001/README.md) |
| `P11-QUAL-001` | Qualification — 20 gates, 20 passed | [P11-QUAL-001](docs/verification/P11-QUAL-001/README.md) |

---

## Parametric CAD Completion — Qualified

`P12` completed the practical parametric-part workflow: parameter expressions
and dimensional analysis; the remaining sketch constraints and entities; datum
planes, axes and coordinate systems; stable feature-face references and
sketches on generated faces; through-all extrude; split and combine; shell,
draft and rib; variable-radius fillets; advanced holes, patterns, sweeps and
lofts; design equations and configurations.

`P12-QUAL-001`: 22 gates, 22 passed. 1259/1259 tests and 0 compiler warnings in
each of Debug, Release and Debug-shared, each rebuilt clean, then every test run
five more times in Release and in Debug. Qualified commit `15d7f75`.

| Milestone | Delivered | Evidence |
| --- | --- | --- |
| `P12-PARAM-001` | Parameter expressions — units, dimensional analysis, dependency-ordered evaluation, cycle and failure handling | [P12-PARAM-001](docs/verification/P12-PARAM-001/README.md) |
| `P12-SKETCH-001` | Sketch constraints — angle, tangent, concentric, midpoint, symmetric, diameter; undoable sketch edits | [P12-SKETCH-001](docs/verification/P12-SKETCH-001/README.md) |
| `P12-SKETCH-002` | Sketch entities — ellipses and B-splines through solver, profiles, kernel and files; exact mass properties for curved faces | [P12-SKETCH-002](docs/verification/P12-SKETCH-002/README.md) |
| `P12-DATUM-001` | Datum geometry — planes, axes and coordinate systems as document objects; attached sketches; datum mirror planes and pattern axes; face-by-face mass properties for curved bodies | [P12-DATUM-001](docs/verification/P12-DATUM-001/README.md) |
| `P12-STREF-001` | Stable feature face references — faces named by feature and role, carried through booleans; sketches and datums on extrude faces follow them | [P12-STREF-001](docs/verification/P12-STREF-001/README.md) |
| `P12-SKETCH-003` | Sketches on arbitrary planar faces — named faces of revolves, sweeps, lofts, holes and chamfers, and the copies patterns and mirrors make; names carried through blends and transforms | [P12-SKETCH-003](docs/verification/P12-SKETCH-003/README.md) |
| `P12-FEAT-001` | Through-all extrude — a cut through all of its target in any direction, its length taken from the target at regeneration; repeated per instance by patterns and mirrors | [P12-FEAT-001](docs/verification/P12-FEAT-001/README.md) |
| `P12-FEAT-002` | Split body / combine — plane splits keeping either side or both, and joins, cuts and intersections of several features' bodies, which they consume | [P12-FEAT-002](docs/verification/P12-FEAT-002/README.md) |
| `P12-FEAT-003` | Shell — bodies hollowed inward or outward into walls of a driven thickness, opened at named faces, with the kernel's result checked rather than trusted | [P12-FEAT-003](docs/verification/P12-FEAT-003/README.md) |
| `P12-FEAT-004` | Draft — named faces tapered by a driven angle about a neutral plane (a datum or a named face), tangent chains included, with the kernel's result checked | [P12-FEAT-004](docs/verification/P12-FEAT-004/README.md) |
| `P12-FEAT-005` | Rib — walls filling from an open sketched profile (lines, arcs, splines) to the body, extended along their tangents, with open sides refused, naming their faces | [P12-FEAT-005](docs/verification/P12-FEAT-005/README.md) |
| `P12-FEAT-006` | Variable-radius fillet — radius stations along straight edges, the kernel's law computed and checked, laws that leave their stations refused; setback and corner transitions deferred | [P12-FEAT-006](docs/verification/P12-FEAT-006/README.md) |
| `P12-HOLE-001` | Hole standards — spotfaces, cosmetic ISO metric threads, the clearance holes of ISO 273 and the hole tolerance classes of ISO 286, stored as their designations and checked against published copies of the standards | [P12-HOLE-001](docs/verification/P12-HOLE-001/README.md) |
| `P12-PATTERN-001` | Pattern instances — directions given a spacing or a total length, symmetric spans about the source, suppressed instances that keep their index, and patterns that repeat another pattern with the whole copy chain on every face | [P12-PATTERN-001](docs/verification/P12-PATTERN-001/README.md) |
| `P12-SWEEP-001` | Advanced sweeps — paths that run through several sketches and so leave any one plane, a twist law the section follows, and guide curves that carry it, on a frame convention measured against the kernel rather than assumed | [P12-SWEEP-001](docs/verification/P12-SWEEP-001/README.md) |
| `P12-PARAM-002` | Design configurations — named sets of overrides to a document's free parameters, with the equations, sketches and features shared rather than a model tree duplicated per configuration; the value in force is the base value with the active configuration's override applied, so a configuration reaches every feature without any of them knowing configurations exist | [P12-PARAM-002](docs/verification/P12-PARAM-002/README.md) |
| `P12-LOFT-001` | Advanced lofts — sections of different shapes, matched by BetterCAD itself along normalized arc length and checked against a mixed-area prismatoid volume derived in closed form, and smooth interpolation running continuously across the intermediate sections; end conditions reported as unavailable on this kernel, with the measurement | [P12-LOFT-001](docs/verification/P12-LOFT-001/README.md) |
| `P12-REF-001` | Production reference models — six realistic mechanical parts exercising the P12 feature set together, each validated against closed forms derived by hand; an adversarial review found and fixed eight defects, four of them parameters that destroyed their own model | [P12-REF-001](docs/verification/P12-REF-001/README.md) |
| `P12-QUAL-001` | Phase qualification — 22 gates, 22 passed | [P12-QUAL-001](docs/verification/P12-QUAL-001/README.md) |

Deferred from this phase: fillet setback controls, selectable corner
transitions and loft end conditions. See *Advanced Surface Modeling* above.

---

## Assemblies — Qualified

`P13` established a complete assembly foundation inside one BetterCAD document:
component definitions and instances; placement intent; internal and external
reference infrastructure; seven basic mates and four mechanical joints
(revolute, slider, cylindrical, planar); a nonlinear constraint solver with an
analytic Jacobian and rank analysis, reporting five distinct states rather than
a boolean; configuration-dependent suppression; stable assembly references;
dependency-driven regeneration; assembly commands with undo; persistence;
transactional CLI workflows; STEP assembly export; and eight production
reference assemblies.

Core invariant:

```text
assembly placement intent = canonical

solved component transforms = derived
```

Solved transforms are not persisted as authoritative model state. A failed
dependency or unresolved mate must never leave stale solved positions presented
as current — a transform one edit out of date still renders, which makes it
worse than none at all.

`P13-QUAL-001`: 22 gates, 22 passed. 1674/1674 tests and 0 compiler warnings in
each of Debug, Release and Debug-shared, each rebuilt clean, then the whole
suite run five more times in Release and in Debug; 0 production defects from the
phase adversarial review. Qualified tree `9cd2330`.

| Milestone | Delivered | Evidence |
| --- | --- | --- |
| `P13-ARCH-001` | Assembly architecture — ADR-002 to ADR-006, deciding where assemblies live, what a reference means, what is intent and what is derived, and the module layering, while writing no code | [P13-ARCH-001](docs/verification/P13-ARCH-001/README.md) |
| `P13-COMP-001` | Components — a new `assembly` module at layer 3, components as document objects instancing a part in the same document, with their own identity, dependency edges and persistence | [P13-COMP-001](docs/verification/P13-COMP-001/README.md) |
| `P13-XFORM-001` | Component transforms — placement as canonical intent, resolvable to a transform from parameters in force, with nothing cached so a stale transform cannot exist | [P13-XFORM-001](docs/verification/P13-XFORM-001/README.md) |
| `P13-REF-001` | Reference infrastructure — references qualified by document UUID rather than path, an injectable resolver, and unresolved as a state rather than an error at load | [P13-REF-001](docs/verification/P13-REF-001/README.md) |
| `P13-MATE-001` | Basic constraints — seven mate kinds, what each may point at and what it refuses, how they depend and how they persist; nothing moves yet | [P13-MATE-001](docs/verification/P13-MATE-001/README.md) |
| `P13-SOLVE-001` | The constraint solver — an equation system with an analytic Jacobian, a Gauss-Newton loop with line search and damping, rank analysis for redundancy, and five distinct solve states; converging from intent alone, with no warm start, so the answer never depends on save history | [P13-SOLVE-001](docs/verification/P13-SOLVE-001/README.md) |
| `P13-MATE-002` | Mechanical mates — revolute, slider, cylindrical and planar, each defined by the freedom it leaves rather than the constraint it adds, so a joint behaving like another is caught by what survived | [P13-MATE-002](docs/verification/P13-MATE-002/README.md) |
| `P13-CONF-001` | Configurations and suppression — which components and mates are in force per configuration, on the existing configuration system rather than a second one | [P13-CONF-001](docs/verification/P13-CONF-001/README.md) |
| `P13-STREF-001` | Stable assembly references — a mate target that survives the model changing under it, or is honestly broken; never a mate that still solves on the wrong face | [P13-STREF-001](docs/verification/P13-STREF-001/README.md) |
| `P13-REGEN-001` | Dependency and regeneration — the assembly solve as a document-level final pass, re-run exactly when one of its own inputs moved, publishing all transforms or none | [P13-REGEN-001](docs/verification/P13-REGEN-001/README.md) |
| `P13-CMD-001` | Commands, undo and redo — six assembly commands in the existing history, and a deletion-undo defect that predated the milestone: an object came back without the configuration overrides that named it | [P13-CMD-001](docs/verification/P13-CMD-001/README.md) |
| `P13-PERSIST-001` | Save and load — what a `.bcad` file says about an assembly, proved whole: intent survives the round trip and no derived state reaches the file | [P13-PERSIST-001](docs/verification/P13-PERSIST-001/README.md) |
| `P13-CLI-001` | Headless workflows — the CLI turned from a reporting tool into an editor, every edit a document transaction and a batch one transaction of many, so a script that fails part way writes nothing | [P13-CLI-001](docs/verification/P13-CLI-001/README.md) |
| `P13-STEP-001` | Assembly STEP export — an AP214 product structure through XCAF, one product per part and one occurrence per placement, exporting the **solved** positions and refusing to write an assembly that did not solve | [P13-STEP-001](docs/verification/P13-STEP-001/README.md) |
| `P13-REFMOD-001` | Production reference assemblies — eight committed models, RM-A to RM-H, every expected DOF and placement derived from the mate equation table before it was measured; RM-H is committed **broken**, so the fault paths are proved by a real artifact rather than by a test reaching in | [P13-REFMOD-001](docs/verification/P13-REFMOD-001/README.md) |
| `P13-QUAL-001` | Phase qualification — 22 gates, 22 passed; 0 production defects, and the stale layer table `ADR-006` had ordered replaced fifteen milestones earlier | [P13-QUAL-001](docs/verification/P13-QUAL-001/README.md) |

### Current P13 limitations

* assemblies operate inside one `Document`
* execution of cross-document dependencies is not implemented
* configuration selection is document-global
* components cannot independently select another configuration of their referenced part
* assembly solving is global rather than incremental per component
* an over-constrained assembly reports its redundant mates and publishes no positions
* STEP read-back remains verification infrastructure rather than general STEP import

Full list with evidence:
[P13-QUAL-001](docs/verification/P13-QUAL-001/README.md).

---

## Technical Drawings — Qualified

`P14` turns BetterCAD's qualified model and assembly engine into engineering
drawing output.

```text
3D engineering model
→ drawing document
→ sheets
→ projected views
→ dimensions
→ annotations
→ sections/details
→ tolerances
→ assembly drawings
→ BOM / balloons
→ PDF / SVG / DXF
```

The authoritative engineering model remains 3D. Drawing geometry is derived.
Drawing annotations, dimensions, view definitions, tolerances and references
are engineering intent.

The milestone sequence and its per-milestone gates are owned by
[TODO.md](TODO.md). What follows is the capability definition, not the plan.

### Sheets

Standard engineering sheet sizes, landscape and portrait, margins, borders,
title blocks, multiple sheets, drawing units, drawing scale.

### Views

Front, rear, left, right, top, bottom, isometric, projected, section, detail
and auxiliary views. Orthographic projection and scale must be mathematically
verified independently.

### Hidden-line processing

Drawing views must distinguish:

```text
visible edges
hidden edges
silhouette edges
tangent edges
```

Assembly views must correctly account for component occlusion.

### Dimensions

Linear, horizontal, vertical, aligned, angular, radius, diameter, and an
ordinate foundation. Values must come from the authoritative model geometry.

### Annotation

Notes, leaders, centerlines, centermarks, hole callouts, a surface-finish
foundation, datum symbols.

### Tolerancing

A foundation for ± dimensions, limit dimensions, fit notation, datum features,
feature-control frames and GD&T characteristics. The semantic tolerance intent
must remain separate from its graphical presentation.

### Assembly drawings

Assembly drawings consume the qualified `P13` assembly state, and must respect:

```text
active configuration
component suppression
solved placement
component occurrence identity
stable references
```

Two `P13` properties reach straight into this: solved transforms are derived,
so a drawing of an assembly costs a solve on open; and an over-constrained
assembly publishes no positions at all, so there is nothing to project.

### BOM and balloons

Item numbering, quantities, grouping of repeated part definitions, separate
component occurrence identity, BOM tables, balloons, deterministic numbering,
and configuration-aware generation.

### Stable references

A drawing dimension or annotation must never depend permanently on:

```text
Face17
Edge34
array index
OCCT traversal order
pointer identity
nearest geometry
```

If its intended target disappears:

```text
reference → unresolved
```

not:

```text
reference → some other convenient edge
```

### Regeneration

A model change must propagate into dependent drawing state:

```text
dimension changes
→ model regenerates
→ drawing view regenerates
→ dimension value updates

assembly configuration changes
→ assembly solves
→ drawing regenerates
→ suppressed components disappear
→ BOM updates
```

Stale drawing geometry must never remain presented as current.

### Export

`P14` targets PDF, SVG and DXF. Exports must preserve physical sheet
dimensions, drawing scale, line types, dimensions, text, symbols, hidden-line
representation and view placement, and require structural or read-back
validation rather than only checking that a file exists.

---

## Materials / Engineering Data — Qualified

`P15` gave BetterCAD engineering material data: what a part is made of, what that
material is, where its numbers came from, and what follows from them.

```text
material library (reference data)
→ document-local material, with its own identity
→ mechanical and thermal properties
→ provenance per property
→ assignment to the part
→ mass, centre of mass, inertia
→ completeness per consumer
→ persistence of intent
→ headless workflows
```

CAD geometry stays the authority for volume; a material supplies a density; mass is
**derived** and never persisted as authority. An engineering property is Known, Unknown
or Derived — never defaulted, never zero-for-missing.

Qualified at `0757b8b` on 2026-09-30. `P15-QUAL-001`: 2814/2814 tests in Debug, Release
and Debug-shared, each from clean; 36582 test executions; 0 failures; **0 warnings in all
six build and rebuild logs**; canonical output byte-identical across the three presets;
qualified tree identical to the committed tree.

| Milestone | Capability | Evidence |
| --- | --- | --- |
| `P15-ARCH-001` | Materials / engineering-data architecture; ADR-025 to ADR-028 | [P15-ARCH-001](docs/verification/P15-ARCH-001/README.md) |
| `P15-UNITS-001` | Engineering quantity and property contracts | [P15-UNITS-001](docs/verification/P15-UNITS-001/README.md) |
| `P15-MAT-001` | Material definition, identity and library | [P15-MAT-001](docs/verification/P15-MAT-001/README.md) |
| `P15-MECH-001` | Mechanical properties; derived G and K | [P15-MECH-001](docs/verification/P15-MECH-001/README.md) |
| `P15-THERM-001` | Thermal and physical properties; ADR-029 | [P15-THERM-001](docs/verification/P15-THERM-001/README.md) |
| `P15-ASSIGN-001` | Material assignment as intent | [P15-ASSIGN-001](docs/verification/P15-ASSIGN-001/README.md) |
| `P15-MASS-001` | Mass, centre of mass and inertia | [P15-MASS-001](docs/verification/P15-MASS-001/README.md) |
| `P15-CUSTOM-001` | Custom materials and controlled overrides | [P15-CUSTOM-001](docs/verification/P15-CUSTOM-001/README.md) |
| `P15-PROV-001` | Per-property provenance, completeness, validation | [P15-PROV-001](docs/verification/P15-PROV-001/README.md) |
| `P15-CMD-001` | Commands with exact undo and redo | [P15-CMD-001](docs/verification/P15-CMD-001/README.md) |
| `P15-PERSIST-001` | Save and load canonical material intent | [P15-PERSIST-001](docs/verification/P15-PERSIST-001/README.md) |
| `P15-CLI-001` | Headless engineering-data workflows | [P15-CLI-001](docs/verification/P15-CLI-001/README.md) |
| `P15-REFMOD-001` | Engineering-data reference models RM-MAT-01..06 | [P15-REFMOD-001](docs/verification/P15-REFMOD-001/README.md) |
| `P15-QUAL-001` | Full P15 qualification | [P15-QUAL-001](docs/verification/P15-QUAL-001/README.md) |
| `INFRA-QT-DEPLOY-001` | Build output outside the synchronised folder | [INFRA-QT-DEPLOY-001](docs/verification/INFRA-QT-DEPLOY-001/README.md) |

Decisions: [ADR-025](docs/architecture/decisions/ADR-025-a-material-is-a-document-object-and-the-library-is-reference-data.md),
[ADR-026](docs/architecture/decisions/ADR-026-a-material-assignment-is-intent-and-mass-is-derived.md),
[ADR-027](docs/architecture/decisions/ADR-027-an-engineering-property-is-known-unknown-or-derivable.md),
[ADR-028](docs/architecture/decisions/ADR-028-provenance-is-per-property-and-solvers-hold-no-material-data.md),
[ADR-029](docs/architecture/decisions/ADR-029-electrical-resistivity-is-a-scoped-strong-type-not-a-sixth-base-dimension.md).

### What P15 leaves for later

The built-in library carries designations, standards and families but **no property
values**; a document supplies its own numbers. There is one material per document, no
per-occurrence material in an assembly, and no assembly mass aggregation —
`ComponentDefinition` has no material field, and cross-document part references need
ADR-003. Temperature-dependent property laws are not represented: constants are stored and
a file containing a law is rejected rather than misread. Command history is not persisted.

`P17` consumes this layer through `requireLinearElasticConstants`, `requireDensity` and the
thermal requirement entry points, which exist so that a solver receives a complete set or an
explicit diagnostic, never a fabricated default.

## Meshing — Qualified

`P16` gave BetterCAD a simulation mesh: separate from the CAD B-Rep, separate
from the viewer's tessellation, and derived from the authoritative geometry
every time.

```text
authoritative CAD model
→ regenerated authoritative geometry      one boundary, which refuses stale,
                                          failed, blocked, non-solid, empty
                                          and configuration-overridden input
→ engineering surface mesh                closed, manifold, coherently
                                          oriented, or it does not exist
→ volume meshing backend                  behind an enforced interface
→ canonical BetterCAD Tet4 mesh           validated, conformity-checked,
                                          volume-recovered, or REFUSED
→ quality / mapping / visualisation / P17
```

**CAD geometry stays the authority and the mesh is derived.** A `MeshControl` is
the canonical intent — which body, how finely, which regions matter — and it is
a document object, in the dependency graph, edited only through commands and
persisted. The mesh itself is held by a service keyed by its control: never a
document object, never persisted, never restored by undo. Generating one leaves
the document byte-identical, and the saved file is byte-identical for a 361-element
and a 1977-element mesh.

**A selection is never a mesh entity.** Loads, restraints and local sizing name
CAD faces by `FaceName`, so they survive a remesh because they were never mesh
selections. A `NodeId` is a handle, not an identity: it cannot reach a `.bcad`
file, a control, a command or a face reference. 15 compile-fail cases pin the
handle types themselves and 45 across the mesh modules refuse the conversions
that would let one escape.

Qualified at `8ab30a31` on 2026-10-06. `P16-QUAL-001`: 3387/3387 tests in
debug-ext, release-ext and debug-shared-ext, each from clean; 698 selected tests
× 5 repeats in two presets; **0 warnings in all three builds** under `-Werror`
over 596 objects; no-op rebuilds doing 0 compiles and 0 links; 1751 tetrahedra
positively oriented with 0 zero and 0 negative; qualified tree identical to the
committed tree.

| Milestone | Capability | Evidence |
| --- | --- | --- |
| `P16-ARCH-001` | Meshing architecture; ADR-030 to ADR-033 | [P16-ARCH-001](docs/verification/P16-ARCH-001/README.md) |
| `P16-DATA-001` | Mesh data model, identity and units | [P16-DATA-001](docs/verification/P16-DATA-001/README.md) |
| `P16-GEOM-001` | Geometry preparation and the regeneration boundary | [P16-GEOM-001](docs/verification/P16-GEOM-001/README.md) |
| `P16-SURF-001` | Engineering surface mesh | [P16-SURF-001](docs/verification/P16-SURF-001/README.md) |
| `INFRA-NETGEN-001` | Netgen toolchain qualification | [INFRA-NETGEN-001](docs/verification/INFRA-NETGEN-001/README.md) |
| `P16-VOL-001` | 3D tetrahedral volume mesh (Tet4) | [P16-VOL-001](docs/verification/P16-VOL-001/README.md) |
| `P16-SIZE-001` | Global and local mesh sizing | [P16-SIZE-001](docs/verification/P16-SIZE-001/README.md) |
| `P16-QUALITY-001` | Mesh quality metrics and validation | [P16-QUALITY-001](docs/verification/P16-QUALITY-001/README.md) |
| `P16-MAP-001` | Geometry ↔ mesh correspondence and regions | [P16-MAP-001](docs/verification/P16-MAP-001/README.md) |
| `INFRA-VIEWER-001` | OCCT visualization toolchain qualification | [INFRA-VIEWER-001](docs/verification/INFRA-VIEWER-001/README.md) |
| `P16-VIZ-001` | Mesh visualisation and inspection | [P16-VIZ-001](docs/verification/P16-VIZ-001/README.md) |
| `P16-CMD-001` | Meshing commands with exact undo and redo | [P16-CMD-001](docs/verification/P16-CMD-001/README.md) |
| `P16-PERSIST-001` | Save and load canonical meshing intent | [P16-PERSIST-001](docs/verification/P16-PERSIST-001/README.md) |
| `P16-CLI-001` | Headless meshing workflows | [P16-CLI-001](docs/verification/P16-CLI-001/README.md) |
| `P16-REFMOD-001` | Meshing reference models RM-MESH-01..08 | [P16-REFMOD-001](docs/verification/P16-REFMOD-001/README.md) |
| `P16-QUAL-001` | Full P16 qualification | [P16-QUAL-001](docs/verification/P16-QUAL-001/README.md) |

Decisions: [ADR-030](docs/architecture/decisions/ADR-030-a-mesh-is-derived-state-and-a-meshing-control-is-the-intent.md),
[ADR-031](docs/architecture/decisions/ADR-031-a-mesh-node-is-a-handle-not-an-identity.md),
[ADR-032](docs/architecture/decisions/ADR-032-a-mesh-boundary-names-a-cad-face-and-a-selection-is-never-a-mesh-entity.md),
[ADR-033](docs/architecture/decisions/ADR-033-the-volume-mesher-is-a-backend-behind-an-enforced-boundary.md).

### What P16 leaves for later

**Tet4 only, and Tet4 is not enough for accurate stress in bending** — the
linear tetrahedron has constant strain and converges slowly on exactly the
stress concentrations engineers care about. ADR-031 records that as a known
limitation of the P16/P17 foundation rather than leaving it to be discovered as
a validation failure, which is why the element type is a tag from the first day.
Adding a second family costs a new struct, not only a new enumerator.

No quadratic elements, no hex meshing, no 1D or 2D element families, no adaptive
or boundary-layer meshing, and no convergence tooling beyond the reference
suite's own studies.

Configuration-driven meshing is **blocked by design**: meshing refuses while a
configuration override is active, because configuration regeneration does not
yet rebuild bodies. Making it supported is a change to the configuration
contract, not to meshing.

The GUI meshes with default controls rather than the document's `MeshControl` —
wiring the two needs GUI undo. A drilled hole's cylindrical wall carries no
`FaceName`, so it cannot be a forward query target; mesh edges are not mapped at
all, deliberately, because the alternative is geometric classification with a
tolerance. Permanent semantic topology across arbitrary edits is `P21`'s.

`P17` consumes this layer as a validated `Mesh` plus a `GeometryMeshMap`, so a
load or a restraint is expressed in CAD terms and resolved to current facets
per mesh — which is what makes it survive a remesh.

---

## Structural FEA — Qualified

`P17` gave BetterCAD a linear-static structural solver whose every input is
canonical state it does not own, and whose every output is derived state
that cannot become authority.

```text
authoritative CAD model + P15 material + P16 meshing intent
→ structural analysis definition      loads, restraints and solver settings
                                      naming CAD faces, never mesh entities
→ one validated input boundary        requireStructuralModel: geometry
                                      eligible, mesh HELD and CURRENT,
                                      material complete for the MODE
→ staged acceptance                   quality on measured accuracy laws,
                                      restraints, loads -- stops at the
                                      first refusing stage
→ DOF numbering from the mesh         interleaved 3k + c + 1, remesh-bound
→ Tet4 element, CSR assembly          one Voigt order, engineering shear
→ SimplicialLDLT behind a pivot gate  and an INDEPENDENT residual
→ displacement / strain / stress /    recovered through the same B and D
  von Mises / principal / hydrostatic
→ reactions = the constrained         force AND moment equilibrium as a gate
  residual
→ a result keyed to the mesh stamp,   inspectable, never persisted, never
  the material and analysis revisions restored by undo, never a second
                                      authority in the GUI or the CLI
```

**CAD geometry, the P15 material and the P16 mesh stay the authority; the
result is derived.** Solving leaves every committed reference document
byte-identical with its revision unmoved. A geometry, mesh-intent, material,
load, restraint or solver-settings edit stales the result for exactly that
reason, an undo does not resurrect it, a failed regeneration cannot yield a
current one, and a loaded document holds intent and no result. A `NodeId`
cannot reach a load, a restraint, a command or the file; the one mesh-local
load that carries one refuses the save.

**Refused, never approximated.** An under-constrained model is refused by
the pivot gate with nothing published; a stale mesh at the inputs stage
before anything is numbered; an unresolved face at the load stage with
nothing substituted; an inverted or degenerate element before any stiffness
exists. The solver's own success is never sufficient: the residual is
recomputed over BetterCAD's own matrix.

**Validated against closed forms that cannot call BetterCAD.** The patch
test and the axial bar on rollers to roundoff (1e-15 against gates of 1e-9);
the mean axial stress under a pressure (`-p`) and under self-weight
(`-rho g H / 2`) exactly; the pressure resultant and `rho V g` balanced by
the reactions; rigid-transform invariance with connectivity correspondence;
local refinement proved by a mirror document; a cantilever convergence study
that is recorded as a trend, with the linear tetrahedron's bending lock and
its cause measured rather than excused.

Qualified at tests fingerprint `90f93c52` on 2026-10-15. `P17-QUAL-001`:
4009/4009 tests in debug-ext, release-ext and debug-shared-ext, each from
clean; 1138 selected tests × 5 repeats in two presets; **0 warnings in all
three builds** under `-Werror` over 658 objects; no-op rebuilds doing 0
compiles and 0 links; suite fingerprints and CLI solve payloads bit-identical
across the three presets; 206 ADR clauses checked against the code; seven
mutation probes on the production tree, seven killed; qualified tree
identical to the committed tree.

| Milestone | Capability | Evidence |
| --- | --- | --- |
| `P17-ARCH-001` | Structural FEA architecture; ADR-034 to ADR-036 | [P17-ARCH-001](docs/verification/P17-ARCH-001/README.md) |
| `P17-DATA-001` | Analysis / result data model, identity and currentness | [P17-DATA-001](docs/verification/P17-DATA-001/README.md) |
| `P17-MAT-001` | Structural material resolution through P15 | [P17-MAT-001](docs/verification/P17-MAT-001/README.md) |
| `P17-DOF-001` | DOF numbering and the constraint model; ADR-037 | [P17-DOF-001](docs/verification/P17-DOF-001/README.md) |
| `P17-ELEM-001` | Tet4 linear-elastic element | [P17-ELEM-001](docs/verification/P17-ELEM-001/README.md) |
| `P17-LOAD-001` | Structural loads: traction, pressure, gravity, nodal | [P17-LOAD-001](docs/verification/P17-LOAD-001/README.md) |
| `P17-BC-001` | Structural restraints | [P17-BC-001](docs/verification/P17-BC-001/README.md) |
| `P17-ASSEMBLY-001` | Deterministic CSR assembly; ADR-038 | [P17-ASSEMBLY-001](docs/verification/P17-ASSEMBLY-001/README.md) |
| `P17-SOLVE-001` | Linear solver with a pivot gate and a residual gate; ADR-039 | [P17-SOLVE-001](docs/verification/P17-SOLVE-001/README.md) |
| `P17-POST-001` | Displacement / strain / stress recovery; ADR-040 | [P17-POST-001](docs/verification/P17-POST-001/README.md) |
| `P17-REACTION-001` | Reaction forces and equilibrium validation; ADR-041 | [P17-REACTION-001](docs/verification/P17-REACTION-001/README.md) |
| `P17-VALID-001` | Structural acceptance policy and the one solve entry; ADR-042 | [P17-VALID-001](docs/verification/P17-VALID-001/README.md) |
| `P17-VIZ-001` | FEA visualisation and inspection; ADR-043 to ADR-046 | [P17-VIZ-001](docs/verification/P17-VIZ-001/README.md) |
| `P17-CMD-001` | Structural commands with exact undo and redo; ADR-047 | [P17-CMD-001](docs/verification/P17-CMD-001/README.md) |
| `P17-PERSIST-001` | Save and load canonical structural intent; ADR-048 | [P17-PERSIST-001](docs/verification/P17-PERSIST-001/README.md) |
| `P17-CLI-001` | Headless structural FEA | [P17-CLI-001](docs/verification/P17-CLI-001/README.md) |
| `P17-REFMOD-001` | Structural reference models RM-FEA-01..10 | [P17-REFMOD-001](docs/verification/P17-REFMOD-001/README.md) |
| `P17-QUAL-001` | Full P17 qualification | [P17-QUAL-001](docs/verification/P17-QUAL-001/README.md) |

Decisions: ADR-034 to ADR-048 in `docs/architecture/decisions/`.

### What P17 leaves for later

**Linear Tet4 locks in bending.** The committed cantilever deflects a few
per cent of Timoshenko across three meshes, monotonically and with the error
falling, because OCCT never subdivides a developable wall along its ruling
and every wall tetrahedron is a needle the full length of the body
(P17-REFMOD-001 findings F2 and F3). Bending on a prismatic body is a trend
BetterCAD measures, not a number it certifies, until a quadratic element or a
surface mesher that subdivides along a ruling exists.

**The 1e-12 moment-equilibrium threshold is crossed by a correct bending
solve** of ~2500 needle elements (F1). The committed cantilever passes it;
the measurement is on file and revising a qualified threshold is the owner's
decision.

Linear static only: no nonlinearity, contact, buckling, dynamics, thermal
stress, shells, beams, Tet10 or multi-solid assemblies -- each refused rather
than approximated (ADR-034). The Qt inspector surface is not wired; the
headless display layer is qualified and the desktop application draws
nothing structural. A pressure, traction or gravity glyph is data and not
drawn. A drilled hole's wall still carries no `FaceName`, so it cannot be
loaded or restrained. Configuration-driven analysis is blocked by design, as
meshing is. Eigen's MPL-2.0 licence is recorded and the project's own licence
remains the owner's to choose.

`P18` consumes this layer as a result keyed to the mesh it was computed on,
a material resolved per analysis mode, and a validation entry whose gates it
inherits rather than re-asks.

---

## Thermal Analysis — Qualified

`P18` gave BetterCAD steady and transient (backward-Euler) heat conduction on
Tet4 meshes: volumetric heat generation, surface heat flux, prescribed
temperature and convection, with the same authority discipline as `P17` — CAD
geometry, the P15 material and the thermal intent are canonical; the mesh,
temperatures, gradients, heat fluxes, heat balance and validation report are
derived and never persisted. Qualified 2026-10-27 by `P18-QUAL-001`: 5804 of
5804 tests in three presets, qualified tree == committed tree. Thermal contact
and the coupling of temperature fields into structural analysis (thermal
expansion and stress) remain later work. Open owner decisions are listed in
[P18-QUAL-001](docs/verification/P18-QUAL-001/LIMITATIONS.md).

---

## CFD Integration — In progress

`P19` starts with laminar, incompressible, Newtonian flow solved by
cell-centred finite volume on the `P16` Tet4 mesh, steady and transient, with
the same authority discipline as `P17` and `P18`. `P19-ARCH-001` (PASS
2026-10-27) fixed the scope, the layer (`cfd` at 42, below thermal and
structural) and the input boundary; see
[P19-ARCH-001](docs/verification/P19-ARCH-001/README.md). `P19-DATA-001` (PASS
2026-10-27) defined the canonical analysis definition, its semantic fingerprint
and the result's provenance, keeping the fluid a P15 material and every field
derived; see [P19-DATA-001](docs/verification/P19-DATA-001/README.md).
`P19-FLUID-001` (PASS 2026-10-28) gave P15 a dynamic viscosity and an
incompressible-flow consumer, so a fluid is a P15 material like any other and CFD
reads it with no fluid data of its own; see
[P19-FLUID-001](docs/verification/P19-FLUID-001/README.md).

`P19-MESH-001` (PASS 2026-10-31) answers the phase's critical question — `P16`'s
Tet4 mesh serves directly as the finite-volume mesh, with no adapter and no second
geometry authority, its interior faces, owner and neighbour derived from it and
never persisted — and proves what `P16` does not itself guarantee through a staged
gate with nine named refusals, bound to the mesh stamp (`ADR-063`). It was
qualified by a second attempt: 5939/5939 in each of the three presets, 2803 x 5 in
two, 0 warnings over 755 objects, 17 of 17 stages exit 0, and the twelve frozen
identities unchanged before and after. Attempt 1 of 2026-10-28 failed 5 of 17
stages for an environmental reason and the machine that produced it was then
permanently replaced, so the replacement environment was qualified in its own right
before the gate was re-run; see
[P19-MESH-001](docs/verification/P19-MESH-001/README.md). Its one measured finding
is that the `P16` reference meshes are poor finite-volume meshes, which makes the
non-orthogonal correction mandatory in the solver and is judged by
`P19-VALID-001`.

`P19-TOPO-001` (PASS 2026-10-31) builds the finite-volume topology on that
proved mesh: a derived, compactly indexed value, never persisted, carrying the
mesh generation it was built for. Four index spaces become four distinct types,
so a cell subscript, a face subscript, a boundary-face subscript and a mesh
handle cannot be confused by a call — zero means "the first cell" in one space
and "invalid" in the other — and no container is sized by a handle's value
(`ADR-064`). Eight named refusals are a subset of what `ADR-063`'s gate already
refuses, so possession of a topology is the proof. Two of them were added during
adversarial review rather than documented away: `faceNodes` promised outward
winding unconditionally while the builder checked neither cell volume nor
shared-face orientation, and that guarantee is what `P19-GEOM-001`'s area vectors
will rest on. Qualified from clean at 5998/5998 in each of three presets, 2839 x
5 in two, 0 warnings under `-Werror`, 17 of 17 stages exit 0, and the eight
qualified subtrees identical before the first build, after the last test run and
in the committed tree; see
[P19-TOPO-001](docs/verification/P19-TOPO-001/README.md). Two of its twenty-two
checklist items are recorded narrower than their wording and one collapses onto
another, each stated in the evidence rather than glossed.

`P19-GEOM-001` (PASS 2026-10-31) turns that topology into the finite-volume
geometric metrics a solver integrates over: the cell volume and centroid, the
face area vector, area, unit normal and centroid, the owner-to-neighbour and
cell-to-face vectors, the orthogonal distance, the orthogonality cosine, the
non-orthogonality angle, the skewness, the boundary outward normal, and the
per-cell surface-vector sum with its closure. One quantity is primary and the
rest are derived from it — the area vector `Sf` — so a face's normal and the
closure identity cannot disagree by construction, and `AreaVector3D` is three
`Area`s rather than three doubles, so a face normal cannot be added to a
position while the dimensionless quality metrics cancel to `double` at the
boundary (`ADR-065`). The key identity is measured rather than asserted away:
`Σ Sf` closes to 2.03e-16 of each cell's own area scale on all eight reference
meshes, and the divergence theorem closes to 1.21e-14 of each cell's volume.
The quality metrics rest on a closed form the milestone had to derive — an
analytic face pair fixing `cos = 3/5` and skewness `= sqrt(10)/15` — because
agreeing with `P19-MESH-001`'s recorded figures checks the plumbing and not the
definition: both transcribe one formula. Qualified from clean at 6058/6058 in
each of three presets, 2964 x 5 in two, 0 warnings under `-Werror`, 17 of 17
stages exit 0, and the eight qualified subtrees identical before the first
build, after the last test run, when recomputed independently afterwards and in
the committed tree (`9d2b1ed`); see
[P19-GEOM-001](docs/verification/P19-GEOM-001/README.md). Two reviews ran
before the freeze and each found a real defect — a compile-fail control target
that did not build under `-Werror` while `ctest` reported its group 29 of 29
green, and a claim in the ADR and the public header written without the work
behind it, four guards called unreachable being reachable. The summary measures
and judges nothing, so `P19-MESH-001`'s owner finding F1 is neither resolved
nor extended here, and a bound on the worst-conditioned quantity,
`orthogonalDistance`, is `P19-VALID-001`'s work.

`P19-DOF-001` (PASS 2026-11-05) fixes where a CFD field lives, how long an array
of it is, which subscript addresses one component, and what each kind of edit
invalidates: pressure and velocity cell-centred, mass flux on faces, under a
frozen variable table that carries each one's SI dimensions. The layout is a
derived index space — the Document holds no field array and no layout, and
canonical state holds neither, so a layout is rebuilt from the topology rather
than persisted (`ADR-066`). A cell vector is component-blocked, `component *
cells + cell`, reversing `P17`'s interleaved `3k+c`, because `ADR-058` fixes
SIMPLE-family segregated coupling. Identity is typed rather than documented:
`CellIndex`, `CellVectorIndex`, `FaceIndex` and `BoundaryFaceIndex` are mutually
non-interconvertible, no mesh handle can subscript anything, and 35 compile-fail
gates hold that at build time instead of asserting it at run time. Invalidation
is three-valued rather than boolean — a remesh invalidates every location, a
patch re-attribution invalidates the boundary layout and leaves the cells
current, and a fluid-property edit invalidates nothing. The array lengths and the
patch partition are counted off `P16`'s mesh and mapping on all eight reference
meshes rather than off the implementation, and the blocked subscript is proved a
bijection onto `[0, 3 cells)` by marking every slot, because a round trip alone
would pass for a layout that mapped two cells to one slot. Qualified at 6120/6120
in each of three presets, 2991 x 5 in two, 0 warnings under `-Werror`, 17 of 17
stages exit 0, and the eight qualified subtrees identical across five readings,
the committed tree (`cdd42a3`) among them; see
[P19-DOF-001](docs/verification/P19-DOF-001/README.md). Two reviews ran
before the freeze, independently, and both found the same four evidence defects —
among them a canonical-state proof that was vacuous, having interrogated a struct
the document never held, and a staged-gate line that was a no-op on every tested
path. Both of those fixes are verified by mutation: deleting the line now fails
loudly where before it passed the whole suite. No field values exist yet, and the
result arrays `CfdResult.hpp` had promised at this milestone are deliberately
deferred — boundary and initial conditions, the operators and the solve are later
milestones, each the owner's to authorize.

`P19-BC-001` (PASS 2026-11-05) resolves the boundary conditions of one CFD
analysis against one mesh, and produces what an operator will read: for every
boundary face, the condition that owns it, that condition's kind, and the values
it prescribes. The per-field treatment is a FROZEN TABLE — six condition kinds
against `p`, `U` and `phi` — and `ADR-067` is the only place it exists, with
four policy values rather than two: a slip wall's velocity is neither fixed nor
zero-gradient but a symmetry condition, and the mass flux at a pressure boundary
is an outcome rather than a datum. Two values would have left every operator to
re-derive the treatment from the kind, and the first that forgot would admit
through-flow at a symmetry plane — a wrong answer that still converges. A
prescribed value is held per boundary FACE, not per condition, because a
prescribed pressure varies hydrostatically along a face that is not level and an
inlet's `-v n_hat` varies over a curved one. Resolution runs through `P16` alone and
there is NO nearest-face fallback: a `FaceName` either resolves to facets or is
refused by name. Coverage is proved in both directions — forward through
`boundaryFacetsOf`, assigned off the binding, and the reverse attribution
counted through `P16`'s own `sourceFaceOf` — and all three counts are recorded
rather than the forward one being asserted twice, because only the third can
disagree. Twenty refusals are declared in phase order and nineteen are asserted
by tag rather than by message, after review found eight whose tag could have
been swapped for another with every message assertion still passing. Validated
against instruments the implementation does not share: an independent
barycentric containment oracle, centroids recomputed from the mesh nodes, and
facets chosen by a node plane read off the mesh rather than by list order.
Qualified at 6184/6184 in each of three presets, 3039 x 5 in two, 0 warnings
under `-Werror`, 17 of 17 stages exit 0 from clean on the first attempt, and
2,136 assertions in 48 cases identically in all three builds, with the eight
qualified subtrees identical across six readings, the committed tree
(`1e0858a`) among them; see
[P19-BC-001](docs/verification/P19-BC-001/README.md). Thirteen behavioural
defects were found before the freeze — four by the author's own adversarial
pass, which two independent reviews then confirmed, and nine more by those
reviews — all fixed, six verified by mutation; among them a cardinality check
that compared the forward count with itself, an over-determined pressure datum
that was accepted, and a claim array indexed on a precondition established
twenty lines earlier, which crashed rather than diagnosed. Three of the author's
own assumptions were corrected by measurement rather than argument and are
recorded as such. No matrix, source vector or solve exists, so nothing here is
shown to produce a correct flow field — only a correct and fully attributed
statement of the boundary intent; the operators, the coupling and the solve are
later milestones, each the owner's to authorize. One narrowing came out of this
milestone's own review and is the owner's to revisit: a moving wall is
planar-only, so a sliding cylindrical wall is refused by name rather than
approximated.

`P19-IC-001` (PASS 2026-11-06) gives a CFD analysis the starting state a solve
will iterate from, and answers the ownership question `ADR-066` had deliberately
left open. It is the first milestone to produce a LAYOUT-INDEXED FIELD ARRAY — one
whose length comes from `size(CfdVariable)` and whose subscript comes from the
field layout — and the answer `ADR-068` reaches is that **no field storage is
added at all**: the expansion writes into a `std::span<double>` the caller owns
and sizes. That discharges `ADR-066` Decision 1 rather than pre-empting it, since
the clause that decides is "with a use in hand" and the only consumer of an
initial field is a solve, which is a later milestone; and it turns "expanded cell
arrays remain derived" from a promise into a structural fact, because the `cfd`
module owns no field memory for a later edit to persist by accident. The cost is
accepted and recorded: every consumer sizes its own array, so a wrongly-sized
array becomes a case refused by name rather than one that cannot be expressed. A
uniform initial field STAYS UNIFORM and gravity does not enter it, where
`ADR-067` made a prescribed *boundary* pressure hydrostatic — a boundary pressure
is a constraint the solve must satisfy along a face that is not level, while an
initial field is a guess whose only requirement is to be a legal state — and the
consequence is stated rather than hidden: a gravity-driven case starts further
from its solution than it needs to. Five of the twelve checklist items were
LARGELY SATISFIED BEFORE the milestone opened, by `P19-DATA-001` and `ADR-061`,
and the evidence says so rather than claiming the work; what this milestone adds
to them is one measured gap closed — the existing NaN/Inf test exercised one
velocity component and the pressure, so it would have passed unchanged checking
only `x` — and the structural guarantees that keep them true. Qualified at
6240/6240 in each of three presets, 3075 x 5 in two, 0 warnings under `-Werror`,
17 of 17 stages exit 0, and 6,589 assertions in 36 cases identically in Debug and
Release, with the eight qualified subtrees identical across six readings, the
committed tree (`c7ce82a`) among them; see
[P19-IC-001](docs/verification/P19-IC-001/README.md). Nineteen review findings
were acted on across three passes — the author's own, written down before either
independent review reported, and two independent agents given the code but not
those conclusions — all fixed, none deferred. One was a substantive code defect:
the analysis's own mesh control was omitted from the source binding with no safety
argument available, so re-pointing an analysis at another mesh left the field
reporting itself current for an analysis whose array must now be a different
length; it is refused by name. THREE DOCUMENTATION CLAIMS WERE WITHDRAWN AS
MEASURABLY FALSE, each by a review that measured rather than argued — a
rationale for a check order that did not hold, a "lossless" fingerprint that is a
64-bit digest, and a claim that every written slot was pinned when reversing the
cell order passes every test, because a uniform field's array is byte-identical
under any permutation of cells. That last one is recorded as a live limitation
rather than repaired: the component-to-block mapping is pinned, the cell arm is
not, and the first milestone to write a NON-UNIFORM field will be the first able
to observe it. The two reviews disagreed once, over whether a remesh changes the
cell count, and the disagreement was settled by measurement rather than by
seniority. Recorded as less than a reader might assume: there is no solve, so
nothing here produces a flow field — only a legal, correctly placed and
correctly bound starting state — no performance result is measured, and "exact
undo / persistence semantics" is met as value semantics plus the fingerprint,
not as a round trip, because no CFD serializer and no CFD command exist yet.
Those are `P19-PERSIST-001` and `P19-CMD-001`, each the owner's to authorize.

`P19-DIFF-001` (PASS 2026-11-06) is the first milestone in which BetterCAD
ASSEMBLES A MATRIX FROM A MESH. It discretises the diffusion term
`div(mu grad U)` over the finite-volume topology as a validated CSR matrix and a
source vector, and `ADR-069` records what kind of object that is: a
PER-COMPONENT SCALAR operator, which follows from `ADR-066` rather than being
chosen here, because a segregated momentum predictor assembles no row coupling
`u` to `v`, so one component's block simply *is* the cell-centred scalar array.
The surface vector is split as `S_f = E_f + T_f` by the OVER-RELAXED
decomposition, and the reason is the ratio that actually governs a
deferred-correction iteration: `|T_f|/|E_f|` is `sin(theta)` there and stays at
or below 1 at every angle, where minimum correction gives `tan(theta)` and so
reaches 81.8 at the 89.3 degrees `ADR-063` measured on a reference mesh. The
algebra was verified numerically over twenty thousand random configurations
before any C++ existed, and both wrong turns taken on the way to it are recorded
in the decision rather than tidied out of it.

**The non-orthogonal correction is caller-supplied, and the omission is measured
rather than assumed away.** The operator needs a gradient that belongs to
`P19-GRAD-001`, so it accepts one, computes each face's deferred arm, and reports
the worst omitted fraction in its summary; with no gradient supplied it is a
correct Laplacian only on an orthogonal mesh, and no P16 reference mesh is one —
the best maximum is 64.09 degrees. `ADR-063` F1 calls the correction "mandatory
in the discretisation, not an option" and in the same breath reserves any
accept-or-refuse threshold to `P19-VALID-001` and the owner, so this operator
REPORTS rather than refuses, and the evidence carries that as a limitation
rather than as a completion. Two of the fourteen checklist items are recorded
narrower than their wording: the correction policy is a policy and a
measurement, and the symmetry guarantee is STRUCTURAL — `gamma` is computed once
and scattered to both sides, so a zero symmetry error is a property of the
scatter and not an independent confirmation of the coefficient, which is what
the row sum and the analytic fixture are for.

Qualified at 6311/6311 in each of three presets, 3124 x 5 in two, 0 warnings
under `-Werror`, 17 of 17 stages exit 0, and 31,688 assertions in 49 cases
identically in Debug and Release, with the eight qualified subtrees identical
across six readings, the committed tree (`dc5e5a1`) among them; see
[P19-DIFF-001](docs/verification/P19-DIFF-001/README.md). Twenty mutations were
applied one at a time and all twenty killed, and the sweep earned its place:
the adversarial review had PREDICTED that eight specific source-vector
mutations would survive, and the eighth died only after the test written to
catch it was itself repaired — a test that could not detect a hard-coded face
index, because internal faces are numbered first and its only contributing face
was therefore index 0. Three probes are reported VOID rather than as survivors
because they did not compile, which is a lesson `P18-ELEM-001` had already
written down. Two review findings were defects in what this author had WRITTEN
DOWN rather than in the code: a decision describing an interpolation weight that
did not exist, implemented rather than reworded; and a symmetry claim too strong,
corrected to the owner explicitly rather than left standing. The author's own
tests then caught two of the author's own defects before any review — a weight
formula that is not convex on a skewed mesh, which measured 3.6095 on the
Cylinder, and a viscosity interpolation that was not exact for a constant field.
Recorded as less than a reader might assume: **there is no solve and no momentum
equation**, so nothing here is shown to produce a flow field — this is one
operator of an equation that does not exist yet, and the matrix has never been
handed to a solver — no skewness correction exists, and no performance result is
measured, because nothing in `cfd` assembled a matrix before this milestone and
so there is no baseline to compare against.

`P19-GRAD-001` (PASS 2026-11-07) RECONSTRUCTS THE CELL-CENTRED GRADIENT, of a
pressure and of each velocity component, and it is the milestone whose method
choice was settled by measurement before any C++ existed. `ADR-070` keeps BOTH
methods — weighted least squares as the default, Green-Gauss retained and
measured against it — and the reason is a numerical fact about the mesh rather
than a preference: the divergence identity `sum_f C_f (x) S_f = V I` holds to
2.900e-16 over twenty thousand random tetrahedra, so Green-Gauss evaluated with
the TRUE face value is linear-exact on ANY mesh, and every bit of its
inexactness is therefore the INTERPOLATION's, not the method's. Measured against
an exactly linear field on a controlled fixture, Green-Gauss with the true face
value stays at rounding at every skewness from 0.00 to 6.50 while the same loop
with an interpolated face value grows to 4.53e+02, and least squares — which
interpolates nothing — stays at rounding throughout. The two scripts behind
those tables are committed with their output, because an adversarial review
found the founding number had no artifact behind it, and a quantitative
justification with no log is a failed gate.

**Five code defects were found by review, and the fifth is the one worth
remembering.** The suite's conditioning oracle had been written as a
line-for-line copy of the function it was meant to check — which is not
validation at all — and replacing it with an independent cyclic Jacobi
diagonalisation showed that the textbook trigonometric form for the SMALLEST
eigenvalue is wrong by a factor of 3.2 at a conditioning of `1e12`, and reports
a positive-definite matrix as having a non-positive eigenvalue. That is a FALSE
REFUSAL: a cell the operator could have solved, declined. The smallest
eigenvalue is recovered from the matrix invariants now, and the flat-cell
sweep's split moved from 4-refused/1-solved to 3-refused/2-solved as a direct
result, both figures recorded. The other four were a non-finite gradient
returned as a SUCCESS — a cell value of `1e308` overflows the surface sum to
infinity, cancels to NaN, and passed every gate because none of the three
finiteness checks looked at the output — a diagnostic that refuted itself by
printing an eigenvalue ratio of 3 against a limit of `1e12`, an out-of-bounds
read held off only by a frozen table in another module, and a constant-mode
flag hard-coded true whose stated justification was simply false.

Qualified at 6381/6381 in each of three presets, 3170 x 5 in two repeats with
15850 executions each, 0 warnings under `-Werror`, 17 of 17 stages exit 0, and
82,762 assertions in 46 cases identically in Debug and Release, with the eight
qualified subtrees identical across six readings, the committed tree (`6c0ea6a`)
among them; see [P19-GRAD-001](docs/verification/P19-GRAD-001/README.md). Eighteen of twenty
mutations were killed and two RETIRED WITH REASONS, and this sweep was the most
useful thing in the milestone precisely because it went badly at first: the first
pass killed only 10, and four of its seven survivors were real defects in the
test suite rather than mutations to be explained away. The sharpest took three
passes to close — a constant-field test cannot see an unstable interpolation at
`0.0`, and cannot see it at `17.25` either, because 69/4 is dyadic and both
products are exact whatever the weight; a search over 200,000 constants found
that 16% of them discriminate at the fixture's weight of 2/3, and the suite uses
`12345.6789`, which the unstable form returns as `12345.678900000003`. One
prediction in `ADR-070` was CONTRADICTED twice — the inverse-distance weights
cannot affect linear exactness in exact arithmetic, but changing them changes the
normal matrix's scaling, so rounding error rises past a bound tight enough to
see — and the decision now separates the two claims instead of being quietly
reworded. One test was deleted outright and half of another cut, because a test
that reads as evidence and is not is worth less than no test: one of them never
called the operator, and one of its assertions reduced to
`value + weight * 0.0 == value`.

Recorded as less than a reader might assume: **nothing consumes this output
yet.** It produces a CELL gradient, and `assembleCfdDiffusion` wants a FACE
gradient, so the interpolation that would close `P19-DIFF-001`'s own limitation
is a coupling step this checklist does not contain and that wiring it in was not
authorized to do. The predictors cannot disagree with the reconstructions because
they share one decision path, so their tests pin that a valid input predicts no
problem, not that two implementations agree. Skewness is MEASURED and reported,
never corrected. And there is still no solve and no momentum equation, so
nothing here is shown to produce a flow field; no performance result is measured,
because nothing in `cfd` has a baseline and this milestone does not create one.

`P19-DIV-001` (PASS 2026-11-07) TAKES THE DIVERGENCE over that same
finite-volume topology — of a face mass flow rate and of a cell velocity field —
and it is the milestone in which a measurement contradicted the design's own
first draft before a line of it was written. `ADR-071` stores the
FACE-INTEGRATED flux and divides by the cell volume on read, because the
integral is the conserved quantity, and it scatters one face product into the
owner and its exact negation into the neighbour rather than gathering per cell.
That choice rests on a measured fact: `(-S).U` is bit-for-bit `-(S.U)` over two
hundred thousand trials with not one mismatch, so an internal face's two
contributions cancel EXACTLY, and conservation becomes a property of the
traversal instead of a numerical hope.

**But the global sum is NOT bit-exact, and the ADR's first draft said it would
be.** Each cell accumulates its own faces before the two meet, and
`(a + x) + (b - x)` is not `a + b` in floating point: over twenty thousand
random meshes the global imbalance is bit-zero on 40.34% of them, with a worst
relative figure of 1.125e-16. A test asserting a bit-zero global imbalance would
therefore have FAILED on a correct operator. The imbalance is a published
measurement now, the ADR carries the withdrawn prediction rather than being
quietly reworded, and two summary members were deleted before any code existed
for the same reason — one would have asked whether `x + (-x) == 0.0` for a finite
`x`, and the other would have reported a constant-mode flag that the measurement
shows to be false as derived. The first draft of the experiment itself divided by
`max(|total|, 1e-300)` and reported a worst gap of `9.992e+284` for arithmetic
correct to half an eps, which is `P19-GRAD-001`'s own recorded mistake recurring
in the first thing this milestone measured; the denominator is the magnitude of
the terms accumulated now, never the answer.

**Reading the existing code changed the design twice, and both corrections are
conservation errors avoided rather than found.** `phi` was already the
face-INTEGRATED `rho (U . Sf)` in kg/s by `ADR-066`, not a per-area flux, so its
divergence is a pure signed sum with no area factor — a design that multiplied
by the face area would have been wrong while staying dimensionally plausible.
And there is no prescribed `phi` at a boundary to read at all: a boundary face
carries a pressure and a velocity, and `ADR-067`'s `phi` column is `Computed` at
both pressure boundaries, so the mass path consults no boundary and must NOT
refuse `Computed`, which is the opposite of what a defensive reading suggests. A
compile-fail gate holds that asymmetry at compile time.

**THE MILESTONE REACHED A PASSING QUALIFICATION WITH NO INDEPENDENT REVIEW, and
that is recorded as its first defect rather than tidied away.** The three
milestones before it each carry one, and the lifecycle puts adversarial review
BEFORE the gate. Two reviews were then run against the frozen tree, and they
converged independently on the same two findings. The first: **the interpolation
weight was completely unpinned.** Every internal face the velocity path ever
interpolated across under an assertion has `w = 2/3` exactly — both hand
fixtures have a 2:1 apex-height ratio — so replacing the whole weight with
`return 2.0 / 3.0;` survived the entire suite, and the oracle that should have
caught it was a character-for-character transcription of the production
expression under a file header claiming independence. That is `P19-GRAD-001`'s
own recorded defect recurring one milestone later, and it is the clearest
argument in the phase so far for why an oracle must be re-derived rather than
copied: the weight is now checked against a form written from the two cell
centroids and the face centroid, reading neither accessor production reads, and
checked over the Cylinder's 946 internal faces where the weight genuinely
varies. The second: **the exact-cancellation claim was pinned only where it is a
tautology**, on the mass path, where the face value is one array slot and the
assertion holds for any implementation — while `ADR-071` Decision 2 is a claim
about the velocity path. Three further code defects followed: range guards that
ran AFTER the span reads they protect, a degenerate face silently returning a
midpoint weight instead of refusing, and a phase reporting another phase's
enumerator. And `ADR-071` Decision 6's claim that the weight "has exactly one
definition, and both operators read it" was FALSE WHEN IT WAS WRITTEN — there
were three copies — so the ADR carries a dated amendment and the gradient's
surviving private copy is recorded as a limitation rather than silently
unified.

Qualified at 6449/6449 in each of three presets, 3210 x 5 in two repeats with
16,050 executions each, 0 warnings under `-Werror`, 17 of 17 stages exit 0, and
36,930 assertions in 40 cases identically in Debug, Release and Debug-shared,
with the eight qualified subtrees unchanged across the run and equal to the
eight its freeze recorded in advance; see
[P19-DIV-001](docs/verification/P19-DIV-001/README.md). Twenty-two mutations
were killed in all — sixteen in the original sweep, whose first pass killed only
8 of 16, and six more written for the claims the reviews pinned. Each original
survivor had the same shape, an assertion that reads as evidence and cannot
fail. A closed cell's flux total is `U . sum_f S_f`, which closure makes zero for
ANY constant velocity, so a test of which velocity was used could not fail; and
moving the claim to a single facet was not enough, because three of a
tetrahedron's four facets lie in coordinate planes and are each blind to two
components. A valid moving wall cannot distinguish a prescribed velocity from a
prescribed zero at all, and that follows from `P19-BC-001`'s own validation,
which requires a wall velocity to be TANGENTIAL: a velocity inlet, whose
prescribed velocity is NORMAL to the face, is the case that can. A fixture built
so the boundary total nearly cancels made it cancel EXACTLY, so a wrong
denominator escaped through its own `> 0` guard. One survivor was a finding
about the code rather than the suite: the per-cell finiteness check is redundant
in BEHAVIOUR, since the total is the sum of the per-cell values, and it is kept
for the DIAGNOSTIC — the totals check names no cell — with the test now
asserting the message says which one. The sharpest of the six later probes had
to be STRUCTURAL rather than a one-line change, because exact cancellation
cannot be broken by flipping a sign: `-flux`, `(-1.0) * flux` and `0.0 - flux`
are all the same double, so the probe is a gather that re-derives the face value
from the neighbour's side — precisely the implementation Decision 2 rejects.

Recorded as less than a reader might assume: **nothing consumes this divergence
either.** It is one term of an equation `P19-MOM-001` and `P19-PRES-001` have not
assembled, so no flow field is produced and none is claimed. The velocity path is
exactly as mesh-sensitive as Green-Gauss, because it is the same surface sum with
the same interpolation — linear-exact with the true face value, and no better
than the interpolation on a skewed mesh, with `worstSkewness` as the measured
bound; the trace of a least-squares gradient would be mesh-independent and is NOT
built, since the checklist states the surface-sum identity. Global conservation is
reported, not guaranteed. Mesh quality is reported and never refused, because
`ADR-063`'s own finding reserves a quality threshold to the owner. Two of the
seven refusals are unreachable through the public API and are recorded as
unreached rather than as covered. One problem enumerator is reachable and tested
but observable only through the diagnostic's wording, which sits against the
`cfd` module's own "a message is not a tag" rule and is recorded rather than
fixed, because adding an accessor is public API this checklist does not contain.
And there is no performance result, because nothing consumes the operator and a
baseline for an operator with no caller would be a number with no meaning.

```text
incompressible laminar
→ turbulence
→ thermal flow
→ compressible flow
→ multiphase
```

Pipeline:

```text
CAD
→ fluid-volume extraction
→ mesh
→ boundary conditions
→ CFD solver
→ results
→ CAD revision linkage
```

CFD should integrate through a stable solver boundary rather than duplicate CFD
functionality inside the CAD kernel.

---

## Design Optimization — Planned

Design variables may include dimensions, materials, suppression, angles and
thicknesses. Objectives may include mass, cost, pressure drop, drag, stress,
stiffness, thermal performance and efficiency. Constraints may include stress
limits, displacement, temperature, factor of safety and manufacturing limits.

```text
parameter sweeps
→ gradient-based optimization
→ genetic methods
→ Bayesian methods
→ surrogate modeling
→ topology optimization
```

---

## Semantic Topology — Planned

Stable identity must continue evolving beyond today's named-feature reference
infrastructure: persistent topology naming, feature provenance, adjacency
signatures, semantic matching, ambiguity detection, recovery after
topology-changing edits, and confidence-based matching where appropriate.

The core rule remains:

```text
wrong target is worse than unresolved target
```

---

## Versioning / Collaboration — Planned

Semantic version control: commit, branch, merge, history, revision, tag and
model diff. The system should eventually compare engineering intent rather than
opaque binary files:

```text
Hole003.diameter
8 mm → 10 mm
```

---

## Python / Automation — Planned

pybind11 or equivalent bindings over the production C++ API, exposing
documents, parameters, sketches, constraints, features, assemblies, drawings,
materials, simulations, optimization and export.

The GUI and AI agent must not possess engineering capabilities unavailable
through the public API.

---

## AI Engineering Agent — Planned

Only after production APIs are reliable.

```text
natural language
→ intent extraction
→ engineering plan
→ structured API calls
→ regeneration
→ validation
→ engineering evidence
```

The agent does not directly invent arbitrary B-Rep geometry. Operations remain
typed, validated, undoable and auditable.

---

## Manufacturing / CAM — Planned

Manufacturing metadata, stock, threads, tolerances, surface finish, sheet-metal
data, bend information, 2.5D CAM, tool libraries, feeds and speeds, toolpaths,
G-code postprocessing, sheet-metal unfolding, additive preparation.

---

## Performance / GPU / Scale — Planned

Large model performance, large assembly solving, regeneration profiling,
parallel evaluation, memory usage, GPU acceleration only where mathematically
and architecturally justified, and benchmark infrastructure.

Correctness must remain identical between accelerated and reference paths.

---

## Production Hardening — Planned

Crash recovery, autosave, corruption detection, file migration, large-model
stress testing, thread safety, plugin isolation, diagnostics, profiling,
installers, update system, release qualification, property-based testing, fuzz
testing, stress testing, file compatibility, GUI acceptance.

---

## BetterCAD 1.0

BetterCAD 1.0 does not mean *many features*. It means:

```text
reliable enough to design real mechanical systems
```

Minimum expected production workflow:

```text
parametric part
→ assembly
→ engineering verification
→ technical drawing
→ interoperable export
→ controlled revision
```

Strong 1.0 foundations should include reliable parametric parts, a robust
sketch solver, production feature modeling, assemblies, technical drawings,
stable references, STEP interoperability, deterministic persistence, materials,
mass properties, an automation API, reliable undo and redo, regression
reference models, production release qualification, and documentation.

Simulation and AI should enter 1.0 only if mature enough not to weaken CAD
reliability.

---

## Release Direction

| Release | Theme | Status |
| --- | --- | --- |
| `v0.1` | Modeling foundation | **Released — v0.1.0** |
| `v0.2` | Practical parametric modeling | Qualified capability exists; release not cut |
| `v0.3` | Production parametric CAD | Qualified capability exists; release not cut |
| `v0.4` | Assemblies | `P13` **Qualified**; release not cut |
| `v0.5` | Technical Drawings | `P14` **Qualified**; release not cut |
| `v0.6` | Simulation foundation | Planned |
| `v0.7` | Multiphysics / optimization | Planned |
| `v0.8` | Engineering platform | Planned |
| `v0.9` | AI-native engineering | Planned |
| `v1.0` | Production BetterCAD | Planned |

---

## Reference Projects

Reference models are permanent regression assets.

Existing: the twelve `P11`/`P12` mechanical part models — stepped shaft, bolted
flange, V-belt pulley, bearing housing, mounting bracket, U-bolt among them —
and the eight-model `P13` assembly reference suite, RM-A … RM-H, one of which is
committed deliberately broken so the fault paths are proved by a real artifact.

`P14` should add production drawing references:

```text
machined-part drawing
multi-view dimensioned drawing
section/detail drawing
hole/pattern drawing
toleranced/GD&T drawing
assembly drawing
BOM/balloon drawing
configuration-dependent drawing
```

Later simulation references: cantilever beam, simply supported beam,
pressure-vessel segment, heat sink, pipe flow, lid-driven cavity, airfoil flow.

A reference artifact that finds a regression stays in the repository
permanently.

---

## Quality Gates

Every release candidate should verify clean configuration, clean build, unit
tests, integration tests, regression tests, production reference models,
serialization, import/export, geometry validity, solver determinism, CLI
workflows, GUI workflows where applicable, performance regression, warnings,
artifact hashes and committed-tree identity.

No capability is complete because code exists. Completion means:

```text
implemented
+ tested
+ independently validated
+ adversarially reviewed
+ regression clean
+ evidence recorded
```

A failed required gate blocks qualification.

---

## Milestone Numbering

```text
ROADMAP.md
→ long-term capabilities and sequencing

TODO.md
→ implementation milestone IDs and current authorization
```

Historical milestone IDs are never renumbered. A bare `P<number>` refers only
to an implementation phase owned by [TODO.md](TODO.md).

---

## Project Documents

| Document | Purpose |
| --- | --- |
| [README.md](README.md) | Project overview and getting started |
| [ROADMAP.md](ROADMAP.md) | Long-term capability direction |
| [TODO.md](TODO.md) | Authoritative implementation status |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Architecture and invariants |
| [CLAUDE.md](CLAUDE.md) | Engineering workflow / Definition of Done |
| [docs/architecture/decisions/](docs/architecture/decisions/) | Architecture decisions |
| [docs/verification/](docs/verification/) | Qualification evidence |
| [docs/engineering/](docs/engineering/CHANGE_WORKFLOW.md) | Reusable engineering templates |
