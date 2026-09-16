# BetterCAD — Architecture

BetterCAD is a modular, headless-first, verification-driven mechanical CAD/CAE platform.

**What this document is for.** It defines how BetterCAD must be *structured*:
its layers, module boundaries, allowed and forbidden dependency directions, and
the invariants every future milestone has to preserve. It describes technical
boundaries, never schedule. What gets built, and when, is decided elsewhere:

| Question | Authority |
| --- | --- |
| What is BetterCAD, and how do I build it? | [README.md](README.md) |
| Where is BetterCAD going? | [ROADMAP.md](ROADMAP.md) |
| What is actually complete, and what is next? | [TODO.md](TODO.md) |
| How must the system be structured? | **this document** |
| How must the work be executed and verified? | [CLAUDE.md](CLAUDE.md) |
| What proves a milestone is complete? | [docs/verification/](docs/verification/) |

**Intended architecture versus as-built.** This file is the *target*
architecture, and much of it is still ahead of the code. For the architecture
**as implemented today** — the real build targets, namespaces, layering checks
and module contracts — see [docs/architecture.md](docs/architecture.md), which
is kept in step with the source and is enforced by the `architecture.layering`
test. Where the two differ, `docs/architecture.md` describes what exists and
this document describes what it must be able to grow into without a rewrite.
Neither file records project status.

The architecture must support long-term growth into:

```text
Parametric CAD
Assemblies
Drawings
Meshing
FEA
Thermal
CFD
Optimization
Python automation
Version control
AI-assisted engineering
Manufacturing
```

without forcing major rewrites of the core data model.

---

# 1. Architectural Goals

BetterCAD should be:

* modular;
* deterministic;
* headless-first;
* testable;
* scriptable;
* simulation-ready;
* resilient to topology changes;
* suitable for large assemblies;
* compatible with external CAD formats;
* extensible without coupling every subsystem together.

The architecture should strongly separate:

```text
engineering state
geometry
visualization
UI
simulation
persistence
external integrations
```

---

## 1.1 Architectural Invariants

These are the rules every milestone must preserve. They are the shortest
statement of this document; the rest of it explains and elaborates them. A
change that breaks one of these is an architecture change, not an
implementation detail, and needs an explicit decision — see
[CLAUDE.md](CLAUDE.md).

```text
1.  The Document owns canonical engineering state.
2.  Generated B-Reps are derived state, never the model itself.
3.  Feature definitions are persistent engineering intent.
4.  Open CASCADE lives behind BetterCAD-owned abstractions.
5.  The GUI does not own CAD state.
6.  The CLI uses the same public/core API as every other client.
7.  Simulation must not depend on the GUI.
8.  AI operates only through validated public APIs.
9.  Stable typed IDs identify engineering objects; indices and addresses do not.
10. Units are part of correctness, not a display concern.
11. Regeneration is dependency-driven and transactional.
12. Failures are explicit, structured and atomic.
13. Dependencies flow downward only; module cycles are prohibited.
14. The core depends on no GUI, no renderer and no kernel implementation type.
```

Invariants 1–4, 6 and 9–12 are exercised by the shipped system today; 5 is
held by the layering check even though the desktop application is still a
placeholder; 7 and 8 constrain subsystems that do not exist yet. See
[TODO.md](TODO.md) for what is implemented and
[docs/verification/](docs/verification/) for the evidence.

---

# 2. Top-Level Architecture

```text
Applications
├── Desktop            Qt, placeholder shell today
├── CLI                bettercad-cli
├── Python  [future]
└── AI      [future]
       │
       ▼
Public BetterCAD API
       │
       ▼
Document / Commands / Transactions
       │
       ├── Parameters
       ├── Sketches
       ├── Features
       ├── Bodies
       ├── Assemblies  [future]
       └── Simulation  [future]
       │
       ▼
Dependency / Regeneration Engine
       │
       ▼
Geometry Services
       │
       ▼
Geometry Backend Adapter
       │
       ▼
OCCT
```

The same stack, drawn by subsystem rather than by layer:

```text
┌──────────────────────────────────────────────────────┐
│                    Applications                      │
│                                                      │
│ Desktop GUI     CLI     Python     Tests     Agent   │
└───────────────────────┬──────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────┐
│               BetterCAD Public API                   │
└───────────────────────┬──────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────┐
│                  Document Model                      │
│                                                      │
│ Parameters  Sketches  Features  Bodies  Metadata     │
└───────────────────────┬──────────────────────────────┘
                        │
          ┌─────────────┼─────────────┐
          ▼             ▼             ▼
┌────────────────┐ ┌────────────┐ ┌────────────────┐
│ Parametric     │ │ Geometry   │ │ Persistence    │
│ Engine         │ │ Services   │ │                │
└───────┬────────┘ └──────┬─────┘ └────────────────┘
        │                 │
        ▼                 ▼
┌────────────────┐ ┌──────────────────────────┐
│ Dependency     │ │ Geometry Backend         │
│ Graph          │ │ Open CASCADE initially   │
└────────────────┘ └──────────────────────────┘
```

Later:

```text
              Document Model
                    │
    ┌───────────────┼──────────────────┐
    ▼               ▼                  ▼
Assembly        Simulation        Manufacturing
    │               │                  │
    ▼               ▼                  ▼
Motion          Mesh/FEA/CFD          CAM
```

Nothing marked `[future]` exists. The architecture reserves its place; it does
not claim it.

---

# 3. Fundamental Rule

The GUI must never become the architecture.

The canonical engineering state belongs in the core.

Bad:

```text
Qt Widget
   │
   ├── stores parameters
   ├── stores geometry
   ├── modifies features
   └── runs calculations
```

Correct:

```text
Qt Widget
   │
   ▼
Command / API
   │
   ▼
Document
   │
   ▼
Parametric Engine
```

The same model must be usable from:

```text
GUI
CLI
tests
Python
batch processing
future AI agent
```

---

# 4. Repository Layout

This is the **target** long-term layout. Directories appear here because the
architecture reserves a place for them, not because they exist: `benchmarks/`,
`results/`, `docs/adr/`, `src/assembly/`, `src/drawing/`, `src/simulation/` and
`src/versioning/` are not in the repository yet, and `src/renderer/` and
`src/scripting/` exist only as empty placeholders. For the directories that
hold code today see
*Repository structure* in [README.md](README.md); for the modules that exist
today see [docs/architecture.md](docs/architecture.md).

```text
BetterCAD/
├── apps/
│   ├── bettercad/
│   └── bettercad_cli/
│
├── include/
│   └── bettercad/
│       ├── core/
│       ├── geometry/
│       ├── sketch/
│       ├── features/
│       ├── assembly/
│       ├── simulation/
│       └── io/
│
├── src/
│   ├── core/
│   │   ├── document/
│   │   ├── ids/
│   │   ├── parameters/
│   │   ├── units/
│   │   ├── commands/
│   │   ├── dependency/
│   │   └── diagnostics/
│   │
│   ├── geometry/
│   │   ├── api/
│   │   ├── topology/
│   │   ├── queries/
│   │   └── occt/
│   │
│   ├── sketch/
│   │   ├── model/
│   │   ├── entities/
│   │   ├── constraints/
│   │   ├── solver/
│   │   └── profile/
│   │
│   ├── features/
│   │   ├── extrude/
│   │   ├── revolve/
│   │   ├── fillet/
│   │   └── ...
│   │
│   ├── assembly/
│   ├── drawing/
│   ├── io/
│   ├── renderer/
│   ├── simulation/
│   ├── scripting/
│   └── versioning/
│
├── tests/
├── benchmarks/
├── examples/
├── docs/
├── data/
├── results/
├── cmake/
│
├── CMakeLists.txt
├── README.md
├── ROADMAP.md
├── TODO.md
├── CLAUDE.md
└── ARCHITECTURE.md
```

---

# 5. Module Dependency Direction

Dependencies should generally flow downward.

```text
apps
 ↓
public_api
 ↓
document
 ↓
parametric_engine
 ↓
sketch / features
 ↓
geometry
 ↓
geometry_backend
```

Simulation:

```text
apps
 ↓
simulation_api
 ↓
simulation
 ↓
mesh
 ↓
numerics
```

Renderer:

```text
apps
 ↓
renderer
 ↓
display_geometry
```

The renderer must not own engineering geometry.

---

# 6. Forbidden Dependency Directions

Avoid:

```text
geometry → GUI
core → Qt
document → renderer
geometry → Python
simulation → GUI
OCCT → entire application
```

Prefer:

```text
GUI → core
renderer → read-only model representation
Python → public API
simulation → public model/query interfaces
```

---

# 7. Core Module

The `core` module defines platform-wide concepts that should remain independent of CAD-specific third-party libraries.

Suggested components:

```text
core/
├── ids/
├── units/
├── parameters/
├── document/
├── commands/
├── dependency/
├── diagnostics/
├── transactions/
└── serialization primitives/
```

The core must not depend on:

```text
Qt
OpenGL
Vulkan
Open CASCADE implementation details
CUDA
Python
```

---

# 8. Strong IDs

Persistent objects should have strongly typed stable identifiers.

Conceptually:

```cpp
struct DocumentId;
struct ObjectId;
struct ParameterId;
struct SketchId;
struct EntityId;
struct ConstraintId;
struct FeatureId;
struct BodyId;
struct FaceId;
struct EdgeId;
struct VertexId;
```

Avoid APIs such as:

```cpp
int featureId;
int faceIndex;
```

Preferred:

```cpp
FeatureId featureId;
FaceId faceId;
```

Benefits:

* prevents accidental ID mixing;
* improves serialization;
* supports semantic references;
* enables stable document history.

---

# 9. Engineering Units

The unit system belongs in the core.

Example:

```cpp
Length width = 100_mm;
Length height = 50_mm;
Angle angle = 45_deg;
Pressure pressure = 2.5_MPa;
```

Internally, quantities should have a canonical representation.

Preferred base:

```text
SI
```

with conversion only at boundaries.

Do not use display units as storage units.

---

# 10. Document Model

The `Document` is the canonical engineering container.

Conceptually:

```text
Document
├── identity
├── metadata
├── parameters
├── sketches
├── features
├── bodies
├── dependencies
├── material assignments
├── configurations
└── future analysis definitions
```

Possible interface:

```cpp
class Document {
public:
    DocumentId id() const;

    ParameterId addParameter(...);
    SketchId addSketch(...);
    FeatureId addFeature(...);

    Object* find(ObjectId);
    const Object* find(ObjectId) const;

    Revision revision() const;
    bool dirty() const;
};
```

The document owns persistent engineering definitions.

---

# 11. Document Object Model

Persistent objects should share a minimal common abstraction.

Conceptually:

```cpp
class DocumentObject {
public:
    ObjectId id() const;
    std::string_view name() const;

    ObjectState state() const;

    virtual ObjectType type() const = 0;
};
```

Possible object states:

```text
Clean
Dirty
Evaluating
Valid
Failed
Suppressed
MissingReference
```

Avoid oversized inheritance hierarchies.

Composition is preferred where possible.

---

# 12. Parameters

Parameters are first-class document objects.

Example:

```text
Parameter
├── ID
├── name
├── physical dimension
├── value
├── display unit
├── optional expression
└── revision
```

Conceptually:

```cpp
Parameter<Length> width;
Parameter<Angle> draftAngle;
```

Long-term expression support:

```text
housing_width = bearing_OD + 2 * wall_thickness
```

Expressions should form dependencies in the same graph used by features.

---

# 13. Parametric Dependency Graph

Dependencies must be explicit.

Example:

```text
width
   ↓
Sketch001
   ↓
Extrude001
   ↓
Fillet001
```

The dependency engine is responsible for:

```text
adding edges
removing edges
cycle detection
dirty propagation
evaluation ordering
failed dependency propagation
```

Use a directed graph.

A valid regeneration order requires a topological sort.

---

# 14. Regeneration Architecture

A document edit should not directly rebuild arbitrary downstream geometry.

Workflow:

```text
parameter change
      ↓
transaction
      ↓
mark source dirty
      ↓
dependency graph propagation
      ↓
determine evaluation order
      ↓
evaluate dirty nodes
      ↓
commit valid outputs
```

Prefer transactional regeneration.

Do not partially overwrite good model state before the new state is known to be valid.

---

# 15. Feature Evaluation Contract

Every parametric feature should follow a common contract.

Conceptually:

```cpp
struct FeatureEvaluationContext {
    const Document& document;
    const GeometryService& geometry;
};

struct FeatureEvaluationResult {
    Status status;
    std::vector<Body> bodies;
    Diagnostics diagnostics;
};

class Feature {
public:
    virtual FeatureEvaluationResult
    evaluate(const FeatureEvaluationContext&) const = 0;
};
```

Important:

```text
feature definition
≠
feature output geometry
```

The feature definition is persistent.

Its generated shape is derived state.

---

# 16. Derived Geometry

Generated B-Reps should be treated as cached outputs.

Conceptually:

```text
ExtrudeFeature
├── definition
│   ├── profile
│   ├── depth
│   └── operation
│
└── generated result
    └── Body
```

This distinction enables:

* regeneration;
* save/load;
* undo/redo;
* configuration changes;
* future history-free caching.

---

# 17. Geometry Abstraction

BetterCAD must define its own geometry-facing interfaces.

Do not expose `TopoDS_Shape` throughout the codebase.

Instead:

```cpp
class Shape;
class Solid;
class Face;
class Edge;
class Vertex;
```

or opaque handles.

Example:

```cpp
class GeometryService {
public:
    Solid makeBox(Length x, Length y, Length z);

    BooleanResult fuse(
        const Solid& a,
        const Solid& b);

    MassProperties properties(
        const Solid& solid);
};
```

OCCT implementation:

```text
GeometryService
      ↓
OcctGeometryService
      ↓
Open CASCADE
```

---

# 18. Why Wrap Open CASCADE

Benefits:

* keeps third-party APIs localized;
* simplifies future upgrades;
* allows mocks/test doubles;
* enables alternative geometry backends;
* reduces compile-time coupling;
* improves error translation;
* supports BetterCAD-specific semantics.

---

# 19. Geometry Representation Layers

Distinguish three representations.

## 19.1 Exact model geometry

```text
B-Rep
curves
surfaces
topology
```

Used for CAD.

## 19.2 Tessellated display geometry

```text
triangles
normals
edges
selection IDs
```

Used for rendering.

## 19.3 Simulation mesh

```text
nodes
elements
boundary regions
```

Used for CAE.

Never treat these as interchangeable.

---

# 20. Topology Model

Topology should conceptually distinguish:

```text
Body
Solid
Shell
Face
Wire
Edge
Vertex
```

BetterCAD IDs should not be identical to raw kernel topology indices.

Raw kernel topology is transient.

BetterCAD topology identity should eventually incorporate provenance and semantic matching.

---

# 21. Semantic Topology Preparation

The full persistent-naming system may come later, but the architecture must support it now.

A topological reference should eventually contain more than:

```text
Face #7
```

Possible structure:

```text
TopologyReference
├── source feature
├── generated entity class
├── geometric signature
├── adjacency signature
├── orientation
├── semantic role
└── fallback kernel identity
```

Example semantic intent:

```text
largest planar +Z face
generated by Extrude001
adjacent to Hole002
```

This allows future recovery after model changes.

---

# 22. Sketch Architecture

A sketch contains:

```text
Sketch
├── placement
├── entities
├── constraints
├── dimensions
└── solver state
```

The sketch should not directly own B-Rep solids.

It produces profiles that features consume.

---

# 23. Sketch Entities

Suggested model:

```cpp
using SketchEntity =
    std::variant<
        SketchPoint,
        SketchLine,
        SketchCircle,
        SketchArc,
        SketchEllipse,
        SketchSpline
    >;
```

Each entity has:

```text
EntityId
construction state
geometry parameters
metadata
```

Avoid requiring inheritance unless it provides clear value.

---

# 24. Sketch Constraints

Constraints should be represented separately.

Example:

```text
Constraint
├── ConstraintId
├── type
├── entity references
├── optional parameter
├── enabled
└── diagnostic state
```

Constraint types:

```text
Coincident
Horizontal
Vertical
Parallel
Perpendicular
Tangent
Equal
Concentric
Distance
Angle
Radius
Diameter
Fixed
Symmetric
Midpoint
```

---

# 25. Sketch Solver Boundary

The sketch model must not depend tightly on one solver implementation.

Preferred:

```text
SketchModel
    ↓
ConstraintProblemBuilder
    ↓
SketchSolver interface
    ↓
Solver implementation
```

Example:

```cpp
class ISketchSolver {
public:
    virtual SketchSolveResult solve(
        const SketchProblem&) = 0;
};
```

This makes numerical experimentation possible without rewriting the sketch model.

---

# 26. Solver Result

Do not return only `bool`.

Use:

```text
SketchSolveResult
├── status
├── solution
├── residual norm
├── iteration count
├── degrees of freedom
├── conflicting constraints
└── diagnostics
```

Possible statuses:

```text
UnderConstrained
FullyConstrained
OverConstrained
Inconsistent
ConvergedWithWarnings
Failed
```

---

# 27. Profile Extraction

Features such as Extrude should consume explicit sketch profiles.

Pipeline:

```text
Sketch geometry
      ↓
intersection/connectivity analysis
      ↓
wire construction
      ↓
closed profile detection
      ↓
Profile objects
```

Do not make Extrude responsible for understanding arbitrary raw sketch entities.

---

# 28. Feature Architecture

Feature modules should remain isolated.

Examples:

```text
features/
├── extrude/
├── revolve/
├── hole/
├── fillet/
├── chamfer/
├── pattern/
├── mirror/
├── sweep/
└── loft/
```

Every feature should depend on generic geometry services, not directly on OCCT.

---

# 29. Extrude Example

Definition:

```text
ExtrudeFeature
├── FeatureId
├── profile reference
├── distance
├── direction
├── operation
└── target body reference
```

Evaluation:

```text
resolve profile
↓
create planar face
↓
extrude
↓
perform Boolean if needed
↓
validate shape
↓
build topology provenance
↓
return result
```

---

# 30. Feature Operations

Features may operate as:

```text
NewBody
Add
Remove
Intersect
```

Do not encode Boolean behavior separately in every feature implementation.

Use shared body-combination infrastructure.

---

# 31. Body Model

A Body is a persistent logical model entity.

It may point to generated exact geometry.

Conceptually:

```text
Body
├── BodyId
├── name
├── feature history
├── material
├── generated shape
├── visibility
└── state
```

Multi-body parts should be possible later without redesigning the document model.

---

# 32. Commands and Transactions

All user-visible modifications should pass through commands or transactions.

Example:

```text
GUI action
↓
ModifyParameterCommand
↓
DocumentTransaction
↓
regeneration
↓
commit
```

Command interface:

```cpp
class Command {
public:
    virtual Result execute(Document&) = 0;
    virtual Result undo(Document&) = 0;
};
```

For complex changes, snapshot/delta-based transaction systems may eventually replace simple command reversal.

---

# 33. Undo / Redo Architecture

Undo history should operate on engineering state, not just UI state.

Store:

```text
command
or
document delta
```

Never implement undo by trying to reverse arbitrary kernel side effects manually.

---

# 34. Diagnostics Architecture

Use structured diagnostics everywhere.

Conceptually:

```cpp
struct Diagnostic {
    DiagnosticCode code;
    Severity severity;
    std::string message;
    std::optional<ObjectId> object;
};
```

Severity:

```text
Info
Warning
Error
Fatal
```

Examples:

```text
MissingReference
DependencyCycle
SketchOverConstrained
BooleanFailed
InvalidSolid
SerializationMismatch
```

---

# 35. Error Philosophy

Expected engineering failure is not necessarily a C++ exception.

Examples:

```text
fillet radius too large
profile not closed
Boolean fails
missing model reference
over-constrained sketch
```

Return structured results.

Use exceptions for exceptional programming/system conditions.

---

# 36. Persistence Architecture

Persistence should serialize engineering definitions, not just geometry.

Suggested layers:

```text
Document
   ↓
Serialization DTO
   ↓
Format writer
   ↓
.bcad
```

Avoid serializing internal C++ object layouts directly.

Use explicit schemas.

---

# 37. Native File Format

Potential structure:

```text
model.bcad
```

internally containing:

```text
manifest.json
document.json
parameters.json
sketches.json
features.json
topology.json
metadata.json
geometry-cache.bin
preview.png
```

A ZIP-like container may eventually be useful.

Initially a simpler representation is acceptable.

---

# 38. Schema Versioning

Every native document should contain:

```text
file format version
application version
schema version
```

Example:

```json
{
  "format": "BetterCAD",
  "schema_version": 3
}
```

Never assume file schemas will remain unchanged forever.

Migration support must be possible.

---

# 39. File Import / Export

External formats should live behind adapters.

```text
io/
├── step/
├── stl/
├── dxf/
├── iges/
└── obj/
```

Import pipeline:

```text
external file
↓
format adapter
↓
geometry/document translation
↓
BetterCAD model
```

Export pipeline:

```text
BetterCAD model
↓
translation
↓
external format
```

---

# 40. Renderer Architecture

Rendering must consume derived display data.

Pipeline:

```text
Exact Geometry
      ↓
Tessellation
      ↓
Render Mesh
      ↓
GPU Upload
      ↓
Renderer
```

Do not render directly from document internals.

The renderer should support:

```text
triangles
wireframe
edges
selection highlighting
transparency
section planes
annotations
```

---

# 41. Render Cache

Maintain a render cache keyed by model revision.

Example:

```text
BodyId
+
GeometryRevision
→
RenderMesh
```

When geometry changes, only affected render meshes should be rebuilt.

---

# 42. Selection Architecture

Selection should map viewport primitives back to semantic model objects.

Example:

```text
GPU selection ID
↓
Render entity
↓
Topology reference
↓
FaceId / EdgeId / BodyId
```

Do not expose raw triangle indices as user-level selections.

---

# 43. GUI Architecture

Use an MVC/MVVM-like separation.

Conceptually:

```text
Qt View
   ↓
Controller / ViewModel
   ↓
Commands
   ↓
Core API
```

Avoid:

```text
QPushButton callback
    ↓
direct OCCT mutation
```

---

# 44. Application Services

Useful application-level services may include:

```text
DocumentManager
SelectionManager
CommandManager
RecentFilesService
ExportService
WorkspaceService
```

These belong above the core model.

They should orchestrate, not own engineering truth.

---

# 45. CLI Architecture

The CLI should call the same public API.

Example:

```text
bettercad-cli validate model.bcad
```

becomes:

```text
load Document
↓
DocumentValidator
↓
RegenerationEngine
↓
GeometryValidator
↓
report
```

This makes CLI validation useful for CI.

---

# 46. Validation Service

Create a central validation subsystem.

Conceptually:

```cpp
class DocumentValidator {
public:
    ValidationReport validate(
        const Document&) const;
};
```

Checks may include:

```text
duplicate IDs
missing references
dependency cycles
invalid parameter dimensions
failed features
invalid bodies
invalid sketches
serialization inconsistencies
```

---

# 47. Simulation Architecture

Simulation must consume the same canonical engineering model.

Long term:

```text
Document
   ↓
SimulationDefinition
   ↓
GeometryRegionMapping
   ↓
Mesh
   ↓
Physics Model
   ↓
Solver
   ↓
Results
```

Simulation definitions should reference semantic geometry regions.

Example:

```text
PressureBC
  face = TopologyReference(...)
  value = 2 MPa
```

not raw mesh triangle IDs.

---

# 48. Mesh Architecture

Simulation mesh is a separate model.

```text
Mesh
├── nodes
├── elements
├── regions
├── boundary sets
├── quality metrics
└── source geometry references
```

Maintain mapping:

```text
CAD face
↔
mesh boundary region
```

This is necessary for CAD-linked analysis.

---

# 49. Solver Separation

Do not embed physics solvers into GUI or CAD feature code.

Preferred:

```text
simulation/
├── common/
├── mesh/
├── structural/
├── thermal/
├── cfd/
└── optimization/
```

Numerical infrastructure may be shared where appropriate.

---

# 50. Results Architecture

Simulation results should be immutable artifacts tied to model state.

Example:

```text
SimulationResult
├── SimulationId
├── source DocumentRevision
├── solver configuration
├── mesh revision
├── result fields
└── validation metadata
```

If the model changes, stale results should be clearly marked.

---

# 51. Assembly Architecture

An assembly should reference part documents or internal definitions.

Conceptually:

```text
Assembly
├── Component instances
├── Mate constraints
├── configurations
└── solved transforms
```

Component:

```text
Component
├── ComponentId
├── source document
├── configuration
├── placement
├── suppression state
└── metadata
```

---

# 52. Assembly Solver

Separate the mate model from the solver.

```text
Assembly Model
    ↓
Constraint Problem
    ↓
Assembly Solver
    ↓
Transforms
```

This mirrors the sketch solver architecture.

---

# 53. Drawing Architecture

Drawings should reference the model, not copy it.

```text
Drawing
├── Sheet
├── Views
├── Dimensions
├── Annotations
└── BOM
```

A drawing view references:

```text
Document revision
Body/component selection
Camera projection
Scale
Section definition
```

---

# 54. Python Architecture

Python must bind to stable public APIs.

Use:

```text
pybind11
```

Conceptually:

```text
Python
  ↓
BetterCAD public API
  ↓
core
```

Not:

```text
Python
  ↓
random internal classes
```

Example:

```python
doc = cad.new_document()

sketch = doc.create_sketch("XY")
sketch.rectangle("100 mm", "50 mm")

feature = doc.extrude(
    sketch,
    distance="20 mm"
)
```

---

# 55. Plugin Architecture

Long-term plugin model:

```text
Plugin
├── manifest
├── declared API version
├── capabilities
├── commands
└── optional UI integration
```

Plugins should communicate through stable extension interfaces.

Do not permit arbitrary access to every private internal class.

---

# 56. AI Architecture

AI must operate above the public API.

```text
User request
    ↓
LLM
    ↓
Structured engineering plan
    ↓
Tool/API calls
    ↓
Document transaction
    ↓
Regeneration
    ↓
Validation
    ↓
Result
```

The AI should never directly fabricate kernel-internal B-Reps.

---

# 57. AI Tool Layer

Example structured tools:

```text
create_document
create_sketch
add_line
add_circle
add_constraint
set_dimension
extrude
revolve
add_hole
assign_material
run_validation
run_simulation
```

All operations should be:

```text
typed
validated
undoable
auditable
```

---

# 58. Versioning Architecture

Future engineering versioning should operate on semantic document state.

Model commits may contain:

```text
document revision
parameter changes
feature changes
sketch changes
material changes
metadata
```

A semantic diff should report:

```text
Parameter width:
100 mm → 120 mm

Hole003 diameter:
8 mm → 10 mm

Feature Fillet002:
suppressed → enabled
```

---

# 59. Configuration Architecture

Part configurations should not duplicate entire documents.

Prefer overrides.

Example:

```text
Base Part
├── Default
├── Long
│   └── length = 150 mm
└── Short
    └── length = 80 mm
```

Configuration-aware regeneration should reuse the same model graph.

---

# 60. Threading Architecture

Avoid introducing concurrency prematurely.

When added:

```text
UI thread
│
├── document commands
│
└── task scheduling
        ↓
worker tasks
```

Potential worker operations:

```text
tessellation
geometry queries
meshing
simulation
import/export
```

Document mutation should remain carefully synchronized.

---

# 61. Immutability Strategy

Prefer immutable generated results where practical.

Example:

```text
Feature definition
↓
evaluate
↓
new ShapeResult
```

rather than mutating a shared shape in place.

This simplifies:

```text
undo
caching
thread safety
regression testing
versioning
```

---

# 62. Caching Strategy

Possible caches:

```text
feature output cache
geometry-property cache
render mesh cache
mesh cache
analysis result cache
```

Each cache must be invalidated by explicit revisions/dependencies.

Never rely on fragile implicit cache invalidation.

---

# 63. Revision Model

Objects should support revision tracking.

Conceptually:

```text
DocumentRevision
ObjectRevision
GeometryRevision
MeshRevision
```

Example:

```text
BodyId = constant
GeometryRevision = increments when regenerated
```

This helps caching and stale-result detection.

---

# 64. Public API Philosophy

The public API should expose engineering concepts.

Good:

```cpp
doc.createSketch(...)
doc.createExtrude(...)
doc.setParameter(...)
```

Avoid exposing kernel-specific details unnecessarily:

```cpp
doc.addTopoDSShape(...)
```

---

# 65. Internal API Philosophy

Internals should be allowed to evolve.

Use internal namespaces:

```cpp
bettercad::detail
bettercad::internal
```

Do not make internal utility classes public by default.

---

# 66. Ownership Model

Use explicit ownership.

Recommended:

```text
Document owns persistent model definitions.

Feature outputs are owned by document-derived state/cache.

Renderer owns GPU resources.

Simulation owns mesh/result artifacts.

Application layer owns windows/controllers.
```

Use:

```cpp
std::unique_ptr
```

for single ownership.

Use:

```cpp
std::shared_ptr
```

only when genuine shared lifetime is required.

---

# 67. Lifetime Safety

Avoid holding raw references to model objects across document mutations unless guaranteed stable.

Prefer stable IDs:

```text
FeatureId
BodyId
SketchId
```

then resolve them through the document when needed.

This avoids dangling references after undo/redo or regeneration.

---

# 68. Testing Architecture

Tests should mirror subsystem boundaries.

```text
tests/
├── core/
├── geometry/
├── sketch/
├── features/
├── document/
├── persistence/
├── integration/
├── regression/
└── validation/
```

---

# 69. Integration Tests

Important workflows should be tested end-to-end.

Example:

```text
create document
↓
parameter width = 100 mm
↓
rectangle sketch
↓
extrude 20 mm
↓
verify volume
↓
change width to 120 mm
↓
regenerate
↓
verify volume changed
↓
save
↓
load
↓
verify same result
```

This is more valuable than testing isolated classes alone.

---

# 70. Reference Models

Store permanent reference models:

```text
data/reference/
├── simple_block/
├── shaft/
├── bearing_housing/
├── bracket/
└── ...
```

Reference models should exercise real user workflows.

---

# 71. Benchmark Architecture

Benchmarks are separate from correctness tests.

```text
benchmarks/
├── sketch_solver/
├── regeneration/
├── tessellation/
├── geometry_boolean/
├── assembly/
└── simulation/
```

Every benchmark must retain correctness checks.

---

# 72. Build Targets

Recommended target structure:

```text
bettercad_core
bettercad_geometry
bettercad_sketch
bettercad_features
bettercad_io
bettercad_renderer
bettercad_simulation

bettercad
bettercad-cli

bettercad_tests
```

Avoid one massive library if module boundaries can remain clean.

---

# 73. CMake Dependency Example

Conceptually:

```text
bettercad_core
     ▲
     │
bettercad_geometry
     ▲
     │
bettercad_sketch
     ▲
     │
bettercad_features
     ▲
     │
bettercad_app
```

Actual dependencies may differ, but cycles are prohibited.

---

# 74. External Dependencies

Potential dependencies:

```text
Open CASCADE    geometry kernel
Eigen           linear algebra
Qt 6            desktop UI
pybind11        Python bindings
Gmsh            meshing
Catch2/GTest    testing
```

Each external dependency must be isolated behind BetterCAD interfaces where practical.

---

# 75. Dependency Injection

Core services that may vary should be injected.

Example:

```cpp
class FeatureEvaluator {
public:
    FeatureEvaluator(
        GeometryService& geometry,
        DiagnosticSink& diagnostics);
};
```

This improves testing and makes alternative implementations possible.

---

# 76. Logging Architecture

Use structured categories:

```text
core
document
dependency
geometry
sketch
solver
feature
io
renderer
simulation
performance
```

Avoid using logs as a substitute for proper diagnostic return values.

---

# 77. Performance Architecture

Performance improvements should preserve modularity.

Possible future optimization areas:

```text
incremental regeneration
parallel feature evaluation
geometry cache
BVH acceleration
GPU tessellation
GPU rendering
large-assembly culling
solver sparsity
mesh partitioning
```

Do not introduce GPU dependencies into the core document model.

---

# 78. GPU Boundary

GPU work belongs in explicit subsystems.

```text
renderer/
gpu/

simulation/
gpu/
```

CPU reference paths should exist where practical for validation.

---

# 79. Security Boundary

Treat these as untrusted inputs:

```text
external CAD files
native documents
plugins
Python scripts
macros
AI-generated commands
network data
```

Parsing should validate:

```text
sizes
references
types
schema
version
resource limits
```

---

# 80. Future Cloud Boundary

Cloud support should be optional.

Architecture:

```text
Desktop BetterCAD
     │
     ├── local CAD
     ├── local rendering
     ├── local files
     │
     └── optional remote services
             ├── simulation
             ├── collaboration
             └── storage
```

Do not make core CAD dependent on constant internet connectivity.

---

# 81. Early Implementation Slice

The first architecture slice should prove:

```text
Document
  ↓
Parameter
  ↓
Sketch
  ↓
Constraint
  ↓
Extrude
  ↓
Body
  ↓
Save/Load
```

GUI is secondary.

---

# 82. First Vertical Milestone

A successful early end-to-end model:

```text
new document

width = 100 mm
height = 50 mm
depth = 20 mm

create XY sketch
create rectangle

constrain:
width = width parameter
height = height parameter

extrude:
depth = depth parameter

validate:
volume = 100000 mm³

change:
width = 120 mm

regenerate

validate:
volume = 120000 mm³

undo

validate:
volume = 100000 mm³

redo

save
close
load

validate again

export STEP
export STL
```

This vertical slice should guide early architecture decisions.

---

# 83. Architecture Gate

Before major feature expansion, verify:

```text
core has no GUI dependency
geometry backend is wrapped
stable IDs work
units work
document state is canonical
dependency graph works
features regenerate
undo/redo works
persistence preserves intent
geometry validation exists
CLI can exercise the core
```

If these are not true, fix the architecture before expanding feature count.

---

# 84. Anti-Patterns

Do not introduce:

```text
global mutable document
Qt widgets owning CAD state
raw OCCT types everywhere
persistent array-index IDs
unitless public engineering APIs
silent feature failures
full-document rebuild for every action forever
serialization of internal memory layout
geometry-only native files
AI directly editing kernel objects
simulation tied to viewport mesh
```

---

# 85. Architectural Decision Records

For major architectural choices, add (`docs/adr/` does not exist yet; the
reasoning it would hold currently lives in the milestone evidence under
[docs/verification/](docs/verification/)):

```text
docs/adr/
```

Example:

```text
ADR-001-open-cascade.md
ADR-002-unit-system.md
ADR-003-document-ownership.md
ADR-004-dependency-graph.md
ADR-005-native-file-format.md
```

Each ADR should record:

```text
context
decision
alternatives
consequences
```

This prevents important architectural reasoning from being lost.

---

# 86. Definition of Architectural Success

The architecture succeeds if BetterCAD can evolve from:

```text
simple parametric block
```

to:

```text
large mechanical assembly
+
linked drawings
+
simulation
+
optimization
+
automation
+
AI engineering workflows
```

without replacing the fundamental document model.

The intended long-term hierarchy is:

```text
Applications
    ↓
Public API
    ↓
Engineering Document
    ↓
Parametric / Assembly / Simulation Models
    ↓
Domain Services
    ↓
Geometry / Numerics / Persistence Backends
```

---

# 87. Architecture vs Roadmap

Four documents govern the project, and they answer different questions. Keeping
them separate is what stops any one of them from quietly becoming the others.

```text
ARCHITECTURE.md   defines allowed structure.
ROADMAP.md        defines intended direction.
TODO.md           authorizes implementation.
CLAUDE.md         defines execution rules.
```

Consequences worth stating plainly:

* **This document never schedules anything.** It says how a subsystem must be
  shaped if it is built, not when it is built and not that it will be. A
  section here on assemblies, simulation or AI is a reserved shape, not a
  commitment and not a claim of existence.
* **Appearing in this document is not authorization.** Only
  [TODO.md](TODO.md) authorizes work, and only its evidence links mark it
  complete.
* **This document does not record status.** No test counts, no milestone
  history, no qualification results. Those belong to [TODO.md](TODO.md) and
  [docs/verification/](docs/verification/).
* **Architecture outranks convenience.** When a milestone's easiest
  implementation would break an invariant in §1.1, the invariant wins or the
  architecture is changed deliberately and written down here first.

The order in which the architecture was built up — core IDs and units,
parameters, document, commands, dependency graph, geometry abstraction, OCCT
backend, sketch model, constraints, solver, extrude, regeneration, save/load,
CLI validation — is recorded in [TODO.md](TODO.md) with its evidence. Each of
those layers exists because the one below it was verified first, and future
subsystems are expected to arrive the same way.

---

# 88. Final Architectural Principle

BetterCAD should treat engineering intent as the source of truth.

```text
Parameters
+
Constraints
+
Features
+
Dependencies
+
References
=
Engineering Model
```

Geometry, render meshes, drawings, simulation meshes, and analysis results are derived representations of that engineering model.

That distinction is the foundation of the entire BetterCAD architecture.

---

# Project Documents

* [README.md](README.md) — project overview and getting started
* [ROADMAP.md](ROADMAP.md) — long-term capability direction
* [TODO.md](TODO.md) — authoritative implementation status and next work
* [ARCHITECTURE.md](ARCHITECTURE.md) — system architecture and dependency rules (this document)
* [CLAUDE.md](CLAUDE.md) — engineering workflow and verification rules
* [docs/architecture.md](docs/architecture.md) — the architecture as built today
* [docs/verification/](docs/verification/) — evidence for completed milestones
