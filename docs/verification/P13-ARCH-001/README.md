# P13-ARCH-001 — Assembly Architecture and Contracts

```text
TASK:            P13-ARCH-001
SCOPE:           decide how assemblies fit the existing architecture, and
                 record the decisions and what they rejected
IMPLEMENTATION:  none, deliberately — this milestone designs
OUTPUT:          ADR-002 … ADR-006, and an Assemblies section plus three new
                 invariants in ARCHITECTURE.md
RESULT:          PASS
EVIDENCE:        this directory, and the ADRs it cites
```

## Scope, and what would have failed it

This milestone produces decisions, not code. Its gate says so, and says why:
writing a stub, a placeholder type or an unused header to "start" the
implementation would be a failure of the milestone rather than progress in
it. **No executable source or test file was changed.** That is verified
below.

The temptation this guards against is specific and real. Two of the
decisions here (ADR-003, ADR-006) describe future structure in enough detail
that adding a `source_document` field or a `src/assembly/` directory now
would feel like getting ahead. Both would be unused, both would appear in
the file format or the build as though they meant something, and neither
would be covered by a test.

## Method

The lifecycle's UNDERSTAND phase first, then ARCHITECT. The existing
architecture was traced — document ownership, the object model, the
dependency graph, regeneration, persistence, commands, transforms,
configurations, stable references, the sketch solver, the layering rules and
the CLI — before any design was written, and each decision below cites the
code it rests on.

Two findings from that reading changed what was designed, rather than
confirming it:

1. **The dependency graph is structurally single-document.**
   `DocumentObject::dependencies()` returns `std::vector<ObjectId>`
   (`include/bettercad/core/document/DocumentObject.hpp:50`), and an
   `ObjectId` means nothing outside its document. `DocumentId` appears
   outside headers in three places and is an *ownership guard* in every one:
   `CommandHistory::bindTo` and `Regenerator::regenerate` both refuse a
   second document. So "reference another part document" is not a feature
   that slots in — it is a change to the document model. That is why ADR-003
   scopes `P13` to internal references.

2. **The layer rule leaves no room.** `tests/architecture/CheckLayering.cmake:86`
   requires a *strictly lower* layer, so same-layer cross-module dependency
   is a violation. Assemblies need `features` (2) and `io` (3) needs
   assemblies, and there is no number in between. The renumber in ADR-006 is
   forced, not stylistic.

## Decisions

Each was chosen against two or three serious alternatives; the ADRs carry the
reasoning and what was rejected.

| ADR | Decision | Chosen over |
| --- | --- | --- |
| [ADR-002](../../architecture/decisions/ADR-002-assemblies-live-in-the-document.md) | Components and mates are `DocumentObject`s in the ordinary `Document`; `ComponentId`/`MateId` widen to `ObjectId` | a separate `AssemblyDocument`; an `AssemblyTable` sub-container |
| [ADR-003](../../architecture/decisions/ADR-003-internal-part-references-first.md) | `P13` references parts **inside one document**; the external contract is specified, not built | external references now; copy-on-insert with remembered origin |
| [ADR-004](../../architecture/decisions/ADR-004-mates-reference-semantic-geometry-only.md) | A mate may reference datums and semantic `FaceName`s only; `FaceSignature` is refused | also allowing `FaceSignature`; datums only; deferring the decision |
| [ADR-005](../../architecture/decisions/ADR-005-placement-is-intent-transforms-are-derived.md) | Placement intent is persisted; the solved transform is derived and never written | persisting solved transforms; persisting a solver seed |
| [ADR-006](../../architecture/decisions/ADR-006-assembly-module-and-layer.md) | New `assembly` module at layer 3, `io` → 4, renderer/scripting → 5; reference types in `core` | assemblies inside `features`; definitions in `core` alone |

### The two that carry the most weight

**ADR-002 makes the file format a non-event.** Because a component *is* a
document object, it serializes through the existing `{id, type, name, data}`
envelope into the existing `objects` array. No new top-level key, no version
bump, and a document with no components stays byte-identical to one written
before `P13`. Contrast `configurations`, which needed a new optional key and
"write only when non-empty" handling to achieve the same
(`src/io/json/DocumentJson.cpp:355`). The decisive argument was not
convenience: `ConfigurationId` deliberately does *not* widen to `ObjectId`
(`include/bettercad/core/Id.hpp:121`) because a configuration feeds the graph
rather than participating in it, whereas a component depends on a part and a
mate depends on components. Graph participants must be document objects or
`dependencies()` cannot express them.

**ADR-004 carries P12's measured lesson forward.** `P12-REF-001` found three
of six production models placing geometry on a `FaceSignature` written as a
literal that happened to equal a driving parameter; each worked at the size
it was authored at and failed at every other. In a part that breaks the part.
In an assembly it would break every assembly instancing the part, far from
the parameter that caused it. Semantic names do not have this failure mode —
`Faces.hpp:134` states that "a name is never moved to a face because of its
geometry" — so mates may use datums and `FaceName`s, and a `FaceSignature` is
refused at validation rather than warned about.

## Known limitations this architecture accepts

Recorded because they are consequences of the decisions, not oversights:

- **A component cannot independently select a part configuration.**
  Configurations are document-global — one active configuration, applied by
  `effectiveParameterValue` to every reader (`Document.cpp:415`) — and
  ADR-003 puts the parts in the same document. Two components of the same
  part therefore cannot differ by configuration. Lifting it needs
  per-component overrides or external part documents.
- **You cannot mate to a hole's face.** `HoleDefinition.face` accepts only a
  `FaceSignature`, so a bore is not nameable. Mating to a hole means mating
  to a datum axis published for it. Giving holes a `FaceName` option is a
  feature change outside `P13`.
- **Opening an assembly costs a solve.** ADR-005 persists no transforms, so
  there is no cached-position fast path. If that ever matters it is a
  measured performance decision, not a reason to persist derived state.
- **The solver must converge from placement intent alone.** That is a real
  constraint ADR-005 places on `P13-SOLVE-001`, accepted in exchange for
  determinism that does not depend on save history.

## Verification

This milestone asserts no measurement, so there is nothing to measure. What
can be checked is that it did what it said and nothing else.

**No executable source or test changed.** The commit recording this
milestone touches only `ARCHITECTURE.md`, `TODO.md`,
`docs/architecture/decisions/` and this directory. The eight source and test
tree IDs that `P12-QUAL-001` qualified are unchanged:

```text
apps              9b7b03a72ef1a773304a38dd02690818f66d50a3
include           0347b09b01baf7ad55ec6a67d3a8e3256676e3a1
src               b5f9b6e92cc4ee1451f67fda6983f89c4b222435
tests             b2184a7bdb512f1882bbc84dce0449727b18ff98
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

**`ARCHITECTURE.md` is true at the moment it is written.** ADR-006 changes
the layer table, but that table is mirrored from
`tests/architecture/CheckLayering.cmake`, which this milestone must not
touch. So the document keeps the *in-force* table as fact and records the
decided table separately as not yet in force, to be applied by the milestone
that creates `src/assembly/`. Writing the new table as though it were
current would have made the document disagree with the build — the same
class of defect found in this document's own ADR paragraph at the start of
this milestone.

**Every decision has its alternatives and its reasons**, and each ADR ends
with how it will be verified when the milestone that implements it runs.

**Links resolve.** 0 broken links across the five root documents and all six
ADRs.

## Result

```text
RESULT: PASS

Every decision made, with alternatives and reasons   PASS  ADR-002 … ADR-006
Architecturally significant decisions recorded       PASS  5 ADRs
ARCHITECTURE.md states the new invariants            PASS  15, 16, 17
No executable source or test changed                 PASS  8 of 8 tree IDs
Evidence recorded                                    PASS  this directory
```

`P13-ARCH-001` is complete. The next milestone is `P13-COMP-001`, which
implements ADR-002 — component definitions and instances — and is the first
`P13` milestone to write code.
