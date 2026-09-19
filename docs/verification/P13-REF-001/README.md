# P13-REF-001 — Internal / External Reference Infrastructure

```text
STATUS:          PASS — 1323/1323 on debug, release and debug-shared
                 from clean; qualified tree == committed tree
BASELINE:        3d7dd23 (P13-REF-001 authorization), clean,
                 HEAD == origin/main
SCOPE:           reference identity, resolution and persistence. An
                 ObjectReference that is internal or external, an injectable
                 resolver, an explicit unresolved state, and a component
                 whose part may be either. No mates, no solver, no
                 cross-document regeneration, no assembly STEP.
IMPLEMENTATION:  ObjectReference and ReferenceResolver in core, resolution
                 in assembly, the component's part widened, its JSON mapping
TESTS:           22 new Catch2 cases in 3 files
EVIDENCE:        this directory
```

## ADR contract

[ADR-003](../../architecture/decisions/ADR-003-internal-part-references-first.md)
recorded the external-reference contract in full while deferring it, and
named this milestone as the one bound by it.
[ADR-006](../../architecture/decisions/ADR-006-assembly-module-and-layer.md)
decides where each piece lives.

| Contract | Where it is honoured |
| --- | --- |
| Identity is the document UUID, not the path | `ObjectReference::document`; `hint` is separate and excluded from `sameTarget` |
| A hint resolving to a different UUID is a failed reference, not a match | `DocumentMismatch`, tested with a resolver that always offers the wrong document |
| Resolution is explicit and injectable; loading reads no filesystem | `ReferenceResolver`, passed in; `loadDocument` resolves nothing |
| Unresolved is a state, not an error at load | `ReferenceState`; `ErrorCode` gained no values |
| A document with missing external parts still loads and reports them | `unresolvedComponents()`, tested through save/load |
| The graph must span documents before external references regenerate | external references contribute no edge; the handler fails them instead |
| Circularity is a hard error | not reachable yet — see "Known limitations" |
| Reference vocabulary in `core`, resolution in `assembly` | `core/document/ObjectReference.hpp` vs `assembly/Resolution.hpp` |

## Implementation

```text
include/bettercad/core/document/ObjectReference.hpp    the reference type
include/bettercad/core/document/ReferenceResolver.hpp  states, resolver, resolve()
src/core/document/ObjectReference.cpp                  both
include/bettercad/assembly/Resolution.hpp              part resolution, reporting,
src/assembly/Resolution.cpp                            the regeneration handler
```

446 lines in five new files, and 149 lines added to six existing ones. The
change to the component model itself is small -- `part` widened, one
`localTarget()` call in `dependencies()`, one branch in `checkComponent` --
because the implicit conversion from `ObjectId` carried every existing
caller. The largest single edit is the JSON mapping, at 62 lines, which has
to read both a bare number and an object.

`apps` changed this milestone, unlike the two before it: widening `part`
broke the CLI's `describeObject`, so it now reports an external part by
identity. That branch has its own test, because `P13-COMP-001` closed a
finding that was exactly an untested CLI branch.

## Identity model

The rule the milestone turns on, and the reason it exists:

```text
ObjectId               local identity, meaningful only inside one Document
external reference     stable document identity
                       + stable object identity
                       + a resolver
```

`P13-COMP-001` measured the problem precisely. Every document allocates
`ObjectId`s from 1, so a number copied from another document either is absent
here (and is refused) or **collides with a local object and binds to it,
undetectably**. That was recorded as a known limitation, and ADR-003 named
this milestone as where it is closed.

It is closed by *qualifying* the `ObjectId`, not by replacing it. No new
identity scheme was invented, because two durable identities already existed:

| Part of the identity | What it already was |
| --- | --- |
| which document | `DocumentId` — a UUID, persisted, survives the file being moved or renamed |
| which object in it | `ObjectId` — allocated in sequence, **never reused**, high-water mark persisted |

An `ObjectId` was never bad *object* identity. What it lacked was any
statement of which document it belongs to.

```cpp
struct ObjectReference {
    ObjectId object{};                     // in the document that owns it
    std::optional<DocumentId> document{};  // absent => this document
    std::string hint{};                    // a locator, never identity
};
```

`ObjectId` converts to `ObjectReference` implicitly, because an `ObjectId`
**is** an internal reference. That is what let a type used throughout the
component model widen without a migration: every existing caller and every
existing test compiled unchanged, and three call sites needed `.object`
because they pass the ID to `findObject`/`nameOf` — type adaptations, with no
assertion altered.

## Path versus identity

A filesystem path is never identity. `hint` is where a resolver may start
looking, and nothing more:

| Expected | Measured |
| --- | --- |
| two references differing only by hint | `sameTarget` true |
| the same, compared strictly | `operator==` false, so a dropped hint is visible to a save/load test |
| a hint edited in the saved file | loads with the new hint, `sameTarget` unchanged, still resolves |
| a hint leading to a different document | **rejected**, `DocumentMismatch` |
| an internal reference carrying a hint | refused by `validate` — nothing to locate |

That last row of the table is the one ADR-003 is emphatic about: "A hint that
resolves to a document with a different UUID is a failed reference, not a
match."

## The resolver: it proposes, resolution decides

```cpp
class ReferenceResolver {
    virtual const Document* candidate(const ObjectReference&) const = 0;
};
```

A resolver is **never asked whether the document it found is the right one**.
It returns a candidate; `resolve()` compares that candidate's own identity
against the reference and rejects a mismatch. A resolver cannot cause a
reference to bind to the wrong document however it is implemented, which is
what makes "the same path now points at another file" a reported mismatch
rather than a silent wrong answer.

`AlwaysOffers`, one of the two test resolvers, exists solely to prove this: it
returns one document whatever it is asked for, and the reference is still
refused — even though that document holds an object of exactly the ID named.

Nothing here reads the filesystem, and there is no global resolver. Loading a
document never resolves anything, which is what lets a file open with its
external parts missing.

## Unresolved is a state, not an error

ADR-003 is explicit, so `ErrorCode` gained **no new values**. Resolution
cannot fail; it reports:

```text
Resolved              the document was found, its identity matched, it has
                      the object
DocumentUnavailable   no resolver, or the resolver had nothing to offer
DocumentMismatch      a candidate was offered; its identity is not the one
                      named
ObjectMissing         the right document, but no object of that ID
```

`unresolvedComponents(document, resolver)` is how a document reports what it
cannot resolve rather than refusing to open. A component with an internal
part appears there too, when that object is gone.

## Internal references

Unchanged in behaviour, which is the point:

| Case | Result |
| --- | --- |
| a valid local target | `Resolved`, no resolver consulted |
| an ID this document does not have | `ObjectMissing` |
| a deleted target | `ObjectMissing` |
| a sketch where a part is wanted | `InvalidArgument`, "produces no body" |
| the old refusals (sketch, unknown ID, self-reference, duplicate name) | all still refuse |

## Cross-document collision

The test builds the collision deliberately rather than hypothesising it: two
documents whose parts genuinely share an ID number, which is the ordinary
case since both allocate from 1.

| Expected | Measured |
| --- | --- |
| a reference naming the other document's object resolves there | resolved in the other document |
| it does **not** bind to the local object of that number | the object found is the other document's, by name and by pointer |
| with no resolver, it does not fall back to the local object | `DocumentUnavailable`, nothing returned |

The last row is the one that matters most. Falling back would be the
collision bug wearing a new hat.

**What is not closed:** a *bare* `ObjectId` still means "in this document",
so a number copied between documents still binds locally. That is not a
defect, it is the definition, and `P13-COMP-001`'s test asserting it is
retained unchanged — it now documents the limitation of the unqualified form
rather than of the system.

## No silent rebinding

| Adversarial case | Result |
| --- | --- |
| target deleted | `ObjectMissing` |
| target deleted, then another object added | still `ObjectMissing`; the newcomer did not take the ID and the reference did not follow |
| target renamed | still resolves, to the renamed object |
| a local object takes the target's old name | captures nothing; resolution still crosses to the other document |
| the locator now points at another file | `DocumentMismatch` |
| no resolver at all | `DocumentUnavailable`, never a local match |

Identity is `(document, object)`. Names, paths, positions and array order
have no vote.

## Recovery

Canonical identity does not change while a target is away, so the same
reference resolves again when it returns:

```text
resolver empty        -> DocumentUnavailable
document added        -> Resolved, the intended object
resolver cleared      -> DocumentUnavailable
```

The reference is never rewritten to make that work, which is checked by
comparing it to a freshly constructed one afterwards.

## Type compatibility

`resolvePart()` resolves and then checks the kind, so a reference to a sketch
in another document is refused exactly as one in this document is —
`InvalidArgument`, "produces no body". The kind check needs
`features::SolidFeature`, which is why it lives in `assembly` while the
reference vocabulary lives in `core`: the split ADR-006 draws.

## Regeneration, and the silence it would otherwise hide

An external part is **not** a dependency edge. `ObjectId` cannot name a node
in another document, and ADR-003 forbids a graph that pretends otherwise
until a wider node identity exists — a phase, not a milestone.

That creates a hazard this milestone had to answer. With no edge, the graph
reports nothing missing; and `P13-COMP-001` established that the regenerator
treats a handler-less object as plain data. A component with an unresolvable
external part would therefore regenerate **as if all were well**.

`assembly::registerHandlers(regenerator, resolver)` closes it. ADR-006
anticipated exactly this shape — "assembly registers its own regeneration
handlers and `features` does not need to know assemblies exist" — and the
resolver is captured at registration because the handler signature has
nowhere to pass one.

The test asserts the silence is real before asserting it is covered:

| Expected | Measured |
| --- | --- |
| the graph has nothing to report | `missing` empty, `dependenciesOf` empty |
| with no resolver, the component | **Failed**, error says "document unavailable" |
| with a resolver that has the document | regeneration succeeds |

## Persistence

An internal reference is still a bare number, so nothing about an existing
file changed:

```json
{ "id": 3, "type": "component", "name": "Block1", "data": { "part": 2 } }
```

An external one carries identity and, separately, the locator:

```json
"part": { "document": "…uuid…", "object": 2, "hint": "parts/there.bcad" }
```

| Expected | Measured |
| --- | --- |
| an internal part | a bare number; no `document`, `object` or `hint` in the entry |
| an external part | round-trips exactly, locator included |
| re-saving a loaded document | byte-identical |
| a document whose source is missing | loads, reports one unresolved component |
| the source returns | the same reference resolves |
| a document identity that is not a UUID | parse error naming the path |
| a nil document identity | refused |
| an unknown key inside the reference | refused by name |
| a non-string locator | refused |

No resolver state is written: the file holds identity and a hint, never a
resolved pointer.

## Legacy compatibility

The strongest check is one that already existed:
`ReferenceModel_SavedModelsMatchTheBuilders` requires each of the twelve
committed `examples/models/reference/*.bcad` files to be reproduced byte for
byte. It passes unchanged, and the qualified `examples` tree ID is identical
to the one `P13-COMP-001` and `P13-XFORM-001` qualified.

The absence assertions are scoped to the component's own entry by balancing
braces. An earlier version searched the whole file for `"document"` — which
every `.bcad` contains as its top-level object — and failed for a reason that
had nothing to do with references. That is the same over-broad-search defect
`P13-COMP-001` recorded against `"placement"`, made a second time and caught
the same way: by running the test.

## Determinism

| Expected | Measured |
| --- | --- |
| resolving twice | identical state, object and document pointers |
| resolving an unresolvable reference twice | identical state |
| save -> load -> resolve | the same object |

Nothing is cached, so there is no stale state to make a second answer differ
from the first.

## Failure atomicity

| Expected | Measured |
| --- | --- |
| a malformed external reference refused at creation | `InvalidArgument` |
| objects, IDs, revision | all unchanged |
| the document afterwards | still usable; the next valid creation succeeds |

Validation runs before anything is built, so a refused reference consumes no
ID.

## Independent validation

The oracle is the invariant, stated before it was measured:

```text
same document identity + same object identity  -> the intended object
same ObjectId, different document identity     -> never the local object
a candidate whose identity differs             -> refused, not accepted
no resolver                                    -> unresolved, not local
locator changed                                -> identity unchanged
target deleted, replacement added              -> still unresolved
```

## Adversarial review

Run against the implementation before completion, against the milestone's own
challenge list. Two gaps found; **no production defect**.

### Cleared

| Question | Answer |
| --- | --- |
| Can a local `ObjectId` collision resolve to the wrong document? | No — tested with a real collision, including the no-resolver case |
| Can a path change change identity? | No — `sameTarget` ignores the hint, through save/load |
| Can malformed UUIDs enter canonical state? | No — nil refused by `validate`, unparseable refused at load |
| Can resolver caching return stale objects? | No cache exists; `resolve()` computes each time |
| Can save/load alter canonical identity? | No — round trip and byte-identical re-save |
| Can an unresolved reference become valid through an ID collision? | No — `DocumentUnavailable`, never a local fallback |
| Can dependency code treat an external reference as local? | No — `localTarget()` gives nothing; `dependenciesOf` is empty |
| Can a resolver implementation leak into persistence? | No — only UUID, object ID and hint are written |
| Did tests use the implementation as their own oracle? | No |

### Finding 1 — rebinding after a replacement was untested

Deletion was tested; deletion *followed by adding another object* was not,
and that is the case where a rebinding bug would actually show. Measured: the
reference stays `ObjectMissing`, and the new object does not take the removed
ID. Regression: `Reference_NeverRebindsToAnObjectThatTookTheTargetsPlace`,
which also fails loudly if ID reuse is ever introduced.

### Finding 2 — names influencing identity was untested

Measured: renaming the target leaves the reference resolving to it, and a
*local* object taking the target's old name captures nothing. Regression:
`Reference_IdentityDoesNotFollowNames`.

### Test defects found by running the tests

- An absence assertion searched the whole file for `"document"`, which every
  `.bcad` contains. Scoped to the component entry. Recorded because it is the
  second occurrence of a defect this project has already recorded once.
- The new tests were tagged `[reference]`, which already means "reference
  model" in this project (`P11-REF-001`, `P12-REF-001`) — a filter matched
  109 cases instead of 19. Retagged `[objectref]`.

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch,
then the whole suite; then the milestone's related tests five times over in
Release and Debug. Run on the frozen tree, `qualify.cmd` recording every exit
code.

| Preset | Configure | Build | C++ TUs | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- | --- | --- |
| `debug` | exit 0 | exit 0, 422 steps | 404 | 0 | **1323/1323** | 172.29 s |
| `release` | exit 0 | exit 0, 422 steps | 404 | 0 | **1323/1323** | 170.09 s |
| `debug-shared` | exit 0 | exit 0, 422 steps | 404 | 0 | **1323/1323** | 179.39 s |

Repeats, `ctest --repeat until-fail:5` over the reference, assembly,
persistence, object-model, parameter, CLI and architecture tests:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **730/730** | 559.21 s |
| `debug` | **730/730** | 624.25 s |

1323 is `P13-XFORM-001`'s qualified baseline of 1301 plus this milestone's 22
new cases. All 21 `Reference*` cases are in the repeat selection, and both
adversarial-review tests ran their full five iterations in each preset.

`debug-shared` is the preset that matters most here. This milestone exports
new symbols from **both** libraries -- `ObjectReference`,
`ReferenceResolver`, `resolve()` and `toString(ReferenceState)` from `core`,
and `resolvePart`, `unresolvedComponents` and `registerHandlers` from
`assembly` -- including an abstract interface across a DLL boundary. It is
the only preset that checks those annotations, and it is where
`P13-COMP-001`'s dllimport failure surfaced.

**The qualified tree is the committed tree.** The eight tree IDs recorded at
01:26:44 were recomputed after the run finished and are unchanged:

```text
apps              c26d17cecf73c6a770439ddad8c61157a287ddf8
include           449e2c9d9b32efd2ae28680e8372d2725acb7bdb
src               c42efaebafb34c63724563319b2c83b9dcc6a40e
tests             c3b7828e9e21ca5b4a43b4a2b2bfda2123ac496f
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`examples` is byte-identical to the tree `P13-COMP-001` and `P13-XFORM-001`
qualified, so the twelve reference models are untouched. `apps` is **not**,
unlike those two milestones: widening `part` obliged the CLI to describe an
external reference, and that branch carries its own test.

### Verification-process checks

`P13-XFORM-001` recorded an incident where a failed link left an older
executable in place and its output was reported as a pass. The checks that
came out of it were applied here throughout:

- every build command propagated the real exit code, never an `echo`'s;
- the executable was proven newer than every source file before any result
  was believed (01:26:04 against 01:14:50);
- both new tests were confirmed present by listing them by name, not by
  inferring them from a case count;
- the test binary was never run while a build of it was in flight -- which is
  what caused that incident, and did not recur.

## Known limitations

- **Cross-document references do not regenerate.** An external part is no
  dependency edge, because the graph speaks in `ObjectId`. ADR-003 defers
  widening it, and this milestone does not pretend otherwise: such a
  component fails regeneration with the state that says why, rather than
  appearing to succeed.
- **A bare `ObjectId` still binds locally.** Qualifying a reference with a
  document identity closes the collision; an unqualified `ObjectId` is
  *defined* as local and is unchanged. `P13-COMP-001`'s test of that
  behaviour is retained.
- **Copying a `.bcad` file duplicates its UUID.** Two files can then claim one
  identity, and which is resolved is the resolver's choice. This is inherent
  to UUID identity rather than to this design, and is the reason a resolver
  is an injected policy rather than a filesystem scan.
- **`ResolvedReference` holds non-owning pointers** into the resolver's
  document, which must outlive the result — the same contract
  `Document::findObject` has always had, stated in the header.
- **No circularity detection.** ADR-003 requires document A referencing B
  referencing A to be refused. Nothing here can create that cycle, because a
  reference does not yet make a document depend on another document; the
  check belongs with cross-document regeneration.
- **The CLI reports an external part but cannot resolve one.** `info` reads
  one file and supplies no resolver, so it prints the identity and says
  "unresolved". Supplying a resolver from the command line is `P13-CLI-001`.

## Result

```text
TASK:            P13-REF-001 — Internal / External Reference Infrastructure
IMPLEMENTATION:  ObjectReference and ReferenceResolver in core, resolution
                 and the regeneration handler in assembly, the component's
                 part widened, its JSON mapping
TESTS:           22 Catch2 cases in 3 files
VALIDATION:      identity invariants, stated expected-versus-measured
REGRESSION:      1323/1323 on debug, release and debug-shared from clean;
                 730/730 five times over in release and debug;
                 0 compiler warnings; qualified tree == committed tree
ADVERSARIAL:     2 gaps found, 0 production defects, 2 regression tests added
RESULT:          PASS
EVIDENCE:        this directory
```

What this milestone actually settles:

**A reference that carries a document identity cannot bind to the wrong
object.** The collision `P13-COMP-001` measured and recorded is closed for
qualified references -- proven against a real collision, including the case
where no resolver is available, which does not fall back to the local object.

**A path can never become identity.** The hint is excluded from `sameTarget`,
a candidate reached through it is accepted only if its own UUID matches, and
a resolver is never asked whether what it found is right.

**Nothing is silently fine.** An external part contributes no dependency
edge, which would have made an unresolvable component regenerate as though
all were well; the handler turns that into a failure naming the state. The
test asserts the silence exists before asserting it is covered.

## Revision

| When | What |
| --- | --- |
| 00:20-01:14 | implementation, tests, and two rounds of test defects found by running them |
| 01:20 | adversarial review; two gaps closed with new tests |
| 01:26:44 | qualifying run began on the frozen tree |
| 02:25:20 | qualifying run finished; all fourteen exit codes 0 |
