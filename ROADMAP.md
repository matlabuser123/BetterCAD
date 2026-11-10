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
(both 2026-11-06) and P19-GRAD-001 and P19-DIV-001 (both 2026-11-07) and
P19-CONV-001, P19-MOM-001 and P19-CONT-001 (all 2026-11-08) and
P19-PVCOUPLE-001, P19-LINSOLVE-001 and P19-STEADY-001 (all 2026-11-09) and
P19-TRANSIENT-001 (2026-11-10) and P19-TURB-ARCH-001 (2026-11-10)
have PASSED.
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
reason it does not count is the useful part. P19-CONV-001 then qualified from
clean on ONE attempt, by doing the only thing its predecessor had got wrong:
BOTH INDEPENDENT REVIEWS RAN BEFORE THE FREEZE. They returned 25 findings — six
code defects, three false published claims and eight test defects — and every
one was fixed before the gate was launched, so no run had to be discarded. The
most serious was a MISSING OWNER/NEIGHBOUR RANGE GUARD that let an unvalidated
SIZE_MAX index reach WRITES: P19-DIV-001's own review had forced that guard into
that operator, and the correction had REGRESSED one milestone later, which is
the clearest evidence in P19 that a review catches what an author's own pass
does not. Its first LAUNCH was a no-op — the launcher could not find its own
script, printed "is not recognized" and the surrounding task still reported exit
code 0 — caught by looking for the output files rather than trusting the code,
which is the same reading rule P19-DOF-001 established. P19-MOM-001 needed TWO
attempts, and its failure is the most instructive in the phase because nothing
about it was environmental. Both independent reviews had run before the first
freeze, as P19-CONV-001 established, and 42 findings were resolved; the gate
still failed 66m28s in, on a `-Werror=array-bounds` rejection at `-O3` in a test
helper. That class of defect had been FOUND AND FIXED BEFORE THAT FREEZE, in a
helper thirty lines above the one that broke the build — and recorded as having
caught the class. It had not. The published note argued the surviving sites safe
because "no access follows them", which is false: `pop_back()` IS the access, it
destroys `*(end() - 1)`, and that is the subscript the optimizer bounds at -1.
Because those sites had been reasoned safe they were never compiled, on the very
next line of a note promising that every site is re-verified by building the
Release preset. So the lesson P19-DIV-001 taught — build the second preset
before freezing — held, and was not enough: the preset must be built AFTER THE
LAST EDIT to anything it compiles, proved by a no-op build rather than by a
timestamp. That proof, added because of this failure, caught the same mistake
again within the hour, when the first post-fix Release build was itself overtaken
by a comment edit. Attempt 1's launcher reported exit code 0 while the harness
reported two failed stages, the second time in two milestones. Its logs and the
freeze it was launched under are kept under qualification-attempt-1/, including
the claim that freeze got wrong, and all three places where the tree carried the
false argument were CORRECTED IN PLACE rather than deleted, because the error is
the finding. The harness has now run SIXTY-NINE milestones with its executable
content unchanged, and is self-checked before each run, because an unchanged
harness is not the same as a working one. `P19-LINSOLVE-001` is the first of
those sixty-nine whose copy of `run-qualification.cmd` was edited AFTER its
gate ran, and the distinction is kept rather than blurred: its sixteen executable
statements are identical before and after, proven by reconstructing the pre-edit
body from the inverse replacements and hashing it back to
`f80acaa80552617393a8501cb86213abb7169aab`, the hash the freeze recorded. What
changed is the comment body, which asserted that no existing source file was
modified four lines above its own correction, and counted six new files where
there are eight. No milestone is open, and nothing after `P19-TURB-ARCH-001` is
authorized;
see TODO.md.

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
with the eight qualified subtrees identical across six readings, the
committed tree (`5732e8d`) among them, and equal to the eight its freeze recorded
in advance; see [P19-DIV-001](docs/verification/P19-DIV-001/README.md). Twenty-two mutations
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

`P19-CONV-001` (PASS 2026-11-08) TRANSPORTS A SCALAR BY THE FLOW over that same
finite-volume topology: it makes the face mass flux `phi` that three milestones
had each been inventing privately, and assembles the convective operator from it
as a first-order upwind or a central scheme. `ADR-072` writes the scatter so an
internal face computes one owner share and writes its exact negation to the
neighbour in the same visit, so internal-face conservation is a property of the
traversal rather than a tolerance, and the matrix is ASYMMETRIC by construction
— no symmetry tolerance is passed to `fromCsr` and the suite asserts the
asymmetry, because convection is directional and a symmetric convection operator
would be wrong rather than merely loose. A wall's flux is the boundary table's
literal `0.0` and never a computed dot product, because a moving wall prescribes
a TANGENTIAL velocity whose `w . S_b` is zero in exact arithmetic and not
bit-zero once the centroid, the area vector and the velocity have each been
rounded, and a computed wall flux leaks mass through a solid boundary.

**Three of this milestone's own published claims were FALSE when written, and
the measurements that had confirmed them were of models narrower than the
claims.** The transpose identity `A(-phi) == A(phi)^T - diag(div phi)` was
derived from the upwind rule and stated unqualified, but a central share is
geometric and does not swap, and a zero-gradient boundary face carries its whole
`phi` on the diagonal, so reversing the flux flips that term instead of removing
it: re-measured, upwind with fixed-value boundaries is 19.25% bit-exact at worst
4.36e-16, while upwind with zero-gradient boundaries and central with any policy
are 0.00% bit-exact at worst relative 1 and 2.1 to 2.7 — finite errors, not
rounding. The original figure of 4.93e-16 was a right number about the wrong
thing: that script had NO CENTRAL BRANCH and modelled every boundary as
fixed-value. The dominance identity was missing the prescribed-inflow term that
the neighbouring decision had corrected one paragraph earlier, measured where
there were no boundary faces at all. And both the ADR and the header claimed a
compile-fail case held the asymmetry when nothing held it, nor could: it is a
runtime branch with no type-level expression.

**Re-measuring then produced an understanding neither the author nor either
reviewer had going in.** The dominance deficit is defined with `|a_ii|`, so once
backflow at a zero-gradient boundary drives the diagonal negative the identity
breaks whatever the inflow term is — which makes `backflowFaces` the identity's
VALIDITY CONDITION rather than a diagnostic standing beside it. The row-sum
identity's correction term is the PRESCRIBED inflow and not all inflow, because
a pressure inlet's velocity column is zero-gradient and its negative flux stays
in the matrix; the all-inflow form departs by a relative 0.9995 on 20,815 of
139,802 cells, a structural error rather than a rounding one.

Qualified at 6525/6525 in each of three presets, 3258 x 5 in two repeats with
16,290 executions each at a ratio of exactly 5.000, 0 warnings under `-Werror`,
17 of 17 stages exit 0, 52,155 test executions, and 34,681 assertions in 48
cases identically in Debug, Release and Debug-shared, with the eight qualified
subtrees identical across four readings, the committed tree (`10ad63a`) among them,
and equal to the eight its freeze recorded in advance; the `cfd` subsystem's 702,187 assertions in 447 cases are
667,506 + 34,681 and 399 + 48 EXACTLY, so no pre-existing assertion moved, and
76 tests were added with 0 removed by set difference against the previous
milestone's own log. See
[P19-CONV-001](docs/verification/P19-CONV-001/README.md). Ten mutation probes
are killed by the suite, each one a mutation a review had shown survived the
first draft; the sharpest, a constant interpolation weight, now costs 3,176
failing assertions and was INVISIBLE before the review, because the hand
fixtures' weight genuinely is 2/3 and the suite's only velocity builder was
uniform — with equal velocities either side, the face value is independent of
the weight for every weight, so the cross-check the ADR designates to protect
the duplicated interpolation was a TAUTOLOGY. That is the same defect
`P19-GRAD-001` and `P19-DIV-001` each recorded, in the same place, a third time.

Recorded as less than a reader might assume: **nothing consumes this operator
either, and boundedness is CONDITIONAL.** Upwind is bounded given a discretely
conservative `phi` and no backflow at a zero-gradient boundary; the operator
cannot see the first, implements the second because the boundary table
prescribes it, and REPORTS both without refusing either. `Central` is unbounded
wherever the cell Peclet number exceeds two, and there is no limiter, no
blending and no Peclet-based switching — nor any Peclet report, which would need
a viscosity this operator does not take. There is no solve, so the one analytic
transport claim is asserted as a residual rather than by solving. Five of twelve
refusals are unreachable through the public API and are recorded as unreached
rather than covered. A non-zero `phi` at a wall is accepted on the assembly
path, deliberately, because a policy says where a value comes from and not
whether a sum may be taken over it. The face-velocity interpolation's
three-line application still exists twice, pinned by a test rather than unified,
because unifying it means editing a qualified operator for a convenience. And
there is still no performance baseline, for the same reason as its predecessor.

`P19-MOM-001` (PASS 2026-11-08) IS THE FIRST MILESTONE OF THIS PHASE THAT
CONSUMES ITS PREDECESSORS instead of adding an operator beside them, which
answers the limitation `P19-GRAD-001`, `P19-DIV-001` and `P19-CONV-001` had each
recorded against themselves: that nothing consumed them. The momentum assembly
is the SUM of the qualified convection and diffusion operators plus a
backward-Euler transient diagonal, a caller-supplied pressure gradient and a
constant body force, behind a fourteen-phase refusal gate. `ADR-073` keeps the
terms separable and makes the meeting point explicit: both operators sit on the
left with positive diagonals, so `A_conv + A_diff` is the operator and no sign is
flipped where they meet. Their sparse patterns are VERIFIED IDENTICAL before the
sums are taken, rather than assumed from the fact that both were built from the
same topology.

**The accumulation ORDER is part of the contract, not an implementation
detail.** `((conv + diff) + trans)` was measured only 78.06% bit-exact against a
different grouping of the same three terms, and the five-term source order
57.45% against its reverse, so the suite reproduces production's order
deliberately — which is why the decomposition identity is bit-exact while
row-sum additivity is a measured bound at `1e-13` rather than an equality.

**Two review findings were central and both were the author's.** Seven
`static_assert`s claiming the dimensional identities — that `rho V/dt`, `mu A/d`
and `phi` are all mass flow rates, and that every source term is a force —
CONSTRAINED NOTHING, because the assembly computed in raw doubles; `ADR-073`'s
own decision had claimed the opposite. Rewriting the assembly to compute in
typed quantities and reach `double` only at storage made all seven load-bearing
at zero arithmetic cost, since `operator*` and `operator/` are
`fromSi(a.si() OP b.si())` and are bit-identical to the raw form. And the
milestone's CENTRAL CLAIM — that convection and diffusion meet with a plus — had
NO INDEPENDENT CHECK: every assertion wrote the production expression, and the
one physically-motivated test was provably sign-blind, because a constant field
lies in each operator's null space separately. Two new tests now pin the two
signs independently.

Qualified at 6601/6601 in each of three presets, 3306 x 5 in two repeats with
16,530 executions each at a ratio of exactly 5.000, 0 warnings under `-Werror`,
17 of 17 stages exit 0, 3h43m06s, 52,863 test executions, and 14,230 assertions
in 48 cases identically in Debug, Release and Debug-shared, with the eight
qualified subtrees identical across four readings, the committed tree
(`b9842f2`) among them, and equal to the eight its freeze recorded in advance;
the `cfd` subsystem's 716,417 assertions in 495 cases are 702,187 + 14,230 and
447 + 48 EXACTLY, so no pre-existing assertion moved. 24 mutation probes were
applied to the real tree and 22 killed — TWO OF THEM BY THE COMPILER, which is
the stronger failure, and both only possible because of the typed-arithmetic fix.
The one real survivor exposed a genuine gap: deleting the matrix finiteness loop
left the suite green, because every reachable overflow also produced a non-finite
source. The route neither sub-operator can see is THE SUM, since each gates its
own coefficients and `1.7e308 + 1.7e308` is not finite; the test that reaches it
took three fixtures, and the first two failed on their own premises. See
[P19-MOM-001](docs/verification/P19-MOM-001/README.md).

**It needed two qualification attempts and the first failure was a process
failure rather than a defect**, recorded above with the harness history. Five
measurement-tool defects of the author's own were found and published rather
than quietly fixed, which is the running theme of this phase: four during
development and a fifth in the freshness check of the passing run itself, where
`dir /s /b /o-d` over eight paths sorts within each argument and not across them,
so it named a file two months stale as the newest under the frozen surface.

Recorded as less than a reader might assume: **this produces a matrix and a
source vector and solves nothing.** There is no velocity or pressure field, no
pressure-velocity coupling and no solve, so every claim is a residual or an
identity rather than a solved result. The body force is a CONSTANT `rho g` at
the single constant density P19 v1 carries — not buoyancy, which `ADR-058`
refuses — and at constant density it is absorbable into a modified pressure, so
it changes no velocity field in a closed domain with no fixed-pressure boundary;
that is a property of the equation, and the implementation does not pretend
otherwise. The pressure gradient is supplied by the caller rather than computed,
so nothing here closes the loop between pressure and velocity. Two mutation
survivors stand: one needs an exact floating-point tie, and one drops a gate the
ADR documents as unreachable, which the probe confirms rather than refutes. And
there is still no performance baseline, for the third milestone running, because
nothing solves yet and a baseline for an assembly with no solver behind it would
be a number with no meaning.

`P19-CONT-001` (PASS 2026-11-08) IS THE FIRST MILESTONE OF THIS PHASE THAT ASKS
WHETHER `phi` IS CORRECT rather than using it. Continuity is a MEASUREMENT, not
an assembly: it produces no matrix and no solve, and reports the two measures
`ADR-058` had already fixed — the worst per-cell continuity residual and the
global boundary imbalance, each relative to the boundary throughput
`0.5 * sum |phi|` — plus inlet and outlet flow rates, a per-patch table and an
exact closed-domain verdict.

**IT DEFINES NO NEW TYPE, AND THAT IS THE MILESTONE'S CENTRAL DECISION.** The
UNDERSTAND phase found that `CfdData.hpp` already DECLARED this checklist's
answer as `CfdMassBalance`, so the work changed from "define and compute a
continuity report" to "compute two measures already fixed, and define none".
Without that phase this milestone would have built the second parameter system
`ARCHITECTURE.md` rejects. `ADR-074` then has it CONSUME the qualified
divergence operator for the per-cell residuals and the global sums, walking
boundary faces only for the attribution that operator cannot do, because
re-deriving the owner/neighbour sign is precisely the error `ADR-072` says a
scatter cannot express — and it would leave nothing independent to check
against. The same phase found `ADR-058`'s own heading, "Mass balance means a
discrete measure, **not a sum of inlets and outlets**", which constrains
checklist item 7 against that item's own wording; the invariant won, and the
inlet/outlet rates are reported while the CHECK is the discrete imbalance. Flow
direction comes from the SIGN of `phi` and never from the condition kind, since
a pressure inlet's rate is an outcome rather than a setting.

**A name collision was found before any code existed, and it would have made
every mass balance pass.** `CfdDivergenceField::globalImbalance()` is a
floating-point artifact near 1e-16; `CfdMassBalance::globalImbalance` is a
physical measure. Identical names, different meanings. It is defended by a
different internal name, a test spanning ten orders of magnitude, and a mutation
probe — and the compile-fail group's header states plainly that the type system
catches the naive assignment but not the `.si()` form, rather than implying a
whole guard. Both tolerances were MEASURED before any C++ was written, each one
decade above its worst observed value, and the two literal bounds now sit inside
`static_assert` failure messages, so tightening one below its measurement breaks
the BUILD rather than a test.

**Two independent reviews found a production defect that 49 passing tests had
not.** The relative accumulation residual was built from an all-cells numerator
over a boundary-only denominator: at 1e12 against 1e-12 it reads exactly `1.0`
for CORRECT arithmetic, and in a closed domain it is identically `0.0`, which
had made an assertion over eight reference bodies vacuous. A third,
independently written model confirmed it before the fix, which was to consume the
qualified operator's own value. Twenty-two of twenty-two behavioural mutations
were then caught — three by the COMPILER — five documented-unreachable guards
survived, and the NO-OP CONTROL SURVIVED. That control is why the table can be
read at all: three earlier harnesses reported every probe as compiler-rejected,
the control included, which is impossible. See
[P19-CONT-001](docs/verification/P19-CONT-001/README.md).

**It qualified from clean ON THE FIRST ATTEMPT** — 6687/6687 in three presets,
3355 x 5 executions in two repeat stages with 16,775 counted `Start` markers each
at a markers-to-results ratio of exactly 5.000, 0 `error:` and 0 `warning:`
diagnostics under `-Werror`, 17 of 17 stages exit 0, 3h12m42s — and it added a
check the harness had never had. `run-qualification.cmd` pins the toolchain by
`PATH` ORDER, while `find_package(CONFIG)` derives candidate prefixes from `PATH`
MEMBERSHIP, so dependency resolution was never pinned at all: a stray regenerate
resolved Open CASCADE to MSYS2's 7.9.3 and wrote it into a build cache, where a
cached value short-circuits `find_package` and `ninja -t clean` does not remove
it. All three caches are now verified before and after the run, and **the
harness itself was left byte-identical** — a check was added rather than a
variable, because the previous milestone passed this only by luck and altering a
gate would have hidden that.

Six measurement-tool defects of the author's own were found and published rather
than quietly fixed. The sixth belongs to the passing run itself: the freshness
check's first form reported `STALE FAIL` on all three presets of a correct tree,
because "the oldest compiled output in the build tree" matches a runtime DLL
copied out of the toolchain, and CMake's copy carries the origin's modification
time. It was proven by hash against its origin rather than explained away, and
the first form is recorded because it would have read as a qualification failure.

Recorded as less than a reader might assume: **this measures and refuses, and
decides nothing.** `ADR-058` assigns the throughput floor and the PASS/FAIL
acceptance thresholds to `P19-MASSBAL-001` BY NAME, so this milestone reports
both measures against measured floating-point bounds and does not judge whether a
mesh is good enough. Seven of that unauthorized milestone's fifteen items restate
items of this checklist, which is the checklists' duplication and not this
implementation's; it is recorded so the later milestone inherits a known boundary
instead of rediscovering it. Five refusal guards are unreachable through the
public API and are recorded as unreached rather than as tested; `ErrorCode::
Internal` has never been observed. The patch-order associativity measurement
models a round-robin order while the real CSR is contiguous, and says so. And
there is still no performance baseline, for the fourth milestone running, because
nothing solves yet.

`P19-PVCOUPLE-001` (PASS 2026-11-09) CLOSES THE LOOP THE THREE OPERATORS BEFORE
IT EACH LEFT OPEN. `P19-MOM-001` takes a pressure gradient from its caller and
`P19-CONT-001` only measures whether `phi` conserves mass, so until now nothing
made the pressure and the velocity agree. SIMPLE is what makes them agree: a
predictor, a pressure-correction system, a velocity correction, a face-flux
correction, a pressure datum, under-relaxation, residuals, a verdict and an
iteration budget.

**IT DOES NOT SOLVE, AND THAT IS THE MILESTONE'S CENTRAL DECISION.** SIMPLE has
two linear solves inside it, and both belong to `P19-LINSOLVE-001` BY NAME in
three independent committed records; `cfd` neither links nor includes
`BetterCAD::linalg`. Ten of the seventeen checklist items begin with the word
"Define" and the next milestone's begin with the solvers, so the boundary is the
checklists' own. `ADR-075` therefore has the coupling CONSUME a caller-supplied
`CfdLinearSolve` port, exactly as `P19-MOM-001` consumes a caller-supplied
pressure gradient, and the decision `ADR-069` Decision 3 deliberately deferred
stays deferred. The same UNDERSTAND phase found that `ADR-058` had already
declared five of the seventeen items' answers — the under-relaxation pair 0.7
and 0.3, the stopping criteria, the divergence factor, the iteration budget and
the pressure datum, which `P19-BC-001` already resolves to a cell — so
"Define X" meant bind to and compute a definition that existed. This is the
`P19-CONT-001` lesson recurring exactly one milestone later.

**THE CLOSURE BOUND IS DERIVED RATHER THAN ASSERTED, AND THE DERIVATION IS THE
MILESTONE'S CENTRE.** The corrected flux's divergence in a cell is identically
the pressure system's own row residual, `div(phi')_P == (A p')_P - b_P`, which
is the number the supplied solve already reports. So continuity closure after
correction is EXACTLY as good as the pressure solve and no better, and a
hard-coded bound of 1e-14 would have been a claim about someone else's solver.
What the operator reports is the measured system residual relative to the
PREDICTED interior flux — predicted, not corrected, because the correction's
whole purpose is to drive the corrected flux toward zero, and scaling a
noise-floor residual by a noise-floor scale reported 0.375 on the first run.

**A flag that no input could falsify was DELETED rather than documented.** An
earlier version reported `continuityClosed` against a bound of
`systemResidual + floor`, and the identity above says the quantity and the bound
are the same number: the comparison held by construction, for every possible
input, and a mutation probe replacing it with a literal `true` SURVIVED because
no test could have killed it. `CfdBoundary.hpp` already states this project's
rule for the same shape of problem — "An enumerator no input can produce is
worse than no enumerator" — so the rule was applied to a boolean and both fields
were removed. What they claimed is now reported by a measured number, and a test
that had asserted the bound SHOULD move with the solver's report was replaced by
its opposite: nothing this operator measures may depend on what a port chooses
to say about a solution it has already returned.

**The identity cannot pin the coefficient it is built from, and a review found
that where the gate could not.** It compares two PRODUCTION outputs, so it is
satisfied by any face coefficient whatever: the correction is a projection, and
the corrected flux is invariant under a uniform rescaling of `a_f`, because `A`
scales while `p'` scales inversely. The coefficient's VALUE is therefore pinned
directly, against a momentum diagonal that varies per cell AND per component. It
is computed once per face and written to both diagonals and both off-diagonals
in ONE visit, because a two-visit assembly was measured bit-identical on only
57.3703% of faces — `1 - w` round-trips exactly just 68.2728% of the time — and
`solveSymmetricSparse` requires symmetry. The measurement written to answer that
question had first answered 100.0000%, because it drew the weight from a uniform
`[0,1)` sample, where `1 - w` is exact BY CONSTRUCTION; a real weight is a
quotient of two distances. The decision was right either way, which is why the
near miss is published: a correct decision resting on a broken measurement.

**Two independent reviews, both READ-ONLY, produced thirty-three findings.** Ten
production defects were fixed; one finding was credible and WRONG, refuted by
measurement — the identity does hold at a pinned cell, because compatibility
forces it — and one SUSPECTED finding was CONFIRMED by this milestone's own
tests: a non-positive assembled momentum diagonal is REACHABLE from a correct
assembly, measured at `-8.085e-04` kg/s on a ten-cell chain at 5 mm/s in water.
The reviews were read-only by design, because `P19-CONT-001`'s reviewer left a
live backup file inside the frozen surface. Round one of the mutation probes then
caught nineteen of the twenty-two survivors a test-adequacy review had PREDICTED
and left three — a premise satisfied by floating-point noise, the unfalsifiable
flag above, and a factor of two that changed no verdict — each a real weakness,
each fixed and re-probed to KILLED. Twenty-three of twenty-three behavioural
probes are now caught, two by the COMPILER, one documented-unreachable state
survived, and the NO-OP CONTROL SURVIVED in both rounds. That control earned its
place twice over: before either round produced a result, `cmd.exe /c` launched
from Git Bash had its `/c` path-converted into a drive root, started an
interactive shell, wrote nothing and EXITED 0, twice. See
[P19-PVCOUPLE-001](docs/verification/P19-PVCOUPLE-001/README.md).

**It qualified from clean ON THE FIRST ATTEMPT** — 6802/6802 in three presets,
3434 x 5 executions in two repeat stages with 17,170 counted `Start` markers
each, 0 `error:` and 0 `warning:` diagnostics under `-Werror`, 17 of 17 stages
exit 0, 3h32m37s — and the cache check `P19-CONT-001` added caught something on
its first outing. It gained `CMAKE_COMMAND`, which showed that every local build
of the session had run MSYS2's cmake 4.4.3 rather than the pinned WinLibs 4.4.1:
in Git Bash a `PATH` entry must be a POSIX path, so prepending a `C:/...`
directory puts NOTHING on the path. The affected tree was deleted and
reconfigured, and the harness was again left byte-identical. The eight frozen
subtree identities were read FOUR times — the freeze record, the harness before
its first build, the harness after its last test, and a recomputation after the
run — and agree in all four, which is what says the qualified tree is the
committed tree even though three frozen files' modification times moved before
the launch.

Recorded as less than a reader might assume: **this couples, and it neither
solves nor orchestrates.** The two linear solves are the caller's,
`P19-STEADY-001` owns the outer loop and the atomic result publication, and the
operator is confined to LOW CELL PECLET NUMBER by the confirmed finding above —
the independent channel benchmark derives its inlet speed from
`Pe = rho*u*dx/mu = 0.199` for exactly that reason, so a reader must not take it
as a validated high-Peclet result. `M23`, the disconnected-region flag, is
reported as a documented-unreachable state rather than as covered, because no
fixture builds a mesh that produces it. Three further measurement-tool defects of
the author's own are published rather than quietly fixed, and the third nearly
accused three earlier milestones' committed figures of being false: a `grep`
whose answer was the SAME number for three different test counts, which cannot
be a property of three different runs. And there is still no performance
baseline, for the fifth milestone running, now for a sharper reason than before —
the solve is the caller's, so the cost of an outer iteration is not this
milestone's to measure.

`P19-LINSOLVE-001` (PASS 2026-11-09) SPENDS THE DECISION `ADR-069` DECISION 3
DEFERRED, AND IT WROTE NO SYMMETRIC SOLVER. Three committed records had already
fixed its shape before any code was written, and reading them is what turned a
solver-writing task into an audit, a `PRIVATE` link and a call:
`tests/architecture/CheckLayering.cmake` sets `layer_linalg 5` and `layer_cfd
42` under a strictly-lower rule, so `cfd` was ALREADY permitted to include it
and no renumbering was needed; `src/cfd/CMakeLists.txt` already named the kernel
to reuse and the `static_assert` that was owed; and `ADR-052`'s own rationale
had anticipated the case — "What would have made Option B right: if the thermal
system needed a different algorithm (an iterative method, a non-symmetric
factorisation). It does not" — which is exactly what CFD's momentum system
needs. A second symmetric factorisation inside `cfd` would have been the
duplication `ADR-052` chose extraction to avoid.

**THE AUDIT'S FINDING IS THE MOST USEFUL THING IN THE MILESTONE.**
`solveSymmetricSparse` performs NO SYMMETRY CHECK: it builds the full CSR into
Eigen and hands it to a `SimplicialLDLT` whose default `UpLo` is `Lower`, so the
upper triangle is simply never read. The word "symmetric" appears in that file
only in comments and type names, and no test anywhere — in `linalg`,
`structural` or `thermal` — had ever shown it an asymmetric matrix. It is not a
defect: the independent residual is computed from the ORIGINAL arrays, so a
wrong answer is refused rather than returned. But it was an untested
precondition under a name that promises otherwise, and it is now pinned by tests
in both kernels, with the measured behaviour recorded — asymmetry 1e-12 and
1e-9 refused as `residual_too_large`, 1e-7 and above as `singular_system`, and
exactly zero solved.

**TWO SYSTEMS, TWO KERNELS, AND THE SECOND IS GATED ON A READABLE `R`
DIAGONAL.** Pressure correction is symmetric by construction and keeps the
`LDLT` that `structural` and `thermal` already call; momentum is asymmetric and
correct that way, so it needs a general factorisation. `SparseLU` was MEASURED
and rejected rather than argued about: it reports `info() == Success` with a
normalized residual of exactly 0 on a system singular to 1e-13,
`logAbsDeterminant` reads `-5.9e+01` for a well-conditioned 3x3 whose entries
are all 1e-9 so it cannot be a gate, and the pinned Eigen exposes no accessor
for `U`'s diagonal — verified against `Eigen/src/SparseLU/SparseLU.h` rather
than taken from documentation — so recovering one means a dense inverse, 272 MB
at `P18`'s smoke size of 5832. `SparseQR::matrixR()` returns a real sparse
matrix, read by iteration and never by `coeff()`, since its contract does not
promise sorted inner indices.

Three gates, in the symmetric kernel's own order: `info() == Success` with
`min|R_ii|/max|R_ii| >= rankRatioFloor`; every component of the solution finite;
and the independent residual passing the normalized bound. **`rank()` is
REPORTED AND NEVER GATED**, because it is `m_nonzeropivots` under a threshold
Eigen chooses that grows with the system size. There is exactly ONE residual
gate in BetterCAD: `solveSparse` calls `linalg::evaluateResidual` and translates
its verdict rather than computing a second residual of its own.

The port routes on EXACT symmetry, `largestSymmetryError() == 0.0`, because the
coupling takes ONE `CfdLinearSolve` for both of its systems. The consequence is
stated rather than left to be discovered: a momentum system at exactly zero flux
IS symmetric positive definite and takes the `LDLT`, so nothing anywhere may
assert bitwise equality across the two kernels.

**A REACHABLE OUT-OF-BOUNDS READ WAS FOUND IN A KERNEL ALREADY QUALIFIED UNDER
P17 AND P18, AND WHETHER TO REPAIR IT HERE WAS THE OWNER'S DECISION.**
`checkSystem` validated a CSR row-offset array's length, first and last entries
and nothing in between, so `{0, 5, 2}` over two rows and two entries passed and
the build loop read three elements past both spans — reproduced as an abort
inside `std::span::operator[]`. The scope question went to the owner and the
answer on 2026-11-09 was to keep the repair in this milestone; `[structural]`
271,414 in 415 and `[thermal]` 595,622 in 1168 were then measured IDENTICAL to
their pre-change baselines, which is the measurement that matters for the first
P19 milestone to modify a module both of those physics link.

**THREE MUTATION-PROBE ROUNDS FOUND WHAT TWO INDEPENDENT REVIEWS COULD NOT,
BECAUSE ALL THREE SURVIVORS WERE IN THE FIXES RATHER THAN THE ORIGINAL CODE.**
The reviews produced 32 findings — 3 production defects fixed, 17 test gaps
closed, one wrong-value claim REFUTED by measurement, one scope question
escalated. Then 29 behavioural probes, of which the three survivors were: `M4c`,
a two-part guard whose five malformed inputs were all caught by the OTHER
clause; `M9a`, a CFD guard the kernel below made invisible by repeating it,
catchable only through the check ORDER; and `M4e`, which was not a missing test
at all but a PROOF that one clause could not fire — the dimension check already
requires `rowStart.back() == inner.size()`, so monotonicity gives
`rowStart[i] <= inner.size()` by transitivity. That clause was REMOVED from both
kernels, because a guard no input can fire is what this project's own rule
rejects and that rule was being cited elsewhere in the same milestone. The guard
that ships is simpler than the one the audit's finding first produced and
provably sufficient rather than defensively redundant.

**It qualified from clean ON THE FIRST ATTEMPT** — 6870/6870 in three presets,
3474 x 5 executions in two repeat stages with 17,370 counted `Start` markers and
result lines each against 3,474 progress-prefixed lines, a ratio of exactly
5.000, 0 `error:` and 0 `warning:` under `-Werror`, 17 of 17 stages exit 0,
3h47m02s. The eight frozen subtree identities were read three times — the
harness before its first build, the harness after its last test, and a
recomputation from the working tree afterwards — and agree in all three. The
cross-preset bit fingerprints and the per-subsystem assertion counts were read
FROM THE THREE QUALIFIED BINARIES, because `ctest --preset` captures no stdout
from a passing test and the earlier claim that the logs established that
identity had no log behind it.

Recorded as less than a reader might assume: **this solves two linear systems,
and it neither iterates nor orchestrates.** `P19-STEADY-001` still owns the
outer loop, the convergence verdict and the atomic result publication. There is
still no performance baseline, for the sixth milestone running: relative kernel
cost WAS measured to choose between the options — QR against LU at 0.35, 0.61
and 1.65 for n = 200, 1000 and 4000 — but that is option-comparison data, not a
benchmark, and optimizing is not what this milestone was authorized to do.

**The post-gate reconciliation found four defects in this milestone's own
evidence**, none of them in the frozen surface, and all are corrected in place
and published rather than quietly rewritten: a harness paragraph asserting a
file was unmodified four lines above its own correction; "six" new files where
there are eight; this milestone's `TESTS` section still holding the pre-review
snapshot and calling it `EXACTLY` three times; and a `cfd` assertion figure four
short, which survived every check because 728,802 + 22,965 = 751,767 made it
INTERNALLY CONSISTENT and therefore unfalsifiable from inside the document —
only reading `[cfd]` off the built binary exposed it. Two of the author's own
measurement tools were wrong before the evidence was: a freshness check that
asked about file extensions instead of asking the build tool, which reported a
FAIL on 24 zero-byte stubs written by the deployment tests; and a progress-line
count that returned 999 in a 6870-test log, which is `1..999` and not a
measurement of anything.

`P19-STEADY-001` (PASS 2026-11-09) IS THE MILESTONE THE TEN BEFORE IT WERE BUILT
FOR, AND IT WROTE NO NUMERICS. `solveCfdSteady` orchestrates fifteen operators
that were already qualified, which is why ten of its nineteen checklist items
were orchestration rather than implementation — and the tree had said so by name
before any code was written. `CfdCoupling.hpp`: "NO LOOP. One iteration, and the
criteria a loop would consult are `cfdCouplingVerdict`. 'Convergence loop' is
`P19-STEADY-001`'s item", and "A PURE FUNCTION OF ITS ARGUMENTS, so that
`P19-STEADY-001`'s loop and this operator's own tests agree by construction".
`ADR-075`'s scope table: "THE DATA MODEL IS FIXED" and "THE ORCHESTRATION IS NOT
OURS". Reading those is what kept this milestone from inventing a second
settings type or a second residual record.

**THE SETTINGS ARE READ FROM THE ANALYSIS DEFINITION AND NOT TAKEN AS A
PARAMETER, AND THAT IS A PROVENANCE DECISION RATHER THAN A STYLE ONE.** An
earlier draft had a `CfdSteadySolveInput` carrying `ADR-058`'s four settings
records. `canonicalText` writes seven settings lines into the definition
fingerprint that `CfdResultSource::definition` carries, so caller-supplied
settings would have let a run publish provenance it did not have, with
`resultCurrency` reporting the result `Current`. It was removed before any test
was written, found by reading `CfdAnalysisDefinition` rather than by testing, and
no test of the numerics could have caught it. `ADR-077` Decision 7, and a
compile-fail gate now forbids the type's return.

**ONE DEFECT CAME OUT OF THE REVIEWS AND IT WAS A DROPPED PHYSICAL TERM.**
`momentum.gravity` was never set, so `assembleCfdMomentum` silently assembled the
gravity-free equation while `prepareCfdBoundary` still applied the hydrostatic
boundary field — the two halves `ADR-073` Decision 6 calls one treatment, with
only the first reached. Nothing refused it: the operator validates gravity only
WHEN PRESENT, and the fingerprint would have published the wrong field under
provenance naming gravity. Fixed in one line, with a regression test VERIFIED TO
FAIL WITHOUT THE FIX. Writing that test corrected two of the author's own
predictions — neither the sign of `dp/dy` nor the transverse flow discriminates;
only the MAGNITUDE of a least-squares `dp/dy` does, 1.158 of `rho*g` against
0.160 — and it found that Earth gravity is unusable in the fixture, because
`rho*g = 9790 Pa/m` drives the flow out of the low-Péclet regime
`P19-PVCOUPLE-001` is qualified in and the operator correctly refuses the run.

**THE INDEPENDENT VALIDATION OF THE SOLUTION CAME FROM A MUTATION PROBE, NOT
FROM THE TEST PLAN.** `M12` replaced the published mass flux with the
pre-correction flux and survived the whole suite: a non-conservative field
published as a solution, with nothing anywhere looking at conservation. Closing
it measures the published flux through `divergeCfdMassFlux`, `P19-DIV-001`'s
qualified operator, at 7.07e-17 worst per-cell against 7.26e-05 for the
pre-correction flux — twelve orders of magnitude, so the bound is set by the
defect's signature rather than fitted to the passing number. That is a property
of the published field alone, which the dense-reference comparison is not: a
wrong loop would be wrong identically in both runs. The dense LU reference
agrees to 1.485e-15 on pressure, and the two runs' iteration counts are asserted
EQUAL, because the agreement silently depended on it.

Three guards were written and then found unable to fire, in one milestone, and
all three were removed rather than kept as defence in depth: a non-finite field
loop that `applyCfdCouplingCorrection` already made unreachable, its
documentation, and four `validate` calls that `create` and `setDefinition` had
already run. The fifth guard was KEPT, because what follows it dereferences an
optional and that is undefined behaviour rather than a wrong diagnostic.

**ITS FIRST QUALIFICATION ATTEMPT FAILED, AND THE FAILURE WAS A WEAKENED PROOF
RATHER THAN A WEAKENED PRODUCT.** `QUALIFICATION FAILED: 3 stage(s) failed`, one
test in 6903, the same one in all three presets, while the launcher reported exit
code 0 — the second milestone in a row to turn on that distinction. Giving
`CfdResult`'s private constructor a third parameter turned
`compile_fail.cfdids.a-result-cannot-be-built-without-its-factory`, inherited
from `P19-DATA-001`, from an ACCESS test into an ARITY test: GCC resolves arity
before access, so the case still failed to compile and still exited 1 while the
claim it exists to make had stopped being tested. The production code was never
wrong — the constructor is private and `create` is still the only route to a
`CfdResult`. Widening the regex was REFUSED, because it would have produced a
pass by retiring the gate; the repair passes the right arity so the private
constructor is selected again, and because the field set cannot be conjured
either, the third argument comes from a real result and the gate now proves more
than it did before. The blast radius had been drawn around the FACTORY and missed
a file that calls it nowhere and names the CONSTRUCTOR instead — a deliberate
non-caller, invisible to a search for `create`. That line is now in the evidence,
in `TODO.md` and in `ADR-077` Decision 2, whose heading promises costs stated
rather than discovered later and now admits that this one was discovered later.

**It qualified from clean on the SECOND attempt** — 6903/6903 in three presets
counted four ways per log, 3493 x 5 = 17,465 executions in two repeat stages
against 3,493 progress-prefixed lines for a ratio of exactly 5.000, 0 `error:`
and 0 `warning:` under `-Werror`, 17 of 17 stages exit 0, 3h23m55s. The eight
frozen subtree identities were read five times — the harness before its first
build, the harness after its last test, a recomputation from the working tree,
the staged tree, and out of the commit object of `370b90da75de236e66d0ddfbda1fb9254e557133` — and agree in all
five, so the qualified tree is the committed tree. `include` and `src` are
bit-identical to the voided first freeze, which is the check that the repair
touched no production code. `[cfd]` reads 753,230 assertions in 663 cases
from each of the three qualified binaries, which is 751,771 + 1,459 and 644 + 19
exactly, while `[linalg]`, `[structural]` and `[thermal]` are identical to
baseline.

The post-gate reconciliation found two defects in its own measuring scripts
before it found any in the tree, and both are published rather than quietly
fixed: a check for `0 tests failed out of 6903`, a string `ctest` cannot print in
a passing run, and an identity comparison that reported the two readings EQUAL
having parsed one path out of each block instead of eight — two one-element
dicts compare equal just as well as two eight-element ones. The second is the
dangerous shape, because it reported a pass. The script now asserts that both
readings hold eight paths before it reports them equal.

Recorded as less than a reader might assume: **a converged run is not a
validated one.** `CfdResult.hpp` says "CONVERGENCE IS RECORDED, NOT RE-JUDGED
HERE", and whether the numbers are good enough belongs to `P19-VALID-001`. Three
of `CfdFailureStage`'s nine values are unreachable from here, measured over
fifteen driving ports rather than argued. Three of twenty behavioural probes
survive BY DESIGN, each with the reason a test cannot reach it, and both no-op
controls survived every round — which is what makes the probe table a
measurement rather than a story about a driver that may never have run. Nine
limitations are recorded rather than fixed. There is still no performance
baseline, for the seventh milestone running.

**One scope decision is open and was reported rather than taken:** `ADR-077`
`L12`. The convection scheme and the gradient method are hard-coded in the
orchestrator, outside the analysis definition and outside the definition
fingerprint, which contradicts `ADR-073` Decision 5 — "which of `ADR-070`'s two
methods made it is the analysis's decision and not this operator's". Of the two,
one is pinned only INCIDENTALLY, by a side effect of the gravity test's `dp/dy`
check, and the other is not pinned at all, because telling the two gradient
methods apart needs an analytic reference this subsystem does not have. Putting
them in the definition means changing `CfdAnalysisDefinition` and
`canonicalText`, both qualified under `P19-DATA-001`, and no item on the
checklist asked for a selector. Evidence
[P19-STEADY-001](docs/verification/P19-STEADY-001/README.md).

`P19-TRANSIENT-001` (PASS 2026-11-10) MARCHES IN TIME AND WROTE NO NUMERICS
EITHER. `solveCfdTransient` calls `cfdMassFlux`,
`reconstructCfdPressureGradient`, `assembleCfdMomentum` three times and
`advanceCfdCoupling`, in that order, every outer iteration of every step — and
the transient term it depends on was qualified two milestones earlier under
`P19-MOM-001`, with `ADR-073`'s unit assertions and a dedicated test for the one
way a caller can get it wrong. `CfdTransientSettings`, `CfdTimeSampling`,
`CfdResultMetadata`'s time and step, and `CfdResult::create`'s "transient: a
finite time > 0 s and a step >= 1" were all written by `P19-DATA-001` FOR this
milestone. Four files were added and three `CMakeLists` gained a registration;
not one signature, field, enumerator or default moved, and `CfdSteady.hpp` and
`CfdSteady.cpp` were not touched at all — which `[cfd][steady]` at 1263
assertions in 19 cases, read off the qualified binaries and identical to
baseline, is what measures rather than asserts.

**THE TWO VELOCITY ARRAYS ARE THE WHOLE MILESTONE.** `CfdMomentumInput` takes
`currentVelocity` (the latest outer iterate) and `previousVelocity` (U^n, frozen
for a step's inner loop) as spans of the same length in the same layout, so the
type system cannot tell them apart. `ADR-073` Decision 4 names the consequence:
passing the current field as the previous one makes the transient term's two
halves cancel, and the run then CONVERGES TO THE CORRECT STEADY ANSWER with
every steady assertion passing. Two checklist items are the tests that see it —
the first step's change must scale with `dt`, measured at ratios 1.9834 and
1.9916 where the defect would give 1, and a short march must NOT be the steady
solution, measured 1.75% of the way there. The fixed-point test `ADR-078` had
proposed cannot see it at all, because at a fixed point the two arrays are
equal.

**A CHECKLIST ITEM SURVIVED A PASSING SUITE, AND TWO REVIEWS FOUND IT.**
`CfdTransientStepRecord::balance` was never assigned, so every step record
published two zeros where the qualified evaluator's two mass-balance measures
belong — and "Per-step mass balance" is one of the nineteen items. The suite
passed because the only assertion on it was `isfinite()`, and zero is finite.
The repair asserts that each sampled step's record EQUALS its sample's own
metadata, a comparison with no tolerance a default can satisfy, and it was
verified to fail without the fix. An adversarial review and a mutation analysis
each found it independently; five test cases and about 370 assertions were added
after them.

**THE MOST INTERESTING FINDING IS RECORDED RATHER THAN FIXED, AND THE
PREDICTION ABOUT IT WAS WRONG.** `ADR-078` `L11`: the per-step convergence
criterion goes vacuous at a small enough `dt`, because on a step's first
iteration the field IS U^n and the transient term's two halves cancel exactly,
leaving a residual numerator independent of `dt` while the normalisation grows
as 1/`dt`. The prediction was that the LATER steps would collapse; that
assertion failed at `65 <= 8` and the measurement replaced it — it is STEP 1
that collapses, from 60 outer iterations to 1, with the work moving into the
later steps rather than disappearing. The answer is not wrong: such a step is a
lagged-pressure step, consistent, first order, and published with a
conservative flux. What stops being worth what a reader expects is the
EVIDENCE, since `outerIterations = 1` beside a residual under tolerance reads
as convergence for a step that coupled nothing. The normalisation belongs to
`advanceCfdCoupling`, qualified under `P19-PVCOUPLE-001`, and changing it would
change what every steady run is accepted on as well.

**THE TWO POLICY ITEMS ARE BUILD FAILURES, because a negative claim with no
test is prose.** "Time-dependent BC policy" and "Time-dependent load policy"
claim that nothing in the canonical data varies with time, and thirteen of the
twenty-eight compile-fail gates are "no member named" gates over every record a
time profile would have to live in, plus gates that a prescribed speed and a
gravity vector do not accept a callable. One gate was NARROWED rather than
widened after review: `A_TIME_GRIDS_STEP_COUNT_IS_WRITABLE` had accepted "no
member named" alongside the access diagnostics, which would let a rename retire
the gate silently — `P19-STEADY-001`'s arity-for-access failure in a new
disguise.

**THE GATE PASSED ON THE THIRD ATTEMPT AND NEITHER FAILURE WAS THE TREE.**
6965/6965 in three presets, 3527 x 5 = 17635 executions in two, 17 of 17 stages
exit 0, 4h49m, and no product code changed between the three attempts. Attempt 1
died 63 minutes in when the machine stopped being able to launch processes — 374
of its 375 ctest failures are `0xC0000142`, and both remaining configures
returned it within 30 ms of being asked. Attempt 2 was launched from a shell
that cannot see the dependency prefix, so CMake never put it on
`CMAKE_PREFIX_PATH` and `find_package(OpenCASCADE)` fell through to msys64's
7.9.3, which the project's own supported-range gate refused; the cause was
established by running one probe in BOTH shells — `cmake EXISTS` reads YES in
one and NO in the other for the same absolute path — rather than inferred from
the message, and the three qualified caches carry 0 msys64 references.

**THAT SECOND ATTEMPT EXPOSED A HOLE IN THE GATE ITSELF.** With no build stage
having run, its two repeat stages still reported `100% tests passed`, off
binaries left on disk, and `release-ext` selected 3522 tests instead of 3527 —
exactly the five cases the reviews had added. The three build trees were emptied
before attempt 3, so that a stage with no build behind it now fails loudly
instead of passing quietly.

**THE RECONCILIATION WAS FALSIFIED BEFORE IT WAS BELIEVED**, which is this
milestone's answer to the check `P19-STEADY-001` found could not fail. Its 82
checks parse every predicted figure out of `FREEZE.md` rather than restating it,
and five single-line sabotages of a COPY of the evidence were each required to
make it fail — a removed `Passed` marker, an altered identity hex, an altered
assertion figure, a removed verdict sentence, a stage exit changed to 1. All
five were caught and the untouched control still passed. The post-gate
reconciliation also found one defect in this milestone's own evidence and
corrected it in place: `KNOWN LIMITATIONS` claimed nine carried limitations plus
one found during implementation, while `ADR-078` carries FOURTEEN, so four were
silently dropped — including `L11`, the finding the same file devotes a
subsection of its own to.

Fourteen limitations are recorded rather than fixed, and there is still no
performance baseline, for the eighth milestone running — which a transient run,
being `steps x iterations` linear solves, makes the most expensive thing in the
tree.

**Two scope decisions are open and were reported rather than taken:** `ADR-078`
`L7`, which inherits `ADR-077` `L12` unchanged — the convection scheme and the
gradient method hard-coded in BOTH orchestrators, outside the analysis
definition and outside its fingerprint — with `ADR-078` `L14` reading the same
finding from the test side, since the steady-limit cross-check cannot
discriminate the two choices when both orchestrators pin the same ones; and
`ADR-078` `L11` above, whose repair would change what every steady run is
accepted on. Evidence
[P19-TRANSIENT-001](docs/verification/P19-TRANSIENT-001/README.md) and
`ADR-078`.

`P19-TURB-ARCH-001` (PASS 2026-11-10) DECIDED THE TURBULENCE ARCHITECTURE AND
DECLARED NO TYPE FOR IT. `ADR-058` defers turbulence in full from P19 v1 and
names this milestone as the one that settles its architecture "before any
model"; `ADR-079` settles it in ten decisions and adds nothing to `include/` or
`src/`, which is why seven of the eight frozen subtrees are BIT-IDENTICAL to the
previous commit and the entire deliverable is `tests/`. The reason is
`ADR-061` Decision 4, read in the file rather than taken from a summary: there
is no field for the coupling algorithm, the time scheme, the convection scheme
or the linear solver, because each has one qualified value or none yet and *"a
selector would be a field with one legal value"*. A laminar/turbulent mode field
is precisely that selector, so the mode is decided to be ABSENT; laminar is what
the solvers do, and the refusal of anything beyond it belongs to
`P19-VALID-001`, which `ADR-058` already assigns the Reynolds threshold to
*"from a reference"*.

**THE DECISION IS HELD BY THIRTY GATES RATHER THAN BY PROSE**, which is the part
that makes this a milestone and not a document. A four-rule source-text
ownership checker, run as twelve ctests — eleven fixtures plus the real tree at
600 files scanned and 0 violations — attributes any turbulence quantity to the
area that would own it and fails the build when it appears anywhere else;
fourteen compile-fail cases prove the absent types absent by name; four runtime
cases pin the variable table, the wall-treatment mirror and the nested settings
records. The gates were made to fail on purpose before they were believed, and
that is how the milestone's own overstatement was found: two mutations meant to
prove the new count pin necessary were already caught by `-Werror=switch`, so
the claim was narrowed in the ADR instead of left standing. Two independent
reviews then found ten surviving mutations, four gates that changed meaning
instead of failing, six tautologies and two false MEASURED claims in `ADR-079`,
and a fourth rule — banning the bare symbols `nut`, `mut`, `epsilon`, `omega`
and `tke` inside `cfd`, where all five measure zero — came out of the review
rather than out of the design. Four more false figures were caught in the
milestone's own verification scripts and corrected in place; the reconciliation
was then falsified by fourteen sabotages, all fourteen caught. Fifteen
limitations are recorded rather than fixed, and `L12` is the one the next
milestone inherits: the checker will FIRE on the refusal `ADR-079` itself
prescribes, because `P19-VALID-001` must name turbulence in order to refuse it,
so the shape of the allow-path is written down for that milestone rather than
built ahead of a consumer that does not exist. Evidence
[P19-TURB-ARCH-001](docs/verification/P19-TURB-ARCH-001/README.md) and
`ADR-079`.

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
