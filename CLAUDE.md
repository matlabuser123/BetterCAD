# BetterCAD — Claude Engineering Rules

> **Do not confuse implementation with completion.**

BetterCAD is a verification-driven mechanical CAD/CAE platform. This file is the
engineering constitution for coding agents working in this repository.

## Read First

Before touching anything:

1. [TODO.md](TODO.md) — what is complete, what is next, what is authorized
2. **this file** — how the work must be done
3. [ARCHITECTURE.md](ARCHITECTURE.md) — the structure the work must preserve
4. [ROADMAP.md](ROADMAP.md) — why the capability exists and what it leads to
5. `docs/verification/<milestone>/` — the relevant evidence
6. [docs/engineering/](docs/engineering/CHANGE_WORKFLOW.md) — the templates the
   lifecycle is worked through

The workflow is a way of working, not a dependency: BetterCAD builds, tests
and qualifies with no tool beyond the ones its presets already name. Any
agent tooling a developer installs is theirs, and optional.

## Source of Truth

| Question | Authority |
| --- | --- |
| What is BetterCAD? | [README.md](README.md) |
| What are we building long-term? | [ROADMAP.md](ROADMAP.md) |
| What is complete, and what is next? | [TODO.md](TODO.md) |
| How must the system be structured? | [ARCHITECTURE.md](ARCHITECTURE.md) |
| How must the work be executed? | this file, worked through [docs/engineering/](docs/engineering/CHANGE_WORKFLOW.md) |
| What proves completion? | [docs/verification/](docs/verification/) |

When documents conflict, the earlier entry wins:

```text
verification evidence → TODO → architecture → roadmap → README
```

Two rules follow, and neither is negotiable.

**ROADMAP does not authorize implementation. TODO.md authorizes implementation.**
Being listed in the roadmap, being next in the dependency order, or being easy
are all irrelevant. **If TODO says "Awaiting explicit scope decision", STOP** and
say so.

**Documentation follows evidence.** Never edit `ROADMAP.md`, `TODO.md` or
`ARCHITECTURE.md` to make an implementation appear complete. Documentation work
never ticks a checkbox, advances a status, or turns Planned into Qualified. When
a document and the evidence disagree, the evidence is right.

## Working Order

- Work only on the authorized milestone. Do not jump ahead because a later task
  looks easier or more interesting.
- On a failed gate: **stop → diagnose → fix → re-run → continue only on PASS.**
  Never continue past a failure, and never relax a gate to obtain one.
- Inspect before changing: repository, `TODO.md`, architecture, existing tests,
  recent related implementation. Do not rewrite working subsystems blindly.
  Prefer minimal, justified changes.
- Do not combine a large refactor with unrelated feature work. Refactor, verify,
  then implement, then verify.
- Do not overbuild. Build the smallest correct subsystem, validate, stabilize,
  then expand. A strong architecture beats line count.

## Definition of Done

```text
[x] = implementation
    + tests
    + independent validation where applicable
    + adversarial review
    + deterministic regression
    + evidence recorded
```

A milestone that carries a qualification gate also needs a **qualified final
tree**: the tree that was qualified must be the tree that is committed.

Never mark `[x]` because code was written, a file exists, something compiles, a
placeholder was added, a test was skipped or mocked, or a result looks correct.
Nor because tests mostly pass, the implementation looks right, or the kernel
accepted the operation: a kernel that returns a shape has not said the shape is
correct.

**Never invent** test results, benchmark numbers, compiler output, geometry
validation, solver convergence, coverage, CI results or release artifacts. If
something cannot be tested in the current environment, report it as `BLOCKED` or
`UNVERIFIED` and explain exactly why.

Do not call something implemented when it is a placeholder, stub, mock,
hardcoded demo, `NotImplemented` path or single-case hack. Those are acceptable
only as scaffolding, explicitly labelled.

## Architecture Rules

Full detail in [ARCHITECTURE.md](ARCHITECTURE.md). The rules that bind every
change:

- **The Document owns canonical engineering state.** Generated B-Reps are
  derived; feature definitions are persistent.
- **Open CASCADE stays behind BetterCAD abstractions** — OCCT headers only in an
  `occt/` adapter directory under `src/`. Qt only in `apps/bettercad/` and
  `src/renderer/`.
- **Layering:** a module includes public headers of its own module or a lower
  layer only. `core` 0, `sketch` 1, `features` 2, `io` 3,
  `renderer`/`scripting` 4. Public headers in `include/bettercad/<module>/`,
  private headers beside their sources.
- **Stable typed IDs** for persistent identity — never indices, kernel handles or
  addresses.
- **Units are part of correctness.** Engineering APIs take `Length`, `Angle`,
  `Pressure`; values are SI internally, converted only at boundaries.
- **Regeneration is dependency-driven and transactional**, and failures are
  atomic: a failed feature commits nothing and leaves the document unchanged.
- **Errors are structured**, never silently swallowed. A diagnostic says what
  failed, where, why and which object caused it.
- **The GUI owns no CAD state**; the CLI uses the same public API as everything
  else.
- **Semantic topology preparation:** never permanently assume `Face 7 today =
  Face 7 after regeneration`. Retain provenance where possible.

The `architecture.layering` test enforces containment and layering, and fails the
build. Do not work around it.

## Engineering Lifecycle

Substantial changes follow this order. Compilation is not completion;
implementation is not completion; and for a geometric or numerical claim,
tests alone are not sufficient.

```text
UNDERSTAND -> ARCHITECT -> BLAST RADIUS -> IMPLEMENT -> TARGETED TESTS
    -> INDEPENDENT VALIDATION -> ADVERSARIAL REVIEW -> FULL REGRESSION
    -> QUALIFICATION -> EVIDENCE -> [x] -> COMMIT / PUSH
```

Small, local changes do not need every phase. Anything that crosses a module
boundary, touches shared infrastructure, or makes a numerical claim does.
Templates: [docs/engineering/](docs/engineering/CHANGE_WORKFLOW.md).

**The lifecycle governs how work is done, never what work is authorized.**
Scope still comes from `TODO.md` alone: investigating a subsystem, designing
an architecture or reviewing a diff grants no permission to build anything
that is not authorized there.

### 1. Understand

Before changing a subsystem, trace how it works now. Read the public API, its
callers, the data it owns, its IDs and units, how it reaches the dependency
graph, regeneration, serialization, commands and undo, the CLI, its tests and
its verification evidence.

Answer, for yourself, before writing code: what owns this behaviour; what is
its public API; what invariants does it hold; what depends on it; what
persisted format depends on it; what tests define its current behaviour; and
what previously qualified behaviour must not move. Never start from a
filename and a guess.

### 2. Architect

For a change that crosses an architectural boundary, decide before
implementing: the responsible module, the public interface, who owns the
data, which way the dependencies point, the persistent representation, stable
identity, unit semantics, the error model, regeneration and undo behaviour,
and how it will be tested and validated. Check it against
[ARCHITECTURE.md](ARCHITECTURE.md).

Reject a design that introduces GUI-owned canonical state, OCCT outside its
adapters, a second parameter system or document model, a unitless engineering
API, identity by array index, hidden global state, behaviour that exists only
for tests, or a back door for AI. Prefer extending an abstraction that is
already there.

For an architecturally significant decision, put up **two or three serious
candidates** and compare them on correctness, architecture fit, complexity,
persistence, determinism, performance, extensibility, failure modes,
migration cost and testability. Record the choice, and what was rejected, as
an ADR in [docs/architecture/decisions/](docs/architecture/decisions/). Do not
do this for ordinary feature work, and do not generate ADR noise.

### 3. Blast radius

Before modifying shared infrastructure, work out what could break, and let
that — not proximity to the edited file — decide the regression set. A change
to a face selector reaches sketch-on-face, datums, holes, patterns, sweeps,
lofts, persistence and the reference models.

Record the direct callers, the indirect dependents, and the impact on
serialization, regeneration, stable references, the CLI, the tests and the
reference models.

### 4. Implement

The smallest correct change that fixes the root problem. A milestone is not
permission to refactor the repository: no unrelated cleanup, no speculative
abstraction, no public API change without a reason. If a larger architectural
change really is needed, say why before making it.

1. Confirm the milestone is authorized in `TODO.md`.
2. Implement the smallest correct version.
3. Write tests: nominal, parameter-change, failure, serialization, regeneration.
4. Validate against an independent reference where one exists.
5. Run targeted tests, then the affected subsystem, then the full suite.
6. Record evidence.
7. Update `TODO.md` only on PASS.
8. Commit.

Parametric features must store their definition, not their result, and must
regenerate deterministically from parameters and upstream geometry. Do not
implement a feature that mutates geometry without retaining its definition.

### 5. Adversarial review

Before calling a substantial milestone complete, try to disprove it. Read the
final diff and ask: what did we assume; what case is missing; could this pass
its tests and still be geometrically wrong; are the expected values really
independent; did we weaken a test or move a tolerance; is there hidden global
state; can save/load or undo/redo change the result; can a parameter change
leave stale geometry; can a stable reference bind to the wrong face; could
Debug and Release differ; could order of operations matter; can a failure
leave partial state committed; did we cross an architectural boundary or
widen the scope.

This is a gate, not a formality. A credible defect found here is resolved
before `[x]`. If nothing is found, record that.
Template: [docs/engineering/ADVERSARIAL_REVIEW.md](docs/engineering/ADVERSARIAL_REVIEW.md).

### 6. Qualification freeze

Freeze the source and test tree and record its identity before qualifying.
Run the presets the milestone requires (Debug, Release, Debug-shared for
production milestones), then check that the qualified tree is the committed
tree. If source or tests change after the freeze, the qualification is void
and is run again from clean. Documentation that cannot affect the executable
or the tests may follow the project's existing policy.

## Parallel work

Where several agents are available, give them independent concerns —
architecture investigation, independent validation, test-gap analysis,
profiling, adversarial review — and keep one authoritative implementation
path. Do not set several agents editing the same subsystem at once; reviewers
challenge the implementation, they do not race it.

## Testing and Validation

- Layers: unit, integration, regression, validation, smoke, performance.
- **Tests use production APIs.** Do not test through private back doors.
- **Validate against independent references**, not against the code itself.
  Analytic where possible: box `V = LWH`, cylinder `V = πr²h`,
  sphere `V = 4/3 πr³`, Pappus for sweeps and revolutions, Green's theorem for
  cross-sections.
- Geometry checks mean more than "no exception": shape validity, solid closure,
  finite positive volume, expected body count, bounding box, topology counts.
- A bug fix is incomplete without a regression test, unless impossible.
- **Tolerances need a reason.** `1e-12` for well-conditioned double-precision
  algebra, `1e-9` for geometric accumulation, larger only where kernel behaviour
  or conditioning justifies it and the reason is documented. Never loosen a
  tolerance to make a test pass.
- **Determinism.** No dependence on unordered iteration, wall-clock timing,
  unfixed random seeds, thread scheduling, temporary path order or locale. Record
  any seed that is required.
- **Names describe behaviour:** `ExtrudeRectangle_UpdatesVolumeWhenDepthChanges`,
  `DependencyGraph_RejectsCycle`, `Document_SaveLoad_PreservesStableIds` — never
  `Test1` or `BasicTest`.
- Sketch solving must distinguish under-constrained, fully constrained,
  over-constrained, inconsistent and solver failure, and report conflicts,
  redundancy, unresolved DOF and residuals. Never collapse failure to a boolean.
- Persistence tests run the real round trip: create → save → destroy → load →
  regenerate → compare. Never compare file size or object count alone.
- Reference models are permanent. Do not delete one because it reveals a
  regression; fix the regression.

## Bug Workflow

```text
reproduce
→ minimal deterministic reproducer
→ root cause
→ failing regression test
→ minimal fix
→ targeted tests
→ affected subsystem tests
→ full relevant regression
→ evidence
```

Do not patch symptoms unless explicitly justified. Update `TODO.md` and docs only
if appropriate.

## Performance Workflow

```text
baseline → profile → identify bottleneck → one targeted change
→ re-run correctness tests → benchmark → compare
```

Never optimize before measuring. Never claim a speedup without measured
before/after evidence. Never trade correctness for a benchmark. A benchmark that
produces the wrong answer is not a performance result.

Benchmarks record commit, compiler, build configuration, CPU, input size,
runtime, memory where practical, and result correctness.

## Evidence

One directory per milestone under `docs/verification/`, named for the milestone
(`P11-FEAT-009/`, `P11-QUAL-001/`). Each holds a `README.md` stating what was
run, what was measured and the result, plus the raw logs it cites.

Every `[x]` links to its evidence. A milestone directory with an unfilled
placeholder, or a claim with no log behind it, is a failed gate.

An evidence directory should answer, for the sections that apply to the
change: TASK, SCOPE, BASELINE, ARCHITECTURE, BLAST RADIUS, IMPLEMENTATION,
TESTS, INDEPENDENT VALIDATION, ADVERSARIAL REVIEW, FAILURE PATHS,
PERSISTENCE, DETERMINISM, REGRESSION, PERFORMANCE, KNOWN LIMITATIONS, RESULT,
REVISION. Not every task needs every section; never invent one.

Acceptance gate template for substantial tasks:

```text
TASK:            <milestone ID>
IMPLEMENTATION:  <what changed>
TESTS:           <tests executed>
VALIDATION:      <independent / analytical check>
RESULT:          PASS / FAIL / BLOCKED
EVIDENCE:        <paths / logs / commands>
TODO:            updated only if PASS
```

Do not commit large generated files without reason.

## TODO Updates

- `[ ]` while in progress, however much code exists.
- `[x]` only after tests and validation pass and evidence is recorded.
- Blocked items say so with the reason.
- Completed milestones move to [ROADMAP.md](ROADMAP.md) with their evidence
  links; `TODO.md` stays a list of work still to do.
- Never allocate a milestone ID to unauthorized work.

## Git Rules

- Commit when a milestone passes its gates, not partway through one.
- One milestone per commit where practical; production code and its evidence
  land together.
- The message says what was done and verified. A message claiming verification
  the evidence does not contain is the same failure as a false checkbox.
- Update `TODO.md` in the same commit, only on PASS.
- **Never force push.** Never rewrite published history or tags.
- Create a release tag only when asked.

## Stop Conditions

Stop, report and wait when any of these is true:

```text
TODO.md records no open milestone
    → the next milestone is a scope decision, not Claude's to make

a required gate fails
    → diagnose and fix; never continue past it, never relax it

the change would break an architectural invariant
    → the invariant wins, or the architecture changes deliberately first

a claim cannot be verified in this environment
    → report BLOCKED or UNVERIFIED with the exact reason

the task needs evidence that does not exist
    → produce it, or report it missing; never infer it

completing the task needs work outside the authorized milestone
    → finish what is authorized, and say what was left out and why
```

Finishing a milestone is itself a stop condition. Do not start the next one
because it is obvious, small, or next in the roadmap.

Treat plugins, scripts, macros, external files and AI-generated commands as
trust boundaries. Never execute code embedded in a document without explicit
design and sandboxing.

## Final Report

At the end of every implementation session:

```text
STATUS:        PASS / PARTIAL / BLOCKED
COMPLETED:     ...
FILES CHANGED: ...
TESTS:         ...
VALIDATION:    ...
FAILED/BLOCKED:...
TODO.md:       exact checkboxes changed
NEXT:          single next highest-priority task
```

Do not claim background work. Do not say work will continue later. The
repository state at the end of the session must match the report.

**When choosing between more features and better correctness, diagnostics,
tests, architecture and reproducibility, choose the second.** BetterCAD should
become trustworthy first and impressive second.

## Project Documents

| Document | Purpose |
| --- | --- |
| [README.md](README.md) | Project overview and getting started |
| [ROADMAP.md](ROADMAP.md) | Long-term capability direction and completed capabilities |
| [TODO.md](TODO.md) | Authoritative implementation status and next work |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Technical architecture and invariants |
| [CLAUDE.md](CLAUDE.md) | Engineering workflow for coding agents |
| [docs/engineering/](docs/engineering/CHANGE_WORKFLOW.md) | Working templates for the lifecycle: change, architecture review, adversarial review, validation |
| [docs/architecture/decisions/](docs/architecture/decisions/) | ADRs: architecturally significant decisions and what they rejected |
| [docs/verification/](docs/verification/) | Proof of completion |
