# BetterCAD — Roadmap

> Where BetterCAD is going, and what it has reached.

This document describes capabilities and their long-term sequencing. It does
not authorize work: [TODO.md](TODO.md) does that, and owns the milestone IDs.

## Vision

A mechanical CAD/CAE platform on one engineering document model that does not
have to be replaced as it grows:

```text
Parametric CAD → Assemblies → Drawings → Materials → Meshing
    → FEA / Thermal / CFD → Optimization → Automation
    → Version Control → AI Engineering → Manufacturing
    → Production BetterCAD
```

The eventual workflow is requirements → parametric model → simulation →
optimization → verified design → drawings and manufacturing → revision, with an
engineering agent able to drive any of it through validated public APIs.

## Principles

1. **Correctness before features.** Every subsystem needs deterministic tests,
   regression tests, independent validation where possible, failure diagnostics
   and reproducible evidence.
2. **Engineering intent is the model.** Parameters, constraints, sketches,
   features and dependencies are persistent. Geometry is derived.
3. **Native units.** Quantities keep their physical dimensions; `Length`, not
   `double`.
4. **Stable identity.** Persistent objects use stable typed IDs, never indices,
   kernel handles or addresses.
5. **Dependency-driven regeneration.** A parameter change rebuilds only what
   depends on it, transactionally.
6. **Headless core.** CLI, GUI, tests, Python and AI use one engine. See
   [ARCHITECTURE.md](ARCHITECTURE.md).

## Current Foundation

A parametric part-modeling core that is complete and qualified. A user, script
or test can create a document, declare parameters, build a constrained sketch,
apply any of ten features, change a dimension, regenerate deterministically,
undo, redo, save, close, load, regenerate, validate and export STEP and STL. Six
realistic mechanical parts exercise that end to end and reproduce
bit-identically across three build configurations.

`P11-QUAL-001`: 20 gates, 20 passed. 738/738 tests and 0 compiler warnings over
274 translation units in each of Debug, Release and Debug-shared, each rebuilt
clean ([evidence](docs/verification/P11-QUAL-001/README.md)).

**One capability is in progress:** Parametric CAD completion, authorized in
[TODO.md](TODO.md) as `P12`. No other capability below is authorized.

## Capability Roadmap

| Capability | Milestones | Status |
| --- | --- | --- |
| Foundation | `P0`–`P10` | **Qualified** |
| Parametric Part Modeling | `P11` | **Qualified** |
| Parametric CAD completion | `P12` | **In progress** |
| Interchange | — | Planned |
| Advanced Surface Modeling | — | Planned |
| Desktop Application | — | Planned |
| Semantic Topology | — | Planned |
| Assemblies | — | Planned |
| Drawings | — | Planned |
| Engineering Data | — | Planned |
| Meshing | — | Planned |
| Structural FEA | — | Planned |
| Thermal | — | Planned |
| CFD | — | Planned |
| Optimization | — | Planned |
| Automation / Python | — | Planned |
| Version Control | — | Planned |
| AI Engineering Agent | — | Planned |
| Manufacturing / CAM | — | Planned |
| Production Hardening | — | Planned |

```text
Qualified  implemented, tested, validated, evidence recorded, and
           re-verified end to end in every supported build configuration
In progress authorized in TODO.md; milestones are being delivered
Planned    intended; not started, and not authorized by TODO.md
```

Planned capabilities have no milestone ID. IDs are allocated by
[TODO.md](TODO.md), in sequence, when a capability is authorized — never in
advance.

### Foundation — Qualified

Repository and build system, engineering core, document model, geometry kernel
foundation, sketch system, constraint solver, the extrude feature, the
regeneration engine, persistence and the CLI. Released as `v0.1.0` (annotated
tag object `93d84f0`, commit `2da8966`) and re-verified by `P11-QUAL-001`,
whose legacy gate passes all 301 of these tests unchanged in every
configuration.

| Milestone | Delivered | Evidence |
| --- | --- | --- |
| `P0-001` | Build system and repository skeleton | [P0-001](docs/verification/P0-001/README.md) |
| `P1-001` | Unit-safe quantities | [P1-001](docs/verification/P1-001/README.md) |
| `P1-002` | Strong IDs | [P1-002](docs/verification/P1-002/README.md) |
| `P1-003` | Parameter system | [P1-003](docs/verification/P1-003/README.md) |
| `P2-001`, `P2-002` | Document; commands, undo/redo | [P2](docs/verification/P2/README.md) |
| `P3-001`…`P3-004` | Open CASCADE integration, primitives, analytic property validation, booleans | [P3](docs/verification/P3/README.md) |
| `P4-001`…`P4-003` | Sketch coordinate system, point/line/circle/arc, geometric query API | [P4](docs/verification/P4/README.md) |
| `P5` | Constraint representation and reference validation | [P5](docs/verification/P5/README.md) |
| `P6` | Constraint equation system, solver diagnostics, conflict and over-constraint validation | [P6](docs/verification/P6/README.md) |
| `P7` | Closed-profile detection, extrude, parameter-driven regeneration, analytic volume regression | [P7](docs/verification/P7/README.md) |
| `P8` | Dependency representation, dirty propagation, topological regeneration, cycle detection | [P8](docs/verification/P8/README.md) |
| `P9` | Save, load, round-trip regression | [P9](docs/verification/P9/README.md) |
| `P10` | CLI `new`, `info`, `validate`; STEP and STL export | [P10](docs/verification/P10/README.md) |

### Parametric Part Modeling — Qualified

Nine parametric features on top of extrude, six mechanical reference models,
and an end-to-end qualification. Each feature stores its definition rather than
its result, regenerates from parameters, fails atomically with a structured
diagnostic, round-trips through save/load, and is validated against
independently computed volumes.

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

### Parametric CAD completion — In progress

The part-modeling capability qualified above is narrower than the original
ambition for it. `P12` completes it: parameter expressions; the remaining
sketch constraints (angle, tangent, concentric, midpoint, symmetric,
diameter) and entities (ellipse, spline); datum planes, axes and coordinate
systems; sketches on planar faces; through-all extrude; split and combine;
shell, draft and rib; variable-radius fillets (setback and corner-transition
controls were deferred to *Advanced Surface Modeling* when `P12-FEAT-006` was
scoped); hole
threads, standard sizes and tolerance classes; richer pattern, sweep and loft
modes; configurations and design equations. [TODO.md](TODO.md) holds the plan
and the order.

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

### Interchange — Planned

STEP and STL **export** are delivered. Outstanding: STEP import; DXF in both
directions; IGES and OBJ; 3MF, glTF, Parasolid and JT where licensing permits;
and schema migration between native document format versions. Every external
format lives behind an adapter — see [ARCHITECTURE.md](ARCHITECTURE.md).

The only STEP reader in the repository today is test-only tooling that reads
exports back to verify them. It is not an import path.

### Advanced Surface Modeling — Planned

Blend work the kernel does not offer and BetterCAD would build itself:
fillet **setback** distances and **selectable corner transitions** where
blended edges meet. OCCT 8.0.1 computes those corners internally and exposes
no input for either, so both need a BetterCAD-owned surface-patch layer
(trimmed blends, N-sided tangent fills, sewing, validation). Both were
deferred from `P12-FEAT-006`
([investigation](docs/verification/P12-FEAT-006/investigation/README.md)).

### Desktop Application — Planned

The desktop executable is a placeholder shell. The goal is the first practical
interactive environment on Qt 6: command system, model tree, property editor, 3D
viewport, sketch environment, diagnostics panel; orbit/pan/zoom/standard views;
selection of bodies, faces, edges, vertices, sketches and features.

**Gate.** A user creates a parametric mechanical part entirely from the GUI,
with the GUI owning none of the engineering state.

### Semantic Topology — Planned

Persistent engineering intent must not depend permanently on transient kernel
identities such as `Face23` or `Edge17`.

BetterCAD currently resolves references by **geometric matching** — an edge's
supporting line or circle, a face's plane and outward side. It never substitutes
the wrong entity: when the geometry a reference described moves, the feature
fails with `NotFound` and keeps no body. But a reference does not follow
geometry that a parameter moves, and that limit propagates to anything built on
references.

Planned work: persistent naming, feature provenance, adjacency signatures,
semantic matching with confidence scores and ambiguity detection, and a resolver
that recovers intended entities after topology-changing edits.

> A minimal stable-reference layer may become a prerequisite for assemblies
> (mates attach to faces and edges), drawings (dimensions attach to edges),
> simulation boundary conditions (loads attach to faces) and manufacturing
> annotations. Pulling it forward requires an explicit milestone decision in
> [TODO.md](TODO.md).

### Assemblies — Planned

Components with a part reference, instance ID, transform, configuration,
visibility and suppression state. Mates: fixed, coincident, concentric,
parallel, perpendicular, distance, angle, tangent, lock, gear, rack-and-pinion,
screw. The mate model stays separate from the solver, mirroring the sketch
solver. Later: collision and interference detection, clearance analysis,
exploded views, motion constraints, assembly mass properties.

**Gate.** Reference assemblies solve deterministically without unstable
placement.

### Drawings — Planned

Sheets, standard/projected/section/detail views, dimensions, centre marks and
lines, annotations, a GD&T foundation, BOM tables, balloons, revision tables and
title blocks. Export to PDF, DXF and SVG. Drawings reference the model rather
than copying it.

**Gate.** Model changes propagate into views and dimensions.

### Engineering Data — Planned

A material database — density, elastic modulus, Poisson ratio, yield and
ultimate strength, thermal conductivity, specific heat, thermal expansion,
viscosity, electrical conductivity — plus custom materials.

**Gate.** Assignments persist through save/load and propagate into mass and
simulation calculations.

### Meshing — Planned

A simulation mesh infrastructure distinct from both exact geometry and display
tessellation: 1D/2D/3D, beam, triangle, quadrilateral and tetrahedron elements
(hexahedra later), global and local sizing, surface refinement, quality metrics,
validation and convergence tools. Gmsh is a candidate backend, behind BetterCAD
abstractions.

### Structural FEA — Planned

Linear elasticity, small deformation, static analysis, isotropic materials.
Fixed supports, forces, pressures, gravity, moments. Displacement, strain,
stress, von Mises stress, reaction forces, factor of safety.

**Gate.** Verification against analytical benchmarks — uniaxial bar, cantilever
beam, simply supported beam, plate problems — before optimization uses any
result. Plausible contours are not evidence.

### Thermal — Planned

Steady and transient conduction, heat generation, prescribed temperature, heat
flux, convection; thermal contact later. Temperature fields couple into
structural analysis for thermal expansion.

### CFD — Planned

Incompressible laminar flow first, then turbulent, thermal and compressible;
multiphase later. Pipeline: fluid volume extraction → mesh → boundary conditions
→ solver → results → design parameters. Results stay linked to the originating
CAD revision.

### Optimization — Planned

Design variables (dimensions, materials, suppression, angles, thicknesses),
objectives (mass, cost, pressure drop, drag, stress, stiffness, efficiency) and
constraints (allowable stress, displacement, temperature, factor of safety,
manufacturing limits). Parameter sweeps and gradient methods first; genetic,
Bayesian, surrogate and topology optimization later.

### Automation and Versioning — Planned

**Python.** pybind11 bindings onto the stable public API, exposing documents,
parameters, sketches, constraints, features, assemblies, materials, simulation,
optimization and export. The GUI should not have capabilities the API lacks.

**Version control.** Git-like commit, branch, merge, history, diff, revision and
tag — but with semantic diffs (`Hole003.diameter 8 mm → 10 mm`), branch
comparison and controlled merging of independent mechanical changes.

### AI Engineering Agent — Planned

Introduced only after the engineering APIs are reliable. Natural language →
intent extraction → engineering plan → structured API calls → validation → model
change → regeneration → verification. The agent never fabricates kernel
geometry; every operation is typed, validated, undoable and auditable.

### Manufacturing / CAM — Planned

Manufacturing metadata, machining stock, threads, tolerances, surface finish,
sheet thickness and bend information. Later: 2.5D CAM, tool libraries,
toolpaths, feeds and speeds, G-code postprocessing, sheet-metal unfolding and
additive preparation.

### Production Hardening — Planned

Crash recovery, autosave, corruption detection, file-version migration, large
assembly stability, thread safety, plugin isolation, diagnostics, profiling,
installer, updates and release qualification. Testing expands to property-based,
fuzz, stress, large-model, file-compatibility and GUI acceptance. Nearer term:
CI, sanitizer and coverage configurations, and an enforced formatting check —
none of which exists today.

## Release Direction

`v0.1.0` released the Foundation. `P11` is qualified but not released; there is
no `v0.2.0`.

| Release | Theme | Status |
| --- | --- | --- |
| v0.1 | Modeling foundation | **Released** — `v0.1.0` |
| v0.2 | Practical part modeling — datum geometry, STEP import, desktop workflow | Not released; revolve, holes, fillets, chamfers and patterns already delivered |
| v0.3 | Production part design — shell, draft, equations, configurations, materials, GUI | Not released; sweep and loft already delivered |
| v0.4 | Assemblies | Not started |
| v0.5 | Drawings | Not started |
| v0.6 | Simulation foundation | Not started |
| v0.7 | Multiphysics and optimization | Not started |
| v0.8 | Engineering platform — Python, plugins, version control | Not started |
| v0.9 | AI-native engineering | Not started |
| v1.0 | Production BetterCAD | Not started |

A 1.0 release should not mean *we implemented many features*. It should mean
*reliable enough to design real mechanical systems*: robust parametric parts, a
stable sketch solver, production feature modeling, assemblies, drawings, STEP
interoperability in both directions, deterministic save/load, materials, mass
properties, an automation API, reliable undo/redo, regression reference models,
verified releases and user documentation. Simulation and AI belong in 1.0 only
if mature, and never at the cost of CAD reliability.

## Reference Projects

Permanent regression assets, not disposable demos. A model that reveals a
regression is kept and the regression is fixed.

**In the repository** — six parts, built through the public API, every dimension
validated against independently computed geometry: stepped shaft, bolted flange,
V-belt pulley, bearing housing (pillow block), mounting bracket (L bracket with
a lofted gusset), U-bolt.

**Planned** — simple block, gearbox housing, motor mount, four-bar linkage;
small gearbox assembly and machine frame once assemblies exist. Later simulation
references: cantilever beam, pressure vessel segment, heat sink, pipe flow,
lid-driven cavity, airfoil flow.

## Quality Gates

Every release candidate verifies: build; unit, integration and regression tests;
reference models; serialization; import/export; geometry validity; solver
determinism; CLI and GUI smoke tests; performance regression; installer and
artifact hashes.

No item is marked complete because code exists. Completion means
**implemented + tested + validated + evidence recorded**. A failing required
gate blocks the release. See [CLAUDE.md](CLAUDE.md) for how gates are run.

## Milestone Numbering

```text
ROADMAP.md  describes capabilities and long-term sequencing.
TODO.md     owns implementation milestone IDs.

Historical milestone IDs are never renumbered.
Future capabilities receive a TODO.md milestone ID only when authorized.
```

Earlier revisions of this document numbered its chapters `P0`–`P26`, which
collided with `TODO.md`'s milestone IDs while meaning something else: roadmap
"P11" was Assemblies while milestone `P11` was Production Part Modeling. Those
chapter numbers are retired. A bare `P<number>` now means a `TODO.md` milestone
and nothing else.

## Project Documents

| Document | Purpose |
| --- | --- |
| [README.md](README.md) | Project overview and getting started |
| [ROADMAP.md](ROADMAP.md) | Long-term capability direction and completed capabilities |
| [TODO.md](TODO.md) | Authoritative implementation status and next work |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Technical architecture and invariants |
| [CLAUDE.md](CLAUDE.md) | Engineering workflow for coding agents |
| [docs/verification/](docs/verification/) | Proof of completion |
