# P13-MATE-001 — Basic Assembly Constraints

```text
STATUS:          PASS — 1349/1349 on debug, release and debug-shared
                 from clean; qualified tree == committed tree
BASELINE:        31b64f3 (P13-MATE-001 authorization), clean,
                 HEAD == origin/main
SCOPE:           the constraint model. Seven kinds, what they may point at,
                 what they refuse, how they depend, and how they persist.
                 NOTHING MOVES: no solver, no Jacobian, no residual, no
                 component repositioned.
IMPLEMENTATION:  MateId, MateTarget in core, MateType/MateDefinition/Mate in
                 assembly, the document-facing operations, the JSON mapping,
                 and the CLI description
TESTS:           26 new Catch2 cases in 4 files, 2 of them compile-failure
EVIDENCE:        this directory
```

## Scope

```text
P13-MATE-001   define and preserve mate intent
P13-SOLVE-001  numerically solve that intent into transforms
```

The split is the one ADR-005 drew for placement, applied again:

```text
canonical, persisted     derived, computed
--------------------     -----------------
MateDefinition      ->   component transforms
```

Absent on purpose, and checked absent in the file format below: any residual,
Jacobian, iteration count, convergence state or solved transform. No
component is moved by any code in this milestone.

## ADR contract

[ADR-004](../../architecture/decisions/ADR-004-mates-reference-semantic-geometry-only.md)
decided what a mate may point at *before* any mate code existed, recording
that it did so "precisely so that `P13-MATE-001` does not have to discover
the constraint after the code exists". It is therefore the contract this
milestone implements rather than one it negotiates.

| Contract | Where it is honoured |
| --- | --- |
| A mate may reference `PlaneReference`, `AxisReference` or `FaceName` | `MateTarget`'s three alternatives |
| It may **not** reference a `FaceSignature` | there is no field it could occupy — see below |
| "not an allowed reference kind" and "does not resolve" stay distinct | `InvalidArgument` from `validate`/`checkMate`; `NotFound` from resolution |
| A mate's `dependencies()` includes the objects its references name | `referencedObjects(MateTarget)`, measured against a real datum |
| A hole's bore is not mateable; use a datum axis | `Concentric` relates two axes and nothing else |
| The mate reference kinds live in `core` (ADR-006) | `core/document/MateReference.hpp` |
| The constraint system lives in `assembly` (ADR-006) | `assembly/Mate.hpp`, `assembly/Mates.hpp` |

**The `FaceSignature` prohibition is structural, not merely validated.** A
`MateTarget` holds an optional plane, axis or face name, and nothing else. A
signature has nowhere to live, so the rule cannot be forgotten by a future
validation path — it is enforced by the type. The word appears once in the
whole mate implementation, in a comment explaining why.

## Implementation

```text
include/bettercad/core/Id.hpp                      MateIdTag, MateId, widening
include/bettercad/core/document/MateReference.hpp  MateTarget, its kinds,
src/core/document/MateReference.cpp                validation, dependencies
include/bettercad/assembly/Mate.hpp                MateType, MateDefinition,
src/assembly/Mate.cpp                              Mate, and all validation
include/bettercad/assembly/Mates.hpp               create / find / enumerate /
src/assembly/Mates.cpp                             edit / remove / report
src/io/json/MateJson.cpp                           the JSON mapping
apps/bettercad_cli/DocumentCommands.cpp            the info description
```

1085 lines in seven new files, and 104 added to eleven existing ones, with
**no line deleted anywhere** -- the model is additive, so nothing about
components, placements or references changed shape to accommodate it.

## Mate identity

`MateId` is a strong typed ID from the document's one allocator, and is not
interchangeable with the IDs it relates. Enforced at compile time by two new
cases in the existing compile-failure suite:

```text
compile_fail.ids.object-to-mate-id        ObjectId -/-> MateId
compile_fail.ids.mate-id-as-component-id  MateId  -/-> ComponentId
```

The second is the one that matters: a mate *relates* components, so a model
that let a `MateId` pass as a `ComponentId` would let a mate constrain
itself.

## Mate model

The shape is deliberately not uniform, because the constraints are not.
`Fixed` holds one **component**; the other six relate two pieces of
**geometry**:

```text
MateDefinition
├── type
├── component      Fixed only
├── a, b           every kind but Fixed
├── distance       Distance only
├── angle          Angle only
└── suppressed
```

Giving `Fixed` a geometry target it ignores would create a field that means
nothing, which is what validation exists to prevent.
`CoordinateSystemDefinition` already carries the same by-kind asymmetry, so
there is precedent in the codebase rather than a new idea.

Validation checks the fields **both ways**: the ones the kind calls for are
required, and the ones it does not are refused. A `Parallel` mate carrying a
distance is rejected as firmly as a `Distance` mate without one.

`Fixed` also settles a question left open two milestones ago. `P13-XFORM-001`
deferred a `grounded` flag on components as meaningless without a solver.
It arrives here instead as a mate, uniform with the rest, so the flag was
never needed.

## The seven kinds

| Kind | Relates | Value | Meaning |
| --- | --- | --- | --- |
| `Fixed` | one component | — | held where it is; the datum others solve against |
| `Coincident` | plane↔plane or axis↔axis | — | in the same plane, or on the same line |
| `Concentric` | axis↔axis **only** | — | on the same line, said of a shaft in a bore |
| `Parallel` | plane↔plane or axis↔axis | — | directions parallel, either sense |
| `Perpendicular` | plane↔plane or axis↔axis | — | directions at a right angle |
| `Distance` | plane↔plane or axis↔axis | `Length` | a given distance apart |
| `Angle` | plane↔plane or axis↔axis | `Angle` | directions at a given angle |

`Concentric` is deliberately narrower than `Coincident` on the same geometry.
It says *why*: a shaft in a bore, not two faces that happen to meet. ADR-004
makes that the only expressible form anyway, since a hole's bore is not
nameable and concentricity must be stated against a published datum axis.

## Target reference rules

A target is geometry **on a component**. Two components of one part are
different targets even when they name the same geometry of that part, which
is the whole reason a mate needs an instance rather than a bare reference.

| Refused | Because |
| --- | --- |
| a target with no component | it names geometry nowhere |
| a target naming two pieces of geometry | exactly one, and the one its `kind` says |
| a `kind` that disagrees with the geometry carried | a stale reference of another kind cannot ride along |
| a face target with no feature | a face is named by what generates it |
| plane↔axis pairs | see below |
| the same target twice | a constraint saying nothing |
| two targets on one component | a component is rigid; this can only over-constrain |
| a component the document does not have | `NotFound` |
| an object that is not a component | `NotFound` |
| a face of **another component's part** | `InvalidArgument` — a modelling mistake |

**Plane↔axis pairs are refused rather than guessed at.** "A plane parallel to
an axis" and "a plane whose *normal* is parallel to an axis" are opposite
statements, and nothing in the model says which was meant. Like with like
only; a later milestone may add mixed pairs with an explicit convention.

The last row is the check a definition cannot make for itself. A face is
named by the feature that generated it, so a face target on component A whose
feature belongs to component B's part is refused — walking the part's
dependencies to find whether that feature is one of the features the part is
built from, since a part is usually several.

## Units and dimensions

| Check | Expected | Measured |
| --- | --- | --- |
| a distance is a `Length` | an `Angle` cannot be passed | compile-time |
| an angle is an `Angle` | a `Length` cannot be passed | compile-time |
| a kind that takes no value | refused | "takes no distance" / "takes no angle" |
| a kind that takes one | required | "must have a distance" / "must have an angle" |
| NaN, +Inf, -Inf | refused for both kinds | "must be finite" |

The dimension is a type, not a runtime tag, so the two ways of getting it
wrong are not expressible rather than merely rejected.

### The conventions, stated rather than left to the solver

A convention left implicit is a defect waiting for `P13-SOLVE-001`.

- A target's **direction** is a plane's or a face's normal, or an axis'
  direction. `Parallel`, `Perpendicular` and `Angle` are about those.
- **`Angle` is the unsigned angle between the two directions, in
  `[0, 180]` degrees, and a value outside that is refused, never
  normalised.** Silently wrapping 190 degrees to 170 would be the model
  deciding what the engineer meant. Measured: 0, 90 and 180 accepted; 190,
  360 and -1 refused.
- **`Distance` between planes is signed**, along the first target's normal,
  so a plane on the other side is a negative distance — accepted and tested.
  **Between axes it is a perpendicular separation, which has no side**, so a
  negative value is refused.

## Unresolved targets

A mate whose target is deleted becomes unresolved and stays that mate.

| Expected | Measured |
| --- | --- |
| the mate is reported unresolved, naming what is gone | one entry, the missing ID |
| the dependency graph records the same absence | `missing` non-empty |
| regeneration no longer succeeds | it does not |
| the mate still names what it named | unchanged |
| adding a replacement component | does **not** capture it; still unresolved |

Unresolved is computed from whether the objects exist, not stored as a flag,
so a file cannot claim a mate is resolved when it is not.

## Dependencies

A mate declares edges on the components it relates **and** on every object
its targets' geometry names, which is what ADR-004 requires so that a mate
rebuilds when the datum it uses moves.

| Expected | Measured |
| --- | --- |
| a fixed mate | exactly one edge, its component |
| a face target | the component and the part feature |
| a datum target | the component and the datum |
| the graph agrees | the mate is a dependent of each |
| order | the order the mate names them, de-duplicated first-seen |

## Failure atomicity

| Expected | Measured |
| --- | --- |
| three refused creations | no object, no ID consumed, no revision change |
| the next valid creation | takes the very next ID |
| a refused edit | the mate keeps its definition, revision unchanged |
| setting the same definition | no change reported, no revision bump |

## Persistence

Every kind round-trips, with exactly the keys its kind calls for:

```json
{ "type": "fixed", "component": 3 }
{ "type": "distance", "a": {…}, "b": {…}, "distance": -0.025 }
```

| Expected | Measured |
| --- | --- |
| all seven kinds | definitions equal, dependencies equal, `contentEquals` |
| a negative distance | `-25 mm` exactly |
| an angle | `30 deg` exactly |
| suppression | preserved |
| re-saving a loaded document | byte-identical |
| a fixed mate's entry | exactly **one** `"component"`, no targets |
| a parallel mate's entry | exactly **two**, one per target, none of its own |
| `residual`, `jacobian`, `solved`, `transform`, `iteration` | all absent |
| format version | still 1 |
| a document with no mates | no trace of the kind |

Loading runs the same validation creation does, so a file cannot describe a
mate the model would refuse: an angle of 4 radians in the file is a parse
failure, not a mate the rest of the system must tolerate.

## Determinism

| Expected | Measured |
| --- | --- |
| the same calls give the same IDs | equal |
| enumeration order | ascending by ID |
| definitions | equal |
| dependency order | equal, and it is naming order rather than hash order |

## Independent validation

```text
seven requested kinds          -> exactly seven canonical kinds     7 -> 7
distance 25 mm                 -> persisted as length, restored     exact
angle 90 deg                   -> persisted as angle, restored      exact
N mates created                -> N distinct stable identities      N -> N
missing target                 -> unresolved, never rebound         unresolved
```

No solver residual is validated, because there is no solver.

## Adversarial review

Two findings, **no production defect**; nothing was weakened.

### Cleared

| Question | Answer |
| --- | --- |
| Can `MateId` alias `ComponentId`/`ObjectId`? | No — two compile-fail cases |
| Can unsupported target types enter a mate? | Not representable; `FaceSignature` has no field |
| Can a mate reference geometry from the wrong component? | Refused, by walking the part's features |
| Can a deleted target silently rebind? | No — unresolved, and a replacement does not capture it |
| Can `Distance` take an angle, or `Angle` a length? | Not expressible |
| Can NaN/Inf enter canonical state? | Refused, both kinds, all three values |
| Can save/load change identity? | No — round trip and byte-identical re-save |
| Can serialization turn unresolved into resolved? | No — unresolved is computed, never stored |
| Can the model depend on solver internals? | There are none; the file is checked for their absence |
| Can a raw OCCT reference leak in? | No OCCT anywhere in the mate model |
| Can dependency ordering be nondeterministic? | No — naming order, de-duplicated first-seen |
| Were previous tests weakened? | No. `git diff --numstat` shows **zero deleted lines** anywhere |

### Finding 1 — the datum dependency existed only in prose

ADR-004 gives "a mate rebuilds when the datum it uses moves" as the *reason* a
mate's dependencies include its references' objects. Every target in the
tests was a principal plane or axis — which names no object at all — or a
face naming the part. So the edge that motivates the whole rule was never
measured.

Measured, and the implementation was already correct: a target naming a real
`DatumPlane` declares the edge, the graph records the mate as its dependent,
moving the datum dirties the mate without rewriting it, and deleting the
datum leaves the mate unresolved and still naming what is gone. Regression:
`Mate_DependsOnTheDatumItUsesAndFollowsItWhenItMoves`.

### Finding 2 — a new object kind was invisible in the CLI

`describeObject` falls through to an empty string for kinds it does not know,
so a mate appeared in `bettercad info` as a row with a type and a name and
**nothing else**. ADR-002 names this hazard exactly when listing the four
places a new object kind must be registered: "Missing the last one is silent
— the CLI prints an empty description — so it needs a test."

Found *after* the qualifying run had started, which was stopped one minute
in rather than allowed to certify a milestone with a blank row. Fixed with
the branch and its test in the same change:

```text
fixed, holds Block1
coincident, Block1 plane to Block2 plane
distance 25 mm, Block1 plane to Block2 plane, suppressed
```

This reverses a call made in `P13-XFORM-001`, which deliberately left
placement out of `info` and recorded it as a limitation. The difference is
arity, not taste: placement was extra detail on a row that already read
"places Block", while a mate is a whole kind whose row would otherwise be
empty.

### Process defect — building one target after a total clean

Killing the first qualifying run left `ninja -t clean` having already deleted
**every** build output. Rebuilding only `bettercad_tests` then left
`bettercad-cli.exe` absent, and fourteen reference CLI tests failed with "no
such file or directory".

Not a regression, and not silent — it failed loudly, which is the difference
between this and the stale-executable incident `P13-XFORM-001` recorded. Both
belong to one family: **a test result is only as trustworthy as the artifacts
it ran against.** The habit that follows is to rebuild with no target after
interrupting a qualification, because the clean is total.

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch,
then the whole suite; then the milestone's related tests five times over in
Release and Debug. Run on the frozen tree, `qualify.cmd` recording every exit
code.

| Preset | Configure | Build | C++ TUs | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- | --- | --- |
| `debug` | exit 0 | exit 0, 428 steps | 410 | 0 | **1349/1349** | 165.91 s |
| `release` | exit 0 | exit 0, 428 steps | 410 | 0 | **1349/1349** | 166.90 s |
| `debug-shared` | exit 0 | exit 0, 428 steps | 410 | 0 | **1349/1349** | 172.99 s |

Repeats, `ctest --repeat until-fail:5` over the mate, assembly, reference,
datum, persistence, object-model, CLI and architecture tests:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **771/771** | 550.31 s |
| `debug` | **771/771** | 587.73 s |

1349 is `P13-REF-001`'s qualified baseline of 1323 plus this milestone's 26
new cases: 23 mate cases, 2 compile-failure cases and the CLI description.
All 23 are in the repeat selection, and both tests that came out of the
adversarial review -- the datum dependency and the CLI description -- ran
their full five iterations in each preset.

Seventeen `cli.validate.reference.*` and `cli.export-step.reference.*` tests
pass in every preset. Those are the ones that failed earlier against a
missing `bettercad-cli.exe`, so their passing is the specific confirmation
that the failure was an absent binary and not a regression.

`debug-shared` matters most again: this milestone exports new symbols from
**both** libraries -- `MateTarget`'s helpers and `toString(MateTargetKind)`
from `core`, and `MateType`, `MateDefinition`, `Mate` and the six `Mates`
functions from `assembly`. It is the only preset that checks those
annotations, and it is where `P13-COMP-001`'s dllimport failure surfaced.

**The qualified tree is the committed tree.** The eight tree IDs recorded at
03:50:33 were recomputed after the run finished and are unchanged:

```text
apps              61778b8e1eb22169857799d8cb00c49be1c4771a
include           b572bbd089854362b9d9be81b7bba2cd481a15e4
src               eb0e9db92bcdd0c343cfd674c05742ead7a19ace
tests             d94d9d38edcaadb51ccf40d0baadd6ad69903920
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`examples` is byte-identical to the tree the three previous milestones
qualified, so the twelve reference models are untouched. `apps` differs from
`P13-REF-001`'s, which is the mechanical evidence that the CLI branch from
Finding 2 exists.

## Known limitations

- **Nothing is solved.** A mate is intent; no component moves, and no mate is
  checked for whether it *can* be satisfied. Over-constraint, redundancy and
  degrees of freedom are `P13-SOLVE-001`.
- **A face target on a component whose part is external is not
  ownership-checked.** Verifying that a face belongs to a part in another
  document needs that document's features, which is resolution rather than
  validation. The code says so where it gives up.
- **Mates are ordered.** `Coincident(A, B)` and `Coincident(B, A)` are
  equivalent but distinct definitions, and serialize differently. Inherent:
  `Distance` takes its sign from `a`'s normal, so there is no canonical form
  to normalise to.
- **Mate values are literals.** A distance or angle cannot yet be driven by a
  parameter, as a placement can. Adding `distanceParameter` later costs
  nothing at the format level, because keys are written only when present.
- **Mixed plane↔axis pairs are not expressible**, deliberately, until a
  convention is chosen for them.
- **A mate has no regeneration handler.** It needs none: its targets are
  ordinary dependency edges, so the graph reports a missing one and blocks
  the mate without any new machinery.

## Result

```text
TASK:            P13-MATE-001 — Basic Assembly Constraints
IMPLEMENTATION:  MateId, MateTarget in core, the constraint model in
                 assembly, the document-facing operations, the JSON
                 mapping, and the info description
TESTS:           26 Catch2 cases in 4 files, 2 of them compile-failure
VALIDATION:      semantic invariants, stated expected-versus-measured
REGRESSION:      1349/1349 on debug, release and debug-shared from clean;
                 771/771 five times over in release and debug;
                 0 compiler warnings; qualified tree == committed tree
ADVERSARIAL:     2 findings, 0 production defects, 2 regression tests added
RESULT:          PASS
EVIDENCE:        this directory
```

What this milestone settles:

**Seven kinds, and the asymmetry between them is honest.** `Fixed` holds a
component; the other six relate geometry. Validation checks the fields both
ways, so a mate cannot carry a value its kind has no use for.

**ADR-004's rule is enforced by the type, not by a check.** A
`FaceSignature` has no field it could occupy in a `MateTarget`, so "a mate
may reference anything that moves with the model" cannot be forgotten by a
future code path.

**The conventions are written down before the solver needs them.** `Angle` is
unsigned in `[0, 180]` degrees and out-of-range values are refused rather
than normalised; `Distance` is signed between planes and a non-negative
separation between axes; plane-to-axis pairs are refused because the two
readings are opposite.

**Nothing moves.** No solver state exists, and the file is checked for the
absence of every name one could take.

## Revision

| When | What |
| --- | --- |
| 03:23:54 | first qualifying run began; **void** |
| 03:24 | stopped one minute in: the CLI showed a blank row for mates (Finding 2) |
| 03:46 | CLI branch and its test added; full suite 1349/1349 |
| 03:50:33 | qualifying run began on the frozen tree |
| 04:45:58 | qualifying run finished; all fourteen exit codes 0 |
