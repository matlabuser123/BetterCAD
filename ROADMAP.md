# BetterCAD — Roadmap

> A modern, programmable, simulation-native, AI-ready mechanical CAD/CAE platform.

**What this document is for.** It answers *where BetterCAD is going*: the major
capabilities planned, the order their dependencies force, what each aims to
accomplish, and the long-term release targets. It is strategic.

**What it is not.** A capability appearing here is neither implemented nor
authorized. This document never marks work complete; it has no checkboxes and
no evidence links. [TODO.md](TODO.md) is the sole authority for what exists,
and every completed item there links to the evidence that earned it.

| Question | Authority |
| --- | --- |
| What is BetterCAD, and how do I build it? | [README.md](README.md) |
| Where is BetterCAD going? | **this document** |
| What is actually complete, and what is next? | [TODO.md](TODO.md) |
| How must the system be structured? | [ARCHITECTURE.md](ARCHITECTURE.md) |
| How must the work be executed and verified? | [CLAUDE.md](CLAUDE.md) |
| What proves a milestone is complete? | [docs/verification/](docs/verification/) |

---

# Vision

BetterCAD aims to combine:

```text
Parametric CAD
+ Assemblies
+ Drawings
+ Simulation
+ Optimization
+ Automation
+ Version control
+ AI-assisted engineering
```

on one engineering document model that does not have to be replaced along the
way.

It should eventually support the complete engineering workflow:

```text
Requirements
    ↓
Parametric CAD
    ↓
Assemblies
    ↓
Simulation
    ↓
Optimization
    ↓
Verification
    ↓
Drawings / Manufacturing
    ↓
Revision / Collaboration
```

Long term:

```text
Engineer
   ↓
Natural-language requirement
   ↓
Engineering agent
   ↓
Parametric model
   ↓
FEA / CFD / Thermal
   ↓
Optimization
   ↓
Verified design
   ↓
Manufacturing output
```

---

# Development Principles

## 1. Correctness before features

Never add large amounts of functionality on top of an unstable foundation.

Every major subsystem requires:

* deterministic tests;
* regression tests;
* analytical or independent validation where possible;
* failure diagnostics;
* reproducible evidence.

## 2. Stable engineering data model

Engineering intent must be stored explicitly.

Do not reduce the model to rendered meshes.

Preserve:

```text
parameters
constraints
sketches
features
bodies
assemblies
materials
loads
boundary conditions
simulation settings
requirements
results
revision history
```

## 3. Native units

Engineering values must retain their physical dimensions.

Prefer:

```cpp
Length width = 100_mm;
Force load = 5_kN;
Pressure pressure = 2.5_MPa;
```

over:

```cpp
double width = 100.0;
```

## 4. Stable identity

Persistent model objects require stable IDs.

Do not rely on transient:

```text
vector index
Face23
Edge11
object memory address
```

for long-term identity.

## 5. Dependency-driven regeneration

A parameter change should rebuild only affected objects.

```text
width
  ↓
Sketch001
  ↓
Extrude001
  ↓
Fillet001
  ↓
Body001
```

## 6. Headless core

The CAD engine must work without the GUI.

```text
                ┌───────────────┐
                │ Desktop GUI   │
                └───────┬───────┘
                        │
                ┌───────▼───────┐
                │ BetterCAD API │
                └───────┬───────┘
                        │
     ┌──────────────────┼──────────────────┐
     ↓                  ↓                  ↓
 Parametric CAD      Simulation        Automation
     │                  │                  │
     └──────────────────┼──────────────────┘
                        ↓
                    Core Model
```

CLI, GUI, Python, tests, and eventually AI should use the same core engine.
See [ARCHITECTURE.md](ARCHITECTURE.md) for how this is enforced.

---

# Current Verified Foundation

BetterCAD has a parametric part-modeling core that is complete and qualified.
A user — or a script, or a test — can create a document, declare parameters,
build a constrained sketch, apply any of ten parametric features, change a
dimension, regenerate deterministically, undo and redo, save, close, load,
regenerate again, validate, and export STEP and STL. Six realistic mechanical
parts exercise that workflow end to end and reproduce bit-identically across
three build configurations.

That is the foundation this roadmap builds on. Its scope and evidence are in
[TODO.md](TODO.md); a summary of what it can do is in
[README.md](README.md).

**No development phase is currently in progress, and the next capability has
not been authorized.** Selecting one is an explicit scope decision, recorded in
[TODO.md](TODO.md) before any work starts.

---

# Capability Roadmap

## Roadmap capability versus implementation milestone

These are two different things and this project has been bitten by confusing
them.

```text
Roadmap capability     a chapter of this document.
                       Named, never numbered. Strategic.
                       Planning it implies nothing about its existence.

Implementation         an ID in TODO.md: P11, P11-FEAT-003, P11-QUAL-001.
milestone              Authorizes work, tracks it, and links its evidence.
                       The only thing that can be marked complete.
```

**Milestone IDs are allocated by [TODO.md](TODO.md), in sequence, when a
capability is authorized — never in advance and never by this document.** A new
capability takes the next free `TODO.md` number at the moment it is authorized,
whatever position it occupies here.

Earlier revisions of this roadmap numbered its chapters `P0`–`P26`, which
collided with `TODO.md`'s milestone IDs while meaning something else entirely:
roadmap "P11" was Assemblies while milestone `P11` was Production Part
Modeling. Those chapter numbers are retired. A bare `P<number>` anywhere in
this repository now means a `TODO.md` milestone and nothing else.

## Status

| Capability | Implementation milestone | Status |
| --- | --- | --- |
| Repository and build foundation | `P0` | **COMPLETE** |
| Engineering core — units, IDs, parameters | `P1` | **COMPLETE** |
| Document model, commands, undo/redo | `P2` | **COMPLETE** |
| Geometry kernel foundation | `P3` | **COMPLETE** |
| Sketch system | `P4` | **COMPLETE** |
| Constraints and sketch solver | `P5`, `P6` | **COMPLETE** |
| Parametric feature engine — extrude | `P7` | **COMPLETE** |
| Regeneration engine | `P8` | **COMPLETE** |
| Persistence | `P9` | **COMPLETE** |
| Interchange — STEP/STL export, CLI | `P10` | **COMPLETE** |
| Production part modeling | `P11` | **QUALIFIED** |
| Desktop CAD application | not authorized | PLANNED |
| Assemblies | not authorized | PLANNED |
| Semantic topology | not scheduled | PLANNED — see dependency note |
| Technical drawings | not scheduled | PLANNED |
| Materials and engineering data | not scheduled | PLANNED |
| Meshing | not scheduled | PLANNED |
| Structural FEA | not scheduled | PLANNED |
| Thermal analysis | not scheduled | PLANNED |
| CFD | not scheduled | PLANNED |
| Design optimization | not scheduled | PLANNED |
| Engineering version control | not scheduled | PLANNED |
| Python automation | not scheduled | PLANNED |
| AI engineering agent | not scheduled | PLANNED |
| Manufacturing / CAM foundation | not scheduled | PLANNED |
| Performance and GPU | not scheduled | PLANNED |
| Production hardening | not scheduled | PLANNED |
| BetterCAD 1.0 | not scheduled | PLANNED |

Status meanings:

```text
COMPLETE       implemented, tested, validated, evidence recorded in TODO.md
QUALIFIED      COMPLETE, and separately re-verified end to end in every
               supported build configuration against gates fixed in advance
PLANNED        intended; not started, and not authorized by TODO.md
NOT SCHEDULED  no position in the queue has been decided
```

`v0.1.0` — annotated tag `v0.1.0` (tag object `93d84f0`) on commit `2da8966` —
released `P0`–`P10`. `P11` is qualified but not released; there is no `v0.2.0`
tag.

## Dependency order

The order below is what the dependencies force, not a schedule. Nothing in it
is authorized.

```text
                    qualified parametric part core
                                 │
        ┌────────────────────────┼────────────────────────┐
        ▼                        ▼                        ▼
  desktop application      stable references        materials and
  (interactive workflow)   (semantic topology)      engineering data
        │                        │                        │
        │                        ▼                        │
        │                   assemblies ◄──────────────────┘
        │                        │
        └────────────┬───────────┘
                     ▼
                  drawings
                     │
                     ▼
              meshing ──► structural FEA ──► thermal ──► CFD
                                 │
                                 ▼
                          design optimization
                                 │
                                 ▼
             version control · Python · AI · CAM · GPU
                                 │
                                 ▼
                        production hardening ──► 1.0
```

## Dependency note — semantic topology

Semantic topology appears late in this roadmap, and the `P11` evidence
suggests part of it may have to come earlier.

BetterCAD currently identifies faces and edges by **geometric matching**: a
reference stores the edge's supporting line or circle, or the face's plane and
outward side, and resolves it against the current body. That is honest and it
never substitutes the wrong entity — when the geometry a reference described
moves, the feature fails with `NotFound` and keeps no body rather than
silently attaching to something else. But it does mean a reference does not
follow geometry that a parameter moves, and that limitation is recorded against
every feature that uses references
([evidence](docs/verification/P11-QUAL-001/README.md)).

Capabilities that lean heavily on persistent references would inherit that
limitation directly:

```text
assemblies                       mates attach to faces and edges
drawings                         dimensions and annotations attach to edges
simulation boundary conditions   loads and supports attach to faces
manufacturing annotations        tolerances and finishes attach to faces
```

> Semantic Topology is currently a later roadmap capability, but a minimal
> stable-reference layer may become a prerequisite for earlier phases. Any such
> scope change requires an explicit milestone decision.

This note records the dependency. It does not reorder the roadmap, authorize
the work, or commit to a design. If the decision is made, it is made in
[TODO.md](TODO.md) as a milestone with its own gates.

---

# Delivered Foundation

What `P0`–`P11` actually delivered, and what the original scope of each area
still leaves outstanding. The outstanding column is the honest part: these
items were planned under these headings and are **not** built. They remain
future work, and the qualified scope is narrower than the original ambition.

Full scope, acceptance criteria and evidence for the delivered column are in
[TODO.md](TODO.md).

## Repository and build foundation — `P0`

**Delivered.** C++23 project; CMake build system; Debug, Release and
Debug-shared configurations; core libraries; CLI executable; desktop
executable; Catch2 test infrastructure; strict warning set with warnings as
errors; `.clang-format`; documentation skeleton.

**Outstanding.** CI (no `.github/workflows` exists, so nothing re-runs the
suite automatically); enforced formatting check; static analysis; sanitizer,
coverage and memory-checking configurations.

## Engineering core — `P1`

**Delivered.** Strongly typed quantities stored in coherent SI, with
dimensional errors caught at compile time; strongly typed stable IDs
(`DocumentId`, `ObjectId`, `ParameterId`, `SketchId`, `EntityId`,
`ConstraintId`, `FeatureId`, `BodyId`, `FaceId`, `EdgeId`, `VertexId`) that are
never container indices and are never reused; parameters with name, value,
dimension, display unit, stable ID and revision.

**Outstanding.** `ComponentId`, `MaterialId` and `SimulationId` (their
subsystems do not exist). **Parameter expressions are stored but not
evaluated**, so a derived dimension such as
`housing_width = bearing_OD + 2 * wall_thickness` needs its own parameter or a
sketch that builds the relation geometrically.

## Document model — `P2`

**Delivered.** Document creation, object registry, name and ID lookup, dirty
state, revisions, command architecture, undo and redo, all compared against
deterministic document state rather than assumed.

**Outstanding.** Configurations; material assignments; assembly and simulation
containers.

## Geometry kernel foundation — `P3`

**Delivered.** Open CASCADE behind BetterCAD's own interfaces, with the kernel
confined to `occt/` adapter directories and the containment enforced by a
build-failing layering test; primitives; volume, surface area, centroid,
bounding box and mass properties; union, difference and intersection; all
validated against closed-form analytic values.

**Outstanding.** Nothing from the original scope; alternative geometry backends
remain a possibility the abstraction keeps open rather than a plan.

## Sketch system — `P4`

**Delivered.** Sketch placement on a `Frame3D` (origin, axes, normal, plane
transformation); points, lines, circles and arcs; length, radius, centre,
endpoint, bounding-box, intersection and distance queries; deterministic under
save/load.

**Outstanding.** Ellipses and splines; sketches on arbitrary planar faces
(placement is explicit, and the global XY/XZ/YZ planes are the usual starting
point).

## Constraints and sketch solver — `P5`, `P6`

**Delivered.** Coincident, horizontal, vertical, parallel, perpendicular,
distance, radius, equal and fixed constraints, represented separately from
geometry and validated when added; a Gauss–Newton solver over free point
coordinates and radii that distinguishes under-constrained, fully constrained,
over-constrained, inconsistent and failed, and reports residual, iterations and
degrees of freedom; validation cases including rectangles, bolt-circle layouts,
conflicts and redundancy.

**Outstanding.** Tangent, concentric, horizontal-distance, vertical-distance,
diameter, angle, symmetric and midpoint constraints. Symmetry is currently
built with construction geometry and equal constraints.

## Parametric feature engine — `P7`, `P11`

**Delivered.** Extrude (`P7`), then revolve, chamfer, fillet, hole, linear
pattern, circular pattern, mirror, sweep and loft (`P11`). Each stores its
definition rather than its result, operates as new body, join, cut or
intersect, validates its geometry, regenerates from parameters, fails
atomically with a structured diagnostic, round-trips through save/load, and is
validated against independently computed volumes.

**Outstanding.** Shell, draft and rib. Variable-radius fillets and
setback/corner controls. Hole threads, drill points, spotfaces, standards
databases and tolerance classes. Symmetric and total-length pattern modes,
suppressed instances, patterns of patterns. Sweep guide curves, twist, scale,
variable sections and non-planar paths. Loft sections of differing shapes,
smooth interpolation, guide curves and end conditions. Mirror and pattern
references to datum geometry rather than explicit model coordinates.

## Regeneration engine — `P8`

**Delivered.** An explicit dependency graph, dirty propagation, topological
ordering, partial regeneration, cycle detection, failure propagation and
transactional rebuild. A failed downstream feature commits nothing and cannot
corrupt the document — checked by a dedicated atomicity test per feature.

**Outstanding.** Parallel feature evaluation; incremental regeneration beyond
the current dirty-subgraph rebuild.

## Persistence and interchange — `P9`, `P10`

**Delivered.** The native `.bcad` format: transparent, deterministic,
pretty-printed JSON storing document metadata, stable IDs, parameters,
sketches, constraints, features and dependencies — inputs only, since geometry
is derived. Atomic saves. A round trip of create → save → destroy → load →
regenerate that is compared as a full fingerprint. STEP (AP214) and STL export,
each verified by reading the result back and measuring it. A CLI with `new`,
`info`, `validate`, `export-step` and `export-stl` on the same public API.

**Outstanding.** **STEP import** — the only STEP reader in the repository is
test-only tooling used to check exports. DXF, IGES and OBJ, in either
direction. 3MF, glTF, Parasolid and JT. A ZIP-style container, a geometry
cache, a preview image, and schema migration between format versions (the
format carries a version, but nothing has needed migrating yet).

## Production part modeling — `P11`

**Delivered.** The nine features above, plus six mechanical reference models —
a stepped shaft, a bolted flange, a V-belt pulley, a pillow block, an L bracket
and a U-bolt — built through the public API alone, with every dimension checked
against geometry computed independently from its parameters, and a full
qualification against twenty gates fixed before the run.

**Outstanding.** The original scope for this area also listed: datum planes,
datum axes, coordinate systems and reference geometry; construction geometry as
a first-class concept; split body and combine; direct measurement; materials
and appearance; design equations; configurations. **None of these is built.**
Multi-body parts and advanced patterns exist only to the extent the features
above provide them.

The qualified scope is the ten milestones in [TODO.md](TODO.md), not this
paragraph's original list.

---

# Planned Capabilities

Everything below is future work. Nothing here is started, and nothing here is
authorized. Each section describes intent and scope, not commitment.

## Desktop CAD application

The desktop executable currently builds as a placeholder shell; no GUI
functionality exists or is claimed. The goal is the first practical interactive
CAD environment, on Qt 6.

Major UI areas:

```text
Menu / command system
Feature toolbar
Model tree
Property editor
3D viewport
Sketch environment
Status bar
Command search
Diagnostics panel
```

Navigation:

```text
orbit
pan
zoom
fit
standard views
perspective
orthographic
```

Selection:

```text
body
face
edge
vertex
sketch
feature
```

**Gate.** A user must be able to create a parametric mechanical part entirely
from the GUI, with the GUI owning none of the engineering state.

## Assemblies

Support mechanical systems composed of multiple parts.

Component model — each component needs:

```text
part reference
instance ID
transform
configuration
visibility
suppression state
```

Mates:

```text
Fixed
Coincident
Concentric
Parallel
Perpendicular
Distance
Angle
Tangent
Lock
Gear
Rack-and-pinion
Screw
```

Assembly state should solve component degrees of freedom, with the mate model
separated from the solver exactly as the sketch model is.

Eventually:

```text
collision detection
interference detection
clearance analysis
exploded views
motion constraints
assembly mass properties
```

**Gate.** Reference assemblies must solve deterministically without unstable
placement.

**Dependency.** Mates attach to faces and edges; see the semantic topology
dependency note above.

## Semantic topology

Reduce one of the most important weaknesses of traditional parametric CAD:
fragile topological references.

Do not permanently identify model intent using only:

```text
Face12
Edge27
```

Develop semantic references:

```text
planar face
generated by Extrude001
normal approximately +Z
largest area
adjacent to Hole003
```

A semantic resolver attempts to recover intended entities after
topology-changing edits.

Research topics:

```text
persistent naming
geometric signatures
feature provenance
adjacency graphs
semantic matching
confidence scores
ambiguity detection
```

This is a major differentiating capability for BetterCAD, and the geometric
signatures shipped in `P11` are the first layer of it — deliberately limited,
with the limits documented and tested rather than hidden.

## Technical drawings

Generate manufacturing-ready drawings from the parametric model:

```text
drawing sheets
standard views
projected views
section views
detail views
broken views
dimensions
center marks
center lines
annotations
GD&T foundation
BOM tables
balloons
revision tables
title blocks
```

Export:

```text
PDF
DXF
SVG
```

Drawings must reference the model, never copy it.

**Gate.** Model changes must propagate into drawing views and dimensions.

## Materials and engineering data

Make engineering properties part of the model:

```text
density
elastic modulus
Poisson ratio
yield strength
ultimate strength
thermal conductivity
specific heat
thermal expansion
viscosity
electrical conductivity
```

Support custom materials.

**Gate.** Material assignments persist through save/load and propagate into
mass and simulation calculations.

## Meshing

A common simulation mesh infrastructure, separate from both exact geometry and
display tessellation.

```text
1D  2D  3D
```

Element types:

```text
beam
triangle
quadrilateral
tetrahedron
hexahedron later
```

Capabilities:

```text
global sizing
local sizing
surface refinement
boundary layers later
quality metrics
mesh validation
mesh convergence tools
```

Potential integration: Gmsh, behind BetterCAD-owned abstractions.

## Structural FEA

Start with linear elasticity, small deformation, static analysis and isotropic
materials.

Loads and supports:

```text
fixed supports
forces
pressures
gravity
moments
```

Results:

```text
displacement
strain
stress
von Mises stress
reaction forces
factor of safety
```

Verification against analytical benchmarks:

```text
uniaxial bar
cantilever beam
simply supported beam
plate problems
```

**Gate.** FEA must satisfy verification tolerances before optimization uses its
results. Visually plausible contours are not evidence of accuracy.

## Thermal analysis

```text
steady conduction
transient conduction
heat generation
prescribed temperature
heat flux
convection
thermal contact later
```

Couple temperature fields into structural analysis for thermal expansion.

## CFD

Long-term scope:

```text
incompressible flow
laminar
turbulent
thermal flow
compressible flow
multiphase later
```

Architecture:

```text
CAD
 ↓
fluid volume extraction
 ↓
mesh
 ↓
boundary conditions
 ↓
CFD solver
 ↓
results
 ↓
design parameters
```

Simulation results should remain linked to the originating CAD revision.

## Design optimization

Turn the CAD model into a design-space exploration platform.

Design variables:

```text
dimensions
material
feature suppression
angles
thicknesses
mesh parameters
```

Objectives:

```text
minimum mass
minimum cost
minimum pressure drop
minimum drag
minimum stress
maximum stiffness
maximum efficiency
```

Constraints:

```text
stress < allowable
displacement < limit
temperature < limit
factor of safety > target
manufacturing limits
```

Algorithms may eventually include parameter sweeps, gradient methods, genetic
algorithms, Bayesian optimization, surrogate models and topology optimization.

## Engineering version control

Git-like concepts for engineering models:

```text
commit
branch
merge
history
diff
revision
tag
```

But diffs must be semantic:

```text
Sketch002.width
100 mm → 120 mm

Hole003.diameter
8 mm → 10 mm

Material
Al 6061 → Al 7075
```

Eventually enable branch comparison and controlled merging of independent
mechanical changes.

## Python automation

Make the entire platform programmable:

```python
part = cad.new_part("Bracket")

sketch = part.sketch("XY")
sketch.rectangle(width="100 mm", height="50 mm")

body = sketch.extrude("20 mm")

body.hole(
    diameter="10 mm",
    through_all=True
)

body.fillet(radius="3 mm")
```

Expose documents, parameters, sketches, constraints, features, assemblies,
materials, simulation, optimization and export.

The GUI should not have capabilities unavailable to the API without a strong
reason.

## AI engineering agent

Introduce AI only after the engineering APIs are reliable. AI must operate
through structured, validated commands, and must never modify internal geometry
directly.

```text
Natural language
      ↓
Intent extraction
      ↓
Engineering plan
      ↓
Structured API calls
      ↓
Validation
      ↓
CAD modification
      ↓
Simulation / verification
      ↓
Result
```

A request such as:

```text
"Create a 150 × 80 × 10 mm mounting plate
with four M8 holes 15 mm from each corner."
```

should compile into explicit modeling operations. An advanced request:

```text
"Reduce this bracket's mass by 20% while keeping
maximum stress below 120 MPa and displacement
below 0.5 mm."
```

drives the pipeline:

```text
requirement
 ↓
design variables
 ↓
candidate models
 ↓
FEA
 ↓
constraint evaluation
 ↓
optimization
 ↓
verified model
```

## Manufacturing / CAM foundation

Connect design intent to manufacturing.

Initial scope:

```text
manufacturing metadata
machining stock
holes
threads
tolerances
surface finish
sheet thickness
bend information
```

Later:

```text
2.5D CAM
tool library
toolpaths
feeds and speeds
G-code postprocessing
sheet-metal unfolding
additive manufacturing preparation
```

## Performance and GPU

Scale BetterCAD to large real-world engineering models. Profile first; optimize
on evidence.

Potential targets:

```text
model regeneration
constraint solving
tessellation
rendering
large assemblies
meshing
FEA
CFD
geometry queries
selection
collision detection
```

GPU usage may include rendering, large mesh visualization, selection
acceleration, simulation kernels, matrix operations and post-processing.

Never claim acceleration without measured before/after benchmark evidence, and
never sacrifice correctness for a benchmark.

## Production hardening

```text
crash recovery
autosave
document corruption detection
migration between file versions
large assembly stability
undo/redo reliability
thread safety
determinism
security
plugin isolation
logging
diagnostics
performance profiling
installer
automatic updates
release qualification
```

Testing expands to:

```text
unit
integration
regression
property-based
fuzz
performance
stress
large-model
file compatibility
GUI acceptance
```

## BetterCAD 1.0

A 1.0 release should not mean:

```text
"we implemented many features"
```

It should mean:

```text
reliable enough to design real mechanical systems
```

Minimum expected 1.0 capabilities:

* robust parametric parts;
* stable sketch solver;
* production feature modeling;
* assemblies;
* drawings;
* STEP interoperability, import as well as export;
* deterministic save/load;
* semantic engineering parameters;
* materials;
* mass properties;
* automation API;
* reliable undo/redo;
* regression reference models;
* verified installers/releases;
* comprehensive user documentation.

Simulation and AI may be included in 1.0 only if sufficiently mature. Do not
weaken CAD reliability merely to include them.

---

# Release Milestones

Release targets, not a schedule. A target is proposed scope; a release happens
only when its gates pass.

| Release | Scope | Status |
| --- | --- | --- |
| **v0.1 — Modeling Foundation** | units, documents, parameters, Open CASCADE, sketch basics, basic constraints, extrude, save/load, CLI | **RELEASED** — tag `v0.1.0`, commit `2da8966` |
| **v0.2 — Practical Part Modeling** | revolve, holes, fillets, chamfers, patterns, datum geometry, improved sketch solver, STEP import/export, desktop workflow | NOT RELEASED — revolve, holes, fillets, chamfers, patterns and STEP export delivered by `P11` and `P10`; datum geometry, further sketch-solver work, STEP import and the desktop workflow outstanding |
| **v0.3 — Production Part Design** | multi-body parts, shell, draft, sweep, loft, equations, configurations, materials, mass properties, production GUI | NOT RELEASED — sweep and loft delivered by `P11`, mass properties by `P3`; multi-body parts only as far as the feature operations provide them; shell, draft, equations, configurations, materials and the GUI outstanding |
| **v0.4 — Assemblies** | components, mates, assembly solver, interference, assembly tree, assembly mass properties | NOT STARTED |
| **v0.5 — Drawings** | drawing sheets, views, sections, dimensions, BOM, PDF/DXF export | NOT STARTED |
| **v0.6 — Simulation Foundation** | meshing, materials, linear structural FEA, thermal foundation, verified benchmarks | NOT STARTED |
| **v0.7 — Multiphysics / Optimization** | CFD integration, thermal coupling, design studies, parameter optimization, simulation-linked CAD | NOT STARTED |
| **v0.8 — Engineering Platform** | Python API, plugin system, engineering version control, semantic diff, automation | NOT STARTED |
| **v0.9 — AI-Native Engineering** | natural-language modeling, model inspection, design modification, simulation orchestration, optimization agent, requirement checking | NOT STARTED |
| **v1.0 — Production BetterCAD** | stable CAD, stable assemblies, stable drawings, interoperability, automation, release qualification, documentation, real engineering reference projects | NOT STARTED |

`P11` delivered part of the v0.2 and v0.3 scope without cutting a release; the
work is qualified, not shipped. Release scope will be re-cut when a release is
actually prepared, against the gates below.

---

# Reference Projects

Maintain permanent models that exercise BetterCAD functionality. They are
regression assets, not disposable demos, and are not deleted because they
reveal a regression — the regression is fixed.

**In the repository today** (six parts, built through the public API, every
dimension validated against independently computed geometry):

```text
shaft              stepped, turned
flange             bolted, with a bolt circle
pulley             V-belt
bearing_housing    pillow block
mounting_bracket   L bracket with a lofted gusset
u_bolt             swept
```

**Planned as capabilities allow:**

```text
simple_block
gearbox_housing
motor_mount
four_bar_linkage
small_gearbox_assembly     needs assemblies
machine_frame              needs assemblies
```

Later simulation references:

```text
cantilever_beam
pressure_vessel_segment
heat_sink
pipe_flow
lid_driven_cavity
airfoil_flow
```

---

# Quality Gates

Every release candidate should verify:

```text
build
unit tests
integration tests
regression models
serialization
import/export
geometry validity
solver determinism
GUI smoke test
CLI smoke test
performance regression
installer/package
release artifact hashes
```

No checkbox is marked complete because code merely exists.

Completion means:

```text
implemented
+
tested
+
validated
+
evidence recorded
```

A failing required gate blocks the release. See [CLAUDE.md](CLAUDE.md) for how
gates are run and recorded.

---

# Beyond 1.0

Possible future directions.

## Generative engineering

```text
requirements
→ generated concepts
→ simulation
→ ranking
→ manufacturability checks
→ verified candidates
```

## Requirements engineering

Allow explicit requirements such as:

```text
REQ-101
Mass < 2.0 kg

REQ-102
Factor of safety ≥ 2.0

REQ-103
Maximum displacement < 0.25 mm

REQ-104
Manufacturing cost < $50
```

Automatically link verification evidence.

## Digital thread

Connect:

```text
Requirement
→ CAD parameter
→ analysis
→ drawing
→ manufacturing
→ test result
```

## Engineering knowledge

Reusable:

```text
design rules
company standards
materials
preferred suppliers
manufacturing constraints
validation procedures
```

## Cloud / distributed solving

Keep the desktop application functional offline while allowing optional remote:

```text
FEA
CFD
optimization
rendering
collaboration
```

---

# What Happens Next

The parametric part-modeling foundation is qualified, so the original
restriction — *do not start assemblies, FEA, CFD, AI, CAM or cloud
collaboration until the foundation is reliable* — has been satisfied as
written. It is replaced by a narrower and permanent one:

> **Listing a capability here does not authorize it.** No capability above may
> be started until [TODO.md](TODO.md) records it as an authorized milestone
> with its own gates. A capability being next in the dependency order is not
> authorization either.

The first major objective has been met:

> Create a small CAD system that can build, edit, regenerate, save, reload and
> export a real parametric mechanical part without breaking.

The next objective is a decision, not a feature. The likely candidates are the
desktop application, a minimal stable-reference layer, or assemblies — and the
choice between them is exactly the kind of scope decision this document must
not make on its own.

---

# Project Documents

* [README.md](README.md) — project overview and getting started
* [ROADMAP.md](ROADMAP.md) — long-term capability direction (this document)
* [TODO.md](TODO.md) — authoritative implementation status and next work
* [ARCHITECTURE.md](ARCHITECTURE.md) — system architecture and dependency rules
* [CLAUDE.md](CLAUDE.md) — engineering workflow and verification rules
* [docs/verification/](docs/verification/) — evidence for completed milestones
