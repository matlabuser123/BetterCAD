# BetterCAD — CLAUDE.md

This file defines how Claude Code must work inside the BetterCAD repository.

BetterCAD is a verification-driven mechanical CAD/CAE platform. The project is intended to grow from a robust parametric CAD core into assemblies, drawings, simulation, optimization, automation, engineering version control, and AI-assisted engineering.

The most important rule is:

> Do not confuse implementation with completion.

A task is complete only when it is implemented, tested, validated, and supported by real evidence.

---

# 1. Core Working Rules

## 1.1 Work top-to-bottom

Follow `TODO.md` in order unless explicitly instructed otherwise.

Do not jump ahead because a later task appears easier or more interesting.

If the current task has a failed gate:

```text
STOP
↓
diagnose
↓
fix
↓
re-run validation
↓
continue only after PASS
```

---

## 1.2 Checkbox meaning

In `TODO.md`:

```text
[ ] = not complete
[x] = implemented AND verified
```

Never mark `[x]` because:

```text
code was written
a file exists
a function compiles
a placeholder was added
a test was skipped
a test was mocked
the expected result "looks correct"
```

Mark `[x]` only when there is real evidence.

---

## 1.3 Never fake evidence

Do not invent:

```text
test results
benchmark numbers
compiler output
geometry validation
screenshots
GUI acceptance results
solver convergence
performance improvements
coverage values
CI results
release artifacts
```

If something cannot be tested in the current environment, report it as:

```text
BLOCKED
```

or:

```text
UNVERIFIED
```

and explain exactly why.

---

## 1.4 Preserve existing good work

Before changing code:

```text
inspect repository
inspect TODO.md
inspect ROADMAP.md
inspect current architecture
inspect tests
inspect recent relevant implementation
```

Do not blindly rewrite working subsystems.

Prefer minimal, well-justified changes.

---

# 2. Project Priority

Current priority is the parametric CAD foundation.

Work roughly in this order:

```text
P0  Repository foundation
P1  Engineering core
P2  Document model
P3  Geometry kernel
P4  Sketch model
P5  Constraint solver
P6  Parametric features
P7  Regeneration engine
P8  Persistence
P9  Minimal desktop CAD workflow
```

Do not start major work on:

```text
assemblies
drawings
FEA
CFD
thermal
optimization
AI
CAM
cloud collaboration
```

until explicitly authorized or the roadmap reaches those phases.

---

# 3. Architecture Principles

## 3.1 Headless core first

The CAD engine must not depend on the GUI.

Preferred architecture:

```text
GUI
CLI
Python API
Tests
AI Agent
   │
   ▼
BetterCAD Public API
   │
   ▼
Document / Parametric Engine
   │
   ├── Sketch
   ├── Features
   ├── Geometry
   ├── Parameters
   └── Dependencies
```

The GUI is a client of the core, not the owner of the engineering state.

---

## 3.2 Keep third-party libraries behind adapters

Do not spread third-party types throughout the codebase.

For example:

```text
BetterCAD Geometry API
        ↓
OCCT adapter
        ↓
Open CASCADE
```

Avoid this:

```text
GUI → OCCT
Sketch solver → OCCT
Document → OCCT
Persistence → OCCT
```

Prefer this:

```text
Everything
    ↓
BetterCAD abstractions
    ↓
Third-party adapter
```

---

## 3.3 Stable identities

Persistent model entities must use stable typed IDs.

Examples:

```text
DocumentId
ObjectId
ParameterId
SketchId
EntityId
ConstraintId
FeatureId
BodyId
FaceId
EdgeId
VertexId
ComponentId
```

Do not use raw array indices as persistent identity.

Do not treat memory addresses as identity.

---

## 3.4 Units are part of correctness

Engineering quantities must preserve dimensions.

Prefer:

```cpp
Length width = 100_mm;
Angle theta = 45_deg;
Pressure pressure = 2.5_MPa;
```

Avoid APIs such as:

```cpp
createBox(100.0, 50.0, 20.0);
```

unless the units are unambiguous and explicitly documented at the boundary.

Do not silently mix:

```text
mm
m
degrees
radians
Pa
MPa
```

---

# 4. Parametric Modeling Rules

## 4.1 Engineering intent is primary

Do not store only final geometry.

The document should preserve:

```text
parameters
sketch entities
constraints
feature definitions
dependencies
body outputs
metadata
```

The generated B-Rep is an output of the model, not the model itself.

---

## 4.2 Features must be regeneratable

Each feature should behave conceptually as:

```text
inputs
+
parameters
+
upstream geometry
↓
feature evaluation
↓
output geometry
```

Changing a parameter must allow deterministic regeneration.

---

## 4.3 No one-shot modeling shortcuts

Do not implement features that mutate geometry without retaining their definitions.

Bad:

```text
take body
apply fillet
replace body forever
```

Preferred:

```text
FilletFeature
├── input body dependency
├── selected references
├── radius
└── generated output body
```

---

# 5. Dependency Graph Rules

Parametric dependencies must be explicit.

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

Implement:

```text
dependency registration
dirty propagation
topological evaluation
cycle detection
failure propagation
```

Do not simply regenerate the entire document unless explicitly used as a temporary reference implementation.

If a parameter changes, regenerate only affected downstream nodes when practical.

---

# 6. Geometry Rules

## 6.1 Geometry validity is mandatory

After solid-modeling operations, validate geometry.

Where available, verify:

```text
shape validity
solid closure
finite properties
non-negative volume
topology consistency
expected body count
```

Do not assume an OCCT operation succeeded merely because it returned an object.

---

## 6.2 Analytical validation where possible

Use independent analytical results whenever possible.

Examples:

Box:

$$
V = LWH
$$

Cylinder:

$$
V = \pi r^2 h
$$

Sphere:

$$
V = \frac{4}{3}\pi r^3
$$

Compare calculated geometry properties against these references.

---

## 6.3 Boolean regression

For Boolean operations, verify more than "no exception."

Where possible test:

```text
expected volume
expected body count
shape validity
bounding box
deterministic topology characteristics
```

---

# 7. Sketch Rules

## 7.1 Separate geometry from constraints

Sketch entities and constraints must be distinct concepts.

Example:

```text
Line entity
+
Horizontal constraint
+
Distance constraint
```

Do not encode constraints implicitly inside geometry objects.

---

## 7.2 Supported solver states

The sketch solver must eventually distinguish at least:

```text
UNDER_CONSTRAINED
FULLY_CONSTRAINED
OVER_CONSTRAINED
INCONSISTENT
SOLVER_FAILURE
```

Do not collapse all failures into a generic boolean.

---

## 7.3 Constraint diagnostics matter

When possible, report:

```text
conflicting constraints
redundant constraints
unresolved degrees of freedom
invalid references
solver residual
iteration count
```

A useful CAD system should explain failure rather than merely reject the sketch.

---

# 8. Persistence Rules

Save/load must preserve engineering intent.

Do not serialize only tessellated geometry.

Native documents must preserve, as applicable:

```text
stable IDs
parameters
units
sketches
entities
constraints
features
dependencies
materials
metadata
document revision
```

A valid round-trip test is:

```text
create
↓
save
↓
destroy in-memory model
↓
load
↓
regenerate
↓
compare
```

Do not compare only file size or object count.

---

# 9. Undo / Redo Rules

Model edits should use commands or transactions.

Avoid GUI code directly mutating core engineering state.

Preferred:

```text
User action
   ↓
Command
   ↓
Document transaction
   ↓
Core mutation
```

Commands should support:

```text
execute
undo
redo
```

Undo/redo tests should compare deterministic document state.

---

# 10. Testing Policy

Every production subsystem must have tests.

Use layers:

```text
unit
integration
regression
validation
smoke
performance
```

---

## 10.1 Unit tests

Use for isolated logic such as:

```text
unit conversion
ID handling
parameter validation
sketch geometry
dependency graph
serialization helpers
```

---

## 10.2 Integration tests

Use for workflows such as:

```text
parameter
→ sketch
→ extrude
→ body
```

---

## 10.3 Regression tests

Keep permanent regression cases for previously fixed bugs.

A bug fix is incomplete without a regression test unless impossible.

---

## 10.4 Analytical validation

For numerical or geometric systems, prefer independent reference values.

Do not validate code only against itself.

---

# 11. Test Naming

Use descriptive names.

Prefer:

```text
ExtrudeRectangle_UpdatesVolumeWhenDepthChanges
DependencyGraph_RejectsCycle
Parameter_RejectsIncompatibleDimension
Document_SaveLoad_PreservesStableIds
```

Avoid:

```text
Test1
BasicTest
Works
CheckStuff
```

---

# 12. Tolerances

Never use arbitrary loose tolerances just to make tests pass.

Every numerical tolerance should have a reason.

For example:

```text
1e-12 for pure double-precision algebra where conditioning permits
1e-9 for geometric calculations with small accumulated error
larger only where justified by kernel behavior or numerical conditioning
```

If a test requires a relaxed tolerance, document why.

---

# 13. Determinism

Prefer deterministic results.

Tests should not depend on:

```text
unordered container iteration
wall-clock timing
random seeds without fixing them
thread scheduling
temporary path order
locale-specific formatting
```

If randomness is required, record the seed.

---

# 14. Performance Work

Do not optimize before measuring.

Required workflow:

```text
baseline
↓
profile
↓
identify bottleneck
↓
make one targeted change
↓
re-run correctness tests
↓
benchmark
↓
compare
```

Never claim speedup without measured before/after evidence.

Never sacrifice correctness for benchmark improvement.

---

# 15. GUI Rules

The GUI should not duplicate core logic.

The GUI may:

```text
collect input
display state
invoke commands
display diagnostics
render geometry
```

The GUI should not own:

```text
feature evaluation
document dependency logic
geometry validity rules
constraint equations
engineering calculations
```

---

# 16. CLI Rules

The CLI is a first-class verification interface.

Prefer commands such as:

```bash
bettercad-cli info model.bcad
bettercad-cli validate model.bcad
bettercad-cli regenerate model.bcad
bettercad-cli export-step model.bcad model.step
bettercad-cli export-stl model.bcad model.stl
```

CLI operations should use the same core APIs as the GUI.

---

# 17. Error Handling

Never silently swallow failures.

Prefer structured status objects.

Example:

```text
FeatureEvaluationResult
├── status
├── message
├── diagnostic code
├── offending input
└── optional recovery information
```

Errors should identify:

```text
what failed
where it failed
why it failed when known
what object caused it
```

---

# 18. Logging

Use useful structured logs.

Include where relevant:

```text
document ID
feature ID
operation
elapsed time
status
diagnostic
```

Do not flood logs with low-value output.

---

# 19. Code Quality

Prefer:

```text
small focused classes
clear ownership
RAII
const correctness
strong types
explicit dependencies
well-defined interfaces
minimal globals
```

Avoid:

```text
god objects
large utility dumping grounds
hidden mutable globals
raw owning pointers
duplicated algorithms
massive switch statements where polymorphism/dispatch fits better
```

---

# 20. C++ Rules

Target modern C++.

Use:

```text
RAII
std::unique_ptr
std::shared_ptr only when shared ownership is truly needed
std::optional
std::variant
std::span
std::filesystem
strong enum classes
move semantics
constexpr where appropriate
```

Avoid unnecessary manual memory management.

---

# 21. Dependency Policy

Before adding a library, answer:

```text
What problem does it solve?
Can the standard library already solve it?
Is it maintained?
Is the license acceptable?
Will it complicate packaging?
Can BetterCAD wrap it behind an interface?
```

Do not add dependencies only to save a few lines of code.

---

# 22. File and Module Organization

Keep subsystem boundaries clear.

Suggested layout:

```text
src/
├── core/
│   ├── document/
│   ├── geometry/
│   ├── parameters/
│   ├── topology/
│   └── units/
├── sketch/
│   ├── entities/
│   ├── constraints/
│   └── solver/
├── features/
├── io/
├── renderer/
├── assembly/
├── simulation/
└── scripting/
```

Avoid circular dependencies between modules.

---

# 23. Public API Stability

Internal implementation can evolve quickly.

Public interfaces should be deliberate.

Before exposing a new public API:

```text
check naming
check ownership
check units
check error model
check stable IDs
check serialization implications
check future extensibility
```

---

# 24. Semantic Topology Preparation

Even before the full semantic-topology system exists, avoid designs that make it impossible later.

Do not permanently assume:

```text
Face 7 today = Face 7 after regeneration
```

Where possible retain provenance:

```text
feature that generated entity
geometric characteristics
adjacency relationships
selection intent
```

---

# 25. Reference Models

Maintain permanent test models.

Recommended progression:

```text
01_simple_block
02_flanged_shaft
03_bearing_housing
04_mounting_bracket
05_pulley
06_motor_mount
07_gearbox_housing
08_four_bar_linkage
09_small_assembly
10_machine_frame
```

Do not delete reference models merely because they reveal regressions.

Fix the regression.

---

# 26. Benchmark Policy

Benchmark files and results should record:

```text
commit
compiler
build configuration
CPU
GPU when relevant
input size
runtime
memory where practical
result correctness
```

A benchmark that produces the wrong answer is not a valid performance result.

---

# 27. Documentation

When implementing a substantial subsystem, update relevant documentation.

At minimum keep synchronized:

```text
README.md
ROADMAP.md
TODO.md
CLAUDE.md
docs/
```

Do not duplicate volatile implementation details unnecessarily.

`ROADMAP.md` answers:

```text
Where are we going?
```

`TODO.md` answers:

```text
What exactly is next?
```

`CLAUDE.md` answers:

```text
How must Claude work?
```

---

# 28. TODO.md Update Policy

Update `TODO.md` as implementation progresses.

Example:

Before:

```markdown
- [ ] P3-002 Primitive solids
```

After implementation but before tests:

```markdown
- [ ] P3-002 Primitive solids
```

After tests and validation pass:

```markdown
- [x] P3-002 Primitive solids
```

Do not use `[x]` prematurely.

If blocked:

```markdown
- [ ] P3-002 Primitive solids — BLOCKED: <reason>
```

where useful.

---

# 29. Evidence

For significant tasks, preserve evidence where practical.

Suggested structure:

```text
results/
├── validation/
├── benchmarks/
├── regression/
└── release/
```

Evidence may include:

```text
test logs
benchmark output
validation comparisons
geometry statistics
release qualification logs
```

Do not commit huge generated files without reason.

---

# 30. Acceptance Gate Template

For substantial tasks, use:

```text
TASK:
<identifier>

IMPLEMENTATION:
<what changed>

TESTS:
<tests executed>

VALIDATION:
<independent/analytical check>

RESULT:
PASS / FAIL / BLOCKED

EVIDENCE:
<paths / logs / commands>

TODO:
updated only if PASS
```

---

# 31. Bug-Fix Workflow

When a bug appears:

```text
1. Reproduce it.
2. Create the smallest deterministic reproducer.
3. Identify root cause.
4. Add a failing regression test.
5. Fix the root cause.
6. Run targeted tests.
7. Run affected subsystem tests.
8. Run broader regression suite.
9. Update TODO/docs only if appropriate.
```

Do not patch symptoms unless explicitly justified.

---

# 32. Refactoring Policy

Do not combine large refactors with unrelated feature work unless necessary.

Prefer:

```text
small refactor
verify
feature implementation
verify
```

This makes regressions easier to isolate.

---

# 33. No Fake Production Features

Do not call something implemented when it is only:

```text
placeholder
stub
mock
hardcoded demo
NotImplemented path
single-case hack
```

These are acceptable as scaffolding only if explicitly labelled as such.

---

# 34. Do Not Overbuild

The long-term roadmap is ambitious.

Do not implement the entire roadmap early.

For each phase:

```text
build smallest correct subsystem
validate
stabilize
then expand
```

The goal is a strong architecture, not maximum line count.

---

# 35. Simulation Rules

When simulation work eventually begins:

```text
verification before optimization
analytical benchmarks before complex cases
mesh convergence before production claims
conservation checks where applicable
residuals must have defined meaning
```

Do not claim engineering accuracy from visually plausible contours.

---

# 36. AI Rules

AI work comes only after reliable structured APIs exist.

AI must not directly mutate undocumented internal geometry.

Preferred flow:

```text
natural language
↓
structured intent
↓
validated engineering plan
↓
public BetterCAD API
↓
model change
↓
regeneration
↓
verification
```

AI-generated changes must remain inspectable and undoable.

---

# 37. Security and Safety

Never execute arbitrary code embedded in a BetterCAD document without explicit design and sandboxing.

Treat:

```text
plugins
scripts
macros
external files
AI-generated commands
```

as trust boundaries.

---

# 38. Release Rules

A release requires more than a successful compile.

A release candidate should verify:

```text
Debug build
Release build
unit tests
integration tests
regression suite
reference models
serialization
CLI smoke tests
GUI smoke tests
import/export
package
installer when available
artifact checksum
```

A failing required gate blocks release.

---

# 39. Current Definition of Success

The immediate objective is not:

> Build something bigger than SolidWorks.

The immediate objective is:

> Build a small, extremely reliable parametric CAD system.

The first important complete workflow is:

```text
create document
↓
create parameters
↓
create constrained sketch
↓
extrude solid
↓
modify parameter
↓
regenerate correctly
↓
undo / redo
↓
save
↓
close
↓
load
↓
regenerate
↓
validate
↓
export STEP/STL
```

Until this works reliably, prioritize foundation work over advanced features.

---

# 40. Session Completion Report

At the end of every implementation session, report concisely:

```text
STATUS:
PASS / PARTIAL / BLOCKED

COMPLETED:
- ...

FILES CHANGED:
- ...

TESTS:
- ...

VALIDATION:
- ...

FAILED / BLOCKED:
- ...

TODO.md:
- exact checkboxes changed

NEXT:
- single next highest-priority task
```

Do not claim background work.

Do not say work will continue later unless the user explicitly starts another session.

The repository state at the end of the current session must accurately reflect the report.

---

# 41. Final Rule

When choosing between:

```text
more features
```

and:

```text
better correctness, diagnostics, tests, architecture, and reproducibility
```

choose the second unless explicitly directed otherwise.

BetterCAD should become trustworthy first and impressive second.
