# BetterCAD — Architecture

> How BetterCAD must be structured: layers, boundaries, dependency rules and the
> invariants every milestone has to preserve.

This document defines **allowed structure**, never schedule and never status.
[ROADMAP.md](ROADMAP.md) defines direction, [TODO.md](TODO.md) authorizes
implementation, [CLAUDE.md](CLAUDE.md) defines execution.

It describes the **target** architecture, parts of which are still ahead of the
code. For the architecture **as built** — the real targets, namespaces, module
contracts and layering checks — see
[docs/architecture.md](docs/architecture.md), which is kept in step with the
source. Anything marked `[future]` here does not exist; the architecture
reserves its place rather than claiming it.

## Principles

BetterCAD should be modular, deterministic, headless-first, testable,
scriptable, simulation-ready, resilient to topology changes, and extensible
without coupling every subsystem together.

The architecture strongly separates:

```text
engineering state · geometry · visualization · UI
simulation · persistence · external integrations
```

The foundational idea:

```text
Parameters + Constraints + Features + Dependencies + References
    = the engineering model
```

Geometry, render meshes, drawings, simulation meshes and analysis results are
derived representations of that model. That distinction is the basis of
everything below.

## System Overview

```text
Desktop ──────┐
CLI ──────────┤
Python ───────┤ future
AI Agent ─────┘ future
       │
       ▼
BetterCAD Public API
       │
       ▼
Document / Commands / Transactions
       │
       ├── Parameters
       ├── Sketches
       ├── Features
       ├── Bodies
       ├── Assemblies   [future]
       └── Simulations  [future]
       │
       ▼
Dependency / Regeneration Engine
       │
       ▼
Geometry Services
       │
       ▼
OCCT Adapter
       │
       ▼
Open CASCADE
```

The same model must be usable from the GUI, the CLI, tests, batch processing,
Python and a future agent. The GUI must never become the architecture.

## Layers

| Layer | Module | Contains |
| --- | --- | --- |
| 0 | `core` | IDs, units, parameters, document, commands, dependency graph, diagnostics, math value types, geometry abstraction |
| 1 | `sketch` | Entities, constraints, solver, profile extraction |
| 2 | `features` | Feature definitions, regeneration, validation |
| 3 | `io` | Native `.bcad`, STEP/STL export |
| 4 | `renderer`, `scripting` | Display data, bindings |
| — | `apps` | Desktop application, CLI |

A module may include the public headers of its own module or of a lower layer,
and nothing else. Public headers live in `include/bettercad/<module>/`; private
headers beside their sources in `src/<module>/`.

Target directories not yet created: `src/assembly/`, `src/drawing/`,
`src/simulation/`, `src/versioning/`, `benchmarks/`, `docs/adr/`.
`src/renderer/` and `src/scripting/` exist but are empty.

## Document Model

`Document` is the canonical engineering container and the single owner of
persistent state. There is no global document.

```text
Document
├── identity (DocumentId — a UUID) and metadata
├── parameters
├── sketches
├── features
├── bodies
├── dependencies
├── material assignments      [future]
├── configurations            [future]
└── analysis definitions      [future]
```

**Objects.** Persistent objects share a minimal `DocumentObject` abstraction —
`id()`, `name()`, `state()`, `type()` — defined in the module that owns the kind,
so `core` never depends on sketches or features. Prefer composition over deep
inheritance. Object states: clean, dirty, evaluating, valid, failed, suppressed,
missing-reference.

**Identity.** Every persistent item has a strongly typed stable ID —
`ObjectId`, `ParameterId`, `SketchId`, `EntityId`, `ConstraintId`, `FeatureId`,
`BodyId`, `FaceId`, `EdgeId`, `VertexId`, and later `ComponentId`,
`MaterialId`, `SimulationId`. IDs are never container indices and are never
reused. Mixing kinds must fail to compile. Hold IDs across mutations, not
pointers, and resolve them through the document; this is what keeps references
valid across undo, redo and regeneration.

**Units.** The unit system lives in `core`. Engineering APIs take `Quantity<D>`
(`Length`, `Angle`, `Pressure`), never bare `double`. Values are stored in
coherent SI and converted only at boundaries — the kernel adapter, the file
format, the UI. Display units are presentation metadata, never storage.

**Revisions.** Objects carry revision counters that advance only on effective
change. Revisions drive dirty state, regeneration and cache invalidation.

## Parametric Model

Parameters are first-class document objects: ID, name, physical dimension,
value, display unit, optional expression, revision. An expression
(`housing_width = bearing_OD + 2 * wall_thickness`) makes a parameter *driven*:
its value is computed, with dimensional analysis, from the parameters the
expression names. Those names are dependencies in the same graph features use,
and driven values are evaluated first in every regeneration pass.

**Features store inputs, not outputs.**

```text
ExtrudeFeature
├── definition          persistent: profile, depth, direction, operation, target
└── generated result    derived: Body
```

Every feature follows one evaluation contract — a context holding the document
and geometry services in, a result holding status, bodies and diagnostics out.
The separation of definition from generated shape is what makes regeneration,
save/load, undo/redo and configurations possible.

Features may operate as `NewBody`, `Add`, `Remove` or `Intersect`. Boolean
behaviour is shared infrastructure, never re-encoded per feature. Feature
modules stay isolated (`features/extrude/`, `revolve/`, `fillet/`, …) and depend
on generic geometry services, never on OCCT.

A `Body` is a persistent logical entity — `BodyId`, name, feature history,
material, generated shape, visibility, state — so multi-body parts become
possible without redesigning the document.

## Dependency and Regeneration

Objects declare their inputs; the document builds a directed dependency graph
from them.

```text
width → Sketch001 → Extrude001 → Fillet001 → Pattern001
```

The engine owns edge management, cycle detection, dirty propagation, evaluation
ordering and failed-dependency propagation. A valid regeneration order is a
topological sort.

```text
parameter change → transaction → mark source dirty
    → propagate → order → evaluate dirty nodes → commit valid outputs
```

Regeneration is **transactional and atomic**. Good model state is never
partially overwritten before the new state is known valid. A failed feature
commits nothing, keeps no body, and cannot corrupt the document; its dependents
are blocked and keep no stale results. Regenerating does not change the
document's revision or dirty state.

## Geometry

BetterCAD defines its own geometry-facing interfaces — `Shape`, `Solid`, `Face`,
`Edge`, `Vertex`, or opaque handles. `TopoDS_Shape` must never appear outside an
adapter.

```text
GeometryService → OcctGeometryService → Open CASCADE
```

Wrapping the kernel localizes third-party APIs, simplifies upgrades, permits
test doubles and alternative backends, reduces compile coupling, and lets
BetterCAD translate kernel errors into its own diagnostics. Kernel model space
is millimetres; conversion happens only in the adapter.

**Three representations, never interchangeable:**

| Representation | Contents | Used for |
| --- | --- | --- |
| Exact model geometry | B-Rep, curves, surfaces, topology | CAD |
| Tessellated display geometry | triangles, normals, edges, selection IDs | rendering |
| Simulation mesh | nodes, elements, boundary regions | CAE |

**Topology.** Distinguish body, solid, shell, face, wire, edge, vertex.
BetterCAD identity must not equal raw kernel topology indices, which are
transient.

**Semantic topology.** Persistent engineering intent must not depend permanently
on transient kernel identities such as `Face23` or `Edge17`. A topological
reference should eventually carry source feature, generated entity class,
geometric signature, adjacency signature, orientation, semantic role and a
fallback kernel identity — enough to express *the largest planar +Z face
generated by Extrude001, adjacent to Hole002* and to recover it after a
topology-changing edit.

Today references are geometric signatures (an edge's supporting line or circle;
a face's plane and outward side). They never substitute the wrong entity: when
the geometry moves, resolution fails with `NotFound`. Semantic topology is
planned, not built — see [ROADMAP.md](ROADMAP.md). A minimal stable-reference
layer may become a prerequisite for assemblies, drawings, simulation boundary
conditions and manufacturing annotations.

## Sketching and Constraints

```text
Sketch
├── placement (Frame3D)
├── entities
├── constraints
├── dimensions
└── solver state
```

A sketch owns no B-Rep solids. It produces profiles that features consume.

**Entities and constraints are separate concepts.** Entities carry an
`EntityId`, construction state, geometry and metadata; a variant is preferred to
an inheritance hierarchy. Constraints are represented independently —
`ConstraintId`, type, entity references, optional driving parameter, enabled
flag, diagnostic state — and never encoded implicitly inside geometry.

**The solver is behind an interface:**

```text
SketchModel → ConstraintProblemBuilder → ISketchSolver → implementation
```

so numerical experimentation does not require rewriting the sketch model.

**The result is never a bool.** `SketchSolveResult` carries status, solution,
residual norm, iteration count, degrees of freedom, conflicting constraints and
diagnostics, with statuses under-constrained, fully constrained,
over-constrained, inconsistent, converged-with-warnings and failed.

**Profile extraction** is its own pipeline — connectivity analysis, wire
construction, closed-profile detection, `Profile` objects. Extrude is not
responsible for understanding arbitrary raw sketch entities.

## Persistence

Serialize engineering definitions, not geometry.

```text
Document → Serialization DTO → format writer → .bcad
```

Never serialize internal C++ object layouts; use explicit schemas. Only inputs
are stored — geometry is regenerated after loading. Values are SI, written with
round-trip-exact decimals. References are IDs, and a reference to a missing item
is a valid document state that regeneration reports. Every ID counter is
persisted so IDs are never reused. Output is deterministic; saving is atomic
(write a sibling temporary, rename over the target).

Every document carries a format tag, a schema version and an application
version. Schemas will change; migration must remain possible.

**Interchange lives behind adapters** (`io/step/`, `io/stl/`, `io/dxf/`, …).
Import: external file → adapter → translation → BetterCAD model. Export: the
reverse. Today only STEP and STL export exist.

**A valid round trip is** create → save → destroy → load → regenerate →
compare — never a file-size or object-count check.

## Commands and Transactions

Every user-visible modification passes through a command or transaction.

```text
GUI action → ModifyParameterCommand → DocumentTransaction
    → regeneration → commit
```

Commands implement `execute` / `undo` / `redo` and are recorded in a history.
Redo must reproduce the exact state of the first execution, IDs included. Undo
operates on engineering state, not UI state, and is never implemented by
reversing kernel side effects by hand.

**Diagnostics are structured everywhere:** code, severity (info, warning, error,
fatal), message, offending object. Expected engineering failure — a fillet
radius too large, an unclosed profile, a failed boolean, a missing reference, an
over-constrained sketch — returns a structured result. Exceptions are for
exceptional programming and system conditions.

A central `DocumentValidator` checks duplicate IDs, missing references,
dependency cycles, invalid parameter dimensions, failed features, invalid bodies
and sketches, and serialization inconsistencies.

## Applications

**GUI.** MVC/MVVM separation: view → controller/view-model → commands → core
API. Never a widget callback mutating the kernel. Application services
(`DocumentManager`, `SelectionManager`, `CommandManager`, `ExportService`) sit
above the core and orchestrate; they do not own engineering truth.

**CLI.** Calls the same public API. `bettercad-cli validate model.bcad` becomes
load → validate → regenerate → validate geometry → report, which makes the CLI
useful for CI.

**Renderer.** Consumes derived display data only:

```text
exact geometry → tessellation → render mesh → GPU upload → renderer
```

Never render from document internals. Cache render meshes by `BodyId` +
geometry revision and rebuild only what changed. Selection maps GPU IDs back to
semantic model objects — `FaceId`, `EdgeId`, `BodyId` — never raw triangle
indices.

## Simulation Architecture

Simulation consumes the same canonical model and must not depend on the GUI.

```text
Document → SimulationDefinition → geometry region mapping
    → mesh → physics model → solver → results
```

Boundary conditions reference **semantic geometry regions**, not mesh triangle
IDs:

```text
PressureBC
  face  = TopologyReference(...)
  value = 2 MPa
```

The mesh is a separate model — nodes, elements, regions, boundary sets, quality
metrics, source geometry references — with a maintained mapping between a CAD
face and a mesh boundary region.

Physics solvers live in `simulation/{common,mesh,structural,thermal,cfd,
optimization}/`, never inside GUI or CAD feature code. Results are immutable
artifacts tied to model state — simulation ID, source document revision, solver
configuration, mesh revision, fields, validation metadata — and are clearly
marked stale when the model changes.

## Automation and AI

**Python** binds to stable public APIs through pybind11, never to internal
classes. **Plugins** declare a manifest, an API version and capabilities, and
communicate through stable extension interfaces rather than private internals.

**AI operates above the public API:**

```text
request → LLM → structured engineering plan → tool/API calls
    → document transaction → regeneration → validation → result
```

Every AI operation must be typed, validated, undoable and auditable. The agent
never fabricates kernel-internal B-Reps.

**Versioning** eventually operates on semantic document state, producing diffs
such as `Hole003 diameter: 8 mm → 10 mm`, not binary blobs.

**Configurations** are overrides on one model graph, never duplicated documents.

## Dependency Rules

Dependencies flow downward:

```text
apps → public_api → document → parametric_engine
    → sketch / features → geometry → geometry_backend
```

Forbidden:

```text
geometry → GUI          core → Qt
document → renderer     geometry → Python
simulation → GUI        OCCT → anything outside an occt/ adapter
```

Required instead:

```text
GUI → core
renderer → read-only model representation
Python → public API
simulation → public model/query interfaces
```

`core` must not depend on Qt, OpenGL, Vulkan, CUDA, Python or Open CASCADE
implementation details. Module cycles are prohibited. The renderer must not own
engineering geometry. GPU work belongs in explicit subsystems with a CPU
reference path where practical.

**Enforcement.** The `architecture.layering` test checks OCCT containment, Qt
containment, layering and the public/private split, and fails the build.
Fixture trees prove the checker catches each violation.

**Other boundaries.** Treat external CAD files, native documents, plugins,
scripts, macros, AI-generated commands and network data as untrusted: validate
sizes, references, types, schema, version and resource limits. Cloud services
must remain optional; core CAD never requires connectivity.

**Ownership.** `unique_ptr` for single ownership, `shared_ptr` only for genuine
shared lifetime. The document owns persistent definitions; derived state owns
feature outputs; the renderer owns GPU resources; simulation owns mesh and
result artifacts; the application layer owns windows and controllers.

**Caching.** Feature output, geometry properties, render meshes, simulation
meshes and analysis results may all be cached, each invalidated by explicit
revisions or dependencies. Never rely on implicit invalidation.

**Testing structure** mirrors subsystem boundaries (`tests/core/`, `geometry/`,
`sketch/`, `features/`, `io/`, `integration/`, `regression/`). Benchmarks live
apart from correctness tests and always retain correctness checks.

## Architectural Invariants

Every milestone must preserve these. Breaking one is an architecture change, not
an implementation detail, and needs a deliberate decision recorded here first.

```text
1.  Document owns canonical engineering state.
2.  Engineering intent is persistent.
3.  Generated B-Reps are derived state.
4.  Stable typed IDs are required.
5.  Units are part of correctness.
6.  OCCT remains behind BetterCAD abstractions.
7.  GUI must not own canonical CAD state.
8.  CLI and GUI use the same core API.
9.  Regeneration is dependency-driven.
10. Regeneration failures are atomic.
11. Dependencies flow downward; module cycles are prohibited.
12. Core depends on no GUI, renderer or kernel implementation type.
13. Simulation does not depend on GUI.
14. AI operates through structured, validated public APIs.
```

**Anti-patterns.** Do not introduce a global mutable document, Qt widgets owning
CAD state, raw OCCT types outside adapters, persistent array-index IDs, unitless
public engineering APIs, silent feature failures, permanent full-document
rebuilds, serialization of internal memory layout, geometry-only native files,
AI editing kernel objects, or simulation tied to the viewport mesh.

**Architecture gate.** Before major feature expansion, verify: core has no GUI
dependency; the geometry backend is wrapped; stable IDs work; units work;
document state is canonical; the dependency graph works; features regenerate;
undo/redo works; persistence preserves intent; geometry validation exists; the
CLI can exercise the core. If any is false, fix the architecture first.

## Future Architecture

Reserved shapes for subsystems that do not exist. None is authorized.

**Assemblies.** An assembly references part documents or internal definitions and
holds component instances, mate constraints, configurations and solved
transforms. A component carries `ComponentId`, source document, configuration,
placement, suppression state and metadata. The mate model stays separate from
the solver, mirroring the sketch solver:
`assembly model → constraint problem → solver → transforms`.

**Drawings.** A drawing references the model rather than copying it: sheets,
views, dimensions, annotations, BOM. A view references a document revision, a
body or component selection, a camera projection, a scale and a section
definition.

**Threading.** Avoid concurrency until measurement justifies it. When added: a UI
thread issuing document commands and scheduling tasks, worker tasks for
tessellation, geometry queries, meshing, simulation and import/export, with
document mutation carefully synchronized. Prefer immutable generated results —
evaluating a definition yields a new shape rather than mutating a shared one,
which simplifies undo, caching, thread safety and regression testing.

**Architectural decision records.** Major choices belong in `docs/adr/`
(not yet created) recording context, decision, alternatives and consequences.
Until then that reasoning lives in the milestone evidence under
[docs/verification/](docs/verification/).

**Success criterion.** The architecture succeeds if BetterCAD can grow from a
simple parametric block to large mechanical assemblies with linked drawings,
simulation, optimization, automation and AI workflows **without replacing the
fundamental document model**.

## Project Documents

| Document | Purpose |
| --- | --- |
| [README.md](README.md) | Project overview and getting started |
| [ROADMAP.md](ROADMAP.md) | Long-term capability direction and completed capabilities |
| [TODO.md](TODO.md) | Authoritative implementation status and next work |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Technical architecture and invariants |
| [CLAUDE.md](CLAUDE.md) | Engineering workflow for coding agents |
| [docs/architecture.md](docs/architecture.md) | The architecture as built today |
| [docs/verification/](docs/verification/) | Proof of completion |
