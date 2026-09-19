# P13-COMP-001 — Component Definitions and Instances

```text
STATUS:          PASS — 1282/1282 on debug, release and debug-shared
                 from clean; qualified tree == committed tree
BASELINE:        a6c63a4 (P13-ARCH-001), clean, HEAD == origin/main
SCOPE:           the component domain model only -- identity, document
                 ownership, internal part references, dependency
                 participation, persistence. No transforms, no mates, no
                 solver, no external references, no assembly CLI commands
                 and no STEP. `info` describes the new object kind; that is
                 the only CLI change.
IMPLEMENTATION:  a new `assembly` module at layer 3, `ComponentId` in core,
                 `Component` as a DocumentObject, its JSON mapping, and the
                 ADR-006 layer renumber
TESTS:           21 new Catch2 cases in 3 files, plus 2 compile-failure cases
EVIDENCE:        this directory
```

## Scope, and what was deliberately not built

This is the first `P13` milestone to write code. It implements
[ADR-002](../../architecture/decisions/ADR-002-assemblies-live-in-the-document.md)
(components are document objects) and
[ADR-003](../../architecture/decisions/ADR-003-internal-part-references-first.md)
(their part is in the same document), and nothing else.

Absent on purpose, and verified absent in the file format below: any
transform or placement field (ADR-005, `P13-XFORM-001`), any external
reference field — path, UUID or document id (ADR-003, `P13-REF-001`), any
mate or constraint type (ADR-004, `P13-MATE-001`), and any per-component
configuration state (the limitation ADR-002 records).

## ADR contracts implemented

| Contract | Where |
| --- | --- |
| Components are `DocumentObject`s in the ordinary `Document` | `include/bettercad/assembly/Component.hpp` |
| `ComponentId` widens to `ObjectId`, like `SketchId`/`FeatureId` | `include/bettercad/core/Id.hpp` |
| A component's `dependencies()` is the part it places | `Component::dependencies()` |
| The part is an `ObjectId` in the same document | `assembly::checkComponent()` |
| No `.bcad` format change: new `type` in the existing `objects[]` | `src/io/json/ComponentJson.cpp` |
| New `assembly` module at layer 3; `io` → 4; renderer/scripting → 5 | `tests/architecture/CheckLayering.cmake` |

## Implementation

```text
include/bettercad/core/Id.hpp          ComponentIdTag, ComponentId, widening
include/bettercad/assembly/Component.hpp   ComponentDefinition, Component
include/bettercad/assembly/Components.hpp  the document-facing operations
src/assembly/Component.cpp                 the object
src/assembly/Components.cpp                create / find / enumerate /
                                           repoint / remove, with validation
src/assembly/CMakeLists.txt                the module
src/io/json/ComponentJson.cpp              the JSON mapping
apps/bettercad_cli/DocumentCommands.cpp    `info` names the part a
                                           component places
```

384 lines in five new implementation files, and 42 lines added to four
existing ones (`Id.hpp`, `DocumentJson.cpp`, `ObjectJson.hpp` and the CLI's
`describeObject`), plus the module's build files. The document-facing operations are free
functions in `assembly`, not `Document` members, because `Document` is core
(layer 0) and must not know assemblies exist (ADR-006).

The layer renumber was the other half of ADR-006, and it cost nothing:
**no source file's includes had to change**. ADR-006 predicted that, and said
that if any had, "a dependency was pointing the wrong way already and that is
a finding, not a fix-up". None did.

## Component identity

`ComponentId` is a strong typed ID from the document's one allocator. It is
not an index and not a pointer, and IDs are never reused — a removed
component's number is never given to another object.

The separation from `ObjectId` is enforced **at compile time**, by two new
cases in the existing compile-failure suite:

```text
compile_fail.ids.object-to-component-id     ObjectId  -/->  ComponentId
compile_fail.ids.component-id-as-feature-id ComponentId -/-> FeatureId
```

Both compile-fail as required. `ComponentId` widens *to* `ObjectId` (it is a
document object) but nothing narrows back, and a component is not
interchangeable with a feature even though both widen.

## Part versus instance identity

The core assembly invariant, tested directly in
`Component_IdentifiesTheInstanceNotThePart`:

| Expected | Measured |
| --- | --- |
| 3 components of one part → 3 distinct `ComponentId`s | 3 distinct |
| 3 components of one part → all name the same `ObjectId` | all equal to the part |
| the part is not duplicated | document holds 1 sketch + 1 extrude + 3 components = 5 objects |
| enumeration is ascending and stable | `components()` == `{a, b, c}` |

A component holds no geometry of its own: its entire state is one `ObjectId`
and one `bool`. There is no body to duplicate.

## Internal references

`checkComponent()` accepts an object of this document that produces a body (a
`SolidFeature`), and refuses everything else with a structured error:

| Case | Result |
| --- | --- |
| an `ObjectId` this document does not have | `NotFound` — "not an object of this document" |
| a sketch | `InvalidArgument` — "a sketch produces no body" |
| a datum plane | `InvalidArgument` |
| another component | `InvalidArgument` |
| an invalid (default) `ObjectId` | `InvalidArgument` — "must name the part" |
| a name already taken | refused by the document |

The same check runs on the modify path: `setComponentDefinition()` validates
before changing anything, so a component cannot be quietly repointed at a
sketch. Self-reference is refused there with its own message
("cannot place itself"), and is *also* caught by the dependency graph's cycle
detection if reached past that path — both are tested.

## Foreign-document rejection — what is actually delivered

This gate needs stating precisely, because part of it is not achievable and
saying otherwise would overclaim.

An `ObjectId` carries **no document identity**. It is a number from its own
document's allocator, and every document allocates from 1. So an ID taken
from another document is one of two things:

| Case | Detectable? | Behaviour |
| --- | --- | --- |
| absent from this document | yes | rejected, `NotFound` |
| colliding with a local object | **no** | binds to the **local** object of that ID |

Both are tested, including the collision, in
`Component_ForeignObjectIdsAreRejectedOnlyWhenTheyAreAbsent`. The test builds
two documents whose parts genuinely share an ID number and shows the
component binding to the local one.

**The invariant that is delivered — and it is the one that matters — is that
a component's reference never resolves outside its own document.** Nothing
crosses a boundary: `checkComponent` and `findComponent` only ever consult
the document passed to them. What cannot be delivered is *detecting* that a
number was copied from elsewhere, because after the copy there is nothing to
detect.

This is not a gap in the implementation. It is the concrete reason ADR-003
gives for why cross-document references need a node identity wider than
`ObjectId`, and it is now demonstrated rather than argued.

## Dependency graph

A component declares exactly one edge, through the existing
`std::vector<ObjectId> dependencies()` contract:

```text
Component  ->  the part it places
```

Verified against the document's own graph, not just the object's method:
`graph.dependenciesOf(component) == {part}` and
`graph.dependentsOf(part).contains(component)`, with `graph.missing` empty.

Because it is an ordinary edge, the existing machinery applies with no new
code, and both halves are tested:

- **the part is deleted** → the graph records a missing reference and
  regeneration fails with "references …, which does not exist". Never
  silently resolved to another body (ADR-003).
- **the part fails** → the component is `Blocked`, not left looking fine.

This works with **no regeneration handler**: the regenerator blocks and fails
a node before it looks for a handler, and treats a handler-less object as
plain data. No stub was written to get this behaviour.

## Failure atomicity

`Component_FailedCreationChangesNothing`: three consecutive refused creations,
then a successful one.

| Expected | Measured |
| --- | --- |
| object count unchanged | unchanged |
| **no ID consumed** | `lastAllocatedId()` unchanged |
| document revision unchanged | unchanged |
| the next valid creation takes the next ID | `good.value() == idBefore + 1` |

No ID is consumed by a failed creation because `checkComponent` runs before
anything is built — the same ordering `Document::createParameter` uses.

## Deletion

`Component_IsRemovedWithoutTouchingItsPart`: the component is gone from both
`findComponent` and `findObject`, the part and the sibling component are
untouched, removing it again fails cleanly with `NotFound`, and a later
component gets a **fresh** ID rather than the removed one.

## Persistence

`ComponentFile_RoundTripsEveryCanonicalRelationship` runs the real trip —
build, save, load — and compares the relationships, not the geometry:

| Expected | Measured |
| --- | --- |
| same `ComponentId`s, same order | `{a, b, c}` preserved |
| same object count and allocator high-water mark | preserved |
| each component's name and definition | equal, via `contentEquals` |
| each reference still names the part | equal, and resolves in the loaded document |
| each dependency edge | `{part}` |
| suppression | preserved (true on one, false on the others) |
| re-saving the loaded document | **byte-identical** |

Malformed component data is a parse error naming its JSON path, not a silent
empty reference: a non-ID `part` is rejected, and an unknown key inside the
component's data is rejected by name.

## .bcad schema check

ADR-002's central claim, checked against the bytes rather than taken on trust
(`ComponentFile_UsesTheExistingObjectEnvelopeAndNoNewKey`). What a saved
component actually looks like:

```json
{
  "id": 3,
  "type": "component",
  "name": "Block1",
  "data": {
    "part": 2
  }
}
```

| Claim | Verified |
| --- | --- |
| `"version": 1` unchanged | yes |
| no new top-level key (`assembly`, `assemblies`, `components`) | absent |
| the ordinary `objects[]` envelope with a new `type` | yes |
| no external-reference field in the component entry | `source_document`, `path`, `uuid`, `document` all absent |
| no transform field in the component entry | `transform`, `placement`, `frame` all absent |
| an unsuppressed component writes no `suppressed` key | absent |

The absence checks are scoped to the component's own entry rather than the
whole file. That was a defect in the first version of this test, found by
running it: `"placement"` is a **sketch's plane frame** and has been a key
since `P0`, so a blanket search failed for a reason that had nothing to do
with components. The implementation was correct; the test was over-broad.

## Legacy compatibility

`ComponentFile_ADocumentWithoutComponentsIsUnchanged` shows a document with
no components contains no trace of the kind.

The stronger check is the one that already existed:
`ReferenceModel_SavedModelsMatchTheBuilders` requires each of the twelve
committed `examples/models/reference/*.bcad` files — all written before `P13`
— to be reproduced **byte for byte** by saving a freshly built document. It
passes unchanged, which is the real proof that adding the component kind
altered nothing about how existing documents serialize. The `examples` tree
ID is identical to the baseline's, so those files were not touched.

## Determinism

| Expected | Measured |
| --- | --- |
| the same sequence of public calls gives the same IDs | `build() == build()` |
| enumeration order | ascending by ID, from the document's ordered map |
| `dependencies()` | a one-element vector; nothing to order |
| save → load → save | byte-identical |

No claim is made beyond what the document's allocator already guarantees:
IDs are sequential from 1 per document, so equality holds for the same call
sequence, not across different ones.

## Independent validation

This is a domain-model milestone, so the oracle is the invariant rather than
a closed form. Each was stated as expected-versus-measured above; the four
load-bearing ones:

```text
N components created        -> N distinct ComponentIds        3 -> 3
M components of one part    -> 1 part definition, M ids       3 -> 1, 3
save/load                   -> identical reference graph      identical
reference resolution        -> never leaves the document      never
```

No geometry validation is claimed, because this milestone introduces no
geometry.

## Adversarial review

Run against the implementation before completion. Four real findings, all
fixed; the rest investigated and cleared with evidence. One was found only by
a build preset, which is why all three are run, and one only by asking which
new code no test executes.

### Finding 1 — the modify path bypassed the document check

`Component::setDefinition` validates only the definition itself, which cannot
see the document and so cannot tell a part from a sketch. Reached through
`Document::modifyObject`, a component could be repointed at a sketch and
**regeneration did not complain** — measured: `regen succeeded = 1`. Unlike a
feature, whose handler would fail resolving the reference, a component has no
handler to catch it.

**Fixed** by adding `assembly::setComponentDefinition()`, which runs the same
`checkComponent` as creation, and by refusing self-reference there with a
specific message. Regression:
`Component_RepointingIsCheckedLikeCreation`.

**Residual, recorded:** reaching past that function with raw
`Document::modifyObject` still gets only the definition's own validation.
That closes when components regenerate (`P13-REGEN-001`). Self-reference is
the one case caught anyway, by cycle detection.

### Finding 2 — the shared build did not link

Found by the third preset, and by nothing else. Debug and Release both built
and passed 1280/1280; `debug-shared` failed to link:

```text
src/io/json/DocumentJson.cpp:83: undefined reference to
`__imp__ZN9bettercad8assembly9Component9kTypeNameE'
```

`Component::kTypeName` is a `constexpr static` of a dll-exported class. The
reader dispatched on it (`*type == assembly::Component::kTypeName`), which
binds a reference to it across the library boundary; in a shared build that
is a dllimport of a symbol the assembly DLL never emits.

The first attempt at a fix -- defining `typeName()` out of line so something
inside the library would odr-use the constant -- **did not work**, and was
reverted rather than left in place looking like a fix. Returning a constexpr
static by value is not an odr-use either.

**Fixed** by dispatching on the literal `"component"`, which is what the
sketch branch beside it has always done, with a `static_assert` in
`ComponentJson.cpp` so the literal and `kTypeName` cannot drift apart:

```cpp
static_assert(assembly::Component::kTypeName == "component",
              "the component type name and the name the reader dispatches on must agree");
```

Because source changed after the regression had begun, that regression run
was **void and was re-run from clean**, per the freeze rule.

### Finding 3 — a test asserted more than the format allows

The schema test searched the whole file for `"placement"`, which is a
sketch's plane frame. Fixed by scoping the assertions to the component entry.
The implementation was never wrong.

### Finding 4 — the one piece of new code no test executed

`describeObject` gained a branch printing `places object:2 (Block)` for a
component, reachable from `info`, and nothing exercised it. No file under
`tests/cli/` mentioned components, while fourteen other CLI test files drive
`info` and assert the lines it prints. The `missing` fallback — the
branch that runs when a component's part has been deleted — had never been
executed at all.

The branch itself was correct. The defect was that the milestone's scope line
said "no assembly CLI" while the diff changed the CLI, so the gap was
invisible: the code was neither in the scope that was tested nor in the scope
that was declared absent.

ADR-002 had already called for this test by name. Listing the four places a
new object kind must be registered, it says of `describeObject`:

> Missing the last one is silent — the CLI prints an empty description — so
> it needs a test.

The branch was added; the test the ADR asked for was not. So this is not a
reviewer's preference about coverage — it is a recorded architectural
requirement that the first implementation of the ADR did not meet.

**Fixed** by `tests/cli/ComponentCliTests.cpp`, two cases through the same
in-process CLI runner every other CLI test uses:

```text
ComponentCli_InfoNamesThePartEachComponentPlaces
    both components' rows, the shared part named once, the suppressed marker
ComponentCli_InfoReportsAComponentWhosePartIsGone
    the missing fallback, on a document saved with its part removed
```

The second is the one that matters: nothing validates references on save or
load, so a document with a dangling part reference is a file a user can
actually have, and `info` has to say so rather than print a blank.

### Investigated and cleared

| Question | Answer |
| --- | --- |
| Can two components share identity? | No — one allocator, never reused; tested including after deletion. |
| Can `ComponentId` be confused with `ObjectId`? | No — prevented at compile time by two new compile-fail cases. |
| Can a component reference the wrong object type? | Refused on both the create and modify paths. |
| Can a foreign `ObjectId` slip through? | Only as a collision, which is undetectable and binds locally — measured and recorded above, not hidden. |
| Can deleting a part leave a valid-looking broken component? | No — missing reference, reported, dependents blocked. |
| Can invalid creation partially mutate the document? | No — no object, no ID, no revision change. |
| Can save/load change a `ComponentId`? | No — tested. |
| New top-level `.bcad` schema? | No — tested against the bytes. |
| Duplicated canonical geometry? | No — a component holds an `ObjectId` and a `bool`. |
| A second object registry? | No — `Document::addObject`/`findObjectAs`/`removeObject` only. |
| Duplicated configuration state? | No — no configuration field exists. |
| Transforms implemented early? | No — no field, and the JSON is checked for its absence. |
| Dependency cycles? | Self-reference is refused, and caught by existing cycle detection if forced. |
| Tests using internal shortcuts? | No — the public API throughout. |
| Any existing test weakened? | No. Every deleted line in the diff is one JSON dispatch line I replaced and the three layer numbers ADR-006 renumbers. |

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch,
then the whole suite; then the milestone's related tests five times over in
Release and Debug. Run on the frozen tree, `qualify.cmd` recording every exit
code.

| Preset | Configure | Build | C++ TUs | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- | --- | --- |
| `debug` | exit 0 | exit 0, 414 steps | 396 | 0 | **1282/1282** | 190.81 s |
| `release` | exit 0 | exit 0, 414 steps | 396 | 0 | **1282/1282** | 179.91 s |
| `debug-shared` | exit 0 | exit 0, 414 steps | 396 | 0 | **1282/1282** | 199.42 s |

Repeats, `ctest --repeat until-fail:5` over the assembly, persistence,
object-model, CLI and architecture tests:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **632/632** | 587.86 s |
| `debug` | **632/632** | 651.96 s |

1282 is the previous baseline of 1280 plus this milestone's two new CLI
cases; nothing was lost or skipped. Both new cases appear five times each in
the repeat logs, so they were exercised by the repeats rather than filtered
out of them.

Warnings are errors in every preset, so a build exit of 0 **is** the
zero-warning result rather than a count taken afterwards. The single
`ninja: warning: premature end of file; recovering` in `build-debug.log` is
ninja repairing its own `.ninja_log`, damaged when earlier interrupted runs
were killed. It is not a compiler diagnostic.

**The qualified tree is the committed tree.** The eight tree IDs recorded at
20:22:13 were recomputed after the run finished and are unchanged:

```text
apps              fdecf1a820c84cd7a76b07a66b81b1e8974ba691
include           22359eb6f7abb94f6fd724a0714b25021aa95bb8
src               ca8153ba8ff7f7ada34e67798fb20379ba2204be
tests             c7296bcc8e96e9eed7167495228d87bb1447fa19
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

### Earlier attempts, all void

Recorded rather than dropped, because quietly discarding a qualification is
what the freeze rule exists to prevent.

| Started | Void because |
| --- | --- |
| 19:04:36 | terminated mid-build; its `debug-shared` build had failed to link (see below) |
| 19:17:31 | source changed after it began (the shared-build fix), and its `debug configure` exited 1 |
| 20:02:08 | its `debug configure` exited 1 |

**No qualification *result* from any of them is cited as a pass.** One
artefact is deliberately kept and is cited: `void-run-1/`, the failing
`debug-shared` build log whose undefined reference to
`__imp__ZN9bettercad8assembly9Component9kTypeNameE` is quoted in
**Finding 2**. That is a defect discovery, not a qualification result, and
discarding it would have hidden how the only preset that catches the bug
caught it.

The two failed configures share one environmental cause: two sessions were
building into `build/debug` at the same time. The same overlap truncated
object files mid-link and left files locked against `ninja -t clean`. The
qualifying run at 20:22:13 had the build tree to itself, and its clean
succeeded on the first attempt in all three presets.

### The harness defect these runs exposed

`qualify.cmd` recorded the configure exit code and never branched on it. A
failed configure fell through to clean, build and ctest and finished looking
complete; only reading `qualification-times.txt` by eye would have caught it.
Both void runs above are instances.

Fixed in this milestone's copy of the script: the configure is now gated the
way the clean already was, and a failure records `build skipped: the
configure failed` and `ctest skipped: the configure failed` instead of
running on. The qualifying run shows the gate passing rather than being
skipped.

The committed evidence was audited for whether the defect had ever masked a
real failure. It had not: all eleven configure, build, ctest and repeat exit
codes are 0 in both `P12-REF-001` and `P12-QUAL-001`. `P11-QUAL-001` predates
this script. Those qualifications stand on their logs.

## Known limitations

- **A colliding foreign `ObjectId` is not detectable.** See above. Inherent
  to `ObjectId`, and the reason ADR-003 defers external references.
- **Raw `Document::modifyObject` bypasses the document-level reference
  check.** The supported path validates; the bypass is caught only for
  self-reference. Closes at `P13-REGEN-001`.
- **A component cannot select a part configuration.** Configurations are
  document-global and the parts are in the same document (ADR-002). No
  per-component configuration state was added, deliberately.
- **A component has no position.** Transforms are `P13-XFORM-001`.
- **Components produce no geometry**, so a document of components validates
  and exports exactly as the parts alone do.

## Result

```text
TASK:            P13-COMP-001 — Component Definitions and Instances
IMPLEMENTATION:  an assembly module at layer 3, ComponentId in core,
                 Component as a DocumentObject, its JSON mapping, its info
                 description, and the ADR-006 layer renumber
TESTS:           21 Catch2 cases in 3 files, plus 2 compile-failure cases
VALIDATION:      invariants, stated expected-versus-measured throughout
REGRESSION:      1282/1282 on debug, release and debug-shared from clean;
                 632/632 five times over in release and debug;
                 0 compiler warnings; qualified tree == committed tree
ADVERSARIAL:     4 findings, all fixed, each with a regression test
RESULT:          PASS
EVIDENCE:        this directory
```

One gate needs its wording read precisely rather than taken at face value.
**"Foreign-document references rejected" is delivered as far as `ObjectId`
allows, and no further.** A component in one document cannot reference an
object owned by another, because resolution never leaves the document it is
given; an ID that is absent is refused with a structured `NotFound`, changing
nothing. What cannot be delivered is *detecting* that a number was copied
from another document when it happens to collide with a local object: at that
point it is not a reference to the other document's object, it is a number
that names a local one, and nothing distinguishes them. Both paths are
tested, collision included. This is the concrete reason ADR-003 defers
cross-document references to `P13-REF-001`, and it is recorded here rather
than ticked as unqualified detection.

## Revision

| When | What |
| --- | --- |
| 19:04:36 | attempt; void — terminated mid-build, `debug-shared` had failed to link |
| 19:17:31 | attempt; void — source changed after it began, and its configure failed |
| 20:02:08 | attempt; void — its configure failed |
| 20:14 | `tests/cli/ComponentCliTests.cpp` added (Finding 4), `qualify.cmd` gated on configure |
| 20:22:13 | qualifying run began on the frozen tree |
| 21:19:06 | qualifying run finished; every exit code 0 |

Every void attempt is listed in **Regression** with the reason it is void.
None of them is cited as a pass. The one artefact kept from them,
`void-run-1/build-debug-shared-FAILED.log`, is cited in **Finding 2** as the
discovery of the shared-build defect.
