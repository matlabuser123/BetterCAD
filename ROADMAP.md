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
```

Current authorized development:

```text
P14 — Technical Drawings
```

Released: `v0.1.0` — Foundation.

Qualified but not yet separately released: `P11`, `P12`, `P13`.

---

## Capability Roadmap

| Capability | Milestone | Status |
| --- | --- | --- |
| Foundation | `P0`–`P10` | **Qualified / Released** |
| Parametric Part Modeling | `P11` | **Qualified** |
| Parametric CAD Completion | `P12` | **Qualified** |
| Assemblies | `P13` | **Qualified** |
| Technical Drawings | `P14` | **Qualified** |
| Materials / Engineering Data | `P15` | **In Progress** |
| Meshing | `P16` | Planned |
| Structural FEA | `P17` | Planned |
| Thermal Analysis | `P18` | Planned |
| CFD Integration | `P19` | Planned |
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

## Technical Drawings — In Progress

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

## Materials / Engineering Data — Planned

A material system: density, elastic modulus, Poisson ratio, yield and ultimate
strength, thermal conductivity, specific heat, coefficient of thermal
expansion, viscosity, electrical conductivity, and custom materials.

**Gate.** Material assignments persist; units correct; mass properties consume
material data; simulation consumes the same material definitions.

---

## Meshing — Planned

Simulation mesh infrastructure separate from both CAD B-Reps and visualization
tessellation. Targets: 1D, 2D and 3D; beam elements, triangles, quadrilaterals,
tetrahedra, hexahedra later; global and local sizing; curvature and surface
refinement; mesh quality; convergence tools.

A meshing backend such as Gmsh may be used behind BetterCAD abstractions.

---

## Structural FEA — Planned

Initial scope: linear elasticity, small deformation, static analysis, isotropic
materials. Boundary conditions: fixed supports, forces, pressures, gravity,
moments. Results: displacement, strain, stress, von Mises stress, reactions,
factor of safety.

Qualification requires analytical benchmarks:

```text
uniaxial bar
cantilever beam
simply supported beam
plate benchmark
```

Plausible contour plots are not validation.

---

## Thermal Analysis — Planned

Steady and transient conduction, volumetric heat generation, prescribed
temperature, heat flux, convection, and thermal contact later. Temperature
fields eventually couple into structural analysis for thermal expansion and
stress.

---

## CFD Integration — Planned

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
