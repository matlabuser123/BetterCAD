# BetterCAD — CLAUDE.md

This file defines how Claude Code must work inside the BetterCAD repository.

BetterCAD is a verification-driven mechanical CAD/CAE platform. The project is intended to grow from a robust parametric CAD core into assemblies, drawings, simulation, optimization, automation, engineering version control, and AI-assisted engineering.

The most important rule is:

> Do not confuse implementation with completion.

A milestone is complete only when there is:

```text
implementation
+ tests
+ independent validation where applicable
+ deterministic regression
+ evidence
```

---

# 0. Read Before Working

Read these, in this order, before touching anything:

```text
1. TODO.md                  what is complete, what is next, what is authorized
2. CLAUDE.md                this file — how the work must be done
3. ARCHITECTURE.md          the structure the work must preserve
4. ROADMAP.md               why this capability exists and what it leads to
5. the relevant evidence    docs/verification/<milestone>/
```

## 0.1 Which document decides what

| Question | Authority |
| --- | --- |
| What is BetterCAD, and how do I build it? | [README.md](README.md) |
| What are we building long-term? | [ROADMAP.md](ROADMAP.md) |
| What is actually complete? | [TODO.md](TODO.md) + [docs/verification/](docs/verification/) |
| What should be implemented next? | [TODO.md](TODO.md) |
| How must the system be structured? | [ARCHITECTURE.md](ARCHITECTURE.md) |
| How must the work be executed? | **this file** |
| What proves completion? | [docs/verification/](docs/verification/) |

Two rules follow from that table, and they are not negotiable.

**Authorization comes only from `TODO.md`.**

> If `TODO.md` says a capability is not authorized, do not begin it merely
> because `ROADMAP.md` lists it next.

Being listed in `ROADMAP.md`, being described in `ARCHITECTURE.md`, being the
obvious next step, or being easy are all irrelevant. A capability is authorized
when `TODO.md` records it as an open milestone with an ID and acceptance gates,
and not before. If the next milestone is not recorded, the correct action is to
stop and say so.

**Documentation follows evidence.**

> Never change `ROADMAP.md`, `TODO.md` or `ARCHITECTURE.md` to make an
> implementation appear complete.

Documentation work cannot produce engineering completion. Editing a document
never ticks a checkbox, never advances a status, and never turns `PLANNED` into
`COMPLETE`. When a document and the evidence disagree, the evidence is right
and the document is corrected to match it — never the other way round.

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

The parametric CAD foundation — `P0`–`P11` — is complete and qualified. That
does not promote anything else; see `TODO.md` for the current position, which
is what this section defers to rather than restates.

Priority is therefore standing rather than sequential:

```text
1. Fix a failed gate or a regression in what already works.
2. Complete the open milestone in TODO.md, if one is open.
3. Otherwise: stop, and ask for the next scope decision.
```

Do not start major work on:

```text
assemblies
drawings
semantic topology
GUI development
FEA
CFD
thermal
optimization
AI
CAM
cloud collaboration
```

until `TODO.md` records it as an authorized milestone. The roadmap "reaching a
phase" is not authorization; only `TODO.md` authorizes.

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

Keep subsystem boundaries clear, and put new code where the architecture says
it goes. `ARCHITECTURE.md` owns the target layout and the dependency rules;
`docs/architecture.md` records the modules and targets that exist today. Do not
invent a third layout here.

The rules that matter while writing code:

```text
public headers    include/bettercad/<module>/, included as <bettercad/...>
private headers   next to their sources in src/<module>/
kernel code       only in an occt/ adapter directory under src/
Qt code           only in apps/bettercad/ and src/renderer/
```

A module may include the public headers of its own module or of a lower layer,
and nothing else. Layers: `core` 0, `sketch` 1, `features` 2, `io` 3,
`renderer`/`scripting` 4.

Avoid circular dependencies between modules. The `architecture.layering` test
enforces all of the above and fails the build; do not work around it.

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

The models that exist live in `examples/models/reference/`, are built by
`examples/reference_models/` through the public API alone, and are validated
against geometry computed independently from their parameters. `TODO.md` and
`ROADMAP.md` record which exist and which are planned; do not restate the list
here.

Rules that apply to every reference model:

```text
built only through the public API
validated against independently computed geometry
regenerated deterministically, across processes and build configurations
saved files kept byte-identical to what the builders produce
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

Each document answers one question, and updating one is not updating another:

```text
README.md          What is BetterCAD, and what can it actually do today?
ROADMAP.md         Where are we going?
TODO.md            What is complete, and what exactly is next?
ARCHITECTURE.md    How must the system be structured?
CLAUDE.md          How must Claude work?
docs/architecture.md   How is it structured today, as built?
docs/verification/     What proves a milestone is complete?
```

Rules:

* Do not duplicate volatile implementation details across documents. Put a
  fact in the document that owns it and link to it from the others.
* Status belongs to `TODO.md` and the evidence. `ROADMAP.md` carries
  capability-level status only; `ARCHITECTURE.md` and `CLAUDE.md` carry none.
* Test counts, qualification results and milestone history belong to
  `TODO.md` and `docs/verification/`. `README.md` may carry a short verified
  summary. Nothing else scatters raw numbers.
* Do not write documentation in the direction that flatters the code. See
  §0.1: documentation follows evidence.

Documentation-only changes are reported as documentation work. They never
change a checkbox, a status or a milestone.

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

For significant tasks, preserve evidence.

Evidence lives in one place, one directory per milestone, named for the
milestone:

```text
docs/verification/
├── P0-001/
├── P1-001/
├── ...
├── P11-FEAT-009/
├── P11-REF-001/
└── P11-QUAL-001/
```

Each directory holds a `README.md` stating what was run, what was measured and
the result, plus the raw logs it cites. Every `[x]` in `TODO.md` links to one
of these. A milestone directory with an unfilled placeholder, or a claim with
no log behind it, is a failed gate.

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

The objective is not:

> Build something bigger than SolidWorks.

The objective is:

> Build a small, extremely reliable parametric CAD system.

The first important complete workflow was:

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

**That workflow works and is qualified**, for ten features rather than extrude
alone, on six realistic mechanical parts, in three build configurations. See
`TODO.md` for the evidence.

Meeting it changes nothing about how the next capability is built. Success is
now measured the same way, one milestone at a time:

```text
the new capability works on realistic input, not a demo case
its failures are refused with a diagnostic, atomically
everything that worked before still works, unchanged
the result is deterministic across processes and configurations
the evidence says so, and says where it cannot speak
```

A capability that cannot meet those five is not finished, however complete it
looks.

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

# 41. Stop Conditions

Stop, report, and wait rather than continuing, when any of these is true:

```text
TODO.md records no open milestone
  → the next milestone is a scope decision, which is not Claude's to make

a required gate fails
  → diagnose and fix; never continue past it and never relax it to pass

the work would break an architectural invariant (ARCHITECTURE.md §1.1)
  → the invariant wins, or the architecture is changed deliberately first

a claim cannot be verified in this environment
  → report BLOCKED or UNVERIFIED, with the exact reason

the task requires evidence that does not exist
  → produce the evidence, or report that it is missing; never infer it

completing the task would need work outside the authorized milestone
  → finish what is authorized, and say exactly what was left out and why
```

Finishing a milestone is also a stop condition. Do not start the next one
because it is obvious, small or next in `ROADMAP.md`.

## 41.1 Commits

Commit when a milestone passes its gates, not partway through one.

```text
one milestone per commit where practical
message says what was done and verified
production code and its evidence land together
TODO.md updated in the same commit, only if PASS
never force push
```

A commit whose message claims verification that the evidence does not contain
is the same failure as a false checkbox.

---

# 42. Final Rule

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

---

# Project Documents

* [README.md](README.md) — project overview and getting started
* [ROADMAP.md](ROADMAP.md) — long-term capability direction
* [TODO.md](TODO.md) — authoritative implementation status and next work
* [ARCHITECTURE.md](ARCHITECTURE.md) — system architecture and dependency rules
* [CLAUDE.md](CLAUDE.md) — engineering workflow and verification rules (this document)
* [docs/architecture.md](docs/architecture.md) — the architecture as built today
* [docs/verification/](docs/verification/) — evidence for every completed milestone
